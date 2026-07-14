#include "doctest.h"

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "Physics/Physics.h"
#include "World/World.h"

namespace
{

// A floor of stone across the whole world at the given tile row.
void buildFloor(World& world, int rowY)
{
    for (int x = 0; x < WORLD_WIDTH; ++x)
        world.set(x, rowY, BlockType::Stone);
}

AABB boxAtTile(float tileX, float tileY, float w = 30.0f, float h = 46.0f)
{
    return AABB{{tileX * TILE_SIZE, tileY * TILE_SIZE}, {w, h}};
}

// Resting exactly on top of a floor row, not embedded in it. A box that starts
// inside a solid tile has no correct push-out direction, so nothing may set one up
// that way - and in the game nothing does: spawn is flush with the surface, and
// placing a block inside the player is rejected.
AABB boxStandingOn(float tileX, int floorRow, float w = 30.0f, float h = 46.0f)
{
    return AABB{{tileX * TILE_SIZE, floorRow * TILE_SIZE - h}, {w, h}};
}

} // namespace

TEST_CASE("a falling box lands on the ground and stops")
{
    World world;
    buildFloor(world, 20);

    AABB box = boxAtTile(10.0f, 10.0f);
    sf::Vector2f velocity{0.0f, 0.0f};

    for (int step = 0; step < 300; ++step)
    {
        velocity.y += 1600.0f * (1.0f / 60.0f); // gravity
        physics::moveAndCollide(box, velocity, world, 1.0f / 60.0f);
    }

    const float floorTop = 20.0f * TILE_SIZE;

    // Resting exactly on the floor, no residual downward speed.
    CHECK(box.position.y + box.size.y == doctest::Approx(floorTop).epsilon(0.001));
    CHECK(velocity.y == doctest::Approx(0.0f));
}

TEST_CASE("grounded is set on landing and cleared in the air")
{
    World world;
    buildFloor(world, 20);

    AABB box = boxAtTile(10.0f, 10.0f);
    sf::Vector2f velocity{0.0f, 0.0f};

    // First step: still falling through open air, nothing underneath yet.
    velocity.y += 1600.0f * (1.0f / 60.0f);
    physics::CollisionResult first = physics::moveAndCollide(box, velocity, world, 1.0f / 60.0f);
    CHECK_FALSE(first.grounded);

    // Fall until it lands.
    physics::CollisionResult result{};
    for (int step = 0; step < 300; ++step)
    {
        velocity.y += 1600.0f * (1.0f / 60.0f);
        result = physics::moveAndCollide(box, velocity, world, 1.0f / 60.0f);
    }
    CHECK(result.grounded);

    // Jumping upward off the floor: moving up is not grounded, even though the
    // box began the step touching the ground.
    velocity.y = -400.0f;
    const physics::CollisionResult jumping =
        physics::moveAndCollide(box, velocity, world, 1.0f / 60.0f);
    CHECK_FALSE(jumping.grounded);
}

TEST_CASE("a ceiling stops upward motion and does not set grounded")
{
    World world;

    for (int x = 0; x < WORLD_WIDTH; ++x)
        world.set(x, 10, BlockType::Stone);

    AABB box = boxAtTile(10.0f, 14.0f);
    sf::Vector2f velocity{0.0f, -600.0f};

    physics::CollisionResult result{};
    for (int step = 0; step < 30; ++step)
        result = physics::moveAndCollide(box, velocity, world, 1.0f / 60.0f);

    // Stopped just under the ceiling: correction came from above, not below.
    CHECK(box.position.y == doctest::Approx(11.0f * TILE_SIZE).epsilon(0.001));
    CHECK(velocity.y == doctest::Approx(0.0f));
    CHECK_FALSE(result.grounded);
}

TEST_CASE("horizontal motion is blocked by a wall")
{
    World world;
    buildFloor(world, 20);

    // A wall column at tile x = 15.
    for (int y = 15; y < 20; ++y)
        world.set(15, y, BlockType::Stone);

    AABB box = boxStandingOn(10.0f, 20);
    sf::Vector2f velocity{200.0f, 0.0f};

    for (int step = 0; step < 200; ++step)
    {
        velocity.x = 200.0f;
        physics::moveAndCollide(box, velocity, world, 1.0f / 60.0f);
    }

    const float wallLeft = 15.0f * TILE_SIZE;

    // Flush against the wall, and it never got past it.
    CHECK(box.position.x + box.size.x == doctest::Approx(wallLeft).epsilon(0.001));
    CHECK(box.position.x + box.size.x <= wallLeft + 0.01f);
}

TEST_CASE("a fast box does not tunnel through a one-tile wall")
{
    World world;

    // A single-tile-thick wall, floor to ceiling.
    for (int y = 0; y < WORLD_HEIGHT; ++y)
        world.set(30, y, BlockType::Stone);

    AABB box = boxAtTile(10.0f, 18.0f);

    // Far faster than the player can move: one step would otherwise carry the box
    // clean past a 16 px wall.
    sf::Vector2f velocity{40000.0f, 0.0f};

    physics::moveAndCollide(box, velocity, world, 1.0f / 60.0f);

    const float wallLeft = 30.0f * TILE_SIZE;

    CHECK(box.position.x + box.size.x <= wallLeft + 0.01f);
    CHECK(velocity.x == doctest::Approx(0.0f));
}

TEST_CASE("a fast falling box does not tunnel through a one-tile floor")
{
    World world;
    buildFloor(world, 40);

    AABB box = boxAtTile(10.0f, 5.0f);
    sf::Vector2f velocity{0.0f, 40000.0f};

    physics::moveAndCollide(box, velocity, world, 1.0f / 60.0f);

    CHECK(box.position.y + box.size.y <= 40.0f * TILE_SIZE + 0.01f);
    CHECK(velocity.y == doctest::Approx(0.0f));
}

TEST_CASE("a box in open air moves freely and keeps its velocity")
{
    const World world; // all air

    AABB box = boxAtTile(10.0f, 10.0f);
    sf::Vector2f velocity{120.0f, 60.0f};

    const physics::CollisionResult result =
        physics::moveAndCollide(box, velocity, world, 1.0f);

    CHECK(box.position.x == doctest::Approx(10.0f * TILE_SIZE + 120.0f));
    CHECK(box.position.y == doctest::Approx(10.0f * TILE_SIZE + 60.0f));

    CHECK(velocity.x == doctest::Approx(120.0f));
    CHECK(velocity.y == doctest::Approx(60.0f));

    CHECK_FALSE(result.grounded);
    CHECK_FALSE(result.hitX);
    CHECK_FALSE(result.hitY);
}

TEST_CASE("the world edge is open, not a wall")
{
    const World world; // all air, no tiles anywhere

    AABB box = boxAtTile(1.0f, 10.0f);
    sf::Vector2f velocity{-500.0f, 0.0f};

    // Out of bounds is Air, so the box walks off the edge rather than hitting
    // undefined behavior.
    const physics::CollisionResult result =
        physics::moveAndCollide(box, velocity, world, 1.0f / 60.0f);

    CHECK_FALSE(result.hitX);
}

TEST_CASE("a 30x46 player fits down a two-tile-wide shaft")
{
    World world;

    // A vertical shaft two tiles wide (x = 10, 11 open), stone either side.
    for (int y = 0; y < 60; ++y)
    {
        for (int x = 0; x < 25; ++x)
        {
            if (x != 10 && x != 11)
                world.set(x, y, BlockType::Stone);
        }
    }

    buildFloor(world, 60);

    // Centred in the shaft: 2 tiles = 32 px, the box is 30 px.
    AABB box{{10.0f * TILE_SIZE + 1.0f, 5.0f * TILE_SIZE}, {30.0f, 46.0f}};
    sf::Vector2f velocity{0.0f, 0.0f};

    for (int step = 0; step < 400; ++step)
    {
        velocity.y += 1600.0f * (1.0f / 60.0f);
        physics::moveAndCollide(box, velocity, world, 1.0f / 60.0f);
    }

    // It fell the whole way down the shaft instead of wedging in it.
    CHECK(box.position.y + box.size.y == doctest::Approx(60.0f * TILE_SIZE).epsilon(0.001));
}
