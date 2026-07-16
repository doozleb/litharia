#include "doctest.h"

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "Items/ItemEntity.h"
#include "Items/Items.h"
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

Player standingAt(const World& world, float tileX, int floorRow)
{
    Player player({tileX * TILE_SIZE, floorRow * TILE_SIZE - Player::HEIGHT});
    return player;
}

// Cursor at the centre of a tile.
sf::Vector2f cursorOn(int tileX, int tileY)
{
    return {(tileX + 0.5f) * TILE_SIZE, (tileY + 0.5f) * TILE_SIZE};
}

} // namespace

TEST_CASE("the item registry maps mined blocks to items")
{
    CHECK(itemForBlock(BlockType::Stone) == ItemType::Stone);
    CHECK(itemForBlock(BlockType::Dirt) == ItemType::Dirt);
    CHECK(itemForBlock(BlockType::CopperOre) == ItemType::CopperOre);
    CHECK(itemForBlock(BlockType::IronOre) == ItemType::IronOre);
    CHECK(itemForBlock(BlockType::Coal) == ItemType::Coal);

    // Grass drops dirt, not grass.
    CHECK(itemForBlock(BlockType::Grass) == ItemType::Dirt);

    // Air drops nothing.
    CHECK(itemForBlock(BlockType::Air) == ItemType::None);

    for (int i = 1; i < static_cast<int>(ItemType::Count); ++i)
    {
        const ItemInfo& info = itemInfo(static_cast<ItemType>(i));

        CHECK_FALSE(info.name.empty());
        CHECK(info.maxStack > 0);
        // Plates are refined goods, not placeable terrain: placeBlock may be Air.
    }
}

TEST_CASE("every terrain block requires a pickaxe, and both tree blocks require an axe")
{
    CHECK(blockInfo(BlockType::Grass).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::Dirt).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::Stone).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::CopperOre).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::IronOre).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::Coal).requiredTool == ToolType::Pickaxe);

    CHECK(blockInfo(BlockType::OakLog).requiredTool == ToolType::Axe);
    CHECK(blockInfo(BlockType::OakLeaves).requiredTool == ToolType::Axe);

    // Neither tree block is solid: the whole tree is non-collidable.
    CHECK_FALSE(blockInfo(BlockType::OakLog).solid);
    CHECK_FALSE(blockInfo(BlockType::OakLeaves).solid);

    // Leaves drop nothing; the log drops itself.
    CHECK(blockInfo(BlockType::OakLeaves).drop == BlockType::Air);
    CHECK(blockInfo(BlockType::OakLog).drop == BlockType::OakLog);
}

TEST_CASE("holding mine breaks a block after its hardness, and it drops itself")
{
    World world;
    buildFloor(world, 30);

    world.set(12, 29, BlockType::Stone); // a block to dig, next to the player

    Player player = standingAt(world, 10.0f, 30);

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    const float hardness = blockInfo(BlockType::Stone).hardness;

    ActionResult result;
    float elapsed = 0.0f;

    // Not broken before its hardness is paid.
    for (int i = 0; i < 200 && !result.broke; ++i)
    {
        result = player.update(input, world, STEP);
        elapsed += STEP;
    }

    REQUIRE(result.broke);

    CHECK(elapsed >= hardness);
    CHECK(elapsed < hardness + 0.05f);

    CHECK(result.brokenBlock == BlockType::Stone);
    CHECK(result.brokenX == 12);
    CHECK(result.brokenY == 29);

    // The tile really is gone.
    CHECK(world.get(12, 29) == BlockType::Air);

    // And it yields the right item.
    CHECK(itemForBlock(result.brokenBlock) == ItemType::Stone);
}

TEST_CASE("harder blocks take longer")
{
    auto timeToBreak = [](BlockType type) {
        World world;
        buildFloor(world, 30);
        world.set(12, 29, type);

        Player player = standingAt(world, 10.0f, 30);

        PlayerInput input;
        input.mine = true;
        input.cursor = cursorOn(12, 29);

        float elapsed = 0.0f;

        for (int i = 0; i < 600; ++i)
        {
            const ActionResult result = player.update(input, world, STEP);
            elapsed += STEP;

            if (result.broke)
                return elapsed;
        }

        return -1.0f;
    };

    const float dirt = timeToBreak(BlockType::Dirt);
    const float stone = timeToBreak(BlockType::Stone);
    const float iron = timeToBreak(BlockType::IronOre);

    REQUIRE(dirt > 0.0f);

    CHECK(dirt < stone);
    CHECK(stone < iron);
}

TEST_CASE("releasing the button resets progress")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::Stone);

    Player player = standingAt(world, 10.0f, 30);

    PlayerInput mining;
    mining.mine = true;
    mining.cursor = cursorOn(12, 29);

    // Most of the way through.
    for (int i = 0; i < 45; ++i)
        player.update(mining, world, STEP);

    REQUIRE(player.isMining());
    REQUIRE(player.miningProgress() > 0.5f);

    // Let go for one tick.
    PlayerInput released;
    released.cursor = mining.cursor;
    player.update(released, world, STEP);

    CHECK_FALSE(player.isMining());
    CHECK(player.miningProgress() == doctest::Approx(0.0f));

    // Resuming starts from scratch: the block does not break instantly.
    const ActionResult result = player.update(mining, world, STEP);

    CHECK_FALSE(result.broke);
    CHECK(world.get(12, 29) == BlockType::Stone);
}

TEST_CASE("switching to a different tile resets progress")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::Stone);
    world.set(11, 29, BlockType::Stone);

    Player player = standingAt(world, 10.0f, 30);

    PlayerInput first;
    first.mine = true;
    first.cursor = cursorOn(12, 29);

    for (int i = 0; i < 45; ++i)
        player.update(first, world, STEP);

    REQUIRE(player.miningProgress() > 0.5f);

    // Move the cursor to the neighbouring block, still holding.
    PlayerInput second = first;
    second.cursor = cursorOn(11, 29);

    player.update(second, world, STEP);

    CHECK(player.miningTarget() == sf::Vector2i(11, 29));
    CHECK(player.miningProgress() < 0.1f);

    // The original block is untouched.
    CHECK(world.get(12, 29) == BlockType::Stone);
}

TEST_CASE("a block out of reach cannot be mined")
{
    World world;
    buildFloor(world, 30);

    // Well beyond the 5 tile reach.
    world.set(40, 29, BlockType::Dirt);

    Player player = standingAt(world, 10.0f, 30);

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(40, 29);

    REQUIRE_FALSE(player.inReach(40, 29));

    for (int i = 0; i < 300; ++i)
    {
        const ActionResult result = player.update(input, world, STEP);
        REQUIRE_FALSE(result.broke);
    }

    CHECK(world.get(40, 29) == BlockType::Dirt);
    CHECK_FALSE(player.isMining());
}

TEST_CASE("mining air does nothing")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(11, 25); // empty sky within reach

    for (int i = 0; i < 120; ++i)
    {
        const ActionResult result = player.update(input, world, STEP);
        REQUIRE_FALSE(result.broke);
    }

    CHECK_FALSE(player.isMining());
}

TEST_CASE("mining progress runs from zero to one")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::Stone);

    Player player = standingAt(world, 10.0f, 30);

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    float previous = 0.0f;

    for (int i = 0; i < 40; ++i)
    {
        player.update(input, world, STEP);

        const float now = player.miningProgress();

        CHECK(now >= previous); // monotonic
        CHECK(now <= 1.0f);     // never overshoots

        previous = now;
    }

    CHECK(previous > 0.0f);
}

TEST_CASE("a dropped item falls and comes to rest on the ground")
{
    World world;
    buildFloor(world, 30);

    ItemEntity drop({ItemType::Stone, 1}, {10.0f * TILE_SIZE, 20.0f * TILE_SIZE}, {40.0f, -60.0f});

    REQUIRE_FALSE(drop.isGrounded());

    for (int i = 0; i < 300; ++i)
        drop.update(world, STEP);

    CHECK(drop.isGrounded());

    // Resting exactly on the floor, not sunk into it or hovering above it.
    CHECK(drop.box().bottom() == doctest::Approx(30.0f * TILE_SIZE).epsilon(0.001));

    // Friction has stopped it sliding.
    CHECK(drop.box().left() > 10.0f * TILE_SIZE);
}

TEST_CASE("a dropped item lands on a cave floor rather than falling through the world")
{
    World world;

    // A one-tile-thick ledge.
    for (int x = 0; x < WORLD_WIDTH; ++x)
        world.set(x, 25, BlockType::Stone);

    ItemEntity drop({ItemType::CopperOre, 3}, {10.0f * TILE_SIZE, 5.0f * TILE_SIZE}, {0.0f, 0.0f});

    for (int i = 0; i < 300; ++i)
        drop.update(world, STEP);

    CHECK(drop.isGrounded());
    CHECK(drop.box().bottom() == doctest::Approx(25.0f * TILE_SIZE).epsilon(0.001));

    // The stack survived the trip.
    CHECK(drop.stack().type == ItemType::CopperOre);
    CHECK(drop.stack().count == 3);
}

TEST_CASE("the player can dig down through the floor and stand in the hole")
{
    World world;

    // Solid ground from row 30 down.
    for (int y = 30; y < 40; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            world.set(x, y, BlockType::Dirt);

    Player player = standingAt(world, 10.0f, 30);

    // Settle.
    for (int i = 0; i < 10; ++i)
        player.update({}, world, STEP);

    REQUIRE(player.isGrounded());
    const float startY = player.position().y;

    // Dig out the two tiles directly under the player's feet.
    for (int tileX : {10, 11})
    {
        PlayerInput input;
        input.mine = true;
        input.cursor = cursorOn(tileX, 30);

        bool broke = false;

        for (int i = 0; i < 200 && !broke; ++i)
            broke = player.update(input, world, STEP).broke;

        REQUIRE(broke);
    }

    // With nothing left underfoot, the player drops into the hole.
    for (int i = 0; i < 60; ++i)
        player.update({}, world, STEP);

    CHECK(player.position().y > startY);
    CHECK(player.isGrounded());
    CHECK(player.box().bottom() == doctest::Approx(31.0f * TILE_SIZE).epsilon(0.01));
}
