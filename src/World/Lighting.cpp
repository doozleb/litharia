#include "Lighting.h"

#include <algorithm>

#include "../Blocks/Blocks.h"
#include "../Core/Constants.h"
#include "../Machines/Machine.h"
#include "../Machines/MachineType.h"
#include "../Machines/Machines.h"
#include "World.h"

Lighting::Lighting()
    : levels(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, LightLevel{0, 0})
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
    std::vector<LightSeed> blockSeeds;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            if (isLava(world.get(x, y)))
                blockSeeds.push_back({x, y, 8});

    for (const Machine& m : machines.all())
        if (m.type == MachineType::Torch)
            blockSeeds.push_back({m.x, m.y, 8});

    const auto blockResult = floodFill(world, blockSeeds);

    std::fill(levels.begin(), levels.end(), LightLevel{0, 0});

    for (const auto& [tile, level] : blockResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].block =
            static_cast<std::uint8_t>(level);
}

int Lighting::skyLight(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    return levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].sky;
}

int Lighting::blockLight(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    return levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].block;
}
