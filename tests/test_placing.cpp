#include "doctest.h"

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "Items/Inventory.h"
#include "Machines/Machines.h"
#include "Player/Player.h"
#include "World/World.h"

namespace
{

constexpr float STEP = 1.0f / 60.0f;

void buildFloor(World& world, int rowY)
{
    for (int x = 0; x < WORLD_WIDTH; ++x)
        world.set(x, rowY, BlockType::Stone);
}

Player standingAt(World& world, float tileX, int floorRow)
{
    Player player({tileX * TILE_SIZE, floorRow * TILE_SIZE - Player::HEIGHT});

    for (int i = 0; i < 10; ++i)
        player.update({}, world, STEP);

    return player;
}

sf::Vector2f cursorOn(int tileX, int tileY)
{
    return {(tileX + 0.5f) * TILE_SIZE, (tileY + 0.5f) * TILE_SIZE};
}

PlayerInput placingAt(int tileX, int tileY)
{
    PlayerInput input;
    input.place = true;
    input.cursor = cursorOn(tileX, tileY);
    return input;
}

} // namespace

TEST_CASE("placing puts the selected block into the world and takes it from the bag")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);

    player.inventory().add({ItemType::Stone, 3});
    player.setSelectedSlot(2);

    REQUIRE(world.get(13, 29) == BlockType::Air);

    const ActionResult result = player.update(placingAt(13, 29), world, STEP);

    CHECK(result.placed);
    CHECK(result.placedX == 13);
    CHECK(result.placedY == 29);

    CHECK(world.get(13, 29) == BlockType::Stone);

    // Exactly one was spent.
    CHECK(player.inventory().count(ItemType::Stone) == 2);
}

TEST_CASE("placing the last item empties the slot")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);

    player.inventory().add({ItemType::Dirt, 1});
    player.setSelectedSlot(2);

    player.update(placingAt(13, 29), world, STEP);

    CHECK(world.get(13, 29) == BlockType::Dirt);
    CHECK(player.inventory().slot(2).empty());

    // Nothing left to place: the next attempt does nothing.
    const ActionResult again = player.update(placingAt(13, 28), world, STEP);

    CHECK_FALSE(again.placed);
    CHECK(world.get(13, 28) == BlockType::Air);
}

TEST_CASE("a block cannot be placed inside the player")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);

    player.inventory().add({ItemType::Stone, 5});
    player.setSelectedSlot(2);

    // The tile the player's own body is standing in.
    const int tileX = static_cast<int>(player.center().x / TILE_SIZE);
    const int tileY = static_cast<int>(player.center().y / TILE_SIZE);

    REQUIRE(world.get(tileX, tileY) == BlockType::Air);

    const ActionResult result = player.update(placingAt(tileX, tileY), world, STEP);

    // Rejected, and the inventory is unchanged.
    CHECK_FALSE(result.placed);
    CHECK(world.get(tileX, tileY) == BlockType::Air);
    CHECK(player.inventory().count(ItemType::Stone) == 5);
}

TEST_CASE("a block cannot be placed into an occupied tile")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);
    player.inventory().add({ItemType::Dirt, 5});
    player.setSelectedSlot(2);

    // Tile 12,30 is part of the stone floor.
    REQUIRE(world.get(12, 30) == BlockType::Stone);

    const ActionResult result = player.update(placingAt(12, 30), world, STEP);

    CHECK_FALSE(result.placed);

    // The stone was not overwritten with dirt, and nothing was spent.
    CHECK(world.get(12, 30) == BlockType::Stone);
    CHECK(player.inventory().count(ItemType::Dirt) == 5);
}

TEST_CASE("a block cannot be placed onto a tile a machine occupies")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);
    player.inventory().add({ItemType::Stone, 5});
    player.setSelectedSlot(2);

    Machines machines;
    machines.place(MachineType::IronBelt, 13, 29, Direction::Right);

    const ActionResult result = player.update(placingAt(13, 29), world, STEP, &machines);

    CHECK_FALSE(result.placed);
    CHECK(world.get(13, 29) == BlockType::Air); // the tile itself was already empty
    CHECK(player.inventory().count(ItemType::Stone) == 5);
}

TEST_CASE("placing still works normally when no machines are passed in")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);
    player.inventory().add({ItemType::Stone, 1});
    player.setSelectedSlot(2);

    const ActionResult result = player.update(placingAt(13, 29), world, STEP);

    CHECK(result.placed);
}

TEST_CASE("a block cannot be placed out of reach")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);
    player.inventory().add({ItemType::Stone, 5});
    player.setSelectedSlot(2);

    REQUIRE_FALSE(player.inReach(40, 29));

    const ActionResult result = player.update(placingAt(40, 29), world, STEP);

    CHECK_FALSE(result.placed);
    CHECK(world.get(40, 29) == BlockType::Air);
    CHECK(player.inventory().count(ItemType::Stone) == 5);
}

TEST_CASE("placing with an empty hand does nothing")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(2); // empty; the two starting tools sit in slots 0-1

    REQUIRE(player.inventory().slot(2).empty());

    const ActionResult result = player.update(placingAt(13, 29), world, STEP);

    CHECK_FALSE(result.placed);
    CHECK(world.get(13, 29) == BlockType::Air);
}

TEST_CASE("placing uses the selected hotbar slot, not just the first one")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);

    player.inventory().add({ItemType::Dirt, 2});      // slot 2
    player.inventory().add({ItemType::CopperOre, 2}); // slot 3

    player.setSelectedSlot(3);

    player.update(placingAt(13, 29), world, STEP);

    CHECK(world.get(13, 29) == BlockType::CopperOre);

    CHECK(player.inventory().count(ItemType::CopperOre) == 1);
    CHECK(player.inventory().count(ItemType::Dirt) == 2);
}

TEST_CASE("the hotbar selection wraps in both directions")
{
    World world;
    Player player({0.0f, 0.0f});

    CHECK(player.selectedSlot() == 0);

    player.cycleSelectedSlot(1);
    CHECK(player.selectedSlot() == 1);

    // Scrolling back off the front wraps to the last hotbar slot.
    player.cycleSelectedSlot(-1);
    player.cycleSelectedSlot(-1);
    CHECK(player.selectedSlot() == Inventory::HOTBAR_SIZE - 1);

    // And off the end wraps back to the first.
    player.cycleSelectedSlot(1);
    CHECK(player.selectedSlot() == 0);
}

TEST_CASE("the hotbar selection cannot be set outside the hotbar")
{
    Player player({0.0f, 0.0f});

    player.setSelectedSlot(5);
    REQUIRE(player.selectedSlot() == 5);

    // Refused: the selection stays where it was.
    player.setSelectedSlot(-1);
    CHECK(player.selectedSlot() == 5);

    player.setSelectedSlot(Inventory::HOTBAR_SIZE);
    CHECK(player.selectedSlot() == 5);

    player.setSelectedSlot(Inventory::SIZE - 1);
    CHECK(player.selectedSlot() == 5);
}

TEST_CASE("mine it, pick it up, place it back: the loop closes")
{
    World world;
    buildFloor(world, 30);
    world.set(13, 29, BlockType::Stone);

    Player player = standingAt(world, 10.0f, 30);

    // Mine it.
    PlayerInput mining;
    mining.mine = true;
    mining.cursor = cursorOn(13, 29);

    ActionResult mined;

    for (int i = 0; i < 200 && !mined.broke; ++i)
        mined = player.update(mining, world, STEP);

    REQUIRE(mined.broke);
    REQUIRE(world.get(13, 29) == BlockType::Air);

    // Pick it up (the game does this via the item entity; the effect is the same).
    player.inventory().add({itemForBlock(mined.broken[0].block), 1});
    REQUIRE(player.inventory().count(ItemType::Stone) == 1);
    player.setSelectedSlot(2);

    // Put it back.
    const ActionResult placed = player.update(placingAt(13, 29), world, STEP);

    CHECK(placed.placed);
    CHECK(world.get(13, 29) == BlockType::Stone);
    CHECK(player.inventory().count(ItemType::Stone) == 0);
}
