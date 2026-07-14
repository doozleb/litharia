#include "doctest.h"

#include <vector>

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "World/TerrainGenerator.h"
#include "World/World.h"

namespace
{

bool isOre(BlockType type)
{
    return type == BlockType::CopperOre || type == BlockType::IronOre;
}

bool sameWorld(const World& a, const World& b)
{
    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            if (a.get(x, y) != b.get(x, y))
                return false;

    return true;
}

} // namespace

TEST_CASE("the same seed produces a byte-identical world")
{
    World a;
    World b;

    TerrainGenerator(4242).generate(a);
    TerrainGenerator(4242).generate(b);

    CHECK(sameWorld(a, b));
}

TEST_CASE("different seeds produce different worlds")
{
    World a;
    World b;

    TerrainGenerator(1).generate(a);
    TerrainGenerator(2).generate(b);

    CHECK_FALSE(sameWorld(a, b));
}

TEST_CASE("every column has a surface, inside the world bounds")
{
    World world;
    const TerrainGenerator generator(777);
    generator.generate(world);

    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        const int surface = generator.surfaceHeight(x);

        CHECK(surface > 0);
        CHECK(surface < WORLD_HEIGHT);

        // The surface tile itself is grass, and there is open air directly above it.
        CHECK(world.get(x, surface) == BlockType::Grass);
        CHECK(world.get(x, surface - 1) == BlockType::Air);
    }
}

TEST_CASE("the surface actually rolls rather than sitting flat")
{
    const TerrainGenerator generator(31337);

    int lowest = WORLD_HEIGHT;
    int highest = 0;

    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        const int surface = generator.surfaceHeight(x);
        lowest = std::min(lowest, surface);
        highest = std::max(highest, surface);
    }

    CHECK(highest - lowest > 10);
}

TEST_CASE("extreme noise cannot push the surface out of the world")
{
    // Sweep many seeds: no seed may produce a surface outside the clamped band.
    for (std::uint32_t seed = 0; seed < 40; ++seed)
    {
        const TerrainGenerator generator(seed);

        for (int x = 0; x < WORLD_WIDTH; x += 7)
        {
            const int surface = generator.surfaceHeight(x);

            REQUIRE(surface >= TerrainGenerator::SURFACE_MIN);
            REQUIRE(surface <= TerrainGenerator::SURFACE_MAX);
        }
    }
}

TEST_CASE("caves carve air below the surface")
{
    World world;
    const TerrainGenerator generator(99);
    generator.generate(world);

    int airBelowSurface = 0;

    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        const int surface = generator.surfaceHeight(x);

        for (int y = surface + 1; y < WORLD_HEIGHT; ++y)
            if (world.get(x, y) == BlockType::Air)
                ++airBelowSurface;
    }

    CHECK(airBelowSurface > 1000);
}

TEST_CASE("ore tiles only ever replace stone")
{
    const TerrainGenerator generator(2024);

    World full;
    generator.generate(full);

    // The same world with passes 1 and 2 only: whatever the ore pass overwrote
    // must have been stone in this one.
    World beforeOre;
    generator.generateBase(beforeOre);

    int oreCount = 0;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
    {
        for (int x = 0; x < WORLD_WIDTH; ++x)
        {
            if (!isOre(full.get(x, y)))
                continue;

            ++oreCount;
            REQUIRE(beforeOre.get(x, y) == BlockType::Stone);
        }
    }

    // And the pass did something.
    CHECK(oreCount > 500);
}

TEST_CASE("the base passes leave no ore behind at all")
{
    World world;
    TerrainGenerator(2024).generateBase(world);

    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            REQUIRE_FALSE(isOre(world.get(x, y)));
}

TEST_CASE("each ore stays inside its own depth band")
{
    World world;
    TerrainGenerator(555).generate(world);

    int copper = 0;
    int iron = 0;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
    {
        for (int x = 0; x < WORLD_WIDTH; ++x)
        {
            const BlockType type = world.get(x, y);

            if (type == BlockType::CopperOre)
            {
                ++copper;
                REQUIRE(y >= TerrainGenerator::COPPER_MIN_Y);
                REQUIRE(y <= TerrainGenerator::COPPER_MAX_Y);
            }
            else if (type == BlockType::IronOre)
            {
                ++iron;
                REQUIRE(y >= TerrainGenerator::IRON_MIN_Y);
                REQUIRE(y <= TerrainGenerator::IRON_MAX_Y);
            }
        }
    }

    // Both ores exist, and copper is the commoner shallow one.
    CHECK(copper > 100);
    CHECK(iron > 100);
}

TEST_CASE("iron sits deeper than copper on average")
{
    World world;
    TerrainGenerator(8080).generate(world);

    long long copperDepthSum = 0;
    long long ironDepthSum = 0;
    int copper = 0;
    int iron = 0;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
    {
        for (int x = 0; x < WORLD_WIDTH; ++x)
        {
            if (world.get(x, y) == BlockType::CopperOre)
            {
                copperDepthSum += y;
                ++copper;
            }
            else if (world.get(x, y) == BlockType::IronOre)
            {
                ironDepthSum += y;
                ++iron;
            }
        }
    }

    REQUIRE(copper > 0);
    REQUIRE(iron > 0);

    CHECK(static_cast<double>(ironDepthSum) / iron >
          static_cast<double>(copperDepthSum) / copper);
}

TEST_CASE("a dirt band separates grass from stone")
{
    World world;
    const TerrainGenerator generator(11);
    generator.generateBase(world);

    // Directly under the grass is dirt, never stone.
    for (int x = 0; x < WORLD_WIDTH; x += 13)
    {
        const int surface = generator.surfaceHeight(x);
        const BlockType below = world.get(x, surface + 1);

        // Air is allowed: a cave may open right under the turf.
        CHECK((below == BlockType::Dirt || below == BlockType::Air));
    }
}
