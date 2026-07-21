#include "Lighting.h"

#include <algorithm>

#include "../Blocks/Blocks.h"
#include "../Core/Constants.h"
#include "../Machines/Machine.h"
#include "../Machines/MachineType.h"
#include "../Machines/Machines.h"
#include "World.h"

Lighting::Lighting()
    : levels(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, LightLevel{0, 0, 0})
{
}

std::vector<std::pair<sf::Vector2i, int>> Lighting::floodFill(const World& world,
                                                                const std::vector<LightSeed>& seeds)
{
    std::vector<std::int8_t> best(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, -1);
    std::vector<LightSeed> queue;

    auto tryVisit = [&](int x, int y, int level)
    {
        if (level <= 0 || !world.inBounds(x, y) || world.isSolid(x, y))
            return;

        const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
        if (level <= best[i])
            return;

        best[i] = static_cast<std::int8_t>(level);
        queue.push_back({x, y, level});
    };

    for (const LightSeed& seed : seeds)
        tryVisit(seed.x, seed.y, seed.level);

    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const LightSeed e = queue[head];
        tryVisit(e.x - 1, e.y, e.level - 1);
        tryVisit(e.x + 1, e.y, e.level - 1);
        tryVisit(e.x, e.y - 1, e.level - 1);
        tryVisit(e.x, e.y + 1, e.level - 1);
    }

    std::vector<std::pair<sf::Vector2i, int>> result;
    result.reserve(queue.size());
    for (const LightSeed& e : queue)
        result.push_back({{e.x, e.y}, e.level});

    return result;
}

void Lighting::recomputeAll(const World& world, const Machines& machines)
{
    std::vector<LightSeed> skySeeds;

    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        for (int y = 0; y < WORLD_HEIGHT; ++y)
        {
            if (world.isSolid(x, y))
                break;

            skySeeds.push_back({x, y, MAX_LIGHT_LEVEL});
        }
    }

    std::vector<LightSeed> torchSeeds;

    for (const Machine& m : machines.all())
        if (m.type == MachineType::Torch)
            torchSeeds.push_back({m.x, m.y, TORCH_LIGHT_LEVEL});

    std::vector<LightSeed> lavaSeeds;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            if (isLava(world.get(x, y)))
                lavaSeeds.push_back({x, y, MAX_LIGHT_LEVEL});

    const auto skyResult = floodFill(world, skySeeds);
    const auto torchResult = floodFill(world, torchSeeds);
    const auto lavaResult = floodFill(world, lavaSeeds);

    std::fill(levels.begin(), levels.end(), LightLevel{0, 0, 0});

    for (const auto& [tile, level] : skyResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].sky =
            static_cast<std::uint16_t>(level);

    for (const auto& [tile, level] : torchResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].torch =
            static_cast<std::uint16_t>(level);

    for (const auto& [tile, level] : lavaResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].lava =
            static_cast<std::uint16_t>(level);
}

int Lighting::skyLight(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    return levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].sky;
}

int Lighting::torchLight(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    return levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].torch;
}

int Lighting::lavaLight(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    return levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].lava;
}

std::vector<std::pair<sf::Vector2i, int>> Lighting::heldTorchLight(const World& world,
                                                                    sf::Vector2i source) const
{
    return floodFill(world, {LightSeed{source.x, source.y, TORCH_LIGHT_LEVEL}});
}

std::vector<std::pair<sf::Vector2i, int>> Lighting::ambientOutline(const World& world, sf::Vector2i playerTile,
                                                                    int radius) const
{
    // A reachability BFS through open tiles only, bounded by step count
    // rather than a decaying value (every reached tile gets the same flat
    // level) - reuses the same "visit each tile at most once, solid tiles
    // are walls" shape as floodFill, but floodFill's early-exit is keyed on
    // a *level* reaching 0, which doesn't fit "same value everywhere, cut
    // off by distance" - so this is its own small BFS instead of a floodFill
    // call.
    if (!world.inBounds(playerTile.x, playerTile.y) || world.isSolid(playerTile.x, playerTile.y))
        return {};

    std::vector<std::int8_t> visited(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0);
    std::vector<int> stepOf(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0);
    std::vector<sf::Vector2i> queue{playerTile};

    visited[static_cast<std::size_t>(playerTile.y) * WORLD_WIDTH + playerTile.x] = 1;

    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const sf::Vector2i tile = queue[head];
        const int steps = stepOf[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x];

        if (steps >= radius)
            continue;

        const sf::Vector2i neighbors[4] = {
            {tile.x - 1, tile.y}, {tile.x + 1, tile.y}, {tile.x, tile.y - 1}, {tile.x, tile.y + 1}};

        for (const sf::Vector2i& n : neighbors)
        {
            if (!world.inBounds(n.x, n.y) || world.isSolid(n.x, n.y))
                continue;

            const std::size_t i = static_cast<std::size_t>(n.y) * WORLD_WIDTH + n.x;
            if (visited[i])
                continue;

            visited[i] = 1;
            stepOf[i] = steps + 1;
            queue.push_back(n);
        }
    }

    std::vector<std::pair<sf::Vector2i, int>> result;
    result.reserve(queue.size() * 5);

    for (const sf::Vector2i& tile : queue)
    {
        result.push_back({tile, AMBIENT_OUTLINE_LEVEL});

        const sf::Vector2i neighbors[4] = {
            {tile.x - 1, tile.y}, {tile.x + 1, tile.y}, {tile.x, tile.y - 1}, {tile.x, tile.y + 1}};

        for (const sf::Vector2i& n : neighbors)
        {
            if (!world.inBounds(n.x, n.y) || !world.isSolid(n.x, n.y))
                continue;

            const int level = isOre(world.get(n.x, n.y)) ? AMBIENT_OUTLINE_ORE_LEVEL : AMBIENT_OUTLINE_LEVEL;
            result.push_back({n, level});
        }
    }

    return result;
}
