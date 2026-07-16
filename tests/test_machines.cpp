#include "doctest.h"

#include "Core/Direction.h"
#include "Machines/Machine.h"
#include "Machines/MachineType.h"
#include "Machines/Machines.h"

TEST_CASE("direction deltas point the right way")
{
    CHECK(dirDX(Direction::Left)  == -1);
    CHECK(dirDX(Direction::Right) ==  1);
    CHECK(dirDY(Direction::Up)    == -1);
    CHECK(dirDY(Direction::Down)  ==  1);

    // The other axis is zero.
    CHECK(dirDY(Direction::Left)  == 0);
    CHECK(dirDX(Direction::Up)    == 0);
}

TEST_CASE("rotateCW cycles through all four and wraps")
{
    CHECK(rotateCW(Direction::Up)    == Direction::Right);
    CHECK(rotateCW(Direction::Right) == Direction::Down);
    CHECK(rotateCW(Direction::Down)  == Direction::Left);
    CHECK(rotateCW(Direction::Left)  == Direction::Up);
}

TEST_CASE("oppositeDirection reverses each direction")
{
    CHECK(oppositeDirection(Direction::Up)    == Direction::Down);
    CHECK(oppositeDirection(Direction::Down)  == Direction::Up);
    CHECK(oppositeDirection(Direction::Left)  == Direction::Right);
    CHECK(oppositeDirection(Direction::Right) == Direction::Left);
}

TEST_CASE("the machine registry has a valid row per type")
{
    for (int i = 1; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineInfo& info = machineInfo(static_cast<MachineType>(i));
        CHECK_FALSE(info.name.empty());
    }

    // A generator supplies power; a drill and smelter draw it.
    CHECK(machineInfo(MachineType::BurnerGenerator).generator);
    CHECK(machineInfo(MachineType::Drill).consumer);
    CHECK(machineInfo(MachineType::Smelter).consumer);

    // Transport machines move items and are neither source nor sink of power.
    CHECK(machineInfo(MachineType::Belt).transport);
    CHECK(machineInfo(MachineType::Chute).transport);
}

TEST_CASE("a default machine is empty")
{
    Machine m;
    CHECK(m.empty());
    CHECK(m.carried == ItemType::None);
    CHECK(m.powered == false);
}

TEST_CASE("a chest is placed with 20 empty storage slots and no power role")
{
    Machines machines;
    Machine* chest = machines.place(MachineType::Chest, 2, 2, Direction::Right);

    REQUIRE(chest != nullptr);
    CHECK(chest->storage.slotCount() == CHEST_SLOTS);
    CHECK(chest->storage.isEmpty());

    const MachineInfo& info = machineInfo(MachineType::Chest);
    CHECK_FALSE(info.generator);
    CHECK_FALSE(info.consumer);
    CHECK_FALSE(info.transport);
}

TEST_CASE("a non-chest machine carries no storage overhead")
{
    Machines machines;
    Machine* belt = machines.place(MachineType::Belt, 0, 0, Direction::Right);

    REQUIRE(belt != nullptr);
    CHECK(belt->storage.slotCount() == 0);
}

TEST_CASE("placing a machine puts it on its tile and nowhere else")
{
    Machines machines;

    CHECK(machines.count() == 0);
    CHECK(machines.at(5, 5) == nullptr);

    Machine* m = machines.place(MachineType::Drill, 5, 5, Direction::Down);
    REQUIRE(m != nullptr);

    CHECK(m->type == MachineType::Drill);
    CHECK(machines.count() == 1);
    CHECK(machines.at(5, 5) == m);
    CHECK(machines.at(6, 5) == nullptr);
}

TEST_CASE("two machines cannot share a tile")
{
    Machines machines;

    REQUIRE(machines.place(MachineType::Belt, 3, 3, Direction::Right) != nullptr);

    CHECK_FALSE(machines.canPlace(3, 3));
    CHECK(machines.place(MachineType::Belt, 3, 3, Direction::Right) == nullptr);
    CHECK(machines.count() == 1);
}

TEST_CASE("removing a machine frees its tile and keeps the rest intact")
{
    Machines machines;

    machines.place(MachineType::Belt, 1, 1, Direction::Right);
    machines.place(MachineType::Belt, 2, 1, Direction::Right);
    machines.place(MachineType::Smelter, 3, 1, Direction::Right);

    REQUIRE(machines.count() == 3);
    REQUIRE(machines.remove(2, 1));

    CHECK(machines.count() == 2);
    CHECK(machines.at(2, 1) == nullptr);

    // The others survive and are still reachable by tile.
    REQUIRE(machines.at(1, 1) != nullptr);
    REQUIRE(machines.at(3, 1) != nullptr);
    CHECK(machines.at(1, 1)->type == MachineType::Belt);
    CHECK(machines.at(3, 1)->type == MachineType::Smelter);

    // Removing an empty tile reports nothing removed.
    CHECK_FALSE(machines.remove(9, 9));
}

TEST_CASE("DRILL_ORES lists exactly the three mineable ore types")
{
    CHECK(DRILL_ORES.size() == 3);
    CHECK(DRILL_ORES[0] == ItemType::CopperOre);
    CHECK(DRILL_ORES[1] == ItemType::IronOre);
    CHECK(DRILL_ORES[2] == ItemType::Coal);
}

TEST_CASE("formatDrillOreList joins every ore's name")
{
    CHECK(formatDrillOreList(DRILL_ORES) == "Copper Ore, Iron Ore, Coal");
}

TEST_CASE("formatDrillOreList on an empty span yields an empty string")
{
    CHECK(formatDrillOreList({}) == "");
}
