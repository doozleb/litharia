#include "doctest.h"

#include <string>

#include "Machines/Machines.h"
#include "World/World.h"

namespace
{
constexpr float STEP = 1.0f / 60.0f;
}

TEST_CASE("inspecting an empty tile returns a default status")
{
    Machines m;
    World world;

    const MachineStatus status = m.inspect(3, 3, world);

    CHECK(status.bar == MachineBar::None);
    CHECK(status.reason.empty());
}

TEST_CASE("a drill with no generator next to it says so")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);

    m.place(MachineType::IronDrill, 0, 0, Direction::Right); // no generator

    std::vector<sf::Vector2i> mined;
    m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(0, 0, world);

    CHECK(status.reason == "No power: not next to a burner generator.");
}

TEST_CASE("a drill next to an unfuelled burner blames the fuel")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);

    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);
    m.place(MachineType::IronDrill, 6, 5, Direction::Right);
    // No coal inserted: the burner is there, it just has nothing to burn.

    std::vector<sf::Vector2i> mined;
    m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(6, 5, world);

    CHECK(status.reason == "No power: the adjacent burner has no fuel.");
}

TEST_CASE("a drill whose burner is already spoken for says so")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);

    // Supply is 10 and each drill demands 5, so the third one placed loses out
    // even though it is touching the burner.
    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);
    m.place(MachineType::IronDrill, 6, 5, Direction::Right);
    m.place(MachineType::IronDrill, 4, 5, Direction::Right);
    m.place(MachineType::IronDrill, 5, 6, Direction::Right);
    REQUIRE(m.tryInsert(5, 5, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 10; ++i)
        m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(5, 6, world);

    CHECK(status.reason == "No power: the adjacent burner is already at capacity.");
}

TEST_CASE("a powered drill with no ore in reach reports that reason")
{
    Machines m;
    World world;
    world.fill(BlockType::Air); // nothing to mine anywhere

    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::IronDrill, 1, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 10; ++i)
        m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(1, 0, world);

    CHECK(status.reason == "No ore within " + std::to_string(DRILL_REACH) + " tiles below.");
}

TEST_CASE("a powered drill with ore in reach reports no reason")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);
    world.set(1, 1, BlockType::CopperOre); // directly beneath the drill

    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::IronDrill, 1, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 10; ++i)
        m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(1, 0, world);

    CHECK(status.reason.empty());
}

TEST_CASE("a drill whose output cannot be pushed anywhere reports that its output is full")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);
    world.set(1, 1, BlockType::CopperOre);

    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::IronDrill, 1, 0, Direction::Up); // faces open sky: nothing accepts its output
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 240; ++i) // long enough (4s > 3.0s cycle) to mine once and fill the output
        m.tick(world, STEP, mined);

    REQUIRE_FALSE(m.at(1, 0)->output.empty());

    const MachineStatus status = m.inspect(1, 0, world);

    CHECK(status.reason == "Output is full.");
}

TEST_CASE("a smelter with no ore yet reports that it is waiting")
{
    Machines m;
    World world;

    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::IronSmelter, 1, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 10; ++i)
        m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(1, 0, world);

    CHECK(status.reason == "Waiting for ore.");
}

TEST_CASE("an out-of-fuel generator with nothing queued reports it needs coal")
{
    Machines m;
    World world;

    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right); // never fuelled

    const MachineStatus status = m.inspect(0, 0, world);

    CHECK(status.reason == "Out of fuel: needs coal.");
}
