#include "doctest.h"

#include "Machines/Machines.h"

namespace
{

// A generator is only a source once it has fuel; the power solve reads .fuel.
void fuel(Machines& m, int x, int y, float seconds)
{
    Machine* g = m.at(x, y);
    REQUIRE(g != nullptr);
    g->fuel = seconds;
}

} // namespace

TEST_CASE("a fuelled generator powers an adjacent consumer")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down);
    fuel(m, 0, 0, 10.0f);

    m.updatePower();

    CHECK(m.at(1, 0)->powered);

    // Same network id for the connected pair.
    CHECK(m.at(0, 0)->network == m.at(1, 0)->network);
}

TEST_CASE("a consumer with no generator is unpowered")
{
    Machines m;
    m.place(MachineType::Drill, 4, 4, Direction::Down);

    m.updatePower();

    CHECK_FALSE(m.at(4, 4)->powered);
}

TEST_CASE("an unfuelled generator supplies nothing")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down);
    // No fuel set.

    m.updatePower();

    CHECK_FALSE(m.at(1, 0)->powered);
}

TEST_CASE("demand beyond supply browns out the whole network")
{
    Machines m;
    // Generator supply is 10; each drill demands 5, so three drills (15) exceed it.
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down);
    m.place(MachineType::Drill, 2, 0, Direction::Down);
    m.place(MachineType::Drill, 3, 0, Direction::Down);
    fuel(m, 0, 0, 10.0f);

    m.updatePower();

    CHECK_FALSE(m.at(1, 0)->powered);
    CHECK_FALSE(m.at(2, 0)->powered);
    CHECK_FALSE(m.at(3, 0)->powered);
}

TEST_CASE("two separated networks do not share power")
{
    Machines m;
    // Group A: powered.
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down);
    fuel(m, 0, 0, 10.0f);

    // Group B: a lone drill far away.
    m.place(MachineType::Drill, 50, 50, Direction::Down);

    m.updatePower();

    CHECK(m.at(1, 0)->powered);
    CHECK_FALSE(m.at(50, 50)->powered);
    CHECK(m.at(1, 0)->network != m.at(50, 50)->network);
}
