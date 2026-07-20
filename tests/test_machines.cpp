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
    CHECK(machineInfo(MachineType::CopperDrill).consumer);
    CHECK(machineInfo(MachineType::IronDrill).consumer);
    CHECK(machineInfo(MachineType::ObsidianDrill).consumer); // <-- new line
    CHECK(machineInfo(MachineType::CopperSmelter).consumer);
    CHECK(machineInfo(MachineType::IronSmelter).consumer);

    // Transport machines move items and are neither source nor sink of power.
    CHECK(machineInfo(MachineType::CopperBelt).transport);
    CHECK(machineInfo(MachineType::IronBelt).transport);
    CHECK(machineInfo(MachineType::ObsidianBelt).transport); // <-- new line
    CHECK(machineInfo(MachineType::CopperChute).transport);
    CHECK(machineInfo(MachineType::IronChute).transport);
    CHECK(machineInfo(MachineType::ObsidianChute).transport); // <-- new line
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
    Machine* belt = machines.place(MachineType::IronBelt, 0, 0, Direction::Right);

    REQUIRE(belt != nullptr);
    CHECK(belt->storage.slotCount() == 0);
}

TEST_CASE("placing a machine puts it on its tile and nowhere else")
{
    Machines machines;

    CHECK(machines.count() == 0);
    CHECK(machines.at(5, 5) == nullptr);

    Machine* m = machines.place(MachineType::IronDrill, 5, 5, Direction::Down);
    REQUIRE(m != nullptr);

    CHECK(m->type == MachineType::IronDrill);
    CHECK(machines.count() == 1);
    CHECK(machines.at(5, 5) == m);
    CHECK(machines.at(6, 5) == nullptr);
}

TEST_CASE("two machines cannot share a tile")
{
    Machines machines;

    REQUIRE(machines.place(MachineType::IronBelt, 3, 3, Direction::Right) != nullptr);

    CHECK_FALSE(machines.canPlace(3, 3));
    CHECK(machines.place(MachineType::IronBelt, 3, 3, Direction::Right) == nullptr);
    CHECK(machines.count() == 1);
}

TEST_CASE("removing a machine frees its tile and keeps the rest intact")
{
    Machines machines;

    machines.place(MachineType::IronBelt, 1, 1, Direction::Right);
    machines.place(MachineType::IronBelt, 2, 1, Direction::Right);
    machines.place(MachineType::IronSmelter, 3, 1, Direction::Right);

    REQUIRE(machines.count() == 3);
    REQUIRE(machines.remove(2, 1));

    CHECK(machines.count() == 2);
    CHECK(machines.at(2, 1) == nullptr);

    // The others survive and are still reachable by tile.
    REQUIRE(machines.at(1, 1) != nullptr);
    REQUIRE(machines.at(3, 1) != nullptr);
    CHECK(machines.at(1, 1)->type == MachineType::IronBelt);
    CHECK(machines.at(3, 1)->type == MachineType::IronSmelter);

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

TEST_CASE("machines are stamped with an increasing placement sequence")
{
    Machines m;

    // Read the stamp straight off each returned pointer: place() may reallocate
    // the machine vector, so a pointer must not be held across the next place().
    const std::uint32_t first = m.place(MachineType::IronDrill, 0, 0, Direction::Right)->placedSeq;
    const std::uint32_t second = m.place(MachineType::IronDrill, 1, 0, Direction::Right)->placedSeq;

    CHECK(second > first);
}

TEST_CASE("a machine placed after a removal still sorts last")
{
    Machines m;
    m.place(MachineType::IronDrill, 0, 0, Direction::Right);
    m.place(MachineType::IronDrill, 1, 0, Direction::Right);

    const std::uint32_t survivor = m.at(1, 0)->placedSeq;

    REQUIRE(m.remove(0, 0));

    // remove() swap-and-pops, so this machine lands in the freed vector slot -
    // but the counter never rewinds, so it cannot jump the queue.
    const std::uint32_t fresh = m.place(MachineType::IronDrill, 2, 0, Direction::Right)->placedSeq;

    CHECK(fresh > survivor);
}

TEST_CASE("the machine registry declares a footprint, 1x1 for every type except the Crafting Table (2x1) and the Furnace (2x2)")
{
    for (int i = 1; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineType type = static_cast<MachineType>(i);
        const MachineInfo& info = machineInfo(type);

        if (type == MachineType::CraftingTable)
        {
            CHECK(info.width == 2);
            CHECK(info.height == 1);
        }
        else if (type == MachineType::Furnace)
        {
            CHECK(info.width == 2);
            CHECK(info.height == 2);
        }
        else
        {
            CHECK(info.width == 1);
            CHECK(info.height == 1);
        }
    }
}

TEST_CASE("itemForMachine maps every placeable machine to its own item")
{
    CHECK(itemForMachine(MachineType::BurnerGenerator) == ItemType::BurnerGenerator);
    CHECK(itemForMachine(MachineType::CopperDrill) == ItemType::CopperDrill);
    CHECK(itemForMachine(MachineType::IronDrill) == ItemType::IronDrill);
    CHECK(itemForMachine(MachineType::ObsidianDrill) == ItemType::ObsidianDrill); // <-- new line
    CHECK(itemForMachine(MachineType::CopperBelt) == ItemType::CopperBelt);
    CHECK(itemForMachine(MachineType::IronBelt) == ItemType::IronBelt);
    CHECK(itemForMachine(MachineType::ObsidianBelt) == ItemType::ObsidianBelt); // <-- new line
    CHECK(itemForMachine(MachineType::CopperChute) == ItemType::CopperChute);
    CHECK(itemForMachine(MachineType::IronChute) == ItemType::IronChute);
    CHECK(itemForMachine(MachineType::CopperSmelter) == ItemType::CopperSmelter);
    CHECK(itemForMachine(MachineType::IronSmelter) == ItemType::IronSmelter);
    CHECK(itemForMachine(MachineType::Chest) == ItemType::Chest);
    CHECK(itemForMachine(MachineType::CraftingTable) == ItemType::CraftingTable);
}

TEST_CASE("itemForMachine maps every Chute tier to its own item")
{
    CHECK(itemForMachine(MachineType::CopperChute) == ItemType::CopperChute);
    CHECK(itemForMachine(MachineType::IronChute) == ItemType::IronChute);
    CHECK(itemForMachine(MachineType::ObsidianChute) == ItemType::ObsidianChute);
}

TEST_CASE("itemForMachine(None) has no matching item")
{
    CHECK(itemForMachine(MachineType::None) == ItemType::None);
}

TEST_CASE("a 2-wide machine occupies both tiles it spans")
{
    Machines machines;
    Machine* table = machines.place(MachineType::CraftingTable, 4, 4, Direction::Right);

    REQUIRE(table != nullptr);
    CHECK(machines.at(4, 4) == table);
    CHECK(machines.at(5, 4) == table);
    CHECK(machines.at(6, 4) == nullptr);
    CHECK_FALSE(machines.canPlace(4, 4));
    CHECK_FALSE(machines.canPlace(5, 4));
}

TEST_CASE("a 2-wide machine cannot be placed if either tile is taken")
{
    Machines machines;
    REQUIRE(machines.place(MachineType::IronBelt, 5, 4, Direction::Right) != nullptr);

    // (4,4) is free but (5,4) is not - the whole placement must fail, not
    // just settle for the tile that collided.
    CHECK(machines.place(MachineType::CraftingTable, 4, 4, Direction::Right) == nullptr);
    CHECK(machines.count() == 1);
    CHECK(machines.at(4, 4) == nullptr);
}

TEST_CASE("removing a 2-wide machine via either tile clears both")
{
    Machines machines;
    machines.place(MachineType::CraftingTable, 4, 4, Direction::Right);

    REQUIRE(machines.remove(5, 4)); // remove via the *second* tile, not the origin
    CHECK(machines.count() == 0);
    CHECK(machines.at(4, 4) == nullptr);
    CHECK(machines.at(5, 4) == nullptr);
}

TEST_CASE("swap-and-pop relocates every tile of a multi-tile machine, not just its origin")
{
    Machines machines;
    machines.place(MachineType::IronBelt, 0, 0, Direction::Right);          // index 0, doomed
    machines.place(MachineType::IronBelt, 1, 0, Direction::Right);          // index 1, untouched survivor
    machines.place(MachineType::CraftingTable, 8, 8, Direction::Right); // index 2 -> swapped into slot 0

    REQUIRE(machines.count() == 3);
    REQUIRE(machines.remove(0, 0));

    // The crafting table (previously last in the vector) now lives at index 0,
    // but must still be reachable from BOTH of its tiles.
    Machine* table = machines.at(8, 8);
    REQUIRE(table != nullptr);
    CHECK(table->type == MachineType::CraftingTable);
    CHECK(machines.at(9, 8) == table);

    // The untouched survivor is still exactly where it was.
    REQUIRE(machines.at(1, 0) != nullptr);
    CHECK(machines.at(1, 0)->type == MachineType::IronBelt);
}

TEST_CASE("itemForMachine maps the Furnace to its own item")
{
    CHECK(itemForMachine(MachineType::Furnace) == ItemType::Furnace);
}

TEST_CASE("a 2x2 machine occupies all four tiles it spans")
{
    Machines machines;
    Machine* furnace = machines.place(MachineType::Furnace, 10, 10, Direction::Right);

    REQUIRE(furnace != nullptr);
    CHECK(machines.at(10, 10) == furnace);
    CHECK(machines.at(11, 10) == furnace);
    CHECK(machines.at(10, 11) == furnace);
    CHECK(machines.at(11, 11) == furnace);
    CHECK(machines.at(12, 10) == nullptr);
    CHECK(machines.at(10, 12) == nullptr);
}

TEST_CASE("a 2x2 machine cannot be placed if any of its four tiles is taken")
{
    Machines machines;
    REQUIRE(machines.place(MachineType::IronBelt, 11, 11, Direction::Right) != nullptr);

    // (10,10), (11,10), (10,11) are free but (11,11) is not - the whole
    // placement must fail, not just settle for the tiles that were free.
    CHECK(machines.place(MachineType::Furnace, 10, 10, Direction::Right) == nullptr);
    CHECK(machines.count() == 1);
    CHECK(machines.at(10, 10) == nullptr);
}

TEST_CASE("removing a 2x2 machine via any of its four tiles clears all of them")
{
    Machines machines;
    machines.place(MachineType::Furnace, 10, 10, Direction::Right);

    REQUIRE(machines.remove(11, 11)); // remove via the far corner, not the origin
    CHECK(machines.count() == 0);
    CHECK(machines.at(10, 10) == nullptr);
    CHECK(machines.at(11, 10) == nullptr);
    CHECK(machines.at(10, 11) == nullptr);
    CHECK(machines.at(11, 11) == nullptr);
}

TEST_CASE("swap-and-pop relocates every tile of a 2x2 machine, including its vertical footprint")
{
    Machines machines;
    machines.place(MachineType::IronBelt, 0, 0, Direction::Right);        // index 0, doomed
    machines.place(MachineType::IronBelt, 1, 0, Direction::Right);        // index 1, untouched survivor
    machines.place(MachineType::Furnace, 20, 20, Direction::Right);   // index 2 -> swapped into slot 0

    REQUIRE(machines.count() == 3);
    REQUIRE(machines.remove(0, 0));

    Machine* furnace = machines.at(20, 20);
    REQUIRE(furnace != nullptr);
    CHECK(furnace->type == MachineType::Furnace);
    CHECK(machines.at(21, 20) == furnace);
    CHECK(machines.at(20, 21) == furnace);
    CHECK(machines.at(21, 21) == furnace);

    REQUIRE(machines.at(1, 0) != nullptr);
    CHECK(machines.at(1, 0)->type == MachineType::IronBelt);
}

TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, and Furnace")
{
    CHECK_FALSE(isFurniture(MachineType::None));
    CHECK_FALSE(isFurniture(MachineType::BurnerGenerator));
    CHECK_FALSE(isFurniture(MachineType::CopperDrill));
    CHECK_FALSE(isFurniture(MachineType::IronDrill));
    CHECK_FALSE(isFurniture(MachineType::ObsidianDrill)); // <-- new line
    CHECK_FALSE(isFurniture(MachineType::CopperBelt));
    CHECK_FALSE(isFurniture(MachineType::IronBelt));
    CHECK_FALSE(isFurniture(MachineType::ObsidianBelt)); // <-- new line
    CHECK_FALSE(isFurniture(MachineType::CopperChute));
    CHECK_FALSE(isFurniture(MachineType::IronChute));
    CHECK_FALSE(isFurniture(MachineType::ObsidianChute)); // <-- new line
    CHECK_FALSE(isFurniture(MachineType::CopperSmelter));
    CHECK_FALSE(isFurniture(MachineType::IronSmelter));

    CHECK(isFurniture(MachineType::Chest));
    CHECK(isFurniture(MachineType::CraftingTable));
    CHECK(isFurniture(MachineType::Furnace));
}

TEST_CASE("the machine registry has a row for Item Acceptor: no power role, 1x1")
{
    const MachineInfo& info = machineInfo(MachineType::ItemAcceptor);
    CHECK_FALSE(info.name.empty());
    CHECK(info.name.compare("Item Acceptor") == 0);
    CHECK_FALSE(info.generator);
    CHECK_FALSE(info.consumer);
    CHECK_FALSE(info.transport);
    CHECK(info.width == 1);
    CHECK(info.height == 1);
}

TEST_CASE("itemForMachine maps Item Acceptor to its own item")
{
    CHECK(itemForMachine(MachineType::ItemAcceptor) == ItemType::ItemAcceptor);
}

TEST_CASE("Item Acceptor is not furniture - it stays in build mode")
{
    CHECK_FALSE(isFurniture(MachineType::ItemAcceptor));
}

TEST_CASE("a placed Item Acceptor gets 10 empty storage slots")
{
    Machines machines;
    Machine* acceptor = machines.place(MachineType::ItemAcceptor, 0, 0, Direction::Right);

    REQUIRE(acceptor != nullptr);
    CHECK(acceptor->storage.slotCount() == ITEM_ACCEPTOR_SLOTS);
    CHECK(acceptor->storage.isEmpty());
}

TEST_CASE("a Copper Drill is COPPER_TIER_SLOWDOWN times slower than an Iron Drill")
{
    const float iron = machineInfo(MachineType::IronDrill).actionTime;
    const float copper = machineInfo(MachineType::CopperDrill).actionTime;

    CHECK(iron == doctest::Approx(3.0f));
    CHECK(copper == doctest::Approx(iron * COPPER_TIER_SLOWDOWN));
}

TEST_CASE("isDrill is true for exactly Copper Drill, Iron Drill, and Obsidian Drill")
{
    CHECK(isDrill(MachineType::CopperDrill));
    CHECK(isDrill(MachineType::IronDrill));
    CHECK(isDrill(MachineType::ObsidianDrill));
    CHECK_FALSE(isDrill(MachineType::None));
    CHECK_FALSE(isDrill(MachineType::BurnerGenerator));
    CHECK_FALSE(isDrill(MachineType::IronSmelter));
    CHECK_FALSE(isDrill(MachineType::CopperBelt));
    CHECK_FALSE(isDrill(MachineType::IronBelt));
}

TEST_CASE("a Copper Belt is COPPER_TIER_SLOWDOWN times slower than an Iron Belt")
{
    const float iron = machineInfo(MachineType::IronBelt).actionTime;
    const float copper = machineInfo(MachineType::CopperBelt).actionTime;

    CHECK(iron == doctest::Approx(0.5f));
    CHECK(copper == doctest::Approx(iron * COPPER_TIER_SLOWDOWN));
}

TEST_CASE("an Obsidian Belt is OBSIDIAN_TIER_SPEEDUP times faster than an Iron Belt")
{
    const float iron = machineInfo(MachineType::IronBelt).actionTime;
    const float obsidian = machineInfo(MachineType::ObsidianBelt).actionTime;

    CHECK(iron == doctest::Approx(0.5f));
    CHECK(obsidian == doctest::Approx(iron * OBSIDIAN_TIER_SPEEDUP));
}

TEST_CASE("isBelt is true for exactly Copper Belt, Iron Belt, and Obsidian Belt")
{
    CHECK(isBelt(MachineType::CopperBelt));
    CHECK(isBelt(MachineType::IronBelt));
    CHECK(isBelt(MachineType::ObsidianBelt));
    CHECK_FALSE(isBelt(MachineType::None));
    CHECK_FALSE(isBelt(MachineType::CopperChute));
    CHECK_FALSE(isBelt(MachineType::IronChute));
    CHECK_FALSE(isBelt(MachineType::IronDrill));
}

TEST_CASE("a Copper Chute is COPPER_TIER_SLOWDOWN times slower than an Iron Chute")
{
    const float iron = machineInfo(MachineType::IronChute).actionTime;
    const float copper = machineInfo(MachineType::CopperChute).actionTime;

    CHECK(iron == doctest::Approx(0.5f));
    CHECK(copper == doctest::Approx(iron * COPPER_TIER_SLOWDOWN));
}

TEST_CASE("isChute is true for exactly Copper Chute, Iron Chute, and Obsidian Chute")
{
    CHECK(isChute(MachineType::CopperChute));
    CHECK(isChute(MachineType::IronChute));
    CHECK(isChute(MachineType::ObsidianChute));
    CHECK_FALSE(isChute(MachineType::None));
    CHECK_FALSE(isChute(MachineType::IronBelt));
}

TEST_CASE("Copper Smelter and Iron Smelter carry the expected speedMultiplier")
{
    CHECK(machineInfo(MachineType::IronSmelter).speedMultiplier == doctest::Approx(1.0f));
    CHECK(machineInfo(MachineType::CopperSmelter).speedMultiplier == doctest::Approx(COPPER_TIER_SLOWDOWN));
}

TEST_CASE("every non-Smelter machine defaults to a speedMultiplier of 1.0")
{
    for (int i = 1; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineType type = static_cast<MachineType>(i);
        if (isSmelter(type))
            continue;

        CHECK(machineInfo(type).speedMultiplier == doctest::Approx(1.0f));
    }
}

TEST_CASE("isSmelter is true for exactly Copper Smelter and Iron Smelter")
{
    CHECK(isSmelter(MachineType::CopperSmelter));
    CHECK(isSmelter(MachineType::IronSmelter));
    CHECK_FALSE(isSmelter(MachineType::None));
    CHECK_FALSE(isSmelter(MachineType::IronDrill));
}

TEST_CASE("an Obsidian Drill is OBSIDIAN_TIER_SPEEDUP times faster than an Iron Drill")
{
    const float iron = machineInfo(MachineType::IronDrill).actionTime;
    const float obsidian = machineInfo(MachineType::ObsidianDrill).actionTime;

    CHECK(iron == doctest::Approx(3.0f));
    CHECK(obsidian == doctest::Approx(iron * OBSIDIAN_TIER_SPEEDUP));
}

TEST_CASE("an Obsidian Chute is OBSIDIAN_TIER_SPEEDUP times faster than an Iron Chute")
{
    const float iron = machineInfo(MachineType::IronChute).actionTime;
    const float obsidian = machineInfo(MachineType::ObsidianChute).actionTime;

    CHECK(iron == doctest::Approx(0.5f));
    CHECK(obsidian == doctest::Approx(iron * OBSIDIAN_TIER_SPEEDUP));
}
