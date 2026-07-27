#include "doctest.h"

#include <algorithm>
#include <cmath>

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "Enemies/Enemy.h"
#include "World/World.h"

namespace
{

constexpr float STEP = 1.0f / 60.0f;

void buildFloor(World& world, int rowY)
{
    for (int x = 0; x < WORLD_WIDTH; ++x)
        world.set(x, rowY, BlockType::Stone);
}

// Drops the enemy onto the floor and settles it, mirroring Player's own
// `standing()` test helper. The "player" is held directly below the enemy's
// own starting x (so there is zero horizontal chase drift while it falls)
// and far below in y (so the AI's "player is above, jump" trigger never
// fires mid-settle) - it simply falls straight down under gravity and rests.
//
// NOTE: an earlier version of this helper placed the far-away point above
// and to the left (both offsets negative), matching Player's test-file
// comment intent of "far enough away to not flip-flop near the alignment
// threshold". That combination is not safe for Enemy: unlike Player's own
// standing() (which passes no input at all, so there's never any drift),
// Enemy::update always walks toward whatever point it's given, every tick,
// airborne or not. Two seconds of continuous -190px/s horizontal chase
// (Nightstalker's moveSpeed) covers 380px - more than the 160px between
// tileX=10 and the world's left edge (x=0) - so the enemy walked clean off
// the built floor's left edge within the 120-tick settle loop. Worse, the
// far-above y placed the player "above" for the entire settle, so the
// grounded-and-player-above jump condition re-fired on every landing,
// bouncing it along the way. Verified numerically with an instrumented
// build: by tick 120 the enemy was well past x<0 and in permanent free
// fall (terminal velocity, never regrounding), which is what made the
// wall-jump / jump-above / no-double-jump cases below fail - not the AI
// logic, the settle fixture never actually reached solid ground.
Enemy standing(EnemyType type, World& world, float tileX, float floorRow)
{
    Enemy enemy(type, {tileX * TILE_SIZE, (floorRow - 4.0f) * TILE_SIZE});

    const sf::Vector2f directlyBelow{enemy.center().x, (floorRow + 1000.0f) * TILE_SIZE};
    for (int i = 0; i < 120; ++i)
        enemy.update(world, directlyBelow, STEP);

    return enemy;
}

} // namespace

TEST_CASE("enemyInfo reports the documented stats for each type, Sunroamer weaker and slower")
{
    const EnemyInfo& night = enemyInfo(EnemyType::Nightstalker);
    CHECK(night.maxHealth == 40);
    CHECK(night.moveSpeed == doctest::Approx(100.0f));
    CHECK(night.jumpSpeed == doctest::Approx(470.0f));
    CHECK(night.contactDamage == 8);
    CHECK(night.contactInterval == doctest::Approx(0.6f));

    const EnemyInfo& day = enemyInfo(EnemyType::Sunroamer);
    CHECK(day.maxHealth == 20);
    CHECK(day.moveSpeed == doctest::Approx(75.0f));
    CHECK(day.jumpSpeed == doctest::Approx(420.0f));
    CHECK(day.contactDamage == 4);
    CHECK(day.contactInterval == doctest::Approx(0.6f));

    CHECK(day.maxHealth < night.maxHealth);
    CHECK(day.moveSpeed < night.moveSpeed);
}

TEST_CASE("a grounded enemy walks toward the player and stops once aligned")
{
    World world;
    buildFloor(world, 30);

    Enemy enemy = standing(EnemyType::Nightstalker, world, 10.0f, 30.0f);
    const float startX = enemy.position().x;

    const sf::Vector2f playerFarRight{startX + 500.0f, enemy.center().y};
    for (int i = 0; i < 30; ++i)
        enemy.update(world, playerFarRight, STEP);

    CHECK(enemy.position().x > startX);
    CHECK(enemy.velocity().x > 0.0f);

    // Player directly overhead, at the enemy's own x: no horizontal pull left.
    for (int i = 0; i < 5; ++i)
        enemy.update(world, {enemy.center().x, enemy.center().y - 5.0f}, STEP);

    CHECK(enemy.velocity().x == doctest::Approx(0.0f));
}

TEST_CASE("a grounded enemy blocked by a wall jumps")
{
    World world;
    buildFloor(world, 30);

    for (int y = 24; y < 30; ++y)
        world.set(20, y, BlockType::Stone);

    Enemy enemy = standing(EnemyType::Nightstalker, world, 10.0f, 30.0f);
    const float groundY = enemy.position().y;

    const sf::Vector2f playerBeyondWall{25.0f * TILE_SIZE, groundY};

    bool leftGround = false;
    for (int i = 0; i < 60; ++i)
    {
        enemy.update(world, playerBeyondWall, STEP);
        if (enemy.position().y < groundY - 1.0f)
            leftGround = true;
    }

    CHECK(leftGround);
}

TEST_CASE("a grounded enemy jumps toward a player more than a tile above, with no obstruction")
{
    World world;
    buildFloor(world, 30);

    Enemy enemy = standing(EnemyType::Nightstalker, world, 10.0f, 30.0f);
    const float groundY = enemy.position().y;

    const sf::Vector2f playerAbove{enemy.center().x, groundY - 5.0f * TILE_SIZE};
    enemy.update(world, playerAbove, STEP);

    CHECK(enemy.position().y < groundY);
    CHECK_FALSE(enemy.isGrounded());
}

TEST_CASE("an airborne enemy does not jump again before landing")
{
    World world;
    buildFloor(world, 30);

    Enemy enemy = standing(EnemyType::Nightstalker, world, 10.0f, 30.0f);
    const float groundY = enemy.position().y;

    const sf::Vector2f playerAbove{enemy.center().x, groundY - 8.0f * TILE_SIZE};

    enemy.update(world, playerAbove, STEP);
    REQUIRE_FALSE(enemy.isGrounded());

    float highest = enemy.position().y;
    for (int i = 0; i < 40; ++i)
    {
        enemy.update(world, playerAbove, STEP);
        highest = std::min(highest, enemy.position().y);
    }

    // Come back down while the player stays overhead the entire time. With
    // the player still unreachable in one jump (8 tiles up, versus this
    // jump's ~3.8-tile apex - verified numerically), the AI re-triggers a
    // fresh jump the instant it regrounds, exactly as documented for the
    // ceiling case below: touching down is a single-tick event, not a rest
    // state. So track every regrounding over the window (same pattern as
    // the wall-jump test's `leftGround` above) rather than asserting on
    // whatever the last sampled tick happens to catch mid-cycle.
    bool landedAgain = false;
    float landedY = highest;
    for (int i = 0; i < 200; ++i)
    {
        enemy.update(world, playerAbove, STEP);
        if (enemy.isGrounded())
        {
            landedAgain = true;
            landedY = enemy.position().y;
        }
    }

    // It must land at least once, back at the original ground height, and
    // its apex must have been meaningfully above the ground (a real jump
    // happened - it didn't just teleport back down).
    CHECK(landedAgain);
    CHECK(landedY == doctest::Approx(groundY).epsilon(0.01));
    CHECK(highest < groundY - 1.0f * TILE_SIZE);
}

TEST_CASE("an enemy under a solid ceiling keeps landing in place instead of crossing it")
{
    World world;
    buildFloor(world, 30);

    // A solid ceiling 10 tiles up, with the player sitting just above it -
    // unreachable without crossing solid rock.
    for (int x = 5; x < 15; ++x)
        world.set(x, 20, BlockType::Stone);

    Enemy enemy = standing(EnemyType::Nightstalker, world, 10.0f, 30.0f);
    const sf::Vector2f playerAboveCeiling{10.0f * TILE_SIZE, 5.0f * TILE_SIZE};

    float highestReached = enemy.position().y;
    for (int i = 0; i < 600; ++i)
    {
        enemy.update(world, playerAboveCeiling, STEP);
        highestReached = std::min(highestReached, enemy.position().y);
    }

    // Never breaches the ceiling: the solid row's bottom face sits at
    // 21 * TILE_SIZE, and the enemy's top can never cross above it.
    CHECK(highestReached >= 21.0f * TILE_SIZE - 1.0f);
}

TEST_CASE("contact damage accumulates on the enemy's own interval while touching, and resets when not")
{
    Enemy enemy(EnemyType::Nightstalker, {0.0f, 0.0f});

    auto tickFor = [&](bool touching, float seconds) {
        int total = 0;
        const int n = static_cast<int>(std::lround(seconds / STEP));
        for (int i = 0; i < n; ++i)
            total += enemy.tickContactDamage(touching, STEP);
        return total;
    };

    CHECK(tickFor(true, 0.4f) == 0);  // under the 0.6s interval: charging
    CHECK(tickFor(true, 0.2f) == 8);  // total 0.6s: first hit (Nightstalker: 8)
    CHECK(tickFor(false, 1.0f) == 0); // stepped apart: no damage, timer resets
    CHECK(tickFor(true, 0.4f) == 0);  // a fresh 0.4s must not carry over
}

TEST_CASE("applyDamage clamps at 0 and isDead reports it")
{
    Enemy enemy(EnemyType::Sunroamer, {0.0f, 0.0f});
    REQUIRE(enemy.health() == 20);

    enemy.applyDamage(13);
    CHECK(enemy.health() == 7);
    CHECK_FALSE(enemy.isDead());

    enemy.applyDamage(100);
    CHECK(enemy.health() == 0);
    CHECK(enemy.isDead());
}

TEST_CASE("applyKnockback sets velocity immediately and holds through the lock window")
{
    World world;
    buildFloor(world, 30);

    Enemy enemy = standing(EnemyType::Nightstalker, world, 10.0f, 30.0f);
    const sf::Vector2f playerFarRight{enemy.center().x + 500.0f, enemy.center().y};

    enemy.applyKnockback(-260.0f);
    CHECK(enemy.velocity().x == doctest::Approx(-260.0f));

    // 10 ticks (~0.167s) is still inside the 0.25s lock - chase must not
    // have reclaimed velocity.x despite the player being far to the right.
    for (int i = 0; i < 10; ++i)
        enemy.update(world, playerFarRight, STEP);

    CHECK(enemy.velocity().x == doctest::Approx(-260.0f));
}

TEST_CASE("enemy chase resumes once the knockback lock has elapsed")
{
    World world;
    buildFloor(world, 30);

    Enemy enemy = standing(EnemyType::Nightstalker, world, 10.0f, 30.0f);
    const sf::Vector2f playerFarRight{enemy.center().x + 500.0f, enemy.center().y};

    enemy.applyKnockback(-260.0f);

    // 40 ticks (~0.667s) clears the 0.25s lock with room to spare; chase
    // should have fully reclaimed velocity.x toward the player by then.
    for (int i = 0; i < 40; ++i)
        enemy.update(world, playerFarRight, STEP);

    CHECK(enemy.velocity().x == doctest::Approx(100.0f));
}
