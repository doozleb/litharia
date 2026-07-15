#include "doctest.h"

#include "Machines/Machines.h"
#include "World/World.h"

TEST_CASE("a belt accepts one item and then is full")
{
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);

    CHECK(m.tryInsert(0, 0, ItemType::CopperOre));
    CHECK(m.at(0, 0)->carried == ItemType::CopperOre);

    // Already carrying: the next item is refused.
    CHECK_FALSE(m.tryInsert(0, 0, ItemType::IronOre));
    CHECK(m.at(0, 0)->carried == ItemType::CopperOre);
}

TEST_CASE("a smelter accepts smeltable ore but not plates or stone")
{
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Right);

    CHECK(m.tryInsert(0, 0, ItemType::CopperOre));
    CHECK(m.at(0, 0)->input.type == ItemType::CopperOre);
    CHECK(m.at(0, 0)->input.count == 1);

    // A second copper ore stacks.
    CHECK(m.tryInsert(0, 0, ItemType::CopperOre));
    CHECK(m.at(0, 0)->input.count == 2);

    // Non-smeltable input is refused.
    Machines m2;
    m2.place(MachineType::Smelter, 0, 0, Direction::Right);
    CHECK_FALSE(m2.tryInsert(0, 0, ItemType::Stone));
    CHECK_FALSE(m2.tryInsert(0, 0, ItemType::CopperPlate));
}

TEST_CASE("a generator accepts coal as fuel but nothing else")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);

    CHECK(m.tryInsert(0, 0, ItemType::Coal));
    CHECK(m.at(0, 0)->input.type == ItemType::Coal);

    CHECK_FALSE(m.tryInsert(0, 0, ItemType::IronOre));
}

TEST_CASE("inserting into an empty tile or a drill is refused")
{
    Machines m;
    m.place(MachineType::Drill, 0, 0, Direction::Down);

    CHECK_FALSE(m.tryInsert(0, 0, ItemType::CopperOre)); // drills are sources only
    CHECK_FALSE(m.tryInsert(9, 9, ItemType::CopperOre)); // nothing there
    CHECK_FALSE(m.tryInsert(0, 0, ItemType::None));
}

TEST_CASE("an item rides a belt to the next belt after the interval")
{
    World world;
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);
    m.place(MachineType::Belt, 1, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Belt interval is 0.5s. Tick past it.
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    // The item has moved one tile along.
    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(1, 0)->carried == ItemType::IronOre);
}

TEST_CASE("a chute moves its item straight down regardless of facing")
{
    World world;
    Machines m;
    m.place(MachineType::Chute, 0, 0, Direction::Right); // facing ignored by chutes
    m.place(MachineType::Belt, 0, 1, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(0, 1)->carried == ItemType::Coal);
}

TEST_CASE("a belt backs up when the tile ahead is full")
{
    World world;
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);
    m.place(MachineType::Belt, 1, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::Stone));
    REQUIRE(m.tryInsert(1, 0, ItemType::Stone)); // tile ahead already occupied

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    // Nothing could move: both still hold their item, nothing was lost.
    CHECK(m.at(0, 0)->carried == ItemType::Stone);
    CHECK(m.at(1, 0)->carried == ItemType::Stone);
}
