#include "FluidSim.h"

#include "../Core/Constants.h"
#include "World.h"

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

    for (const sf::Vector2i& pos : toProcess)
    {
        const BlockType type = world.get(pos.x, pos.y);

        if (!isFluid(type))
            continue;

        if (isLava(type) && reactAt(world, pos.x, pos.y, changedTiles))
            continue;

        if (fallAt(world, pos.x, pos.y, type, changedTiles))
            continue;

        spreadAt(world, pos.x, pos.y, type, changedTiles);
    }
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

    const bool sameFluidBelow = (isWater(type) && isWater(below)) || (isLava(type) && isLava(below));

    if (sameFluidBelow && fluidLevel(below) < 8)
    {
        world.set(x, y + 1, fluidAtLevel(type, fluidLevel(below) + 1));
        world.set(x, y, fluidAtLevel(type, fluidLevel(type) - 1));
        activateAround(x, y);
        activateAround(x, y + 1);
        changed.push_back({x, y});
        changed.push_back({x, y + 1});
        return true;
    }

    return false;
}

bool FluidSim::spreadAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed)
{
    const int level = fluidLevel(type);

    if (level < 2)
        return false;

    const int half = level / 2;      // floor - goes to the neighbor
    const int remainder = level - half; // ceil - stays at the source

    if (world.get(x - 1, y) == BlockType::Air && world.inBounds(x - 1, y))
    {
        world.set(x - 1, y, fluidAtLevel(type, half));
        world.set(x, y, fluidAtLevel(type, remainder));
        activateAround(x, y);
        activateAround(x - 1, y);
        changed.push_back({x, y});
        changed.push_back({x - 1, y});
        return true;
    }

    if (world.get(x + 1, y) == BlockType::Air && world.inBounds(x + 1, y))
    {
        world.set(x + 1, y, fluidAtLevel(type, half));
        world.set(x, y, fluidAtLevel(type, remainder));
        activateAround(x, y);
        activateAround(x + 1, y);
        changed.push_back({x, y});
        changed.push_back({x + 1, y});
        return true;
    }

    return false;
}
