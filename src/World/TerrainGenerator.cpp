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

// --- Pass 3: ore -------------------------------------------------------------
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

// --- Pass 4: trees ------------------------------------------------------------
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

void TerrainGenerator::generate(World& world) const
{
    generateBase(world);
    scatterOre(world);
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

        const float forestFactor = noise::fbm1D(static_cast<float>(x) * FOREST_FREQUENCY,
                                                 worldSeed + SALT_FOREST,
                                                 FOREST_OCTAVES);

        const float density = lerp(FOREST_DENSITY_MIN, FOREST_DENSITY_MAX, forestFactor);

        if (noise::hashFloat(x, 0, worldSeed + SALT_TREE) >= density)
            continue;

        const float heightRoll = noise::hashFloat(x, 0, worldSeed + SALT_TREE + 1u);
        const int height =
            TREE_MIN_HEIGHT + static_cast<int>(heightRoll * (TREE_MAX_HEIGHT - TREE_MIN_HEIGHT + 1));

        placeTree(world, x, surfaceHeight(x), height);

        lastTrunkX = x;
    }
}
