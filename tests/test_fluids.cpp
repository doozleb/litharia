#include "doctest.h"

#include "Blocks/Blocks.h"

TEST_CASE("isWater/isLava/isFluid correctly classify every fluid level, and nothing else")
{
    for (int level = 1; level <= 8; ++level)
    {
        CHECK(isWater(waterAtLevel(level)));
        CHECK_FALSE(isLava(waterAtLevel(level)));
        CHECK(isFluid(waterAtLevel(level)));

        CHECK(isLava(lavaAtLevel(level)));
        CHECK_FALSE(isWater(lavaAtLevel(level)));
        CHECK(isFluid(lavaAtLevel(level)));
    }

    CHECK_FALSE(isFluid(BlockType::Air));
    CHECK_FALSE(isFluid(BlockType::Stone));
    CHECK_FALSE(isFluid(BlockType::Obsidian));
    CHECK_FALSE(isWater(BlockType::Lava8));
    CHECK_FALSE(isLava(BlockType::Water8));
}

TEST_CASE("fluidLevel round-trips with waterAtLevel/lavaAtLevel, and is 0 for non-fluid blocks")
{
    for (int level = 1; level <= 8; ++level)
    {
        CHECK(fluidLevel(waterAtLevel(level)) == level);
        CHECK(fluidLevel(lavaAtLevel(level)) == level);
    }

    CHECK(fluidLevel(BlockType::Air) == 0);
    CHECK(fluidLevel(BlockType::Stone) == 0);
    CHECK(fluidLevel(BlockType::Obsidian) == 0);
}

TEST_CASE("fluidAtLevel clamps to Air at or below zero, and matches the source's fluid family")
{
    CHECK(fluidAtLevel(BlockType::Water8, 0) == BlockType::Air);
    CHECK(fluidAtLevel(BlockType::Water8, -1) == BlockType::Air);
    CHECK(fluidAtLevel(BlockType::Water8, 5) == BlockType::Water5);
    CHECK(fluidAtLevel(BlockType::Water1, 5) == BlockType::Water5);
    CHECK(fluidAtLevel(BlockType::Lava3, 5) == BlockType::Lava5);
    CHECK(fluidAtLevel(BlockType::Lava8, 1) == BlockType::Lava1);
}

TEST_CASE("every fluid block is non-solid, unmineable, and drops nothing")
{
    for (int level = 1; level <= 8; ++level)
    {
        const BlockInfo& water = blockInfo(waterAtLevel(level));
        CHECK_FALSE(water.solid);
        CHECK(water.requiredTool == ToolType::None);
        CHECK(water.drop == BlockType::Air);
        CHECK_FALSE(water.name.empty());

        const BlockInfo& lava = blockInfo(lavaAtLevel(level));
        CHECK_FALSE(lava.solid);
        CHECK(lava.requiredTool == ToolType::None);
        CHECK(lava.drop == BlockType::Air);
        CHECK_FALSE(lava.name.empty());
    }
}

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Core/Constants.h"
#include "World/FluidSim.h"
#include "World/FluidSurface.h"
#include "World/World.h"
#include "World/TerrainGenerator.h"

namespace
{
constexpr float FLUID_STEP = FluidSim::TICK_INTERVAL;

int fluidLevelAt(const World& world, int x, int y)
{
    return fluidLevel(world.get(x, y));
}
}

TEST_CASE("a fluid tile falls straight down into open air")
{
    World world;
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 10) == BlockType::Air);
    CHECK(world.get(10, 11) == BlockType::Water8);
}

TEST_CASE("a fluid tile at the bottom row of the world does not fall out of bounds and vanish")
{
    World world;
    const int y = WORLD_HEIGHT - 1;
    world.set(4, y, BlockType::Stone); // walls on both sides, so the only
    world.set(6, y, BlockType::Stone); // possible move is falling off the
                                       // bottom edge of the world
    world.set(5, y, BlockType::Water8);

    FluidSim sim;
    sim.activate(5, y);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // There is nothing below the bottom row (world.get there is the OOB "Air").
    // The fall-into-air branch must not fire against an out-of-bounds
    // destination, or the tile would be cleared here while the write below it
    // is silently dropped by World::set - a net loss of fluid at the edge.
    CHECK(world.get(5, y) == BlockType::Water8);
}

TEST_CASE("a fluid tile at the left column of the world does not spread out of bounds and vanish")
{
    World world;
    const int y = 10;
    world.set(0, y + 1, BlockType::Stone); // floor - nothing to fall onto
    world.set(1, y, BlockType::Stone);     // right neighbor blocked - the only
                                            // remaining move is spreading left,
                                            // which is off the world edge
    world.set(0, y, BlockType::Water8);

    FluidSim sim;
    sim.activate(0, y);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // Column -1 is out of bounds (World::get returns Air there). The spread
    // branch must not treat that as an open destination, or the tile's level
    // would be halved here while the write to column -1 is silently dropped -
    // a net loss of fluid at the edge.
    CHECK(world.get(0, y) == BlockType::Water8);
}

TEST_CASE("an adjacent lava pool and water pool react end-to-end, producing obsidian and strictly reducing total fluid")
{
    World world;

    const int x_lava = 10;
    const int x_water = 11;
    const int yTop = 10;
    const int yBottom = 15; // exclusive - fluid occupies [yTop, yBottom)

    // Stone floor under both columns, and stone walls enclosing both sides so
    // neither pool can spread away from the contact line between them.
    for (int y = yTop; y < yBottom; ++y)
    {
        world.set(x_lava - 1, y, BlockType::Stone);  // left wall
        world.set(x_water + 1, y, BlockType::Stone); // right wall
    }
    for (int x = x_lava; x <= x_water; ++x)
        world.set(x, yBottom, BlockType::Stone); // floor

    for (int y = yTop; y < yBottom; ++y)
    {
        world.set(x_lava, y, BlockType::Lava8);
        world.set(x_water, y, BlockType::Water8);
    }

    int totalBefore = 0;
    for (int y = yTop; y < yBottom; ++y)
    {
        totalBefore += fluidLevelAt(world, x_lava, y);
        totalBefore += fluidLevelAt(world, x_water, y);
    }

    FluidSim sim;
    sim.activateAll(world);

    std::vector<sf::Vector2i> changed;
    for (int i = 0; i < 100; ++i)
        sim.tick(world, FLUID_STEP, changed);

    bool obsidianExists = false;
    int totalAfter = 0;
    for (int y = yTop; y < yBottom; ++y)
    {
        if (world.get(x_lava, y) == BlockType::Obsidian || world.get(x_water, y) == BlockType::Obsidian)
            obsidianExists = true;

        totalAfter += fluidLevelAt(world, x_lava, y);
        totalAfter += fluidLevelAt(world, x_water, y);
    }

    CHECK(obsidianExists);
    CHECK(totalAfter < totalBefore);
}

TEST_CASE("a fully-enclosed fluid tile does not fall through solid ground")
{
    World world;
    world.set(10, 11, BlockType::Stone); // floor below
    world.set(9, 10, BlockType::Stone);  // walls on both sides, so the only
    world.set(11, 10, BlockType::Stone); // possible move left is falling down
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // Nowhere to fall (Stone below) and nowhere to spread (Stone both sides):
    // the tile stays put rather than draining through the solid floor. The
    // sideways-open case is exercised separately by the spread test below - it
    // must NOT share this test's setup, or the two would assert opposite
    // outcomes for identical input.
    CHECK(world.get(10, 10) == BlockType::Water8);
    CHECK(world.get(10, 11) == BlockType::Stone);
}

TEST_CASE("a resting, uneven row of water settles flat and conserves its total")
{
    World world;

    // A four-wide basin: stone floor, stone walls at each end.
    for (int x = 8; x <= 11; ++x)
        world.set(x, 11, BlockType::Stone);
    world.set(7, 10, BlockType::Stone);
    world.set(12, 10, BlockType::Stone);

    // Deep on the left, shallow on the right - total 8 + 8 + 2 + 2 = 20.
    world.set(8, 10, BlockType::Water8);
    world.set(9, 10, BlockType::Water8);
    world.set(10, 10, BlockType::Water2);
    world.set(11, 10, BlockType::Water2);

    FluidSim sim;
    for (int x = 8; x <= 11; ++x)
        sim.activate(x, 10);

    // Gradual slosh: run to a full stop rather than a single snap.
    std::vector<sf::Vector2i> changed;
    int emptyRun = 0;
    for (int i = 0; i < 200 && emptyRun < 3; ++i)
    {
        changed.clear();
        sim.tick(world, FLUID_STEP, changed);
        emptyRun = changed.empty() ? emptyRun + 1 : 0;
    }

    // 20 over 4 columns settles to exactly 5 each, conserving the total.
    int total = 0;
    for (int x = 8; x <= 11; ++x)
        total += fluidLevelAt(world, x, 10);
    CHECK(total == 20);
    for (int x = 8; x <= 11; ++x)
        CHECK(world.get(x, 10) == BlockType::Water5);
}

TEST_CASE("an uneven run settles flat within one level and conserves exactly")
{
    World world;

    for (int x = 8; x <= 11; ++x)
        world.set(x, 11, BlockType::Stone);
    world.set(7, 10, BlockType::Stone);
    world.set(12, 10, BlockType::Stone);

    // Total 6 + 6 + 6 + 1 = 19 over 4 columns. Conservative settling keeps the
    // total at 19 (three cells at 5, one at 4) - flat within a single level -
    // rather than rounding to 20.
    world.set(8, 10, BlockType::Water6);
    world.set(9, 10, BlockType::Water6);
    world.set(10, 10, BlockType::Water6);
    world.set(11, 10, BlockType::Water1);

    FluidSim sim;
    for (int x = 8; x <= 11; ++x)
        sim.activate(x, 10);

    std::vector<sf::Vector2i> changed;
    int emptyRun = 0;
    for (int i = 0; i < 200 && emptyRun < 3; ++i)
    {
        changed.clear();
        sim.tick(world, FLUID_STEP, changed);
        emptyRun = changed.empty() ? emptyRun + 1 : 0;
    }

    int total = 0;
    int minLevel = 8;
    int maxLevel = 1;
    for (int x = 8; x <= 11; ++x)
    {
        const int lvl = fluidLevelAt(world, x, 10);
        total += lvl;
        minLevel = std::min(minLevel, lvl);
        maxLevel = std::max(maxLevel, lvl);
    }
    CHECK(total == 19);            // conserved exactly
    CHECK(maxLevel - minLevel <= 1); // flat within one level
}

TEST_CASE("water widens into open, supported space beside it")
{
    World world;

    // A flat stone floor, no walls: a lone deep tile should spread sideways.
    for (int x = 8; x <= 12; ++x)
        world.set(x, 11, BlockType::Stone);
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // The neighbouring air rests on stone, so the tile widens into it, moving
    // half its level (ties go left). It grows one cell wider per step.
    CHECK(world.get(10, 10) == BlockType::Water4);
    CHECK(world.get(9, 10) == BlockType::Water4);
}

TEST_CASE("water spills over a ledge and falls down the far side")
{
    World world;

    // A one-tile shelf: stone under the water and a stone wall on its left, but
    // open air to the right with open air beneath it - a ledge.
    world.set(10, 11, BlockType::Stone);
    world.set(9, 10, BlockType::Stone); // wall: nothing to level or widen into
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // (11,10) is air with air beneath it, so the resting tile tips over the
    // edge, ready to fall down the far side next step.
    CHECK(world.get(11, 10) == BlockType::Water8);
    CHECK(world.get(10, 10) == BlockType::Air);
}

TEST_CASE("a fluid tile falling onto a lower-level match fills the space below as much as fits")
{
    World world;
    world.set(10, 11, BlockType::Water3);
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // The tile below had room for 5 more (3 -> 8); the tile above pours all 5
    // of that down in one step, filling the space rather than trickling one
    // level at a time. Nothing is created or lost: 8 + 3 == 8 + 3.
    CHECK(world.get(10, 11) == BlockType::Water8);
    CHECK(world.get(10, 10) == BlockType::Water3);
}

TEST_CASE("lava adjacent to water solidifies into obsidian and the water loses one level")
{
    World world;
    world.set(10, 10, BlockType::Lava8);
    world.set(11, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 10) == BlockType::Obsidian);
    CHECK(world.get(11, 10) == BlockType::Water7);
}

TEST_CASE("obsidian seals the reaction site - it never reverts or reacts again")
{
    World world;
    world.set(10, 10, BlockType::Lava8);
    world.set(11, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);
    sim.activate(11, 10);

    std::vector<sf::Vector2i> changed;

    for (int i = 0; i < 5; ++i)
        sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 10) == BlockType::Obsidian);
}

TEST_CASE("a full lava wall meeting a full water wall along a 10-tile contact yields exactly 10 obsidian")
{
    World world;

    for (int y = 0; y < 10; ++y)
    {
        world.set(10, y, BlockType::Lava8);
        world.set(11, y, BlockType::Water8);
    }

    // Activate only the lava front. This is a single-step reaction test: if the
    // water column were also activated this tick, each water tile - after being
    // drained one level by its lava neighbor's reaction - would independently
    // fall/spread later in the SAME step (it's still in the active batch),
    // obscuring the clean "-1 level" outcome. That broader multi-tick flow is
    // already exercised by the fall/spread tests above; here we isolate the
    // reaction itself.
    FluidSim sim;
    for (int y = 0; y < 10; ++y)
        sim.activate(10, y);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    int obsidianCount = 0;
    for (int y = 0; y < 10; ++y)
        if (world.get(10, y) == BlockType::Obsidian)
            ++obsidianCount;

    CHECK(obsidianCount == 10);

    for (int y = 0; y < 10; ++y)
        CHECK(world.get(11, y) == BlockType::Water7);
}

TEST_CASE("a body of water settles to a flat surface, filling the width of its basin")
{
    World world;

    const int floorY = 20;
    const int leftWall = 9;
    const int rightWall = 21;

    // A basin: stone floor and two stone walls.
    for (int x = leftWall; x <= rightWall; ++x)
        world.set(x, floorY, BlockType::Stone);
    for (int y = 0; y <= floorY; ++y)
    {
        world.set(leftWall, y, BlockType::Stone);
        world.set(rightWall, y, BlockType::Stone);
    }

    // All the water starts piled in one central column.
    for (int y = 11; y < floorY; ++y)
        world.set(15, y, BlockType::Water8);

    FluidSim sim;
    sim.activateAll(world);

    std::vector<sf::Vector2i> changed;
    for (int i = 0; i < 400; ++i)
        sim.tick(world, FLUID_STEP, changed);

    // Surface (topmost water row) of each interior column that holds water.
    int columnsWithWater = 0;
    int minSurface = floorY;
    int maxSurface = 0;

    for (int x = leftWall + 1; x < rightWall; ++x)
    {
        int surface = -1;
        for (int y = 0; y < floorY; ++y)
        {
            if (isWater(world.get(x, y)))
            {
                surface = y;
                break;
            }
        }

        if (surface >= 0)
        {
            ++columnsWithWater;
            if (surface < minSurface) minSurface = surface;
            if (surface > maxSurface) maxSurface = surface;
        }
    }

    // It spread out of the single starting column to fill most of the basin...
    CHECK(columnsWithWater >= 9);

    // ...and the top surface came to rest flat, within a single level step.
    CHECK(maxSurface - minSurface <= 1);
}

TEST_CASE("lava flows slower than water: after the same time it has fallen less far")
{
    World world; // empty - all air

    world.set(5, 0, BlockType::Water8);
    world.set(15, 0, BlockType::Lava8);

    FluidSim sim;
    sim.activate(5, 0);
    sim.activate(15, 0);

    std::vector<sf::Vector2i> changed;
    for (int i = 0; i < 9; ++i)
        sim.tick(world, FLUID_STEP, changed);

    auto rowOf = [&](int x, bool water) {
        for (int y = 0; y < WORLD_HEIGHT; ++y)
        {
            const BlockType b = world.get(x, y);
            if (water ? isWater(b) : isLava(b))
                return y;
        }
        return -1;
    };

    const int waterRow = rowOf(5, true);
    const int lavaRow = rowOf(15, false);

    REQUIRE(waterRow >= 0);
    REQUIRE(lavaRow >= 0);

    // Water falls every step; lava only on one step in LAVA_MOVE_INTERVAL, so in
    // the same number of ticks the water has dropped well below the lava.
    CHECK(lavaRow > 0);        // lava did creep down some
    CHECK(waterRow > lavaRow); // but water is much further down
}

TEST_CASE("tick does nothing until a full TICK_INTERVAL has accumulated")
{
    World world;
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FluidSim::TICK_INTERVAL * 0.4f, changed);

    // Well under one interval: nothing has happened yet.
    CHECK(world.get(10, 10) == BlockType::Water8);
    CHECK(world.get(10, 11) == BlockType::Air);

    sim.tick(world, FluidSim::TICK_INTERVAL * 0.7f, changed);

    // 0.4 + 0.7 = 1.1 intervals - comfortably past the threshold (not a
    // razor's-edge 0.5 + 0.5, which float rounding could land on either side
    // of) - so the tile falls.
    CHECK(world.get(10, 10) == BlockType::Air);
    CHECK(world.get(10, 11) == BlockType::Water8);
}

TEST_CASE("equalize leaves a one-level surface difference alone (no flicker)")
{
    World world;
    world.set(8, 11, BlockType::Stone);
    world.set(9, 11, BlockType::Stone);
    world.set(7, 10, BlockType::Stone);
    world.set(10, 10, BlockType::Stone);
    world.set(8, 10, BlockType::Water5);
    world.set(9, 10, BlockType::Water4);

    FluidSim sim;
    sim.activate(8, 10);
    sim.activate(9, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // A difference of one is the stable remainder: nothing moves.
    CHECK(world.get(8, 10) == BlockType::Water5);
    CHECK(world.get(9, 10) == BlockType::Water4);
    CHECK(changed.empty());
}

TEST_CASE("equalize sloshes one unit per step toward level, and settles flat, conserving")
{
    World world;
    world.set(8, 11, BlockType::Stone);
    world.set(9, 11, BlockType::Stone);
    world.set(7, 10, BlockType::Stone);
    world.set(10, 10, BlockType::Stone);
    world.set(8, 10, BlockType::Water8);
    world.set(9, 10, BlockType::Water2);

    // Activate ONLY the higher cell so exactly one cell is processed this tick,
    // making the one-unit slosh deterministic.
    FluidSim sim;
    sim.activate(8, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // One unit relocated from the run's highest cell to its lowest: 8,2 -> 7,3.
    CHECK(world.get(8, 10) == BlockType::Water7);
    CHECK(world.get(9, 10) == BlockType::Water3);
    CHECK(fluidLevelAt(world, 8, 10) + fluidLevelAt(world, 9, 10) == 10);

    int emptyRun = 0;
    for (int i = 0; i < 50 && emptyRun < 3; ++i)
    {
        changed.clear();
        sim.tick(world, FLUID_STEP, changed);
        emptyRun = changed.empty() ? emptyRun + 1 : 0;
    }
    CHECK(world.get(8, 10) == BlockType::Water5);
    CHECK(world.get(9, 10) == BlockType::Water5);
    CHECK(fluidLevelAt(world, 8, 10) + fluidLevelAt(world, 9, 10) == 10);
}

TEST_CASE("a settled pool produces no further changes (fully quiescent)")
{
    World world;
    for (int x = 8; x <= 11; ++x)
        world.set(x, 11, BlockType::Stone);
    world.set(7, 10, BlockType::Stone);
    world.set(12, 10, BlockType::Stone);
    for (int x = 8; x <= 11; ++x)
        world.set(x, 10, BlockType::Water5);

    FluidSim sim;
    for (int x = 8; x <= 11; ++x)
        sim.activate(x, 10);

    std::vector<sf::Vector2i> changed;
    for (int i = 0; i < 5; ++i)
        sim.tick(world, FLUID_STEP, changed);

    // Already flat and conserved: an equal, resting row never churns.
    CHECK(changed.empty());
    for (int x = 8; x <= 11; ++x)
        CHECK(world.get(x, 10) == BlockType::Water5);
}

TEST_CASE("every generated world's fluids settle to a full stop")
{
    // Seeds 1, 42 and 555 all oscillated forever under the old flatten/spread
    // rules; a conservative sim must bring each to quiescence.
    for (std::uint32_t seed : {1u, 42u, 555u, 7u, 88u})
    {
        World world;
        TerrainGenerator gen(seed);
        gen.generate(world);

        FluidSim sim;
        sim.activateAll(world);

        bool settled = false;
        int emptyRun = 0;
        for (int i = 0; i < 4000; ++i)
        {
            std::vector<sf::Vector2i> changed;
            sim.tick(world, FLUID_STEP, changed);
            emptyRun = changed.empty() ? emptyRun + 1 : 0;
            if (emptyRun >= 3) { settled = true; break; }
        }

        CHECK(settled);
    }
}

TEST_CASE("fluidSurfaceHeight reports one flat height across a within-one-level run")
{
    World world;
    for (int x = 8; x <= 10; ++x)
        world.set(x, 11, BlockType::Stone);

    // Surface run 5, 4, 5 with open air above each: average (5+4+5)/3 = 4.667.
    world.set(8, 10, BlockType::Water5);
    world.set(9, 10, BlockType::Water4);
    world.set(10, 10, BlockType::Water5);

    const float h8 = fluidSurfaceHeight(world, 8, 10);
    const float h9 = fluidSurfaceHeight(world, 9, 10);
    const float h10 = fluidSurfaceHeight(world, 10, 10);

    CHECK(h8 == doctest::Approx(h9));
    CHECK(h9 == doctest::Approx(h10));
    CHECK(h8 == doctest::Approx((14.0f / 3.0f) / 8.0f));
}

TEST_CASE("fluidSurfaceHeight stops a run at a solid gap and at a different fluid")
{
    World world;
    world.set(8, 10, BlockType::Water8);
    world.set(9, 10, BlockType::Stone);  // gap breaks the run
    world.set(10, 10, BlockType::Water2);
    world.set(11, 10, BlockType::Lava8); // different fluid breaks the run

    // The run through x=8 is just {8}: 8/8 = 1.0.
    CHECK(fluidSurfaceHeight(world, 8, 10) == doctest::Approx(1.0f));
    // The run through x=10 is just {10} (stone left, lava right): 2/8.
    CHECK(fluidSurfaceHeight(world, 10, 10) == doctest::Approx(2.0f / 8.0f));
}

TEST_CASE("fluidSurfaceHeight on a submerged tile reports its own level, not the run")
{
    World world;
    world.set(8, 11, BlockType::Stone);
    // A two-tall column: surface Water4 on top of a full Water8 (submerged).
    world.set(8, 9, BlockType::Water4);
    world.set(8, 10, BlockType::Water8);

    // The submerged tile (fluid directly above) is not a surface run member, so
    // it reports its own level (8/8 = 1.0), not an average.
    CHECK(fluidSurfaceHeight(world, 8, 10) == doctest::Approx(1.0f));
    // The surface tile above it reports its own level (lone run).
    CHECK(fluidSurfaceHeight(world, 8, 9) == doctest::Approx(4.0f / 8.0f));
}

TEST_CASE("fluidSurfaceHeight returns 0 for a non-fluid tile")
{
    World world;
    world.set(8, 10, BlockType::Stone);
    CHECK(fluidSurfaceHeight(world, 8, 10) == doctest::Approx(0.0f));
    CHECK(fluidSurfaceHeight(world, 5, 5) == doctest::Approx(0.0f)); // air
}
