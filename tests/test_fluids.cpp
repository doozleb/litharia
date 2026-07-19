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

#include <vector>

#include "World/FluidSim.h"
#include "World/World.h"

namespace
{
constexpr float FLUID_STEP = FluidSim::TICK_INTERVAL;
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

TEST_CASE("a blocked fluid tile spreads sideways, splitting its level with the neighbor")
{
    World world;
    world.set(10, 11, BlockType::Stone); // floor - nothing to fall onto
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // Level 8 splits evenly: source keeps ceil(8/2)=4, left neighbor gets floor(8/2)=4.
    CHECK(world.get(10, 10) == BlockType::Water4);
    CHECK(world.get(9, 10) == BlockType::Water4);
}

TEST_CASE("a level-1 fluid tile cannot spread any further")
{
    World world;
    world.set(10, 11, BlockType::Stone);
    world.set(10, 10, BlockType::Water1);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 10) == BlockType::Water1);
    CHECK(world.get(9, 10) == BlockType::Air);
    CHECK(world.get(11, 10) == BlockType::Air);
}

TEST_CASE("a fluid tile falling onto a lower-level match of itself tops it up by exactly one level")
{
    World world;
    world.set(10, 11, BlockType::Water3);
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 11) == BlockType::Water4);
    CHECK(world.get(10, 10) == BlockType::Water7);
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
