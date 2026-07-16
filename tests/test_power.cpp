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

TEST_CASE("power does not conduct through a belt")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Belt, 1, 0, Direction::Right);
    m.place(MachineType::Drill, 2, 0, Direction::Down); // two tiles from the generator
    fuel(m, 0, 0, 10.0f);

    m.updatePower();

    // Touching a belt that touches a generator is not touching a generator.
    CHECK_FALSE(m.at(2, 0)->powered);
}

TEST_CASE("a generator's supply caps how many neighbours it can run")
{
    Machines m;
    // Supply is 10 and each drill demands 5, so only the first two placed run,
    // even though all three are touching the generator.
    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);
    m.place(MachineType::Drill, 6, 5, Direction::Down);
    m.place(MachineType::Drill, 4, 5, Direction::Down);
    m.place(MachineType::Drill, 5, 6, Direction::Down);
    fuel(m, 5, 5, 10.0f);

    m.updatePower();

    CHECK(m.at(6, 5)->powered);
    CHECK(m.at(4, 5)->powered);
    CHECK_FALSE(m.at(5, 6)->powered);
}

TEST_CASE("a consumer falls back to a second adjacent generator when the first is spent")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);
    m.place(MachineType::BurnerGenerator, 7, 5, Direction::Right);

    // These two exhaust the first generator's whole 10.
    m.place(MachineType::Drill, 5, 4, Direction::Down);
    m.place(MachineType::Drill, 5, 6, Direction::Down);

    // This one touches both generators. The first has nothing left, so it runs
    // on the second.
    m.place(MachineType::Drill, 6, 5, Direction::Down);

    fuel(m, 5, 5, 10.0f);
    fuel(m, 7, 5, 10.0f);

    m.updatePower();

    CHECK(m.at(5, 4)->powered);
    CHECK(m.at(5, 6)->powered);
    CHECK(m.at(6, 5)->powered);
}

TEST_CASE("power is claimed in placement order, even after a removal")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);

    // Four drills around one generator; the first two placed win its 10.
    m.place(MachineType::Drill, 5, 4, Direction::Down); // A
    m.place(MachineType::Drill, 6, 5, Direction::Down); // B
    m.place(MachineType::Drill, 4, 5, Direction::Down); // C
    m.place(MachineType::Drill, 5, 6, Direction::Down); // D
    fuel(m, 5, 5, 10.0f);

    m.updatePower();

    REQUIRE(m.at(5, 4)->powered);
    REQUIRE(m.at(6, 5)->powered);
    REQUIRE_FALSE(m.at(4, 5)->powered);
    REQUIRE_FALSE(m.at(5, 6)->powered);

    // Removing A swap-and-pops D into A's vector slot. Ordering by placement
    // sequence rather than vector position is what keeps B and C the winners:
    // read off the raw vector, D would wrongly jump ahead of both.
    REQUIRE(m.remove(5, 4));

    m.updatePower();

    CHECK(m.at(6, 5)->powered);
    CHECK(m.at(4, 5)->powered);
    CHECK_FALSE(m.at(5, 6)->powered);
}
