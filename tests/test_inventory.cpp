#include "doctest.h"

#include "Items/Inventory.h"
#include "Items/Items.h"

TEST_CASE("adding to an empty inventory opens a slot")
{
    Inventory bag;

    REQUIRE(bag.isEmpty());

    const int leftover = bag.add({ItemType::Stone, 5});

    CHECK(leftover == 0);
    CHECK(bag.slot(0).type == ItemType::Stone);
    CHECK(bag.slot(0).count == 5);
    CHECK(bag.count(ItemType::Stone) == 5);
}

TEST_CASE("adding tops up an existing stack before opening a new slot")
{
    Inventory bag;

    bag.add({ItemType::Dirt, 10});
    bag.add({ItemType::Dirt, 7});

    CHECK(bag.slot(0).count == 17);

    // Still only one slot in use.
    CHECK(bag.slot(1).empty());
    CHECK(bag.count(ItemType::Dirt) == 17);
}

TEST_CASE("a different item does not merge into another item's stack")
{
    Inventory bag;

    bag.add({ItemType::Dirt, 4});
    bag.add({ItemType::Stone, 3});

    CHECK(bag.slot(0).type == ItemType::Dirt);
    CHECK(bag.slot(0).count == 4);

    CHECK(bag.slot(1).type == ItemType::Stone);
    CHECK(bag.slot(1).count == 3);
}

TEST_CASE("overflow spills into a second slot at max stack size")
{
    Inventory bag;

    const int max = itemInfo(ItemType::Stone).maxStack;

    const int leftover = bag.add({ItemType::Stone, max + 5});

    CHECK(leftover == 0);

    CHECK(bag.slot(0).count == max);
    CHECK(bag.slot(1).type == ItemType::Stone);
    CHECK(bag.slot(1).count == 5);

    CHECK(bag.count(ItemType::Stone) == max + 5);
}

TEST_CASE("a full inventory returns the correct leftover and destroys nothing")
{
    Inventory bag;

    const int max = itemInfo(ItemType::Stone).maxStack;

    // Fill every slot to the brim.
    const int capacity = Inventory::SIZE * max;

    REQUIRE(bag.add({ItemType::Stone, capacity}) == 0);
    REQUIRE(bag.count(ItemType::Stone) == capacity);

    // There is nowhere left to put these.
    const int leftover = bag.add({ItemType::Stone, 12});

    CHECK(leftover == 12);

    // Nothing was silently discarded, and nothing was silently gained.
    CHECK(bag.count(ItemType::Stone) == capacity);
}

TEST_CASE("a full bag still tops up a partial stack of the same item")
{
    Inventory bag;

    const int max = itemInfo(ItemType::Stone).maxStack;

    // Every slot full except the last, which is one short.
    bag.add({ItemType::Stone, Inventory::SIZE * max - 1});

    REQUIRE(bag.slot(Inventory::SIZE - 1).count == max - 1);

    // Two offered, one fits.
    const int leftover = bag.add({ItemType::Stone, 2});

    CHECK(leftover == 1);
    CHECK(bag.slot(Inventory::SIZE - 1).count == max);
}

TEST_CASE("a full bag has no room for a different item at all")
{
    Inventory bag;

    const int max = itemInfo(ItemType::Dirt).maxStack;
    bag.add({ItemType::Dirt, Inventory::SIZE * max});

    const int leftover = bag.add({ItemType::IronOre, 3});

    CHECK(leftover == 3);
    CHECK(bag.count(ItemType::IronOre) == 0);
}

TEST_CASE("removeOne decrements a stack and clears the slot at zero")
{
    Inventory bag;

    bag.add({ItemType::CopperOre, 2});

    CHECK(bag.removeOne(0));
    CHECK(bag.slot(0).count == 1);
    CHECK(bag.slot(0).type == ItemType::CopperOre);

    CHECK(bag.removeOne(0));
    CHECK(bag.slot(0).empty());
    CHECK(bag.slot(0).type == ItemType::None);

    // Nothing left to take.
    CHECK_FALSE(bag.removeOne(0));
    CHECK(bag.isEmpty());
}

TEST_CASE("removing from an out-of-range slot is refused, not undefined")
{
    Inventory bag;
    bag.add({ItemType::Stone, 1});

    CHECK_FALSE(bag.removeOne(-1));
    CHECK_FALSE(bag.removeOne(Inventory::SIZE));
    CHECK_FALSE(bag.removeOne(999));

    // The real stack is untouched.
    CHECK(bag.count(ItemType::Stone) == 1);
}

TEST_CASE("reading an out-of-range slot returns an empty stack")
{
    const Inventory bag;

    CHECK(bag.slot(-1).empty());
    CHECK(bag.slot(Inventory::SIZE).empty());
}

TEST_CASE("adding an empty stack is a no-op")
{
    Inventory bag;

    CHECK(bag.add({ItemType::None, 5}) == 0);
    CHECK(bag.add({ItemType::Stone, 0}) == 0);

    CHECK(bag.isEmpty());
}

TEST_CASE("the hotbar is the front of the same bag")
{
    Inventory bag;

    bag.add({ItemType::Stone, 1});

    // Slot 0 is both the first inventory slot and the first hotbar slot.
    CHECK(bag.slot(0).type == ItemType::Stone);

    CHECK(Inventory::HOTBAR_SIZE == 10);
    CHECK(Inventory::SIZE == 40);
    CHECK(Inventory::HOTBAR_SIZE < Inventory::SIZE);
}

TEST_CASE("take empties a slot and returns what was there")
{
    Inventory bag;
    bag.add({ItemType::Stone, 4});

    const ItemStack taken = bag.take(0);

    CHECK(taken.type == ItemType::Stone);
    CHECK(taken.count == 4);
    CHECK(bag.slot(0).empty());
}

TEST_CASE("take on an empty slot returns an empty stack")
{
    Inventory bag;
    CHECK(bag.take(0).empty());
}

TEST_CASE("exchange drops a stack into an empty slot")
{
    Inventory bag;

    const ItemStack leftover = bag.exchange(0, {ItemType::IronOre, 3});

    CHECK(leftover.empty());
    CHECK(bag.slot(0).type == ItemType::IronOre);
    CHECK(bag.slot(0).count == 3);
}

TEST_CASE("exchange merges onto a matching stack and returns the overflow")
{
    Inventory bag;
    const int max = itemInfo(ItemType::Stone).maxStack;
    bag.add({ItemType::Stone, max - 2});

    const ItemStack leftover = bag.exchange(0, {ItemType::Stone, 5});

    CHECK(bag.slot(0).count == max);
    CHECK(leftover.type == ItemType::Stone);
    CHECK(leftover.count == 3);
}

TEST_CASE("exchange swaps wholesale when the slot holds a different item")
{
    Inventory bag;
    bag.add({ItemType::Dirt, 2});

    const ItemStack displaced = bag.exchange(0, {ItemType::Coal, 1});

    CHECK(bag.slot(0).type == ItemType::Coal);
    CHECK(bag.slot(0).count == 1);
    CHECK(displaced.type == ItemType::Dirt);
    CHECK(displaced.count == 2);
}

TEST_CASE("exchanging an empty stack in changes nothing")
{
    Inventory bag;
    bag.add({ItemType::Dirt, 2});

    const ItemStack result = bag.exchange(0, {});

    CHECK(result.empty());
    CHECK(bag.slot(0).type == ItemType::Dirt);
    CHECK(bag.slot(0).count == 2);
}

TEST_CASE("exchange on an out-of-range slot is refused, not undefined")
{
    Inventory bag;

    const ItemStack bounced = bag.exchange(999, {ItemType::Stone, 1});

    CHECK(bounced.type == ItemType::Stone);
    CHECK(bounced.count == 1);
}

TEST_CASE("a smaller inventory reports its own slot count and stays within it")
{
    Inventory chest(20);

    CHECK(chest.slotCount() == 20);
    CHECK(chest.slot(19).empty());
    CHECK(chest.slot(20).empty()); // out of range: refused, not undefined
    CHECK_FALSE(chest.removeOne(20));
}

TEST_CASE("the default inventory size is still the player's 40 slots")
{
    Inventory bag;
    CHECK(bag.slotCount() == Inventory::SIZE);
}
