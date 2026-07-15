#include "doctest.h"

#include "Core/Direction.h"
#include "Machines/Machine.h"
#include "Machines/MachineType.h"

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
