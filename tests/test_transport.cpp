#include "doctest.h"

#include "Machines/Machines.h"
#include "World/World.h"

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

TEST_CASE("a chest accepts items into multiple slots, not just one")
{
    Machines m;
    m.place(MachineType::Chest, 0, 0, Direction::Right);

    const int max = itemInfo(ItemType::Stone).maxStack;

    for (int i = 0; i < max + 3; ++i)
        CHECK(m.tryInsert(0, 0, ItemType::Stone));

    CHECK(m.at(0, 0)->storage.count(ItemType::Stone) == max + 3);
    CHECK(m.at(0, 0)->storage.slot(0).count == max);
    CHECK(m.at(0, 0)->storage.slot(1).count == 3);
}

TEST_CASE("a chest with every slot full refuses further items")
{
    Machines m;
    m.place(MachineType::Chest, 0, 0, Direction::Right);

    const int max = itemInfo(ItemType::Dirt).maxStack;
    Machine* chest = m.at(0, 0);
    for (int i = 0; i < chest->storage.slotCount(); ++i)
        chest->storage.exchange(i, {ItemType::Dirt, max});

    CHECK_FALSE(m.tryInsert(0, 0, ItemType::Dirt));
}

TEST_CASE("a belt delivers its carried item into a chest ahead of it")
{
    World world;
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);
    m.place(MachineType::Chest, 1, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(1, 0)->storage.count(ItemType::IronOre) == 1);
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

TEST_CASE("a smelter's automatic insert only accepts ore arriving from its facing side")
{
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Left);

    // Arriving from the left (the facing/input side): accepted.
    CHECK(m.tryInsert(0, 0, ItemType::CopperOre, Direction::Left));

    // Arriving from any other side: refused, like a drill facing the wrong way.
    CHECK_FALSE(m.tryInsert(0, 0, ItemType::CopperOre, Direction::Right));
    CHECK_FALSE(m.tryInsert(0, 0, ItemType::CopperOre, Direction::Up));
    CHECK_FALSE(m.tryInsert(0, 0, ItemType::CopperOre, Direction::Down));
}

TEST_CASE("tryInsert without a direction is unrestricted, e.g. the player's F key")
{
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Left);

    // No fromSide given: works regardless of facing, same as before this change.
    CHECK(m.tryInsert(0, 0, ItemType::CopperOre));
}

TEST_CASE("a smelter only accepts belt-fed ore from its facing side")
{
    World world;
    Machines m;

    // Facing Left: only a belt pushing in from the left may feed it.
    m.place(MachineType::Smelter, 5, 5, Direction::Left);
    m.place(MachineType::Belt, 4, 5, Direction::Right); // feeds from the left: should work
    m.place(MachineType::Belt, 5, 4, Direction::Down);  // feeds from above: should be refused

    REQUIRE(m.tryInsert(4, 5, ItemType::CopperOre));
    REQUIRE(m.tryInsert(5, 4, ItemType::CopperOre));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    // The left belt's ore made it in; the belt from above is still stuck holding
    // its item, refused because that side isn't the smelter's facing.
    CHECK(m.at(5, 5)->input.type == ItemType::CopperOre);
    CHECK(m.at(5, 5)->input.count == 1);
    CHECK(m.at(5, 4)->carried == ItemType::CopperOre);
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

TEST_CASE("an item rides a belt to the next belt after the interval")
{
    World world;
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);
    m.place(MachineType::Belt, 1, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Belt interval is 0.5s. Tick past it.
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    // The item has moved one tile along.
    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(1, 0)->carried == ItemType::IronOre);
}

TEST_CASE("a chute moves its item straight down regardless of facing")
{
    World world;
    Machines m;
    m.place(MachineType::Chute, 0, 0, Direction::Right); // facing ignored by chutes
    m.place(MachineType::Belt, 0, 1, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(0, 1)->carried == ItemType::Coal);
}

TEST_CASE("a belt backs up when the tile ahead is full")
{
    World world;
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);
    m.place(MachineType::Belt, 1, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::Stone));
    REQUIRE(m.tryInsert(1, 0, ItemType::Stone)); // tile ahead already occupied

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    // Nothing could move: both still hold their item, nothing was lost.
    CHECK(m.at(0, 0)->carried == ItemType::Stone);
    CHECK(m.at(1, 0)->carried == ItemType::Stone);
}

TEST_CASE("a machine never outputs onto its own facing side, which is reserved for input")
{
    World world;
    Machines m;

    // Facing Down: that side is reserved for input (an ore vein, a feeder
    // belt) and must never receive output, even though a belt sits right there.
    m.place(MachineType::Smelter, 5, 5, Direction::Down);
    m.place(MachineType::Belt, 5, 6, Direction::Right); // directly below: the facing side

    m.at(5, 5)->output = {ItemType::CopperPlate, 1};

    std::vector<sf::Vector2i> mined;
    m.tick(world, 1.0f / 60.0f, mined);

    // Nowhere else to go, and the facing side is off-limits: the output waits.
    CHECK(m.at(5, 5)->output.type == ItemType::CopperPlate);
    CHECK(m.at(5, 6)->carried == ItemType::None);
}

TEST_CASE("a smelter alternates its output between two belts on non-facing sides")
{
    World world;
    Machines m;

    m.place(MachineType::Smelter, 5, 5, Direction::Down); // input side: nothing placed there
    m.place(MachineType::Belt, 6, 5, Direction::Right);   // right: a valid output side
    m.place(MachineType::Belt, 4, 5, Direction::Right);   // left: a valid output side

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    m.at(5, 5)->output = {ItemType::CopperPlate, 1};
    m.tick(world, step, mined);

    const bool firstWentRight = m.at(6, 5)->carried == ItemType::CopperPlate;
    const bool firstWentLeft = m.at(4, 5)->carried == ItemType::CopperPlate;
    REQUIRE((firstWentRight || firstWentLeft));

    // Clear whichever belt got it, then send again: it must go to the OTHER
    // one, not pile back onto the same belt.
    if (firstWentRight)
        m.at(6, 5)->carried = ItemType::None;
    else
        m.at(4, 5)->carried = ItemType::None;

    m.at(5, 5)->output = {ItemType::CopperPlate, 1};
    m.tick(world, step, mined);

    if (firstWentRight)
        CHECK(m.at(4, 5)->carried == ItemType::CopperPlate);
    else
        CHECK(m.at(6, 5)->carried == ItemType::CopperPlate);
}

TEST_CASE("output waits when every non-facing side is already occupied")
{
    World world;
    Machines m;

    m.place(MachineType::Smelter, 5, 5, Direction::Down);
    m.place(MachineType::Belt, 6, 5, Direction::Right);
    m.place(MachineType::Belt, 4, 5, Direction::Right);
    m.place(MachineType::Belt, 5, 4, Direction::Right);

    // Fill all three non-facing belts so none of them can take anything more.
    REQUIRE(m.tryInsert(6, 5, ItemType::Stone));
    REQUIRE(m.tryInsert(4, 5, ItemType::Stone));
    REQUIRE(m.tryInsert(5, 4, ItemType::Stone));

    m.at(5, 5)->output = {ItemType::CopperPlate, 1};

    std::vector<sf::Vector2i> mined;
    m.tick(world, 1.0f / 60.0f, mined);

    // Nowhere to put it: the plate stays right where it was, and production
    // stalls behind it (mirrors the existing output-full backpressure).
    CHECK(m.at(5, 5)->output.type == ItemType::CopperPlate);
    CHECK(m.at(5, 5)->output.count == 1);
}
