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
