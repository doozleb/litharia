#include "doctest.h"

#include "Machines/Machines.h"
#include "World/World.h"

// The whole point of the slice: place a generator, a drill over ore, a belt, and a
// smelter, then let it run. Plates must come out with no hand-mining.
TEST_CASE("a coal-fed drill-belt-smelter line produces plates on its own")
{
    World world;
    world.fill(BlockType::Air);

    // A short copper vein straight under the drill's column.
    for (int y = 1; y <= 4; ++y)
        world.set(10, y, BlockType::CopperOre);

    Machines m;

    // Layout (all on row 0 except the ore below the drill):
    //   (9,0) generator  (10,0) drill  (11,0) belt  (12,0) smelter
    m.place(MachineType::BurnerGenerator, 9, 0, Direction::Right);
    m.place(MachineType::Drill,           10, 0, Direction::Right);
    m.place(MachineType::Belt,            11, 0, Direction::Right);
    m.place(MachineType::Smelter,         12, 0, Direction::Right);

    // Prime the generator with plenty of coal.
    for (int i = 0; i < 10; ++i)
        REQUIRE(m.tryInsert(9, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Run ~30 seconds of simulation.
    for (int i = 0; i < 1800; ++i)
        m.tick(world, step, mined);

    // The vein is untouched - mining no longer destroys the block...
    for (int y = 1; y <= 4; ++y)
        CHECK(world.get(10, y) == BlockType::CopperOre);

    // ...and copper plates exist somewhere in the line (smelter output or belt).
    const bool platesMade = m.at(12, 0)->output.type == ItemType::CopperPlate
        || m.at(11, 0)->carried == ItemType::CopperPlate;
    CHECK(platesMade);
    // A one-tile vein could only ever have produced 1 ore total under the old
    // destroy-on-mine rule (there is only one reachable ore tile here - see the
    // drill's placement above). Comfortably exceeding that proves the vein is
    // being mined over and over, not drained.
    CHECK(m.at(12, 0)->output.count >= 5);
}
