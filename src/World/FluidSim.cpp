#include "FluidSim.h"

#include <algorithm>

#include "../Core/Constants.h"
#include "World.h"

namespace
{

// Water stays water and lava stays lava - two fluids only ever level with their
// own kind (where they meet, they react instead; see reactAt).
bool sameFluid(BlockType a, BlockType b)
{
    return (isWater(a) && isWater(b)) || (isLava(a) && isLava(b));
}

} // namespace

FluidSim::FluidSim()
    : pending(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
{
}

void FluidSim::activate(int x, int y)
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return;

    // Already queued this step: don't enqueue a duplicate. This is what keeps
    // the pending queue proportional to the number of live fluid tiles rather
    // than exploding by the fan-out of activateAround.
    const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
    if (pending[i])
        return;

    pending[i] = 1;
    active.push_back({x, y});
}

void FluidSim::activateAround(int x, int y)
{
    activate(x, y);
    activate(x - 1, y);
    activate(x + 1, y);
    activate(x, y - 1);
    activate(x, y + 1);
}

void FluidSim::activateAll(const World& world)
{
    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            if (isFluid(world.get(x, y)))
                activate(x, y);
}

void FluidSim::tick(World& world, float dt, std::vector<sf::Vector2i>& changedTiles)
{
    timer += dt;

    if (timer < TICK_INTERVAL)
        return;

    timer -= TICK_INTERVAL;
    step(world, changedTiles);
}

void FluidSim::step(World& world, std::vector<sf::Vector2i>& changedTiles)
{
    std::vector<sf::Vector2i> toProcess;
    toProcess.swap(active);

    const bool lavaMovesThisStep = (stepCount % LAVA_MOVE_INTERVAL == 0);

    for (const sf::Vector2i& pos : toProcess)
    {
        const int x = pos.x;
        const int y = pos.y;

        // Consume this tile: clear its pending flag so that any re-activation of
        // it during this step (by its own rules or a neighbour's) re-queues it
        // for the next step instead of being suppressed as a duplicate.
        pending[static_cast<std::size_t>(y) * WORLD_WIDTH + x] = 0;

        const BlockType type = world.get(x, y);

        if (!isFluid(type))
            continue;

        // Reaction is a chemical change, not a flow, so it runs at full speed
        // for lava - the throttle below only ever slows lava's MOVEMENT.
        if (isLava(type) && reactAt(world, x, y, changedTiles))
            continue;

        // Lava is viscous: on its off-steps it does not move, but stays pending
        // if it still has somewhere to go (so settled lava goes quiescent).
        if (isLava(type) && !lavaMovesThisStep)
        {
            if (canMove(world, x, y, type))
                activate(x, y);

            continue;
        }

        // Down first, then level a resting row flat, then widen into open
        // space, then spill over a ledge - the first rule that acts wins.
        if (fallAt(world, x, y, type, changedTiles))
            continue;

        if (equalizeAt(world, x, y, type, changedTiles))
            continue;

        cascadeAt(world, x, y, type, changedTiles);
    }

    ++stepCount;
}

bool FluidSim::reactAt(World& world, int x, int y, std::vector<sf::Vector2i>& changed)
{
    const int dx[4] = {-1, 1, 0, 0};
    const int dy[4] = {0, 0, -1, 1};

    for (int i = 0; i < 4; ++i)
    {
        const int nx = x + dx[i];
        const int ny = y + dy[i];
        const BlockType neighbor = world.get(nx, ny);

        if (!isWater(neighbor))
            continue;

        world.set(x, y, BlockType::Obsidian);
        world.set(nx, ny, fluidAtLevel(neighbor, fluidLevel(neighbor) - 1));

        activateAround(x, y);
        activateAround(nx, ny);
        changed.push_back({x, y});
        changed.push_back({nx, ny});
        return true;
    }

    return false;
}

bool FluidSim::fallAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed)
{
    const BlockType below = world.get(x, y + 1);

    // Open air below: the whole tile drops. Gated on inBounds so fluid never
    // pours off the bottom edge of the world (World::set would silently drop
    // the write, losing the fluid).
    if (below == BlockType::Air && world.inBounds(x, y + 1))
    {
        world.set(x, y + 1, type);
        world.set(x, y, BlockType::Air);
        activateAround(x, y);
        activateAround(x, y + 1);
        changed.push_back({x, y});
        changed.push_back({x, y + 1});
        return true;
    }

    // Same fluid below with room to spare: pour down as much as fits, filling
    // the space as full as possible rather than trickling one level at a time.
    if (sameFluid(type, below))
    {
        const int belowLevel = fluidLevel(below);
        const int space = 8 - belowLevel;

        if (space > 0)
        {
            const int move = std::min(fluidLevel(type), space);

            world.set(x, y + 1, fluidAtLevel(type, belowLevel + move));
            world.set(x, y, fluidAtLevel(type, fluidLevel(type) - move));
            activateAround(x, y);
            activateAround(x, y + 1);
            changed.push_back({x, y});
            changed.push_back({x, y + 1});
            return true;
        }
    }

    return false;
}

bool FluidSim::equalizeAt(World& world, int x, int y, BlockType type,
                          std::vector<sf::Vector2i>& changed)
{
    // A tile "rests" if it cannot fall: the tile below is not open in-bounds
    // air. Only resting same-fluid tiles form a levelling run.
    auto rests = [&](int cx) {
        const BlockType t = world.get(cx, y);
        if (!sameFluid(type, t))
            return false;
        const BlockType below = world.get(cx, y + 1);
        return !(below == BlockType::Air && world.inBounds(cx, y + 1));
    };

    if (rests(x))
    {
        // Gather the maximal contiguous run of resting same-fluid tiles.
        int xL = x;
        int xR = x;
        while (xL - 1 >= 0 && rests(xL - 1))
            --xL;
        while (xR + 1 < WORLD_WIDTH && rests(xR + 1))
            ++xR;

        // Slosh one unit per step from the run's highest cell to its lowest, so
        // the surface visibly settles instead of snapping flat in one tick.
        // Using the whole run's max and min (not just adjacent cells) avoids
        // stalling on a staircase like 6,5,4,4 where every neighbour differs by
        // only one yet the surface is not flat. Moving exactly one unit is
        // exactly conservative and strictly shrinks the run's spread, so it
        // reaches flat-within-one-level and then stops.
        int maxX = xL;
        int minX = xL;
        int maxLevel = fluidLevel(world.get(xL, y));
        int minLevel = maxLevel;
        for (int cx = xL + 1; cx <= xR; ++cx)
        {
            const int lvl = fluidLevel(world.get(cx, y));
            if (lvl > maxLevel) { maxLevel = lvl; maxX = cx; }
            if (lvl < minLevel) { minLevel = lvl; minX = cx; }
        }

        if (maxLevel - minLevel >= 2)
        {
            world.set(maxX, y, fluidAtLevel(type, maxLevel - 1));
            world.set(minX, y, fluidAtLevel(type, minLevel + 1));
            activateAround(maxX, y);
            activateAround(minX, y);
            changed.push_back({maxX, y});
            changed.push_back({minX, y});
            return true;
        }
    }

    // Already flat within one level (or a lone tile): widen one step into open,
    // resting air beside it. Air over a drop is a ledge, left to cascadeAt.
    // Ties go left. A single unit cannot widen without emptying itself.
    const int level = fluidLevel(type);

    if (level < 2)
        return false;

    for (const int dx : {-1, 1})
    {
        const int nx = x + dx;

        if (!world.inBounds(nx, y) || world.get(nx, y) != BlockType::Air)
            continue;

        const BlockType belowNeighbor = world.get(nx, y + 1);
        const bool neighborRests = !(belowNeighbor == BlockType::Air && world.inBounds(nx, y + 1));

        if (!neighborRests)
            continue;

        const int move = level / 2;

        world.set(nx, y, fluidAtLevel(type, move));
        world.set(x, y, fluidAtLevel(type, level - move));
        activateAround(x, y);
        activateAround(nx, y);
        changed.push_back({x, y});
        changed.push_back({nx, y});
        return true;
    }

    return false;
}

bool FluidSim::cascadeAt(World& world, int x, int y, BlockType type,
                         std::vector<sf::Vector2i>& changed)
{
    // Spill over a ledge: an air neighbour with open air beneath it. The tile
    // tips over the edge so it falls down the far side next step - this is what
    // lets a full basin overflow its lip.
    for (const int dx : {-1, 1})
    {
        const int nx = x + dx;

        if (!world.inBounds(nx, y) || world.get(nx, y) != BlockType::Air)
            continue;

        if (!(world.get(nx, y + 1) == BlockType::Air && world.inBounds(nx, y + 1)))
            continue;

        world.set(nx, y, type);
        world.set(x, y, BlockType::Air);
        activateAround(x, y);
        activateAround(nx, y);
        changed.push_back({x, y});
        changed.push_back({nx, y});
        return true;
    }

    return false;
}

bool FluidSim::canMove(const World& world, int x, int y, BlockType type) const
{
    const BlockType below = world.get(x, y + 1);

    // Can fall into open air, or pour into same fluid below with room.
    if (below == BlockType::Air && world.inBounds(x, y + 1))
        return true;
    if (sameFluid(type, below) && fluidLevel(below) < 8)
        return true;

    // Can level: mirror equalizeAt's run scan - a resting run whose highest and
    // lowest cells differ by at least 2 still has a unit to slosh.
    auto rests = [&](int cx) {
        const BlockType t = world.get(cx, y);
        if (!sameFluid(type, t))
            return false;
        const BlockType b = world.get(cx, y + 1);
        return !(b == BlockType::Air && world.inBounds(cx, y + 1));
    };

    if (rests(x))
    {
        int xL = x;
        int xR = x;
        while (xL - 1 >= 0 && rests(xL - 1))
            --xL;
        while (xR + 1 < WORLD_WIDTH && rests(xR + 1))
            ++xR;

        int maxLevel = fluidLevel(world.get(xL, y));
        int minLevel = maxLevel;
        for (int cx = xL + 1; cx <= xR; ++cx)
        {
            const int lvl = fluidLevel(world.get(cx, y));
            maxLevel = std::max(maxLevel, lvl);
            minLevel = std::min(minLevel, lvl);
        }

        if (maxLevel - minLevel >= 2)
            return true;
    }

    // Can widen into resting open air beside it (needs level >= 2), or spill
    // over a ledge (air neighbour with open air beneath it).
    const int level = fluidLevel(type);
    for (const int dx : {-1, 1})
    {
        const int nx = x + dx;

        if (!world.inBounds(nx, y) || world.get(nx, y) != BlockType::Air)
            continue;

        const BlockType belowNeighbor = world.get(nx, y + 1);
        const bool neighborOverDrop = (belowNeighbor == BlockType::Air && world.inBounds(nx, y + 1));

        if (neighborOverDrop)
            return true;   // can cascade (spill over the ledge)
        if (level >= 2)
            return true;   // neighbour rests: can widen into it
    }

    return false;
}
