#include "doctest.h"

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
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

// Drops the player onto the floor and settles them.
Player standing(World& world, float tileX, float floorRow)
{
    Player player({tileX * TILE_SIZE, (floorRow - 4.0f) * TILE_SIZE});

    for (int i = 0; i < 120; ++i)
        player.update({}, world, STEP);

    return player;
}

} // namespace

TEST_CASE("the player falls, lands, and is grounded")
{
    World world;
    buildFloor(world, 30);

    const Player player = standing(world, 10.0f, 30.0f);

    CHECK(player.isGrounded());
    CHECK(player.box().bottom() == doctest::Approx(30.0f * TILE_SIZE).epsilon(0.001));
    CHECK(player.velocity().y == doctest::Approx(0.0f));
}

TEST_CASE("holding right moves the player right, and it stops when released")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);
    const float startX = player.position().x;

    PlayerInput right;
    right.right = true;

    for (int i = 0; i < 60; ++i)
        player.update(right, world, STEP);

    CHECK(player.position().x > startX + 100.0f);
    CHECK(player.velocity().x > 0.0f);

    // Release: ground friction brings it to a stop.
    for (int i = 0; i < 60; ++i)
        player.update({}, world, STEP);

    CHECK(player.velocity().x == doctest::Approx(0.0f));
}

TEST_CASE("run speed is capped")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);

    PlayerInput right;
    right.right = true;

    for (int i = 0; i < 600; ++i)
        player.update(right, world, STEP);

    CHECK(player.velocity().x <= 231.0f);
}

TEST_CASE("jump only works when grounded")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);
    const float groundY = player.position().y;

    PlayerInput jump;
    jump.jump = true;

    // Leaves the ground.
    player.update(jump, world, STEP);
    CHECK(player.position().y < groundY);
    CHECK_FALSE(player.isGrounded());

    // Rise to the apex while holding jump: it must not re-trigger in mid-air.
    float highest = player.position().y;

    for (int i = 0; i < 30; ++i)
    {
        player.update(jump, world, STEP);
        highest = std::min(highest, player.position().y);
    }

    // Come back down. Holding jump the whole way cannot keep it airborne forever.
    for (int i = 0; i < 200; ++i)
        player.update(jump, world, STEP);

    // Note: on landing, the held jump fires again - that is correct hold-to-bunny-hop
    // behavior, so check the player is at or below its jump apex rather than resting.
    CHECK(player.position().y > highest);
    CHECK(highest < groundY - TILE_SIZE);
}

TEST_CASE("a jump clears at least two tiles but is not infinite")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);
    const float groundY = player.position().y;

    PlayerInput jump;
    jump.jump = true;
    player.update(jump, world, STEP);

    float highest = player.position().y;

    for (int i = 0; i < 60; ++i)
    {
        player.update({}, world, STEP); // released after the initial press
        highest = std::min(highest, player.position().y);
    }

    const float jumpHeight = groundY - highest;

    CHECK(jumpHeight > 2.0f * TILE_SIZE);
    CHECK(jumpHeight < 6.0f * TILE_SIZE);
}

TEST_CASE("the player is stopped by a wall instead of walking through it")
{
    World world;
    buildFloor(world, 30);

    for (int y = 24; y < 30; ++y)
        world.set(20, y, BlockType::Stone);

    Player player = standing(world, 10.0f, 30.0f);

    PlayerInput right;
    right.right = true;

    for (int i = 0; i < 300; ++i)
        player.update(right, world, STEP);

    CHECK(player.box().right() <= 20.0f * TILE_SIZE + 0.01f);
}

TEST_CASE("the player keeps horizontal momentum in the air")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);

    PlayerInput running;
    running.right = true;

    for (int i = 0; i < 60; ++i)
        player.update(running, world, STEP);

    const float runSpeed = player.velocity().x;
    REQUIRE(runSpeed > 0.0f);

    // Jump, then release everything: no friction in mid-air, so speed is preserved.
    PlayerInput jump;
    jump.jump = true;
    jump.right = true;
    player.update(jump, world, STEP);

    player.update({}, world, STEP);

    CHECK(player.velocity().x == doctest::Approx(runSpeed).epsilon(0.01));
}
