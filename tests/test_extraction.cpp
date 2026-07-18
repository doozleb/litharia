#include "doctest.h"

#include "Machines/Machines.h"

TEST_CASE("extracting from a drill takes its whole output and empties it")
{
    Machines m;
    Machine* drill = m.place(MachineType::IronDrill, 0, 0, Direction::Right);
    REQUIRE(drill != nullptr);
    drill->output = {ItemType::CopperOre, 1};

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.type == ItemType::CopperOre);
    CHECK(taken.count == 1);
    CHECK(m.at(0, 0)->output.empty());
}

TEST_CASE("extracting from an item acceptor takes the first non-empty stack")
{
    Machines m;
    m.place(MachineType::ItemAcceptor, 0, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));
    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.type == ItemType::IronOre);
    CHECK(taken.count == 2);
    CHECK(m.at(0, 0)->storage.isEmpty());
}

TEST_CASE("extracting from an empty item acceptor returns nothing")
{
    Machines m;
    m.place(MachineType::ItemAcceptor, 0, 0, Direction::Right);

    CHECK(m.tryExtract(0, 0).empty());
}

TEST_CASE("putBack restores a stack into an item acceptor rather than destroying it")
{
    Machines m;
    m.place(MachineType::ItemAcceptor, 0, 0, Direction::Right);

    m.putBack(0, 0, {ItemType::Coal, 4});

    CHECK(m.at(0, 0)->storage.count(ItemType::Coal) == 4);
}

TEST_CASE("a chest is no longer a network node: tryInsert refuses it")
{
    Machines m;
    m.place(MachineType::Chest, 0, 0, Direction::Right);

    CHECK_FALSE(m.tryInsert(0, 0, ItemType::IronOre));
    CHECK(m.at(0, 0)->storage.isEmpty());
}

TEST_CASE("a chest is no longer a network node: tryExtract takes nothing from it")
{
    Machines m;
    Machine* chest = m.place(MachineType::Chest, 0, 0, Direction::Right);
    REQUIRE(chest != nullptr);

    // Put contents in directly (bypassing tryInsert, which now refuses a
    // chest) to prove tryExtract ignores existing contents too, not just
    // that nothing can get in.
    chest->storage.add({ItemType::IronOre, 3});

    CHECK(m.tryExtract(0, 0).empty());
    CHECK(m.at(0, 0)->storage.count(ItemType::IronOre) == 3);
}

TEST_CASE("extracting from a smelter takes the whole stacked output")
{
    Machines m;
    Machine* smelter = m.place(MachineType::Smelter, 0, 0, Direction::Right);
    REQUIRE(smelter != nullptr);
    smelter->output = {ItemType::CopperPlate, 5};

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.type == ItemType::CopperPlate);
    CHECK(taken.count == 5);
    CHECK(m.at(0, 0)->output.empty());
}

TEST_CASE("extracting from an empty output returns nothing")
{
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Right);

    CHECK(m.tryExtract(0, 0).empty());
}

TEST_CASE("extracting from a stopped belt takes its carried item")
{
    Machines m;
    Machine* belt = m.place(MachineType::Belt, 0, 0, Direction::Right);
    REQUIRE(belt != nullptr);
    belt->carried = ItemType::Coal;
    belt->carryTimer = 0.0f; // finished waiting: stopped

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.type == ItemType::Coal);
    CHECK(taken.count == 1);
    CHECK(m.at(0, 0)->carried == ItemType::None);
}

TEST_CASE("extracting from a belt mid-transfer is refused")
{
    Machines m;
    Machine* belt = m.place(MachineType::Belt, 0, 0, Direction::Right);
    REQUIRE(belt != nullptr);
    belt->carried = ItemType::Coal;
    belt->carryTimer = 0.3f; // still counting down: moving

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.empty());
    CHECK(m.at(0, 0)->carried == ItemType::Coal); // untouched
}

TEST_CASE("extracting from a generator is always refused: it has no output")
{
    Machines m;
    Machine* gen = m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    REQUIRE(gen != nullptr);
    gen->fuel = 10.0f;

    CHECK(m.tryExtract(0, 0).empty());
}

TEST_CASE("extracting from an empty tile returns nothing")
{
    Machines m;
    CHECK(m.tryExtract(5, 5).empty());
}

TEST_CASE("putBack restores a stack to a machine's output")
{
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Right);

    m.putBack(0, 0, {ItemType::CopperPlate, 3});

    CHECK(m.at(0, 0)->output.type == ItemType::CopperPlate);
    CHECK(m.at(0, 0)->output.count == 3);
}

TEST_CASE("putBack restores a single item to a belt's carried slot")
{
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);

    m.putBack(0, 0, {ItemType::Coal, 1});

    CHECK(m.at(0, 0)->carried == ItemType::Coal);
}

TEST_CASE("putBack does nothing for an empty stack or an empty tile")
{
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Right);

    m.putBack(0, 0, {});
    CHECK(m.at(0, 0)->output.empty());

    m.putBack(9, 9, {ItemType::Coal, 1}); // no machine there: must not crash
}
