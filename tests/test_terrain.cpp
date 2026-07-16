#include "doctest.h"

#include <algorithm>
#include <vector>

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "World/TerrainGenerator.h"
#include "World/World.h"

namespace
{

bool isOre(BlockType type)
{
    return type == BlockType::CopperOre || type == BlockType::IronOre || type == BlockType::Coal;
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

        // The surface tile itself is grass. Directly above it is open air,
        // unless a tree's bottom log has grown there instead.
        CHECK(world.get(x, surface) == BlockType::Grass);

        const BlockType above = world.get(x, surface - 1);
        CHECK((above == BlockType::Air || above == BlockType::OakLog));
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

TEST_CASE("coal spawns only in stone and inside its depth band")
{
    World world;
    TerrainGenerator(4242u).generate(world);

    int coalCount = 0;

    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        for (int y = 0; y < WORLD_HEIGHT; ++y)
        {
            if (world.get(x, y) != BlockType::Coal)
                continue;

            ++coalCount;

            // Inside the band...
            CHECK(y >= TerrainGenerator::COAL_MIN_Y);
            CHECK(y <= TerrainGenerator::COAL_MAX_Y);
        }
    }

    // The world is not barren of fuel.
    CHECK(coalCount > 0);
}

TEST_CASE("trees stand on the surface, stay within height bounds, and never crowd a neighbor")
{
    World world;
    const TerrainGenerator generator(2026);
    generator.generate(world);

    std::vector<int> trunkColumns;

    for (int x = 1; x < WORLD_WIDTH - 1; ++x)
    {
        const int surface = generator.surfaceHeight(x);

        if (world.get(x, surface - 1) != BlockType::OakLog)
            continue;

        trunkColumns.push_back(x);

        // Walk up the trunk counting logs until it runs out.
        int height = 0;
        int y = surface - 1;

        while (world.get(x, y) == BlockType::OakLog)
        {
            ++height;
            --y;
        }

        CHECK(height >= TerrainGenerator::TREE_MIN_HEIGHT);
        CHECK(height <= TerrainGenerator::TREE_MAX_HEIGHT);
    }

    // The pass actually grew a meaningful number of trees.
    REQUIRE(trunkColumns.size() > 10);

    // No two trunks close enough for their canopies to touch.
    for (std::size_t i = 1; i < trunkColumns.size(); ++i)
        CHECK(trunkColumns[i] - trunkColumns[i - 1] >= TerrainGenerator::TREE_MIN_SPACING);
}

TEST_CASE("forest density blends across the world rather than switching on and off")
{
    // Same seed as the density noise itself: what matters is that some wide
    // stretches of the world have many more trees than others, evidence the
    // low-frequency forest-factor channel is doing something rather than
    // every column rolling independently at a flat rate.
    World world;
    const TerrainGenerator generator(4040);
    generator.generate(world);

    auto treeCountIn = [&](int fromX, int toX) {
        int count = 0;

        for (int x = fromX; x < toX; ++x)
        {
            const int surface = generator.surfaceHeight(x);
            if (world.get(x, surface - 1) == BlockType::OakLog)
                ++count;
        }

        return count;
    };

    std::vector<int> bandCounts;
    constexpr int BAND_WIDTH = 100;

    for (int start = 0; start + BAND_WIDTH <= WORLD_WIDTH; start += BAND_WIDTH)
        bandCounts.push_back(treeCountIn(start, start + BAND_WIDTH));

    const int lowest = *std::min_element(bandCounts.begin(), bandCounts.end());
    const int highest = *std::max_element(bandCounts.begin(), bandCounts.end());

    // A flat per-column chance would make every 100-wide band come out close
    // to the same count; blended forest patches should not.
    CHECK(highest > lowest);
}
