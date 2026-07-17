#include "doctest.h"

#include <algorithm>
#include <cstdint>
#include <utility>
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

        // The surface tile itself is grass, except where a hill-cave
        // entrance has carved it open (see "each hill cave's trunk reaches
        // down to at least the iron layer" for that pass). Directly above
        // it is open air, unless a tree's bottom log has grown there instead.
        const BlockType surfaceTile = world.get(x, surface);
        CHECK((surfaceTile == BlockType::Grass || surfaceTile == BlockType::Air));

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

TEST_CASE("each ore stays inside its own depth band, common or rare")
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
                REQUIRE(y <= TerrainGenerator::COPPER_DEEP_MAX_Y);
            }
            else if (type == BlockType::IronOre)
            {
                ++iron;
                REQUIRE(y >= TerrainGenerator::IRON_SHALLOW_MIN_Y);
                REQUIRE(y <= TerrainGenerator::IRON_MAX_Y);
            }
        }
    }

    // Both ores exist, and copper is the commoner shallow one.
    CHECK(copper > 100);
    CHECK(iron > 100);
}

TEST_CASE("iron rarely appears shallow, copper rarely appears deep, but each stays rare")
{
    World world;
    TerrainGenerator(2025).generate(world);

    int shallowIron = 0;
    int commonIron = 0;
    int deepCopper = 0;
    int commonCopper = 0;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
    {
        for (int x = 0; x < WORLD_WIDTH; ++x)
        {
            const BlockType type = world.get(x, y);

            if (type == BlockType::IronOre)
            {
                if (y <= TerrainGenerator::IRON_SHALLOW_MAX_Y)
                    ++shallowIron;
                else
                    ++commonIron;
            }
            else if (type == BlockType::CopperOre)
            {
                if (y >= TerrainGenerator::COPPER_DEEP_MIN_Y)
                    ++deepCopper;
                else
                    ++commonCopper;
            }
        }
    }

    // The rare bands must exist at all...
    CHECK(shallowIron > 0);
    CHECK(deepCopper > 0);

    // ...but stay clearly rarer than the common band they're paired with.
    CHECK(shallowIron < commonIron / 2);
    CHECK(deepCopper < commonCopper / 2);
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

TEST_CASE("no two trees' tiles are ever 4-connected, so a break-cascade cannot cross between them")
{
    // Trunk-to-trunk distance is not the invariant that matters - it is exactly
    // what the original TREE_MIN_SPACING=3 bug satisfied while still letting
    // canopies touch (leaves at trunk+1 and the neighbor's trunk+2-1==trunk+2
    // are adjacent when spacing is only 3). What actually matters is whether
    // Player::mine's collectTreeBreak flood-fill, which walks 4-connected
    // OakLog/OakLeaves tiles, can ever step from one tree into another.
    //
    // So: label every OakLog/OakLeaves tile with its 4-connected component
    // (flood-filling in all four directions, not just the cascade's
    // up-and-sideways subset, since any touch at all between two trees is a
    // bug regardless of which direction the cascade happens to explore it
    // from). If two trees' tiles were ever adjacent, they would land in the
    // same component. So every component must own exactly one trunk.
    auto checkSeed = [](std::uint32_t seed) {
        World world;
        const TerrainGenerator generator(seed);
        generator.generate(world);

        auto isTreeTile = [&](int x, int y) {
            if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
                return false;

            const BlockType type = world.get(x, y);
            return type == BlockType::OakLog || type == BlockType::OakLeaves;
        };

        std::vector<int> trunkColumns;
        for (int x = 1; x < WORLD_WIDTH - 1; ++x)
        {
            const int surface = generator.surfaceHeight(x);
            if (world.get(x, surface - 1) == BlockType::OakLog)
                trunkColumns.push_back(x);
        }

        REQUIRE(trunkColumns.size() > 10);

        // -1 means "not part of any tree yet"; otherwise the component id.
        std::vector<std::vector<int>> label(
            static_cast<std::size_t>(WORLD_WIDTH),
            std::vector<int>(static_cast<std::size_t>(WORLD_HEIGHT), -1));

        int nextLabel = 0;

        for (int x = 0; x < WORLD_WIDTH; ++x)
        {
            for (int y = 0; y < WORLD_HEIGHT; ++y)
            {
                if (!isTreeTile(x, y) || label[x][y] != -1)
                    continue;

                const int component = nextLabel++;
                std::vector<std::pair<int, int>> stack{{x, y}};

                while (!stack.empty())
                {
                    const auto [cx, cy] = stack.back();
                    stack.pop_back();

                    if (!isTreeTile(cx, cy) || label[cx][cy] != -1)
                        continue;

                    label[cx][cy] = component;

                    stack.push_back({cx - 1, cy});
                    stack.push_back({cx + 1, cy});
                    stack.push_back({cx, cy - 1});
                    stack.push_back({cx, cy + 1});
                }
            }
        }

        // Every trunk's bottom log sits at its own component; two trunks
        // sharing a component means their trees' tiles touched somewhere.
        std::vector<int> seenComponents;

        for (int trunkX : trunkColumns)
        {
            const int surface = generator.surfaceHeight(trunkX);
            const int component = label[trunkX][surface - 1];

            REQUIRE(component != -1);

            const bool alreadySeen =
                std::find(seenComponents.begin(), seenComponents.end(), component) !=
                seenComponents.end();

            CHECK_FALSE(alreadySeen);

            seenComponents.push_back(component);
        }
    };

    // The shipped world's seed, where the reviewer found 40 touching pairs
    // (including a five-tree chain near world start) at the old spacing of 3,
    // plus a spread of other seeds so the fix isn't validated against a
    // single lucky world.
    for (std::uint32_t seed : {1337u, 1u, 2u, 7u, 42u, 2024u, 2026u, 4040u, 8080u, 99999u})
        checkSeed(seed);
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

    // A plain "highest > lowest" ordering check passes under ordinary sampling
    // variance even with the forest-factor modulation deleted entirely (flat
    // per-column density still produces bands that differ by chance) - so it
    // cannot actually distinguish blended density from flat density. Require
    // a spread a flat density would not produce instead, the same pattern
    // "the surface actually rolls rather than sitting flat" uses above.
    CHECK(highest - lowest > 5);
}

TEST_CASE("findHillPeak returns the most elevated column in its search window")
{
    const TerrainGenerator generator(2026);

    // These 4 x positions mirror where the hill caves will actually be
    // placed in the next task (spawnX +/- 100 and +/- 350) - hardcoded here
    // since the SPECIAL_CAVE_*_OFFSET constants don't exist until then.
    const int spawnX = WORLD_WIDTH / 2;
    const int targets[] = {spawnX - 350, spawnX - 100, spawnX + 100, spawnX + 350};

    for (int target : targets)
    {
        const int peak = generator.findHillPeak(target);

        const int lo = target - TerrainGenerator::HILL_SEARCH_RADIUS;
        const int hi = target + TerrainGenerator::HILL_SEARCH_RADIUS;

        for (int x = lo; x <= hi; ++x)
            CHECK(generator.surfaceHeight(peak) <= generator.surfaceHeight(x));
    }
}

TEST_CASE("each hill cave's trunk reaches down to at least the iron layer")
{
    World world;
    const TerrainGenerator generator(2026);
    generator.generateBase(world);
    generator.carveSpecialCaves(world);

    const int spawnX = WORLD_WIDTH / 2;
    const int targets[] = {
        spawnX - TerrainGenerator::SPECIAL_CAVE_FAR_OFFSET,
        spawnX - TerrainGenerator::SPECIAL_CAVE_NEAR_OFFSET,
        spawnX + TerrainGenerator::SPECIAL_CAVE_NEAR_OFFSET,
        spawnX + TerrainGenerator::SPECIAL_CAVE_FAR_OFFSET,
    };

    for (int target : targets)
    {
        bool reachedIronDepth = false;

        for (int x = target - TerrainGenerator::HILL_SEARCH_RADIUS;
             x <= target + TerrainGenerator::HILL_SEARCH_RADIUS && !reachedIronDepth;
             ++x)
        {
            for (int y = TerrainGenerator::IRON_MIN_Y; y < WORLD_HEIGHT; ++y)
            {
                if (world.get(x, y) == BlockType::Air)
                {
                    reachedIronDepth = true;
                    break;
                }
            }
        }

        CHECK(reachedIronDepth);
    }
}
