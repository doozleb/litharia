#include "Lighting.h"

#include <algorithm>
#include <cmath>

#include "../Blocks/Blocks.h"
#include "../Core/Constants.h"
#include "../Machines/Machine.h"
#include "../Machines/MachineType.h"
#include "../Machines/Machines.h"
#include "World.h"

Lighting::Lighting()
    : levels(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, LightLevel{0, 0, 0})
    , floodStamp(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
    , floodBest(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
    , outlineStamp(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
    , outlineStep(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
{
}

std::vector<std::pair<sf::Vector2i, int>> Lighting::floodFill(const World& world,
                                                                const std::vector<LightSeed>& seeds) const
{
    // See floodStamp/floodBest's declaration in Lighting.h for why this is
    // a counter bump instead of a std::fill over the whole grid.
    ++floodGeneration;
    if (floodGeneration == 0)
    {
        std::fill(floodStamp.begin(), floodStamp.end(), 0);
        floodGeneration = 1;
    }

    std::vector<std::pair<sf::Vector2i, int>> result;
    floodHeap.clear();

    static constexpr int NEIGHBOR_OFFSETS[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    const float diagonalWeight = std::sqrt(2.0f);

    // A single shared multi-source Dijkstra across every seed, instead of
    // an independent local search per seed - see docs/superpowers/specs/
    // 2026-07-22-floodfill-shared-search-design.md. Precondition (see
    // Lighting.h's comment on this method): every seed shares the same
    // seed.level.
    int seedLevel = 0;

    for (const LightSeed& seed : seeds)
    {
        if (seed.level <= 0 || !world.inBounds(seed.x, seed.y) || world.isSolid(seed.x, seed.y))
            continue;

        seedLevel = seed.level;
        floodHeap.push_back({0.0f, seed.x, seed.y});
        std::push_heap(floodHeap.begin(), floodHeap.end(), FloodEntryGreater{});
    }

    while (!floodHeap.empty())
    {
        std::pop_heap(floodHeap.begin(), floodHeap.end(), FloodEntryGreater{});
        const FloodEntry entry = floodHeap.back();
        floodHeap.pop_back();

        const std::size_t i = static_cast<std::size_t>(entry.y) * WORLD_WIDTH + entry.x;

        // Already finalized via an earlier (necessarily smaller-or-equal
        // distance) pop this call - a tile can be pushed more than once as
        // different paths reach it, but only its first pop is authoritative
        // (standard lazy-deletion Dijkstra).
        if (floodStamp[i] == floodGeneration)
            continue;

        const int level = static_cast<int>(std::floor(static_cast<double>(seedLevel) - entry.distance));
        if (level <= 0)
            continue;

        floodStamp[i] = floodGeneration;
        floodBest[i] = static_cast<std::int8_t>(level);
        result.push_back({{entry.x, entry.y}, level});

        for (const auto& offset : NEIGHBOR_OFFSETS)
        {
            const int dx = offset[0];
            const int dy = offset[1];
            const int nx = entry.x + dx;
            const int ny = entry.y + dy;

            if (!world.inBounds(nx, ny) || world.isSolid(nx, ny))
                continue;

            const std::size_t ni = static_cast<std::size_t>(ny) * WORLD_WIDTH + nx;
            if (floodStamp[ni] == floodGeneration)
                continue;

            if (dx != 0 && dy != 0)
            {
                // Corner-cutting guard: a diagonal step is only taken if
                // both flanking orthogonal tiles are open too - unchanged
                // from the per-seed search this replaces.
                if (!world.inBounds(entry.x + dx, entry.y) || world.isSolid(entry.x + dx, entry.y))
                    continue;
                if (!world.inBounds(entry.x, entry.y + dy) || world.isSolid(entry.x, entry.y + dy))
                    continue;
            }

            const float edgeWeight = (dx != 0 && dy != 0) ? diagonalWeight : 1.0f;
            const float newDistance = entry.distance + edgeWeight;

            // No point pushing a candidate that would decay to 0 or below.
            if (static_cast<int>(std::floor(static_cast<double>(seedLevel) - newDistance)) <= 0)
                continue;

            floodHeap.push_back({newDistance, nx, ny});
            std::push_heap(floodHeap.begin(), floodHeap.end(), FloodEntryGreater{});
        }
    }

    return result;
}

void Lighting::recomputeLavaChannel(const World& world)
{
    std::vector<LightSeed> lavaSeeds;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
    {
        for (int x = 0; x < WORLD_WIDTH; ++x)
        {
            if (!isLava(world.get(x, y)))
                continue;

            // An interior lava tile - every orthogonal neighbour also lava -
            // is a redundant flood-fill seed: it can never light anything
            // outside the lava body that a strictly closer boundary tile of
            // the same body doesn't already light at least as well, since
            // lava is non-solid and never blocks the flood fill from passing
            // through it. Skipping these seeds is what keeps recomputeAll's
            // lava-channel cost proportional to a body's boundary instead of
            // its fill - see docs/superpowers/specs/
            // 2026-07-22-lighting-lava-seed-design.md. The lava tiles
            // themselves still always read MAX_LIGHT_LEVEL regardless - see
            // the force-set pass below, not this seed list.
            const bool interior = isLava(world.get(x - 1, y)) && isLava(world.get(x + 1, y)) &&
                                   isLava(world.get(x, y - 1)) && isLava(world.get(x, y + 1));

            if (interior)
                continue;

            lavaSeeds.push_back({x, y, MAX_LIGHT_LEVEL});
        }
    }

    const auto lavaResult = floodFill(world, lavaSeeds);

    // Reset only the lava channel - sky/torch are left exactly as the
    // caller already had them, unlike recomputeAll's full-struct
    // std::fill. When called from recomputeAll (below), this means the
    // lava field gets zeroed twice in a row (once by recomputeAll's own
    // std::fill, once here) - a harmless, negligible redundancy confined
    // to the already-existing full-recompute path; recomputeLava's fast
    // path only ever does this single lava-only zero.
    for (LightLevel& level : levels)
        level.lava = 0;

    for (const auto& [tile, level] : lavaResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].lava =
            static_cast<std::uint16_t>(level);

    // Every lava tile is always fully lit at its own position, independent
    // of how far it sits from a boundary seed - see docs/superpowers/specs/
    // 2026-07-22-lighting-lava-seed-design.md for why this can't be left to
    // the (boundary-only) flood fill above: a deep interior tile could
    // otherwise read dimmer than MAX_LIGHT_LEVEL. Cheap by construction - no
    // BFS, no sqrt, just a linear scan - so this costs a small, fixed amount
    // regardless of how the lava is shaped.
    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            if (isLava(world.get(x, y)))
                levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].lava =
                    static_cast<std::uint16_t>(MAX_LIGHT_LEVEL);
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

    const auto skyResult = floodFill(world, skySeeds);
    const auto torchResult = floodFill(world, torchSeeds);

    std::fill(levels.begin(), levels.end(), LightLevel{0, 0, 0});

    for (const auto& [tile, level] : skyResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].sky =
            static_cast<std::uint16_t>(level);

    for (const auto& [tile, level] : torchResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].torch =
            static_cast<std::uint16_t>(level);

    recomputeLavaChannel(world);
}

void Lighting::recomputeLava(const World& world)
{
    recomputeLavaChannel(world);
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
    // call. Uses outlineStamp/outlineStep (Lighting.h) as scratch, same
    // generation-stamp trick as floodFill's floodStamp/floodBest - see that
    // declaration's comment for why.
    if (!world.inBounds(playerTile.x, playerTile.y) || world.isSolid(playerTile.x, playerTile.y))
        return {};

    ++outlineGeneration;
    if (outlineGeneration == 0)
    {
        std::fill(outlineStamp.begin(), outlineStamp.end(), 0);
        outlineGeneration = 1;
    }

    std::vector<sf::Vector2i> queue{playerTile};

    const std::size_t playerIndex = static_cast<std::size_t>(playerTile.y) * WORLD_WIDTH + playerTile.x;
    outlineStamp[playerIndex] = outlineGeneration;
    outlineStep[playerIndex] = 0;

    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const sf::Vector2i tile = queue[head];
        const std::size_t tileIndex = static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x;
        const int steps = outlineStep[tileIndex];

        if (steps >= radius)
            continue;

        const sf::Vector2i neighbors[4] = {
            {tile.x - 1, tile.y}, {tile.x + 1, tile.y}, {tile.x, tile.y - 1}, {tile.x, tile.y + 1}};

        for (const sf::Vector2i& n : neighbors)
        {
            if (!world.inBounds(n.x, n.y) || world.isSolid(n.x, n.y))
                continue;

            const std::size_t i = static_cast<std::size_t>(n.y) * WORLD_WIDTH + n.x;
            if (outlineStamp[i] == outlineGeneration)
                continue;

            outlineStamp[i] = outlineGeneration;
            outlineStep[i] = static_cast<std::int16_t>(steps + 1);
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
