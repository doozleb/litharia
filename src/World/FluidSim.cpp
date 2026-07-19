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

void FluidSim::activate(int x, int y)
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return;

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

        if (flattenAt(world, x, y, type, changedTiles))
            continue;

        if (spreadAt(world, x, y, type, changedTiles))
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

bool FluidSim::flattenAt(World& world, int x, int y, BlockType type,
                         std::vector<sf::Vector2i>& changed)
{
    // A tile "rests" on this row if it cannot fall - the tile below is not open,
    // in-bounds air. Only resting same-fluid tiles form a levelling run; a tile
    // that can still fall is left for fallAt.
    auto rests = [&](int cx) {
        const BlockType t = world.get(cx, y);

        if (!sameFluid(type, t))
            return false;

        const BlockType below = world.get(cx, y + 1);
        return !(below == BlockType::Air && world.inBounds(cx, y + 1));
    };

    if (!rests(x))
        return false;

    // Gather the maximal contiguous run of resting same-fluid tiles at this row.
    int xL = x;
    int xR = x;
    while (xL - 1 >= 0 && rests(xL - 1))
        --xL;
    while (xR + 1 < WORLD_WIDTH && rests(xR + 1))
        ++xR;

    const int n = xR - xL + 1;

    if (n <= 1)
        return false; // a lone tile is already as level as it gets

    int total = 0;
    for (int cx = xL; cx <= xR; ++cx)
        total += fluidLevel(world.get(cx, y));

    // Is the run boxed in at both ends (a wall or the world edge), or still open
    // to widen into air? Rounding is applied only to a settled, enclosed run;
    // rounding a run that is still spreading would compound its error every
    // step as it widens and visibly inflate the pool.
    const bool openLeft = xL > 0 && world.get(xL - 1, y) == BlockType::Air;
    const bool openRight = xR < WORLD_WIDTH - 1 && world.get(xR + 1, y) == BlockType::Air;
    const bool enclosed = !openLeft && !openRight;

    bool anyChange = false;

    for (int cx = xL; cx <= xR; ++cx)
    {
        int level;

        if (enclosed)
        {
            // Settled in a basin: round the average to the nearest whole level
            // and store that single level everywhere, so the surface is dead
            // flat. The run is all fluid (each cell >= level 1), so the result
            // lands in [1, 8]. This is a one-shot nudge of up to half a level
            // per cell - the trade for a perfectly flat, uniform surface.
            level = std::clamp((total + n / 2) / n, 1, 8);
        }
        else
        {
            // Still spreading toward an open end: level conservatively (base,
            // with the leftover single level in the centre cells) so nothing is
            // created or lost while the body is in motion.
            const int base = total / n;
            const int rem = total % n;
            const int remStart = xL + (n - rem) / 2;
            level = base + (cx >= remStart && cx < remStart + rem ? 1 : 0);
        }

        const BlockType want = fluidAtLevel(type, level);

        if (world.get(cx, y) != want)
        {
            world.set(cx, y, want);
            activateAround(cx, y);
            changed.push_back({cx, y});
            anyChange = true;
        }
    }

    return anyChange;
}

bool FluidSim::spreadAt(World& world, int x, int y, BlockType type,
                        std::vector<sf::Vector2i>& changed)
{
    const int level = fluidLevel(type);

    if (level < 2)
        return false; // a single unit cannot widen without emptying itself

    // Widen into open air beside it, but only where that air is itself resting
    // on support (so a puddle grows sideways). Air over a drop is a ledge, left
    // to cascadeAt. Ties go left. Move half the level so the two even out.
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

    if (below == BlockType::Air && world.inBounds(x, y + 1))
        return true;

    if (sameFluid(type, below) && fluidLevel(below) < 8)
        return true;

    // An air neighbour means it can spread or spill; a same-fluid neighbour at a
    // different level means the row can still level.
    for (const int dx : {-1, 1})
    {
        if (!world.inBounds(x + dx, y))
            continue;

        const BlockType n = world.get(x + dx, y);

        if (n == BlockType::Air)
            return true;

        if (sameFluid(type, n) && fluidLevel(n) != fluidLevel(type))
            return true;
    }

    return false;
}
