#include "TerrainGenerator.h"

#include <algorithm>
#include <cmath>

#include "../Core/Constants.h"
#include "../Core/Noise.h"
#include "World.h"

namespace
{

// --- Pass 1: surface ---------------------------------------------------------
constexpr int SURFACE_BASE = 170;      // tiles from the top of the world
constexpr int SURFACE_AMPLITUDE = 45;  // how far the hills swing either way
constexpr float SURFACE_FREQUENCY = 0.012f;
constexpr int SURFACE_OCTAVES = 4;

constexpr int DIRT_DEPTH = 8;

// --- Pass 2: caves -----------------------------------------------------------
constexpr float CAVE_FREQUENCY = 0.055f;
constexpr int CAVE_OCTAVES = 3;

// The threshold noise must beat to become air. High near the surface (few caves,
// so the landscape is not shredded into holes), lower with depth.
constexpr float CAVE_THRESHOLD_SHALLOW = 0.80f;
constexpr float CAVE_THRESHOLD_DEEP = 0.60f;

constexpr int CAVE_MIN_DEPTH = 4;   // no caves in the top few tiles of ground
constexpr int CAVE_FADE_DEPTH = 45; // fully deep-threshold by this depth

// --- Pass 3: hill caves --------------------------------------------------------
constexpr float SPECIAL_CAVE_RADIUS = 2.5f;

constexpr int TRUNK_MAX_STEPS = 3000; // loop-safety cap; the downward bias
                                       // means this is never expected to bind

constexpr int BRANCH_MIN_COUNT = 3;
constexpr int BRANCH_MAX_COUNT = 6;
constexpr int BRANCH_MIN_STEPS = 15;
constexpr int BRANCH_MAX_STEPS = 40;

// findHillPeak's search may expand well past HILL_SEARCH_RADIUS to find a
// genuine peak, but never so far it could reach into a neighboring special
// cave's own territory. The near pair sits 2*SPECIAL_CAVE_NEAR_OFFSET apart
// (200 tiles); a near-to-far gap is SPECIAL_CAVE_FAR_OFFSET -
// SPECIAL_CAVE_NEAR_OFFSET (250 tiles). Both caps stay comfortably under
// half of the tighter gap that actually bounds each cave, so no two caves
// can ever expand into the same hill.
constexpr int NEAR_HILL_MAX_RADIUS = 90;
constexpr int FAR_HILL_MAX_RADIUS = 120;

constexpr std::uint32_t SALT_SPECIAL_CAVE = 0x8000u;

// --- Pass 4: ore -------------------------------------------------------------
// Candidate vein centres are hashed once per cell of this size.
constexpr int VEIN_CELL = 10;

constexpr float COPPER_DENSITY = 0.34f;
constexpr float IRON_DENSITY = 0.24f;
constexpr float COAL_DENSITY = 0.30f;

// Rare bands roll at a fifth of their ore's normal density - a real find,
// not a routine one.
constexpr float COPPER_DEEP_DENSITY = COPPER_DENSITY / 5.0f;
constexpr float IRON_SHALLOW_DENSITY = IRON_DENSITY / 5.0f;

constexpr float VEIN_MIN_RADIUS = 1.4f;
constexpr float VEIN_MAX_RADIUS = 3.1f;

// Salts keep the different hashed decisions from correlating with one another.
constexpr std::uint32_t SALT_SURFACE = 0x1000u;
constexpr std::uint32_t SALT_CAVE = 0x2000u;
constexpr std::uint32_t SALT_COPPER = 0x3000u;
constexpr std::uint32_t SALT_COPPER_DEEP = 0x3001u;
constexpr std::uint32_t SALT_IRON = 0x4000u;
constexpr std::uint32_t SALT_IRON_SHALLOW = 0x4001u;
constexpr std::uint32_t SALT_COAL = 0x5000u;

// --- Pass 5: trees ------------------------------------------------------------
// A wider wavelength than the surface noise, so forested and bare stretches
// span many tens of tiles rather than flickering column to column.
constexpr float FOREST_FREQUENCY = 0.006f;
constexpr int FOREST_OCTAVES = 3;

// The forest factor in [0, 1] is remapped into this density range before each
// column rolls against it - so even a "bare" stretch occasionally grows a
// tree, and a "forest" stretch is dense but still gated by TREE_MIN_SPACING.
constexpr float FOREST_DENSITY_MIN = 0.05f;
constexpr float FOREST_DENSITY_MAX = 0.6f;

constexpr std::uint32_t SALT_FOREST = 0x6000u;
constexpr std::uint32_t SALT_TREE = 0x7000u;

// --- Pass 6: fluids ------------------------------------------------------
constexpr float POOL_MIN_RADIUS = 2.5f;
constexpr float POOL_MAX_RADIUS = 4.5f;

constexpr std::uint32_t SALT_LAKE = 0xA000u;
constexpr std::uint32_t SALT_WATER_POOL = 0xB000u;
constexpr std::uint32_t SALT_LAVA_POOL = 0xC000u;

// --- Sharp Rocks ---------------------------------------------------------
constexpr std::uint32_t SALT_SHARP_ROCK = 0x9000u;

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float caveThreshold(int depth)
{
    const float t = std::clamp(
        static_cast<float>(depth - CAVE_MIN_DEPTH) / (CAVE_FADE_DEPTH - CAVE_MIN_DEPTH),
        0.0f,
        1.0f);

    return lerp(CAVE_THRESHOLD_SHALLOW, CAVE_THRESHOLD_DEEP, t);
}

} // namespace

TerrainGenerator::TerrainGenerator(std::uint32_t seed)
    : worldSeed(seed)
{
}

int TerrainGenerator::surfaceHeight(int x) const
{
    const float n = noise::fbm1D(static_cast<float>(x) * SURFACE_FREQUENCY,
                                 worldSeed + SALT_SURFACE,
                                 SURFACE_OCTAVES);

    // n is in [0, 1]; centre it so the hills swing both ways around the base.
    const float height = SURFACE_BASE + (n - 0.5f) * 2.0f * SURFACE_AMPLITUDE;

    // Clamped, so no amount of extreme noise can push the surface out of the world.
    return std::clamp(static_cast<int>(std::lround(height)), SURFACE_MIN, SURFACE_MAX);
}

int TerrainGenerator::findHillPeak(int targetX, int maxRadius) const
{
    int radius = HILL_SEARCH_RADIUS;

    while (true)
    {
        const int lo = std::max(0, targetX - radius);
        const int hi = std::min(WORLD_WIDTH - 1, targetX + radius);

        int bestX = lo;
        int bestHeight = surfaceHeight(lo);

        for (int x = lo + 1; x <= hi; ++x)
        {
            const int height = surfaceHeight(x);
            if (height < bestHeight)
            {
                bestHeight = height;
                bestX = x;
            }
        }

        // Landing on the window's own edge means the terrain was still
        // improving right up to where the scan stopped - the true peak is
        // further out, not at this edge. Widen the search and try again
        // rather than settling for a slope's shoulder.
        const bool atWindowEdge = (bestX == lo || bestX == hi);
        if (!atWindowEdge || radius >= maxRadius)
            return bestX;

        radius = std::min(radius * 2, maxRadius);
    }
}

void TerrainGenerator::generate(World& world) const
{
    generateBase(world);
    carveSpecialCaves(world);
    scatterOre(world);
    scatterFluids(world);
    scatterTrees(world);
}

void TerrainGenerator::generateBase(World& world) const
{
    generateSurface(world);
    carveCaves(world);
}

void TerrainGenerator::generateSurface(World& world) const
{
    world.fill(BlockType::Air);

    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        const int surface = surfaceHeight(x);

        world.set(x, surface, BlockType::Grass);

        for (int y = surface + 1; y < WORLD_HEIGHT; ++y)
        {
            const BlockType type =
                (y <= surface + DIRT_DEPTH) ? BlockType::Dirt : BlockType::Stone;

            world.set(x, y, type);
        }
    }
}

void TerrainGenerator::carveCaves(World& world) const
{
    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        const int surface = surfaceHeight(x);

        for (int y = surface + CAVE_MIN_DEPTH; y < WORLD_HEIGHT; ++y)
        {
            const float n = noise::fbm2D(static_cast<float>(x) * CAVE_FREQUENCY,
                                         static_cast<float>(y) * CAVE_FREQUENCY,
                                         worldSeed + SALT_CAVE,
                                         CAVE_OCTAVES);

            if (n > caveThreshold(y - surface))
                world.set(x, y, BlockType::Air);
        }
    }
}

void TerrainGenerator::growVein(World& world,
                                int centerX,
                                int centerY,
                                float radius,
                                BlockType ore,
                                int minY,
                                int maxY) const
{
    const int reach = static_cast<int>(std::ceil(radius));
    const float radiusSquared = radius * radius;

    for (int dy = -reach; dy <= reach; ++dy)
    {
        for (int dx = -reach; dx <= reach; ++dx)
        {
            if (static_cast<float>(dx * dx + dy * dy) > radiusSquared)
                continue;

            const int x = centerX + dx;
            const int y = centerY + dy;

            // The band is a hard boundary, not a suggestion: a blob near the edge is
            // clipped rather than allowed to spill out of its depth range.
            if (y < minY || y > maxY)
                continue;

            // The rule that keeps ore out of caves and out of the dirt band.
            if (world.get(x, y) != BlockType::Stone)
                continue;

            world.set(x, y, ore);
        }
    }
}

void TerrainGenerator::growPool(World& world,
                                 int centerX,
                                 int centerY,
                                 float radius,
                                 BlockType fluid,
                                 int minY,
                                 int maxY) const
{
    const int reach = static_cast<int>(std::ceil(radius));
    const float radiusSquared = radius * radius;

    for (int dy = -reach; dy <= reach; ++dy)
    {
        for (int dx = -reach; dx <= reach; ++dx)
        {
            if (static_cast<float>(dx * dx + dy * dy) > radiusSquared)
                continue;

            const int x = centerX + dx;
            const int y = centerY + dy;

            if (y < minY || y > maxY)
                continue;

            // A pool carves through whatever is there - unlike growVein, which
            // only ever replaces Stone, a pool is a basin that displaces the
            // terrain, not a mineral that only forms inside it.
            world.set(x, y, fluid);
        }
    }
}

void TerrainGenerator::carveTunnelPoint(World& world, int cx, int cy, float radius) const
{
    const int reach = static_cast<int>(std::ceil(radius));
    const float radiusSquared = radius * radius;

    for (int dy = -reach; dy <= reach; ++dy)
    {
        for (int dx = -reach; dx <= reach; ++dx)
        {
            if (static_cast<float>(dx * dx + dy * dy) > radiusSquared)
                continue;

            const int x = cx + dx;
            const int y = cy + dy;

            // A tunnel opens through solid ground only - it never punches
            // into a cave that's already open (nothing to do there) and
            // there is no ore yet at this pass. Grass is included so an
            // entrance carved right at the surface actually opens a visible
            // mouth instead of leaving an intact grass lid over it.
            const BlockType current = world.get(x, y);
            if (current != BlockType::Stone && current != BlockType::Dirt && current != BlockType::Grass)
                continue;

            world.set(x, y, BlockType::Air);
        }
    }
}

std::vector<std::pair<int, int>> TerrainGenerator::carveTrunk(World& world,
                                                               int caveIndex,
                                                               int startX,
                                                               int startY) const
{
    std::vector<std::pair<int, int>> path;

    int x = startX;
    int y = startY;

    const std::uint32_t seed =
        worldSeed + SALT_SPECIAL_CAVE + static_cast<std::uint32_t>(caveIndex) * 997u;

    for (int step = 0; step < TRUNK_MAX_STEPS; ++step)
    {
        carveTunnelPoint(world, x, y, SPECIAL_CAVE_RADIUS);
        path.push_back({x, y});

        if (y >= IRON_MIN_Y)
            break;

        // Mostly down, sometimes flat, sometimes back up - a trunk that
        // reliably descends but doesn't fall in a straight line.
        const float dyRoll = noise::hashFloat(step, 0, seed);
        y += (dyRoll < 0.50f) ? 1 : (dyRoll < 0.80f ? 0 : -1);

        // A sideways step of up to 2 tiles, not just 1, gives the trunk
        // noticeably more horizontal travel per tile of depth gained, so it
        // reads as a winding path rather than a near-straight vertical shaft.
        const float dxRoll = noise::hashFloat(step, 1, seed);
        if (dxRoll < 0.2f)
            x -= 2;
        else if (dxRoll < 0.4f)
            x -= 1;
        else if (dxRoll < 0.6f)
            x += 0;
        else if (dxRoll < 0.8f)
            x += 1;
        else
            x += 2;
    }

    return path;
}

void TerrainGenerator::carveBranch(World& world,
                                    int caveIndex,
                                    int branchIndex,
                                    int startX,
                                    int startY) const
{
    const std::uint32_t seed = worldSeed + SALT_SPECIAL_CAVE +
                                static_cast<std::uint32_t>(caveIndex) * 997u +
                                static_cast<std::uint32_t>(branchIndex) * 131u;

    const float lengthRoll = noise::hashFloat(branchIndex, 2, seed);
    const int length =
        BRANCH_MIN_STEPS + static_cast<int>(lengthRoll * (BRANCH_MAX_STEPS - BRANCH_MIN_STEPS + 1));

    int x = startX;
    int y = startY;

    for (int step = 0; step < length; ++step)
    {
        carveTunnelPoint(world, x, y, SPECIAL_CAVE_RADIUS);

        // No downward bias here - a branch wanders freely and simply stops
        // when its length runs out. That stop is the dead end.
        const float dyRoll = noise::hashFloat(step, 3, seed);
        y += (dyRoll < 1.0f / 3.0f) ? -1 : (dyRoll < 2.0f / 3.0f ? 0 : 1);

        const float dxRoll = noise::hashFloat(step, 4, seed);
        x += (dxRoll < 1.0f / 3.0f) ? -1 : (dxRoll < 2.0f / 3.0f ? 0 : 1);
    }
}

void TerrainGenerator::carveSpecialCaves(World& world) const
{
    const int spawnX = WORLD_WIDTH / 2;
    const int targets[4] = {
        spawnX - SPECIAL_CAVE_FAR_OFFSET,
        spawnX - SPECIAL_CAVE_NEAR_OFFSET,
        spawnX + SPECIAL_CAVE_NEAR_OFFSET,
        spawnX + SPECIAL_CAVE_FAR_OFFSET,
    };
    const int maxRadii[4] = {
        FAR_HILL_MAX_RADIUS,
        NEAR_HILL_MAX_RADIUS,
        NEAR_HILL_MAX_RADIUS,
        FAR_HILL_MAX_RADIUS,
    };

    for (int caveIndex = 0; caveIndex < 4; ++caveIndex)
    {
        const int peakX = findHillPeak(targets[caveIndex], maxRadii[caveIndex]);
        const int startY = surfaceHeight(peakX) + 2;

        const std::vector<std::pair<int, int>> trunkPath =
            carveTrunk(world, caveIndex, peakX, startY);

        const std::uint32_t caveSeed =
            worldSeed + SALT_SPECIAL_CAVE + static_cast<std::uint32_t>(caveIndex) * 997u;

        const float countRoll = noise::hashFloat(caveIndex, 5, caveSeed);
        const int branchCount =
            BRANCH_MIN_COUNT + static_cast<int>(countRoll * (BRANCH_MAX_COUNT - BRANCH_MIN_COUNT + 1));

        for (int branchIndex = 0; branchIndex < branchCount; ++branchIndex)
        {
            const float pickRoll = noise::hashFloat(branchIndex, 6, caveSeed);
            const std::size_t rawIndex =
                static_cast<std::size_t>(pickRoll * static_cast<float>(trunkPath.size()));
            const std::size_t pathIndex = std::min(rawIndex, trunkPath.size() - 1);

            const auto [branchX, branchY] = trunkPath[pathIndex];
            carveBranch(world, caveIndex, branchIndex, branchX, branchY);
        }
    }
}

void TerrainGenerator::scatterOre(World& world) const
{
    struct Ore
    {
        BlockType type;
        int minY;
        int maxY;
        float density;
        std::uint32_t salt;
    };

    const Ore ores[] = {
        {BlockType::CopperOre, COPPER_MIN_Y, COPPER_MAX_Y, COPPER_DENSITY, SALT_COPPER},
        {BlockType::CopperOre, COPPER_DEEP_MIN_Y, COPPER_DEEP_MAX_Y, COPPER_DEEP_DENSITY, SALT_COPPER_DEEP},
        {BlockType::IronOre, IRON_MIN_Y, IRON_MAX_Y, IRON_DENSITY, SALT_IRON},
        {BlockType::IronOre, IRON_SHALLOW_MIN_Y, IRON_SHALLOW_MAX_Y, IRON_SHALLOW_DENSITY, SALT_IRON_SHALLOW},
        {BlockType::Coal, COAL_MIN_Y, COAL_MAX_Y, COAL_DENSITY, SALT_COAL},
    };

    for (const Ore& ore : ores)
    {
        const int firstCellY = ore.minY / VEIN_CELL;
        const int lastCellY = ore.maxY / VEIN_CELL;
        const int lastCellX = (WORLD_WIDTH - 1) / VEIN_CELL;

        for (int cellY = firstCellY; cellY <= lastCellY; ++cellY)
        {
            for (int cellX = 0; cellX <= lastCellX; ++cellX)
            {
                const std::uint32_t seed = worldSeed + ore.salt;

                if (noise::hashFloat(cellX, cellY, seed) >= ore.density)
                    continue;

                // Jitter the centre inside its cell so veins do not sit on a grid.
                const float jitterX = noise::hashFloat(cellX, cellY, seed + 1u);
                const float jitterY = noise::hashFloat(cellX, cellY, seed + 2u);
                const float sizeRoll = noise::hashFloat(cellX, cellY, seed + 3u);

                const int centerX = cellX * VEIN_CELL + static_cast<int>(jitterX * VEIN_CELL);
                const int centerY = cellY * VEIN_CELL + static_cast<int>(jitterY * VEIN_CELL);

                const float radius =
                    VEIN_MIN_RADIUS + sizeRoll * (VEIN_MAX_RADIUS - VEIN_MIN_RADIUS);

                growVein(world, centerX, centerY, radius, ore.type, ore.minY, ore.maxY);
            }
        }
    }
}

void TerrainGenerator::placeTree(World& world, int trunkX, int surface, int height) const
{
    for (int i = 1; i <= height; ++i)
        world.set(trunkX, surface - i, BlockType::OakLog);

    const int topY = surface - height;

    // Flanking the top log.
    world.set(trunkX - 1, topY, BlockType::OakLeaves);
    world.set(trunkX + 1, topY, BlockType::OakLeaves);

    // The 3-wide row above that.
    for (int dx = -1; dx <= 1; ++dx)
        world.set(trunkX + dx, topY - 1, BlockType::OakLeaves);

    // The single apex tile on top.
    world.set(trunkX, topY - 2, BlockType::OakLeaves);
}

void TerrainGenerator::scatterTrees(World& world) const
{
    // Far enough back that the very first eligible column can still place a
    // tree instead of being rejected for "too close to the last one".
    int lastTrunkX = -TREE_MIN_SPACING;

    // Columns 0 and WORLD_WIDTH - 1 are skipped: a canopy needs a tile on
    // each side of its trunk, and one at the world edge would not have it.
    for (int x = 1; x < WORLD_WIDTH - 1; ++x)
    {
        if (x - lastTrunkX < TREE_MIN_SPACING)
            continue;

        const int surface = surfaceHeight(x);

        // A hill-cave entrance may have carved this column's surface open;
        // a tree needs solid ground under it, not thin air over a cave mouth.
        if (world.get(x, surface) != BlockType::Grass)
            continue;

        const float forestFactor = noise::fbm1D(static_cast<float>(x) * FOREST_FREQUENCY,
                                                 worldSeed + SALT_FOREST,
                                                 FOREST_OCTAVES);

        const float density = lerp(FOREST_DENSITY_MIN, FOREST_DENSITY_MAX, forestFactor);

        if (noise::hashFloat(x, 0, worldSeed + SALT_TREE) >= density)
            continue;

        const float heightRoll = noise::hashFloat(x, 0, worldSeed + SALT_TREE + 1u);
        const int height =
            TREE_MIN_HEIGHT + static_cast<int>(heightRoll * (TREE_MAX_HEIGHT - TREE_MIN_HEIGHT + 1));

        placeTree(world, x, surface, height);

        lastTrunkX = x;
    }
}

std::vector<FluidPoolSpawn> TerrainGenerator::scatterFluids(World& world) const
{
    std::vector<FluidPoolSpawn> spawns;
    spawns.reserve(SURFACE_LAKE_COUNT + WATER_POOL_COUNT + LAVA_POOL_COUNT);

    // Surface lakes: anchored to each chosen column's own surface height, one
    // roughly every WORLD_WIDTH / SURFACE_LAKE_COUNT tiles.
    const int lakeBinWidth = (WORLD_WIDTH - 2) / SURFACE_LAKE_COUNT;

    for (int i = 0; i < SURFACE_LAKE_COUNT; ++i)
    {
        const int binStart = 1 + i * lakeBinWidth;
        const float xRoll = noise::hashFloat(i, 0, worldSeed + SALT_LAKE);
        const int x = binStart + static_cast<int>(xRoll * lakeBinWidth);

        const float radiusRoll = noise::hashFloat(i, 1, worldSeed + SALT_LAKE);
        const float radius = POOL_MIN_RADIUS + radiusRoll * (POOL_MAX_RADIUS - POOL_MIN_RADIUS);

        // Centered radius-below the surface, so the blob's top edge just
        // reaches the surface contour rather than poking a dome above ground.
        const int surface = surfaceHeight(x);
        const int centerY = surface + static_cast<int>(radius);

        growPool(world, x, centerY, radius, BlockType::Water8, 0, WORLD_HEIGHT - 1);
        spawns.push_back({x, centerY, PoolKind::Lake});
    }

    // Underground water pools: within the existing cave-depth range, above the
    // lava band, no depth bias.
    const int waterBinWidth = (WORLD_WIDTH - 2) / WATER_POOL_COUNT;

    for (int i = 0; i < WATER_POOL_COUNT; ++i)
    {
        const int binStart = 1 + i * waterBinWidth;
        const float xRoll = noise::hashFloat(i, 0, worldSeed + SALT_WATER_POOL);
        const int x = binStart + static_cast<int>(xRoll * waterBinWidth);

        const float yRoll = noise::hashFloat(i, 1, worldSeed + SALT_WATER_POOL);
        const int y = WATER_POOL_MIN_Y +
                      static_cast<int>(yRoll * (WATER_POOL_MAX_Y - WATER_POOL_MIN_Y));

        const float radiusRoll = noise::hashFloat(i, 2, worldSeed + SALT_WATER_POOL);
        const float radius = POOL_MIN_RADIUS + radiusRoll * (POOL_MAX_RADIUS - POOL_MIN_RADIUS);

        growPool(world, x, y, radius, BlockType::Water8, WATER_POOL_MIN_Y, WATER_POOL_MAX_Y);
        spawns.push_back({x, y, PoolKind::WaterPool});
    }

    // Lava pools: below the iron layer, biased toward the deeper end of the
    // band (squaring a uniform roll concentrates it near 0, so subtracting
    // that from LAVA_MAX_Y keeps most rolls close to LAVA_MAX_Y) so lava gets
    // progressively more common - and dangerous - the deeper the player digs.
    const int lavaBinWidth = (WORLD_WIDTH - 2) / LAVA_POOL_COUNT;

    for (int i = 0; i < LAVA_POOL_COUNT; ++i)
    {
        const int binStart = 1 + i * lavaBinWidth;
        const float xRoll = noise::hashFloat(i, 0, worldSeed + SALT_LAVA_POOL);
        const int x = binStart + static_cast<int>(xRoll * lavaBinWidth);

        const float yRoll = noise::hashFloat(i, 1, worldSeed + SALT_LAVA_POOL);
        const int y = LAVA_MAX_Y - static_cast<int>(yRoll * yRoll * (LAVA_MAX_Y - LAVA_MIN_Y));

        const float radiusRoll = noise::hashFloat(i, 2, worldSeed + SALT_LAVA_POOL);
        const float radius = POOL_MIN_RADIUS + radiusRoll * (POOL_MAX_RADIUS - POOL_MIN_RADIUS);

        growPool(world, x, y, radius, BlockType::Lava8, LAVA_MIN_Y, LAVA_MAX_Y);
        spawns.push_back({x, y, PoolKind::LavaPool});
    }

    return spawns;
}

std::pair<int, int> TerrainGenerator::randomSurfaceSpot(const World& world, std::uint32_t salt) const
{
    constexpr int MAX_ATTEMPTS = 8;

    int x = 1;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
    {
        const float roll = noise::hashFloat(attempt, 0, worldSeed + SALT_SHARP_ROCK + salt);
        x = 1 + static_cast<int>(roll * (WORLD_WIDTH - 2));

        if (world.get(x, surfaceHeight(x)) == BlockType::Grass)
            break;
    }

    return {x, surfaceHeight(x)};
}

std::vector<std::pair<int, int>> TerrainGenerator::scatterSharpRocks(const World& world) const
{
    std::vector<std::pair<int, int>> spots;
    spots.reserve(SHARP_ROCK_COUNT);

    const int binWidth = (WORLD_WIDTH - 2) / SHARP_ROCK_COUNT;
    constexpr int MAX_ATTEMPTS = 8;

    for (int i = 0; i < SHARP_ROCK_COUNT; ++i)
    {
        const int binStart = 1 + i * binWidth;
        int x = binStart;

        for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
        {
            const float roll = noise::hashFloat(i, attempt, worldSeed + SALT_SHARP_ROCK);
            x = binStart + static_cast<int>(roll * binWidth);

            if (world.get(x, surfaceHeight(x)) == BlockType::Grass)
                break;
        }

        spots.push_back({x, surfaceHeight(x)});
    }

    return spots;
}
