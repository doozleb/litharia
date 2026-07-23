#include "doctest.h"

#include <cstdint>

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "Enemies/Enemy.h"
#include "Enemies/EnemySpawner.h"
#include "World/TerrainGenerator.h"
#include "World/World.h"

namespace
{

// Deep enough that TerrainGenerator::SURFACE_MAX (260) can never reach it
// for any column - guarantees "underground" no matter where the real
// surface rolls for that seed.
constexpr int DEEP_Y = 400;

// Shallow enough that TerrainGenerator::SURFACE_MIN (80) is always at least
// this far below it, and well within SPAWN_VERTICAL_SEARCH_TILES.
constexpr int SHALLOW_Y = 50;

ViewBounds viewCenteredOn(int tileY, float leftTileX = 500.0f, float rightTileX = 520.0f)
{
    return ViewBounds{leftTileX * TILE_SIZE,
                       static_cast<float>(tileY - 5) * TILE_SIZE,
                       rightTileX * TILE_SIZE,
                       static_cast<float>(tileY + 5) * TILE_SIZE};
}

// Carves an open-pocket foothold at (x, y) on both of attemptSpawn's two
// possible candidate columns (view.left - margin, view.right + margin) - the
// test doesn't need to know which edge the implementation's internal
// coin-flip lands on.
void carveFootholdOnBothEdges(World& world, const ViewBounds& view, int y)
{
    const int leftX = static_cast<int>(view.left / TILE_SIZE) - SPAWN_MARGIN_TILES;
    const int rightX = static_cast<int>(view.right / TILE_SIZE) + SPAWN_MARGIN_TILES;

    for (int x : {leftX, rightX})
    {
        world.set(x, y, BlockType::Air);
        world.set(x, y + 1, BlockType::Stone);
    }
}

} // namespace

TEST_CASE("an underground foothold always spawns a Nightstalker, day or night")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(DEEP_Y);
    carveFootholdOnBothEdges(world, view, DEEP_Y);

    const auto nightResult = attemptSpawn(world, generator, view, 0.0f, 0, 1u);
    REQUIRE(nightResult.has_value());
    CHECK(nightResult->type == EnemyType::Nightstalker);

    const auto dayResult = attemptSpawn(world, generator, view, 1.0f, 0, 2u);
    REQUIRE(dayResult.has_value());
    CHECK(dayResult->type == EnemyType::Nightstalker);
}

TEST_CASE("an above-ground foothold at night spawns a Nightstalker")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(SHALLOW_Y);

    // SHALLOW_Y (50) is meant to sit just above the real surface so the
    // vertical search finds it within SPAWN_VERTICAL_SEARCH_TILES - but that
    // only holds if this view's actual columns happen to roll a surface
    // near TerrainGenerator::SURFACE_MIN (80). They don't: for seed 2026u,
    // surfaceHeight(497) == 184 and surfaceHeight(523) == 172 (verified via
    // a throwaway probe against the real noise/surface code) - both far
    // outside the [50, 90] window the search actually covers from here, so
    // the natural terrain never gives this test a foothold to find. Carve
    // one explicitly instead, the same way carveFootholdOnBothEdges already
    // does for the underground case, so the test exercises the night/day/
    // cave classification rather than incidental terrain noise.
    carveFootholdOnBothEdges(world, view, SHALLOW_Y);

    const auto result = attemptSpawn(world, generator, view, 0.0f, 0, 3u);

    REQUIRE(result.has_value());
    CHECK(result->type == EnemyType::Nightstalker);
}

TEST_CASE("an above-ground foothold by day never spawns a Nightstalker, and only rarely spawns a Sunroamer")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(SHALLOW_Y);

    // See the comment in the previous test case: the real terrain at this
    // view's columns doesn't roll a surface within reach of SHALLOW_Y, so
    // carve an explicit above-ground foothold rather than relying on it.
    carveFootholdOnBothEdges(world, view, SHALLOW_Y);

    int sunroamerCount = 0;
    for (std::uint32_t salt = 0; salt < 500; ++salt)
    {
        const auto result = attemptSpawn(world, generator, view, 1.0f, 0, salt);
        if (!result.has_value())
            continue;

        CHECK(result->type == EnemyType::Sunroamer);
        ++sunroamerCount;
    }

    // ~8% (SUNROAMER_SPAWN_CHANCE) of 500 is ~40; a wide band avoids
    // flakiness while still catching an "always" or "never" bug.
    CHECK(sunroamerCount > 10);
    CHECK(sunroamerCount < 150);
}

TEST_CASE("no foothold within the search range returns nullopt")
{
    World world; // default-constructed: every tile is Air, so no foothold ever exists.
    TerrainGenerator generator(2026u);

    const ViewBounds view = viewCenteredOn(SHALLOW_Y);

    CHECK_FALSE(attemptSpawn(world, generator, view, 0.0f, 0, 4u).has_value());
    CHECK_FALSE(attemptSpawn(world, generator, view, 1.0f, 0, 5u).has_value());
}

TEST_CASE("attemptSpawn never spawns at or above MAX_ENEMIES")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(DEEP_Y);
    carveFootholdOnBothEdges(world, view, DEEP_Y);

    CHECK_FALSE(attemptSpawn(world, generator, view, 0.0f, MAX_ENEMIES, 6u).has_value());
    CHECK_FALSE(attemptSpawn(world, generator, view, 0.0f, MAX_ENEMIES + 5, 7u).has_value());
}

TEST_CASE("a spawned enemy's bottom edge is seated on top of the floor tile, not embedded in it")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(DEEP_Y);
    carveFootholdOnBothEdges(world, view, DEEP_Y);

    // DEEP_Y is always underground (see its comment above), so this always
    // spawns a Nightstalker at foundY == DEEP_Y - the exact tile the
    // foothold was carved at.
    const auto result = attemptSpawn(world, generator, view, 0.0f, 0, 1u);
    REQUIRE(result.has_value());

    const float height = enemyInfo(result->type).height;
    const float expectedFloorTopY = static_cast<float>(DEEP_Y + 1) * TILE_SIZE;

    CHECK(result->position.y + height == doctest::Approx(expectedFloorTopY));
}

TEST_CASE("attemptSpawn is a pure function: identical inputs always give the identical result")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(DEEP_Y);
    carveFootholdOnBothEdges(world, view, DEEP_Y);

    const auto first = attemptSpawn(world, generator, view, 0.3f, 2, 42u);
    const auto second = attemptSpawn(world, generator, view, 0.3f, 2, 42u);

    REQUIRE(first.has_value() == second.has_value());
    if (first.has_value())
    {
        CHECK(first->type == second->type);
        CHECK(first->position == second->position);
    }
}
