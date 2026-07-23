#include "doctest.h"

#include <cmath>

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "Core/Direction.h"
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

// Drops the player onto the floor and settles them.
Player standing(World& world, float tileX, float floorRow)
{
    Player player({tileX * TILE_SIZE, (floorRow - 4.0f) * TILE_SIZE});

    for (int i = 0; i < 120; ++i)
        player.update({}, world, STEP);

    return player;
}

// Drops the player so its feet start `fallTiles` above the floor row, then ticks
// until it lands (or the tick budget runs out).
Player dropFrom(World& world, float tileX, int floorRow, int fallTiles, int maxTicks = 2000)
{
    const float top =
        floorRow * TILE_SIZE - Player::HEIGHT - static_cast<float>(fallTiles) * TILE_SIZE;

    Player player({tileX * TILE_SIZE, top});

    for (int i = 0; i < maxTicks && !player.isGrounded(); ++i)
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

TEST_CASE("gravity is reduced while the player overlaps a fluid tile")
{
    World world;
    buildFloor(world, 30);

    // A deep column of water well above the floor, so a player dropped into it
    // free-falls through fluid the whole time.
    for (int y = 5; y < 30; ++y)
        world.set(10, y, BlockType::Water8);

    Player inWater({10.0f * TILE_SIZE, 5.0f * TILE_SIZE});
    Player inAir({20.0f * TILE_SIZE, 5.0f * TILE_SIZE});

    for (int i = 0; i < 10; ++i)
    {
        inWater.update({}, world, STEP);
        inAir.update({}, world, STEP);
    }

    // Both started from rest and are still airborne (nowhere near the floor
    // yet) - the one falling through water should have picked up less
    // downward speed than the one falling through open air.
    CHECK(inWater.velocity().y < inAir.velocity().y);
    CHECK(inWater.velocity().y > 0.0f); // still falling, just more slowly
}

TEST_CASE("a jump from water clears about 2 tiles more than a land jump, not the full water-gravity height")
{
    World world;
    buildFloor(world, 30);

    for (int y = 5; y < 30; ++y)
        world.set(10, y, BlockType::Water8);

    Player inWater({10.0f * TILE_SIZE, (30.0f - 4.0f) * TILE_SIZE});
    Player onLand = standing(world, 20.0f, 30.0f);

    for (int i = 0; i < 120; ++i)
        inWater.update({}, world, STEP);

    REQUIRE(inWater.isGrounded());

    const float waterGroundY = inWater.position().y;
    const float landGroundY = onLand.position().y;

    PlayerInput jump;
    jump.jump = true;
    inWater.update(jump, world, STEP);
    onLand.update(jump, world, STEP);

    float waterHighest = inWater.position().y;
    float landHighest = onLand.position().y;

    for (int i = 0; i < 120; ++i)
    {
        inWater.update({}, world, STEP);
        onLand.update({}, world, STEP);
        waterHighest = std::min(waterHighest, inWater.position().y);
        landHighest = std::min(landHighest, onLand.position().y);
    }

    const float waterJumpHeight = waterGroundY - waterHighest;
    const float landJumpHeight = landGroundY - landHighest;

    // ~2 tiles above the land jump...
    CHECK(waterJumpHeight > landJumpHeight + 1.0f * TILE_SIZE);
    CHECK(waterJumpHeight < landJumpHeight + 3.0f * TILE_SIZE);
    // ...and nowhere near the ~12.8 tiles JUMP_SPEED would reach under 0.3x gravity.
    CHECK(waterJumpHeight < 8.0f * TILE_SIZE);
}

TEST_CASE("a jump from a puddle only one tile deep matches a land jump, not the deeper water jump")
{
    World world;
    buildFloor(world, 30);

    // A single-tile-deep puddle sitting on the floor - not the deep column
    // the other water-jump tests use.
    world.set(10, 29, BlockType::Water8);

    Player inPuddle({10.0f * TILE_SIZE, (30.0f - 4.0f) * TILE_SIZE});
    Player onLand = standing(world, 20.0f, 30.0f);

    for (int i = 0; i < 120; ++i)
        inPuddle.update({}, world, STEP);

    REQUIRE(inPuddle.isGrounded());

    const float puddleGroundY = inPuddle.position().y;
    const float landGroundY = onLand.position().y;

    PlayerInput jump;
    jump.jump = true;
    inPuddle.update(jump, world, STEP);
    onLand.update(jump, world, STEP);

    float puddleHighest = inPuddle.position().y;
    float landHighest = onLand.position().y;

    for (int i = 0; i < 120; ++i)
    {
        inPuddle.update({}, world, STEP);
        onLand.update({}, world, STEP);
        puddleHighest = std::min(puddleHighest, inPuddle.position().y);
        landHighest = std::min(landHighest, onLand.position().y);
    }

    const float puddleJumpHeight = puddleGroundY - puddleHighest;
    const float landJumpHeight = landGroundY - landHighest;

    // Close to the land jump, well short of the ~2-tile-higher deep-water jump.
    CHECK(puddleJumpHeight > landJumpHeight - 1.0f * TILE_SIZE);
    CHECK(puddleJumpHeight < landJumpHeight + 1.0f * TILE_SIZE);
}

TEST_CASE("a fall of exactly the safe height (14 tiles) deals no damage")
{
    World world;
    buildFloor(world, 100);

    // dropFrom's starting height is an exact multiple of TILE_SIZE above the
    // floor, so this lands at precisely 14.0 tiles fallen - the boundary
    // itself, which the "> FALL_SAFE_TILES" check must treat as still safe.
    Player player = dropFrom(world, 20.0f, 100, 14);

    REQUIRE(player.isGrounded());
    CHECK(player.health() == Player::MAX_HEALTH);
}

TEST_CASE("a big fall past the safe height removes health but can be survived")
{
    World world;
    buildFloor(world, 100);

    Player player = dropFrom(world, 20.0f, 100, 40);

    REQUIRE(player.isGrounded());
    CHECK(player.health() < Player::MAX_HEALTH);
    CHECK(player.health() > 0);
}

TEST_CASE("a fall of exactly the lethal height (63 tiles) is fatal")
{
    World world;
    buildFloor(world, 100);

    Player player = dropFrom(world, 20.0f, 100, 63);

    REQUIRE(player.isGrounded());
    CHECK(player.health() == 0);
    CHECK(player.isDead());
}

TEST_CASE("jumping and landing back at the same height deals no fall damage")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);

    PlayerInput jump;
    jump.jump = true;

    // One tick to leave the ground, then release: a normal single hop, not a
    // held bunny-hop. fallStartY should hold the ground height throughout,
    // and landing back at (near enough) that same height must not fall-damage
    // a player just for jumping.
    player.update(jump, world, STEP);
    REQUIRE_FALSE(player.isGrounded());

    const PlayerInput noInput;

    for (int i = 0; i < 200 && !player.isGrounded(); ++i)
        player.update(noInput, world, STEP);

    REQUIRE(player.isGrounded());
    CHECK(player.health() == Player::MAX_HEALTH);
}

TEST_CASE("ActionResult.damageTaken is 0 on a tick with no damage")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);
    const ActionResult result = player.update({}, world, STEP);

    CHECK(result.damageTaken == 0);
}

TEST_CASE("ActionResult reports the exact fall damage taken, on the landing tick")
{
    World world;
    buildFloor(world, 100);

    const float top = 100 * TILE_SIZE - Player::HEIGHT - 40.0f * TILE_SIZE;
    Player player({20.0f * TILE_SIZE, top});

    int reportedDamage = 0;
    for (int i = 0; i < 2000 && !player.isGrounded(); ++i)
    {
        const ActionResult result = player.update({}, world, STEP);
        if (result.damageTaken > 0)
            reportedDamage = result.damageTaken;
    }

    REQUIRE(player.isGrounded());
    // The popup value must match what actually happened to health, not just
    // be nonzero - this is the exact number Game will show the player.
    CHECK(reportedDamage == Player::MAX_HEALTH - player.health());
    CHECK(reportedDamage > 0);
}

TEST_CASE("ActionResult reports the exact lava damage taken, on the hit tick")
{
    World world;
    for (int x = 8; x <= 12; ++x)
        for (int y = 10; y <= 16; ++y)
            world.set(x, y, BlockType::Lava8);
    for (int x = 8; x <= 12; ++x)
        world.set(x, 17, BlockType::Stone);

    Player player({10 * TILE_SIZE, 12 * TILE_SIZE});

    int reportedDamage = 0;
    for (int i = 0; i < 60 && reportedDamage == 0; ++i)
    {
        const ActionResult result = player.update({}, world, STEP);
        reportedDamage = result.damageTaken;
    }

    CHECK(reportedDamage == 20);
}

TEST_CASE("respawn restores full health and position, and zeroes velocity")
{
    World world;
    buildFloor(world, 100);

    Player player = dropFrom(world, 20.0f, 100, 80);
    REQUIRE(player.isDead());

    const sf::Vector2f spawn{123.0f, 234.0f};
    player.respawn(spawn);

    CHECK(player.health() == Player::MAX_HEALTH);
    CHECK_FALSE(player.isDead());
    CHECK(player.position() == spawn);
    CHECK(player.velocity() == sf::Vector2f{0.0f, 0.0f});
}

TEST_CASE("standing in lava deals 20 damage every half second, three hits kill")
{
    World world;
    for (int x = 8; x <= 12; ++x)
        for (int y = 10; y <= 16; ++y)
            world.set(x, y, BlockType::Lava8);
    for (int x = 8; x <= 12; ++x)
        world.set(x, 17, BlockType::Stone); // a floor to rest on (lava is not solid)

    Player player({10 * TILE_SIZE, 12 * TILE_SIZE});

    auto tickFor = [&](float seconds) {
        const int n = static_cast<int>(std::lround(seconds / STEP));
        for (int i = 0; i < n; ++i)
            player.update({}, world, STEP);
    };

    tickFor(0.4f); // under one interval: charging, no hit yet
    CHECK(player.health() == Player::MAX_HEALTH);

    tickFor(0.2f); // total 0.6 s: first hit landed
    CHECK(player.health() == 30);

    tickFor(0.5f); // total 1.1 s: second hit
    CHECK(player.health() == 10);

    tickFor(0.5f); // total 1.6 s: third hit -> dead
    CHECK(player.isDead());
    CHECK(player.health() == 0);
}

TEST_CASE("stepping out of lava resets the damage timer (no carryover)")
{
    World world;
    auto fillLava = [&](BlockType b) {
        for (int x = 8; x <= 12; ++x)
            for (int y = 10; y <= 16; ++y)
                world.set(x, y, b);
    };
    fillLava(BlockType::Lava8);
    for (int x = 8; x <= 12; ++x)
        world.set(x, 17, BlockType::Stone);

    Player player({10 * TILE_SIZE, 12 * TILE_SIZE});

    // 0.4 s in lava: charging, no hit.
    for (int i = 0; i < 24; ++i)
        player.update({}, world, STEP);
    CHECK(player.health() == Player::MAX_HEALTH);

    // Lava removed: the player now stands in air on the floor, so the timer resets.
    fillLava(BlockType::Air);
    for (int i = 0; i < 6; ++i)
        player.update({}, world, STEP);
    CHECK(player.health() == Player::MAX_HEALTH);

    // Lava returns: a fresh 0.4 s must not trigger a hit (would be 30 with carryover).
    fillLava(BlockType::Lava8);
    for (int i = 0; i < 24; ++i)
        player.update({}, world, STEP);
    CHECK(player.health() == Player::MAX_HEALTH);
}

TEST_CASE("the player faces the last direction they moved, held until they move the other way")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);
    CHECK(player.facing() == Direction::Right); // default facing

    PlayerInput left;
    left.left = true;
    player.update(left, world, STEP);
    CHECK(player.facing() == Direction::Left);

    // Releasing input holds the last facing direction rather than resetting.
    player.update({}, world, STEP);
    CHECK(player.facing() == Direction::Left);

    PlayerInput right;
    right.right = true;
    player.update(right, world, STEP);
    CHECK(player.facing() == Direction::Right);
}

TEST_CASE("holding mine with a sword selected starts a swing, and meleeHit fires exactly once, at the midpoint")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);
    player.inventory().exchange(2, {ItemType::WoodSword, 1});
    player.setSelectedSlot(2);

    PlayerInput swing;
    swing.mine = true;

    const float duration = SWORD_SWING_SECONDS[static_cast<std::size_t>(ToolTier::Wood)];
    const int totalTicks = static_cast<int>(std::lround(duration / STEP));

    // +1: the tick that starts the swing spends its own dt on it (see
    // Player::swing), the same way mine() spends dt on the tick it starts
    // targeting a new tile - it is not a free transition tick. That makes a
    // swing's own accumulated timer reach `duration` by summing STEP
    // `totalTicks` times, but repeated float addition of 1/60 drifts a few
    // ULPs below the mathematically exact total for Wood's 0.8s (and
    // Stone's 0.75s) specifically, so completion lands one tick later than
    // the nominal totalTicks. The "releasing mid-swing" test right below
    // already budgets for this same extra tick.
    int hitTicks = 0;
    int reportedDamage = 0;
    for (int i = 0; i < totalTicks + 1; ++i)
    {
        const ActionResult result = player.update(swing, world, STEP);
        if (result.meleeHit)
        {
            ++hitTicks;
            reportedDamage = result.meleeDamage;
        }
    }

    CHECK(hitTicks == 1);
    CHECK(reportedDamage == itemInfo(ItemType::WoodSword).meleeDamage);
    CHECK_FALSE(player.isSwinging()); // finished by the end of its own duration
}

TEST_CASE("a fixed 0.5s delay follows every swing before a new one can start")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);
    player.inventory().exchange(2, {ItemType::ObsidianSword, 1}); // shortest swing, easiest to isolate the delay
    player.setSelectedSlot(2);

    PlayerInput swing;
    swing.mine = true;

    const float duration = SWORD_SWING_SECONDS[static_cast<std::size_t>(ToolTier::Obsidian)];
    const int swingTicks = static_cast<int>(std::lround(duration / STEP));

    for (int i = 0; i < swingTicks; ++i)
        player.update(swing, world, STEP);

    REQUIRE_FALSE(player.isSwinging());

    // Still inside the 0.5s delay: holding mine must not start a new swing.
    const int delayTicksBeforeReady = static_cast<int>(std::lround(SWORD_SWING_DELAY / STEP)) - 1;
    for (int i = 0; i < delayTicksBeforeReady; ++i)
    {
        player.update(swing, world, STEP);
        CHECK_FALSE(player.isSwinging());
    }

    // One more tick crosses the delay: a new swing starts.
    player.update(swing, world, STEP);
    CHECK(player.isSwinging());
}

TEST_CASE("switching away from a sword mid-delay does not let a new swing start early or skip the remaining delay")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);
    player.inventory().exchange(2, {ItemType::ObsidianSword, 1}); // shortest swing, easiest to isolate the delay
    player.setSelectedSlot(2);

    PlayerInput swing;
    swing.mine = true;

    const float duration = SWORD_SWING_SECONDS[static_cast<std::size_t>(ToolTier::Obsidian)];
    const int swingTicks = static_cast<int>(std::lround(duration / STEP));

    for (int i = 0; i < swingTicks; ++i)
        player.update(swing, world, STEP);

    REQUIRE_FALSE(player.isSwinging()); // now in the trailing Delay phase

    // Switch to a non-sword item (the starting Wood Pickaxe, slot 0) for a
    // few ticks, still holding mine the whole time - this is the exploit
    // scenario: the delay must keep ticking in the background even though a
    // sword is no longer held, rather than resetting.
    player.setSelectedSlot(0);

    const int switchAwayTicks = 3;
    for (int i = 0; i < switchAwayTicks; ++i)
        player.update(swing, world, STEP);

    // Switch back to the sword, still holding mine. Only a few ticks of the
    // 0.5s delay have elapsed (all of it spent while the pickaxe was held),
    // so a new swing must not have started early.
    player.setSelectedSlot(2);
    player.update(swing, world, STEP);
    CHECK_FALSE(player.isSwinging());

    // The delay ticked down by switchAwayTicks + 1 ticks so far (the
    // switch-away ticks plus the one update just above), all counted
    // regardless of what was held. Burn through the rest of it - well short
    // of the boundary, to stay clear of any float-drift ambiguity there -
    // and confirm it is still not swinging: the delay survived the switch
    // rather than being shortened or reset.
    const int elapsedDelayTicks = switchAwayTicks + 1;
    const int totalDelayTicks = static_cast<int>(std::lround(SWORD_SWING_DELAY / STEP));
    const int safelyBeforeReadyTicks = totalDelayTicks - elapsedDelayTicks - 2;
    REQUIRE(safelyBeforeReadyTicks > 0);

    for (int i = 0; i < safelyBeforeReadyTicks; ++i)
    {
        player.update(swing, world, STEP);
        CHECK_FALSE(player.isSwinging());
    }

    // Finish out the rest of the real elapsed delay (a couple of ticks of
    // slack past the boundary, rather than probing the exact crossing tick):
    // a new swing must eventually be allowed to start once the state machine
    // naturally reaches Idle with a sword held and mine held.
    bool sawNewSwing = false;
    for (int i = 0; i < 4; ++i)
    {
        player.update(swing, world, STEP);
        if (player.isSwinging())
            sawNewSwing = true;
    }

    CHECK(sawNewSwing);
}

TEST_CASE("releasing mine mid-swing does not cancel the swing or shorten its delay")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);
    player.inventory().exchange(2, {ItemType::WoodSword, 1});
    player.setSelectedSlot(2);

    PlayerInput swing;
    swing.mine = true;
    player.update(swing, world, STEP); // start the swing

    REQUIRE(player.isSwinging());

    const float duration = SWORD_SWING_SECONDS[static_cast<std::size_t>(ToolTier::Wood)];
    const int remainingTicks = static_cast<int>(std::lround(duration / STEP)) - 1;

    // Release immediately: the swing already in progress must run to completion
    // even with no input held.
    for (int i = 0; i < remainingTicks; ++i)
    {
        player.update({}, world, STEP);
        CHECK(player.isSwinging());
    }

    player.update({}, world, STEP);
    CHECK_FALSE(player.isSwinging());
}

TEST_CASE("switching to a sword and back resets stale mining progress")
{
    World world;
    buildFloor(world, 30);
    world.set(11, 29, BlockType::Stone); // a block in reach to mine

    Player player = standing(world, 10.0f, 30.0f);

    PlayerInput mineInput;
    mineInput.mine = true;
    mineInput.cursor = {11.5f * TILE_SIZE, 29.5f * TILE_SIZE};

    // Partial progress with the starting Wood Pickaxe (slot 0). Stone
    // hardness (0.90s) / Wood's 0.6x multiplier = 1.5s to break, so 5 ticks
    // is partial progress nowhere near breaking it.
    player.setSelectedSlot(0);
    for (int i = 0; i < 5; ++i)
        player.update(mineInput, world, STEP);

    REQUIRE(player.miningProgress() > 0.0f);

    // Switch to a sword (one tick) and back to the pickaxe (one tick): if
    // progress had carried over, one fresh tick's worth (1/60/1.5 =~ 0.011)
    // would instead read as 6 ticks' worth (6/60/1.5 =~ 0.067).
    player.inventory().exchange(2, {ItemType::WoodSword, 1});
    player.setSelectedSlot(2);
    player.update(mineInput, world, STEP);

    player.setSelectedSlot(0);
    player.update(mineInput, world, STEP);

    CHECK(player.miningProgress() < 0.05f);
}
