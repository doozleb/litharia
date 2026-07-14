#include "TerrainGenerator.h"

#include "World.h"

namespace
{

// Placeholder until step 2: a flat surface with a dirt band over stone. Keeps the
// world visible while the simulation/rendering split lands.
constexpr int PLACEHOLDER_SURFACE = 200;
constexpr int DIRT_DEPTH = 8;

} // namespace

TerrainGenerator::TerrainGenerator(std::uint32_t seed)
    : worldSeed(seed)
{
}

void TerrainGenerator::generate(World& world) const
{
    world.fill(BlockType::Air);

    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        const int surface = PLACEHOLDER_SURFACE;

        world.set(x, surface, BlockType::Grass);

        for (int y = surface + 1; y < WORLD_HEIGHT; ++y)
        {
            const BlockType type =
                (y <= surface + DIRT_DEPTH) ? BlockType::Dirt : BlockType::Stone;

            world.set(x, y, type);
        }
    }
}
