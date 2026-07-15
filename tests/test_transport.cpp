#include "doctest.h"

#include "Machines/Machines.h"

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
