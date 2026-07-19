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

// A horizontal neighbour's effective level for levelling: Air is 0, same-fluid
// is its own level, anything else (solid, obsidian, the other fluid) is not a
// candidate at all and is reported as -1.
int levelTarget(BlockType type, BlockType neighbor)
{
    if (neighbor == BlockType::Air)
        return 0;

    if (sameFluid(type, neighbor))
        return fluidLevel(neighbor);

    return -1;
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

        if (fallAt(world, x, y, type, changedTiles))
            continue;

        equalizeAt(world, x, y, type, changedTiles);
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
    const int level = fluidLevel(type);

    // A single unit has nothing to give without emptying itself, so a level-1
    // tile is as flat as it can get and stays put.
    if (level < 2)
        return false;

    // Level toward the lower of the two horizontal neighbours; ties go left.
    // Only in-bounds Air or same-fluid tiles are candidates - a wall or the
    // world edge is not somewhere fluid can go.
    int bestDx = 0;
    int bestLevel = level;

    for (const int dx : {-1, 1})
    {
        if (!world.inBounds(x + dx, y))
            continue;

        const int nl = levelTarget(type, world.get(x + dx, y));

        if (nl < 0)
            continue;

        if (nl < bestLevel)
        {
            bestLevel = nl;
            bestDx = dx;
        }
    }

    if (bestDx == 0)
        return false;

    // Stop once neighbours differ by at most one level - that already reads as
    // flat, and moving further would only slosh a single unit back and forth.
    const int diff = level - bestLevel;

    if (diff < 2)
        return false;

    // Move half the difference toward the lower side. Halving never overshoots
    // (the source stays >= the neighbour), so a body converges to level quickly
    // and without oscillating.
    const int move = diff / 2;
    const int nx = x + bestDx;

    world.set(nx, y, fluidAtLevel(type, bestLevel + move));
    world.set(x, y, fluidAtLevel(type, level - move));
    activateAround(x, y);
    activateAround(nx, y);
    changed.push_back({x, y});
    changed.push_back({nx, y});
    return true;
}

bool FluidSim::canMove(const World& world, int x, int y, BlockType type) const
{
    const BlockType below = world.get(x, y + 1);

    if (below == BlockType::Air && world.inBounds(x, y + 1))
        return true;

    if (sameFluid(type, below) && fluidLevel(below) < 8)
        return true;

    const int level = fluidLevel(type);

    for (const int dx : {-1, 1})
    {
        if (!world.inBounds(x + dx, y))
            continue;

        const int nl = levelTarget(type, world.get(x + dx, y));

        if (nl >= 0 && level - nl >= 2)
            return true;
    }

    return false;
}
