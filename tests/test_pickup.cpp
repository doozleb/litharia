#include "doctest.h"

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "Items/Inventory.h"
#include "Items/ItemEntity.h"
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

} // namespace

TEST_CASE("a drop inside the magnet radius accelerates toward the player")
{
    World world;
    buildFloor(world, 30);

    // The player standing at tile 20.
    const sf::Vector2f playerCenter{20.0f * TILE_SIZE, 29.0f * TILE_SIZE};

    // A drop two tiles to the left of them, at rest.
    ItemEntity drop({ItemType::Stone, 1},
                    {18.0f * TILE_SIZE, 29.0f * TILE_SIZE},
                    {0.0f, 0.0f});

    REQUIRE(drop.withinMagnetRange(playerCenter));

    const float startDistance = playerCenter.x - drop.center().x;

    drop.update(world, STEP, playerCenter);

    CHECK(drop.isMagnetized());

    // Moving toward the player, not away.
    CHECK(drop.velocity().x > 0.0f);

    const float nowDistance = playerCenter.x - drop.center().x;
    CHECK(nowDistance < startDistance);
}

TEST_CASE("a drop outside the magnet radius is not attracted")
{
    World world;
    buildFloor(world, 30);

    const sf::Vector2f playerCenter{20.0f * TILE_SIZE, 29.0f * TILE_SIZE};

    // Ten tiles away: well beyond the 5 tile magnet.
    ItemEntity drop({ItemType::Stone, 1},
                    {30.0f * TILE_SIZE, 29.0f * TILE_SIZE},
                    {0.0f, 0.0f});

    REQUIRE_FALSE(drop.withinMagnetRange(playerCenter));

    for (int i = 0; i < 30; ++i)
        drop.update(world, STEP, playerCenter);

    CHECK_FALSE(drop.isMagnetized());

    // It fell straight down and stayed put horizontally: no sideways pull.
    CHECK(drop.velocity().x == doctest::Approx(0.0f));
    CHECK(drop.center().x == doctest::Approx(30.0f * TILE_SIZE + ItemEntity::SIZE * 0.5f));
}

TEST_CASE("a magnetized drop reaches the player and is absorbed")
{
    World world;
    buildFloor(world, 30);

    Player player({20.0f * TILE_SIZE, 30.0f * TILE_SIZE - Player::HEIGHT});

    for (int i = 0; i < 10; ++i)
        player.update({}, world, STEP);

    ItemEntity drop({ItemType::CopperOre, 4},
                    {23.0f * TILE_SIZE, 29.0f * TILE_SIZE},
                    {0.0f, 0.0f});

    REQUIRE(drop.withinMagnetRange(player.center()));

    bool touched = false;

    for (int i = 0; i < 120 && !touched; ++i)
    {
        drop.update(world, STEP, player.center());
        touched = physics::overlaps(drop.box(), player.box());
    }

    REQUIRE(touched);

    // Absorb it exactly as Game does.
    const int leftover = player.inventory().add(drop.stack());

    CHECK(leftover == 0);
    CHECK(player.inventory().count(ItemType::CopperOre) == 4);
}

TEST_CASE("a full inventory leaves the drop on the ground, holding its leftovers")
{
    World world;
    buildFloor(world, 30);

    Player player({20.0f * TILE_SIZE, 30.0f * TILE_SIZE - Player::HEIGHT});

    // Fill the bag with dirt so there is no room for anything at all.
    const int max = itemInfo(ItemType::Dirt).maxStack;
    player.inventory().add({ItemType::Dirt, Inventory::SIZE * max});

    ItemEntity drop({ItemType::IronOre, 3},
                    {20.0f * TILE_SIZE, 29.0f * TILE_SIZE},
                    {0.0f, 0.0f});

    const int leftover = player.inventory().add(drop.stack());
    drop.stack().count = leftover;

    // The rule: nothing is destroyed by a full bag.
    CHECK(leftover == 3);
    CHECK_FALSE(drop.stack().empty());
    CHECK(drop.stack().count == 3);
    CHECK(drop.stack().type == ItemType::IronOre);

    CHECK(player.inventory().count(ItemType::IronOre) == 0);
}

TEST_CASE("a partially-full inventory takes what it can and leaves the rest")
{
    Inventory bag;

    const int max = itemInfo(ItemType::Stone).maxStack;

    // Every slot full but one, which has room for exactly 2 more.
    bag.add({ItemType::Stone, Inventory::SIZE * max - 2});

    ItemEntity drop({ItemType::Stone, 5}, {0.0f, 0.0f}, {0.0f, 0.0f});

    const int leftover = bag.add(drop.stack());
    drop.stack().count = leftover;

    CHECK(leftover == 3);
    CHECK(drop.stack().count == 3);
    CHECK(bag.count(ItemType::Stone) == Inventory::SIZE * max);
}

TEST_CASE("mining and picking up puts the block in the bag")
{
    World world;
    buildFloor(world, 30);
    world.set(21, 29, BlockType::Stone);

    Player player({19.0f * TILE_SIZE, 30.0f * TILE_SIZE - Player::HEIGHT});

    PlayerInput input;
    input.mine = true;
    input.cursor = {21.5f * TILE_SIZE, 29.5f * TILE_SIZE};

    ActionResult result;

    for (int i = 0; i < 200 && !result.broke; ++i)
        result = player.update(input, world, STEP);

    REQUIRE(result.broke);

    // The drop the game would spawn.
    ItemEntity drop({itemForBlock(result.broken[0].block), 1},
                    {static_cast<float>(result.broken[0].x * TILE_SIZE),
                     static_cast<float>(result.broken[0].y * TILE_SIZE)},
                    {0.0f, 0.0f});

    // Let the magnet do its work.
    bool touched = false;

    for (int i = 0; i < 240 && !touched; ++i)
    {
        player.update({}, world, STEP);
        drop.update(world, STEP, player.center());

        touched = physics::overlaps(drop.box(), player.box());
    }

    REQUIRE(touched);

    player.inventory().add(drop.stack());

    CHECK(player.inventory().count(ItemType::Stone) == 1);
}
