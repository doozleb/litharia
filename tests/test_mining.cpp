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

// A trunk of `height` oak logs standing on the grass row at `groundY`, with a
// fixed 6-tile canopy above it - same shape TerrainGenerator will place.
void buildTree(World& world, int trunkX, int groundY, int height)
{
    for (int i = 1; i <= height; ++i)
        world.set(trunkX, groundY - i, BlockType::OakLog);

    const int topY = groundY - height;

    world.set(trunkX - 1, topY, BlockType::OakLeaves);
    world.set(trunkX + 1, topY, BlockType::OakLeaves);

    for (int dx = -1; dx <= 1; ++dx)
        world.set(trunkX + dx, topY - 1, BlockType::OakLeaves);

    world.set(trunkX, topY - 2, BlockType::OakLeaves);
}

} // namespace

TEST_CASE("wood pickaxe, wood axe, and oak log are correctly typed items")
{
    CHECK(itemInfo(ItemType::WoodPickaxe).toolType == ToolType::Pickaxe);
    CHECK(itemInfo(ItemType::WoodPickaxe).tier == ToolTier::Wood);
    CHECK(itemInfo(ItemType::WoodPickaxe).maxStack == 1);

    CHECK(itemInfo(ItemType::WoodAxe).toolType == ToolType::Axe);
    CHECK(itemInfo(ItemType::WoodAxe).tier == ToolTier::Wood);
    CHECK(itemInfo(ItemType::WoodAxe).maxStack == 1);

    CHECK(itemInfo(ItemType::OakLog).toolType == ToolType::None);

    CHECK(itemForBlock(BlockType::OakLog) == ItemType::OakLog);
    CHECK(itemForBlock(BlockType::OakLeaves) == ItemType::None);

    // Every non-tool item still reports no tool type.
    CHECK(itemInfo(ItemType::Dirt).toolType == ToolType::None);
}

TEST_CASE("every item has a real icon color, even non-placeable ones")
{
    // Index 0 is "Nothing" - never rendered, skip it.
    for (int i = 1; i < static_cast<int>(ItemType::Count); ++i)
    {
        const ItemInfo& info = itemInfo(static_cast<ItemType>(i));
        const bool allBlack = info.iconColor.r == 0 && info.iconColor.g == 0 && info.iconColor.b == 0;

        CHECK_FALSE(allBlack);
    }
}

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

TEST_CASE("holding mine breaks a block after its (tier-adjusted) hardness, and it drops itself")
{
    World world;
    buildFloor(world, 30);

    world.set(12, 29, BlockType::Stone); // a block to dig, next to the player

    Player player = standingAt(world, 10.0f, 30);

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    const float expectedTime =
        blockInfo(BlockType::Stone).hardness / toolTierSpeedMultiplier(ToolTier::Wood);

    ActionResult result;
    float elapsed = 0.0f;

    // Not broken before its (tier-adjusted) hardness is paid.
    for (int i = 0; i < 300 && !result.broke; ++i)
    {
        result = player.update(input, world, STEP);
        elapsed += STEP;
    }

    REQUIRE(result.broke);
    REQUIRE(result.broken.size() == 1);

    CHECK(elapsed >= expectedTime);
    CHECK(elapsed < expectedTime + 0.05f);

    CHECK(result.broken[0].block == BlockType::Stone);
    CHECK(result.broken[0].x == 12);
    CHECK(result.broken[0].y == 29);

    // The tile really is gone.
    CHECK(world.get(12, 29) == BlockType::Air);

    // And it yields the right item.
    CHECK(itemForBlock(result.broken[0].block) == ItemType::Stone);
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
    for (int i = 0; i < 60; ++i)
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

    for (int i = 0; i < 60; ++i)
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

TEST_CASE("the player spawns already holding a wood pickaxe and a wood axe")
{
    World world;
    buildFloor(world, 30);
    Player player = standingAt(world, 10.0f, 30);

    CHECK(player.inventory().slot(0).type == ItemType::WoodPickaxe);
    CHECK(player.inventory().slot(0).count == 1);
    CHECK(player.inventory().slot(1).type == ItemType::WoodAxe);
    CHECK(player.inventory().slot(1).count == 1);
}

TEST_CASE("the wrong tool cannot break a block at all")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::Stone);

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(1); // Axe, not a Pickaxe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    for (int i = 0; i < 300; ++i)
    {
        const ActionResult result = player.update(input, world, STEP);
        REQUIRE_FALSE(result.broke);
    }

    CHECK(world.get(12, 29) == BlockType::Stone);
    CHECK_FALSE(player.isMining());
}

TEST_CASE("a pickaxe cannot fell a tree")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::OakLog);

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(0); // Pickaxe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    for (int i = 0; i < 300; ++i)
    {
        const ActionResult result = player.update(input, world, STEP);
        REQUIRE_FALSE(result.broke);
    }

    CHECK(world.get(12, 29) == BlockType::OakLog);
}

TEST_CASE("an empty hand cannot mine anything")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::Dirt);

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(2); // an empty hotbar slot

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    for (int i = 0; i < 300; ++i)
    {
        const ActionResult result = player.update(input, world, STEP);
        REQUIRE_FALSE(result.broke);
    }

    CHECK(world.get(12, 29) == BlockType::Dirt);
}

TEST_CASE("breaking the bottom log fells the whole tree and drops every log")
{
    World world;
    buildFloor(world, 30);
    buildTree(world, 12, 30, 5); // trunk rows 25..29, canopy above row 25

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(1); // Axe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29); // the bottom log

    ActionResult result;

    for (int i = 0; i < 200 && !result.broke; ++i)
        result = player.update(input, world, STEP);

    REQUIRE(result.broke);

    int logCount = 0;
    for (const BrokenTile& tile : result.broken)
        if (tile.block == BlockType::OakLog)
            ++logCount;

    CHECK(logCount == 5);
    CHECK(result.broken.size() == 5 + 6); // 5 logs + the 6-tile canopy

    // The whole column, trunk and canopy, is gone.
    for (int y = 20; y < 30; ++y)
        CHECK(world.get(12, y) == BlockType::Air);
}

TEST_CASE("breaking a log partway up a tree only fells what's above the cut")
{
    World world;
    buildFloor(world, 30);
    buildTree(world, 12, 30, 5); // trunk rows 25..29 (25 = top, 29 = bottom)

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(1); // Axe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 27); // third log from the bottom

    ActionResult result;

    for (int i = 0; i < 200 && !result.broke; ++i)
        result = player.update(input, world, STEP);

    REQUIRE(result.broke);

    int logCount = 0;
    for (const BrokenTile& tile : result.broken)
        if (tile.block == BlockType::OakLog)
            ++logCount;

    // Rows 25, 26, 27 come down (3 logs); the canopy comes with them.
    CHECK(logCount == 3);
    CHECK(result.broken.size() == 3 + 6);

    // The untouched lower trunk survives.
    CHECK(world.get(12, 28) == BlockType::OakLog);
    CHECK(world.get(12, 29) == BlockType::OakLog);
}

TEST_CASE("leaves never drop an item, whether broken directly or as part of a cascade")
{
    World world;
    buildFloor(world, 30);
    buildTree(world, 12, 30, 4); // canopy apex sits at row 30 - 4 - 2 = 24, within reach

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(1); // Axe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 24); // the lone apex leaf, isolated from the rest

    ActionResult result;

    for (int i = 0; i < 200 && !result.broke; ++i)
        result = player.update(input, world, STEP);

    REQUIRE(result.broke);
    REQUIRE(result.broken.size() == 1);
    CHECK(result.broken[0].block == BlockType::OakLeaves);
    CHECK(itemForBlock(result.broken[0].block) == ItemType::None);
}

TEST_CASE("a felled tree's logs are the only thing an axe drops")
{
    World world;
    buildFloor(world, 30);
    buildTree(world, 12, 30, 4);

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(1); // Axe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29); // bottom log

    ActionResult result;

    for (int i = 0; i < 200 && !result.broke; ++i)
        result = player.update(input, world, STEP);

    REQUIRE(result.broke);

    for (const BrokenTile& tile : result.broken)
    {
        const ItemType dropped = itemForBlock(tile.block);
        CHECK((dropped == ItemType::OakLog || dropped == ItemType::None));
    }
}

TEST_CASE("Copper Ore requires at least Stone tier; everything else still defaults to Wood tier")
{
    CHECK(blockInfo(BlockType::CopperOre).requiredTier == ToolTier::Stone);

    CHECK(blockInfo(BlockType::Stone).requiredTier == ToolTier::Wood);
    CHECK(blockInfo(BlockType::Coal).requiredTier == ToolTier::Wood);
    CHECK(blockInfo(BlockType::IronOre).requiredTier == ToolTier::Wood);
    CHECK(blockInfo(BlockType::OakLog).requiredTier == ToolTier::Wood);
    CHECK(blockInfo(BlockType::Dirt).requiredTier == ToolTier::Wood);
    CHECK(blockInfo(BlockType::Grass).requiredTier == ToolTier::Wood);
}

TEST_CASE("meetsTier is a simple ordered comparison")
{
    CHECK(meetsTier(ToolTier::Wood, ToolTier::Wood));
    CHECK_FALSE(meetsTier(ToolTier::Wood, ToolTier::Stone));
    CHECK(meetsTier(ToolTier::Stone, ToolTier::Wood));
    CHECK(meetsTier(ToolTier::Obsidian, ToolTier::Iron));
    CHECK_FALSE(meetsTier(ToolTier::Iron, ToolTier::Obsidian));
}

TEST_CASE("a wood pickaxe cannot mine copper ore, even though it is a pickaxe")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::CopperOre);

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(0); // Wood Pickaxe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    for (int i = 0; i < 300; ++i)
    {
        const ActionResult result = player.update(input, world, STEP);
        REQUIRE_FALSE(result.broke);
    }

    CHECK(world.get(12, 29) == BlockType::CopperOre);
    CHECK_FALSE(player.isMining());
}

TEST_CASE("stick, sharp rock, and the stone tools are correctly typed items")
{
    CHECK(itemInfo(ItemType::Stick).toolType == ToolType::None);
    CHECK(itemInfo(ItemType::SharpRock).toolType == ToolType::None);

    CHECK(itemInfo(ItemType::StonePickaxe).toolType == ToolType::Pickaxe);
    CHECK(itemInfo(ItemType::StonePickaxe).tier == ToolTier::Stone);

    CHECK(itemInfo(ItemType::StoneAxe).toolType == ToolType::Axe);
    CHECK(itemInfo(ItemType::StoneAxe).tier == ToolTier::Stone);
}

TEST_CASE("a stone pickaxe can mine copper ore, at Stone tier's own speed")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::CopperOre);

    Player player = standingAt(world, 10.0f, 30);
    player.inventory().exchange(2, {ItemType::StonePickaxe, 1});
    player.setSelectedSlot(2);

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    const float expectedTime =
        blockInfo(BlockType::CopperOre).hardness / toolTierSpeedMultiplier(ToolTier::Stone);

    ActionResult result;
    float elapsed = 0.0f;

    for (int i = 0; i < 300 && !result.broke; ++i)
    {
        result = player.update(input, world, STEP);
        elapsed += STEP;
    }

    REQUIRE(result.broke);
    CHECK(elapsed >= expectedTime);
    CHECK(elapsed < expectedTime + 0.05f);
    CHECK(world.get(12, 29) == BlockType::Air);
}
