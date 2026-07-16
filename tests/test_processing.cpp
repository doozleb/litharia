#include "doctest.h"

#include "Machines/Machines.h"
#include "World/World.h"

namespace
{
constexpr float STEP = 1.0f / 60.0f;
}

TEST_CASE("a generator converts a coal into burn time when a consumer needs it")
{
    World world;
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down); // creates demand
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    m.tick(world, STEP, mined);

    // The coal became fuel (minus the fraction burned this tick).
    Machine* gen = m.at(0, 0);
    CHECK(gen->input.empty());
    CHECK(gen->fuel > COAL_BURN_SECONDS - 1.0f);
    CHECK(gen->fuel <= COAL_BURN_SECONDS);
}

TEST_CASE("a generator with no load does not waste its coal")
{
    World world;
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right); // no consumer nearby
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 120; ++i)
        m.tick(world, STEP, mined);

    // It lit one coal (fuel is available) but, with nothing drawing power, it holds
    // that fuel steady rather than draining it.
    Machine* gen = m.at(0, 0);
    CHECK(gen->fuel == doctest::Approx(COAL_BURN_SECONDS));
}

TEST_CASE("a powered drill eats the ore below it and outputs onto a belt")
{
    World world;
    world.fill(BlockType::Air);
    world.set(0, 1, BlockType::CopperOre); // directly beneath the drill

    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Right); // outputs to the right
    m.place(MachineType::Belt, 2, 0, Direction::Right);
    REQUIRE(m.at(1, 0) != nullptr);

    // Put the ore under the drill at (1,2) as well: drill at (1,0) scans down.
    world.set(1, 1, BlockType::CopperOre);

    // Fuel the generator so the drill is powered.
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Drill work time is 3.0s; run 4s to be safe, plus belt handoff.
    for (int i = 0; i < 240; ++i)
        m.tick(world, step, mined);

    // The ore is not destroyed: the vein is inexhaustible now.
    CHECK(world.get(1, 1) == BlockType::CopperOre);

    // ...and copper ore reached the belt (or is sitting in the drill output).
    const bool onBelt = m.at(2, 0)->carried == ItemType::CopperOre;
    const bool inDrill = m.at(1, 0)->output.type == ItemType::CopperOre;
    CHECK((onBelt || inDrill));
}

TEST_CASE("a drill's vein never runs out: it mines the same tile again and again")
{
    World world;
    world.fill(BlockType::Air);
    world.set(1, 1, BlockType::CopperOre);

    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    // Facing Up with nothing to receive output, so it fills and we can count how
    // many times it has mined without a downstream machine interfering.
    m.place(MachineType::Drill, 1, 0, Direction::Up);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // One mining cycle (3.0s) fills the output; it then stays full since nothing
    // is there to take it. The tile must still be ore no matter how long this runs -
    // a finite vein of one tile would already be Air well before 10 seconds in.
    for (int i = 0; i < 600; ++i) // 10 seconds
        m.tick(world, step, mined);

    CHECK(world.get(1, 1) == BlockType::CopperOre);
    CHECK(m.at(1, 0)->output.type == ItemType::CopperOre);
}

TEST_CASE("a drill with no ore in reach stays idle")
{
    World world;
    world.fill(BlockType::Air); // nothing to mine

    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(1, 0)->output.empty());
    CHECK(mined.empty());
}

TEST_CASE("a powered smelter turns copper ore into a copper plate")
{
    World world;
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Smelter, 1, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));   // power
    REQUIRE(m.tryInsert(1, 0, ItemType::CopperOre)); // work

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Copper recipe is 2.0s.
    for (int i = 0; i < 150; ++i)
        m.tick(world, step, mined);

    Machine* s = m.at(1, 0);
    CHECK(s->input.empty());
    CHECK(s->output.type == ItemType::CopperPlate);
    CHECK(s->output.count == 1);
}

TEST_CASE("an unpowered smelter makes no progress")
{
    World world;
    Machines m;
    m.place(MachineType::Smelter, 1, 0, Direction::Right); // no generator
    REQUIRE(m.tryInsert(1, 0, ItemType::CopperOre));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 150; ++i)
        m.tick(world, step, mined);

    Machine* s = m.at(1, 0);
    CHECK(s->input.type == ItemType::CopperOre); // untouched
    CHECK(s->output.empty());
}

TEST_CASE("a generator supplying nothing does not burn, even next to a powered machine")
{
    World world;
    Machines m;

    // Both burners touch the drill, but its whole 5 comes from one of them.
    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);
    m.place(MachineType::BurnerGenerator, 7, 5, Direction::Right);
    m.place(MachineType::Drill, 6, 5, Direction::Down);

    REQUIRE(m.tryInsert(5, 5, ItemType::Coal));
    REQUIRE(m.tryInsert(7, 5, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 120; ++i)
        m.tick(world, STEP, mined);

    REQUIRE(m.at(6, 5)->powered);

    // Exactly one generator supplied it, so exactly one burned - asserted
    // without pinning down which, since that is down to neighbour scan order.
    const float left = m.at(5, 5)->fuel;
    const float right = m.at(7, 5)->fuel;

    const bool leftBurned = left < COAL_BURN_SECONDS
        && right == doctest::Approx(COAL_BURN_SECONDS);
    const bool rightBurned = right < COAL_BURN_SECONDS
        && left == doctest::Approx(COAL_BURN_SECONDS);

    CHECK((leftBurned || rightBurned));
}
