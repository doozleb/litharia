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

constexpr float VEIN_MIN_RADIUS = 1.4f;
constexpr float VEIN_MAX_RADIUS = 3.1f;

// Salts keep the different hashed decisions from correlating with one another.
constexpr std::uint32_t SALT_SURFACE = 0x1000u;
constexpr std::uint32_t SALT_CAVE = 0x2000u;
constexpr std::uint32_t SALT_COPPER = 0x3000u;
constexpr std::uint32_t SALT_IRON = 0x4000u;

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
        {BlockType::IronOre, IRON_MIN_Y, IRON_MAX_Y, IRON_DENSITY, SALT_IRON},
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
