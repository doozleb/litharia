# Health and Damage System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the player 50 HP with fall damage and lava damage, death-and-respawn, a HUD health bar, and retune the underground pool counts.

**Architecture:** Health and damage live on `Player` (core library, unit-testable); a new `physics::overlapsLava` mirrors `overlapsFluid`; `Game` handles respawn-on-death and passes health to a new `Hud::drawHealth`. Pool counts are two constants in `TerrainGenerator.h`.

**Tech Stack:** C++20, MSVC (Visual Studio generator), SFML 3, doctest. Build via the existing CMake project in `build/`.

## Global Constraints

- C++20 (`CMAKE_CXX_STANDARD 20`).
- `Player` and `Physics` are in `Litharia_core`, which links **SFML::System only** — no Graphics/Window. Health/damage/physics code must not touch SFML Graphics. `Hud` and `Game` are in the `Litharia` executable target and may use Graphics.
- Values (from the design spec): `MAX_HEALTH = 50`; lava `20` damage per `0.5 s`; fall damage only past `7` tiles, scaling so a terminal-velocity fall (`TERMINAL_VELOCITY = 1100`) removes all 50 HP; on death respawn at the world spawn with full health, inventory untouched.
- Existing physics constants (in `Player.cpp`'s anonymous namespace): `GRAVITY = 1800.0f`, `TERMINAL_VELOCITY = 1100.0f`. `TILE_SIZE = 16` (from `Core/Constants.h`).
- Build (Debug): `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug`
- Run tests: `& "build/Debug/Litharia_tests.exe"`
- Use the **PowerShell tool** for the build/test commands (PowerShell `& "..."` call syntax).

---

### Task 1: `physics::overlapsLava`

A lava-specific mirror of `overlapsFluid`, so lava damage can be detected distinctly from water.

**Files:**
- Modify: `src/Physics/Physics.h`
- Modify: `src/Physics/Physics.cpp`
- Test: `tests/test_physics.cpp`

**Interfaces:**
- Consumes: `isLava` (from `Blocks.h`, already reachable via `World.h`), `tileRange` (file-local in `Physics.cpp`).
- Produces: `bool physics::overlapsLava(const AABB& box, const World& world);`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_physics.cpp`:

```cpp
TEST_CASE("overlapsLava is true only over lava tiles, not water or air")
{
    World world;
    world.set(5, 5, BlockType::Lava8);
    world.set(7, 5, BlockType::Water8);

    // A small box sitting inside the lava tile.
    CHECK(physics::overlapsLava(boxAtTile(5.1f, 5.1f, 4.0f, 4.0f), world));

    // Over the water tile: fluid, but not lava.
    CHECK_FALSE(physics::overlapsLava(boxAtTile(7.1f, 5.1f, 4.0f, 4.0f), world));

    // Over empty air.
    CHECK_FALSE(physics::overlapsLava(boxAtTile(20.1f, 5.1f, 4.0f, 4.0f), world));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: FAIL to compile — `physics::overlapsLava` is not declared.

- [ ] **Step 3: Declare it in `src/Physics/Physics.h`**

After the `overlapsFluid` declaration, add:

```cpp
// True if any lava tile overlaps the box.
bool overlapsLava(const AABB& box, const World& world);
```

- [ ] **Step 4: Implement it in `src/Physics/Physics.cpp`**

Directly after the `overlapsFluid` definition, add:

```cpp
bool overlapsLava(const AABB& box, const World& world)
{
    int x0, x1, y0, y1;
    tileRange(box, x0, x1, y0, y1);

    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (isLava(world.get(x, y)))
                return true;

    return false;
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: PASS, `[doctest] Status: SUCCESS!`.

- [ ] **Step 6: Commit**

```bash
git add src/Physics/Physics.h src/Physics/Physics.cpp tests/test_physics.cpp
git commit -m "feat: add physics::overlapsLava for lava-specific overlap tests

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Player health + fall damage

Add the health field and API, and fall damage on landing. This is the health foundation; it ships with fall damage so it is exercisable through the public API.

**Files:**
- Modify: `src/Player/Player.h`
- Modify: `src/Player/Player.cpp`
- Test: `tests/test_player.cpp`

**Interfaces:**
- Consumes: `GRAVITY`, `TERMINAL_VELOCITY` (file-local in `Player.cpp`); `physics::moveAndCollide`.
- Produces: `Player::MAX_HEALTH` (public `static constexpr int`), `int Player::health() const`, `bool Player::isDead() const`, `void Player::respawn(sf::Vector2f)`, private `void Player::applyDamage(int)`, private `int hp` member.

- [ ] **Step 1: Write the failing tests**

Add this helper to the anonymous namespace at the top of `tests/test_player.cpp` (after the existing `standing` helper):

```cpp
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
```

Then append these test cases:

```cpp
TEST_CASE("a fall within the safe height (under 7 tiles) deals no damage")
{
    World world;
    buildFloor(world, 100);

    Player player = dropFrom(world, 20.0f, 100, 5);

    REQUIRE(player.isGrounded());
    CHECK(player.health() == Player::MAX_HEALTH);
}

TEST_CASE("a big fall past the safe height removes health but can be survived")
{
    World world;
    buildFloor(world, 100);

    Player player = dropFrom(world, 20.0f, 100, 12);

    REQUIRE(player.isGrounded());
    CHECK(player.health() < Player::MAX_HEALTH);
    CHECK(player.health() > 0);
}

TEST_CASE("a terminal-velocity fall is lethal")
{
    World world;
    buildFloor(world, 100);

    Player player = dropFrom(world, 20.0f, 100, 60);

    REQUIRE(player.isGrounded());
    CHECK(player.health() == 0);
    CHECK(player.isDead());
}

TEST_CASE("respawn restores full health and position, and zeroes velocity")
{
    World world;
    buildFloor(world, 100);

    Player player = dropFrom(world, 20.0f, 100, 60);
    REQUIRE(player.isDead());

    const sf::Vector2f spawn{123.0f, 234.0f};
    player.respawn(spawn);

    CHECK(player.health() == Player::MAX_HEALTH);
    CHECK_FALSE(player.isDead());
    CHECK(player.position() == spawn);
    CHECK(player.velocity() == sf::Vector2f{0.0f, 0.0f});
}
```

- [ ] **Step 2: Run to verify failure**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: FAIL to compile — `Player::MAX_HEALTH`, `health()`, `isDead()`, `respawn()` do not exist.

- [ ] **Step 3: Add the public API and `hp` member to `src/Player/Player.h`**

Add the constant beside `WIDTH`/`HEIGHT` (in the public section near the top of the class):

```cpp
    // Full health. The player dies at 0 and is respawned by Game.
    static constexpr int MAX_HEALTH = 50;
```

Add the accessors beside the other accessors (e.g. after `bool isGrounded() const { return grounded; }`):

```cpp
    int health() const { return hp; }
    bool isDead() const { return hp <= 0; }

    // Restores full health at `topLeft`, velocity cleared. Called by Game on death.
    void respawn(sf::Vector2f topLeft);
```

Declare the private helper beside the other private methods (after `void place(...);`):

```cpp
    void applyDamage(int amount);
```

Add the private member beside `bool grounded`:

```cpp
    int hp = MAX_HEALTH;
```

- [ ] **Step 4: Add the fall constants to `src/Player/Player.cpp`**

In the anonymous namespace, directly after the `JUMP_SPEED` line, add:

```cpp
// Fall damage: a landing under FALL_SAFE_TILES does no harm. Above it, damage
// scales with how far the impact speed exceeded the speed a safe fall reaches,
// tuned so a terminal-velocity landing removes all of MAX_HEALTH.
constexpr int FALL_SAFE_TILES = 7;
const float FALL_SAFE_SPEED = std::sqrt(2.0f * GRAVITY * FALL_SAFE_TILES * TILE_SIZE);
const float FALL_DAMAGE_SCALE = Player::MAX_HEALTH / (TERMINAL_VELOCITY - FALL_SAFE_SPEED);
```

(`<cmath>` and `<algorithm>` are already included in `Player.cpp`.)

- [ ] **Step 5: Add fall damage to `Player::move` in `src/Player/Player.cpp`**

Find the tail of `move`:

```cpp
    const float gravity = physics::overlapsFluid(body, world) ? GRAVITY * 0.3f : GRAVITY;
    speed.y += gravity * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);

    const physics::CollisionResult result = physics::moveAndCollide(body, speed, world, dt);

    grounded = result.grounded;
}
```

Replace it with:

```cpp
    const float gravity = physics::overlapsFluid(body, world) ? GRAVITY * 0.3f : GRAVITY;
    speed.y += gravity * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);

    // moveAndCollide zeroes speed.y on a landing, so record the speed we are
    // about to hit at first, for fall-damage.
    const bool wasGrounded = grounded;
    const float impactSpeed = speed.y;

    const physics::CollisionResult result = physics::moveAndCollide(body, speed, world, dt);

    grounded = result.grounded;

    // Fall damage fires once, on the airborne->grounded transition, scaled by how
    // far the impact speed exceeded a safe FALL_SAFE_TILES fall.
    if (!wasGrounded && grounded && impactSpeed > FALL_SAFE_SPEED)
    {
        const int damage =
            static_cast<int>(std::lround((impactSpeed - FALL_SAFE_SPEED) * FALL_DAMAGE_SCALE));
        applyDamage(damage);
    }
}
```

- [ ] **Step 6: Add `applyDamage` and `respawn` to `src/Player/Player.cpp`**

Add these definitions (e.g. after `Player::cycleSelectedSlot`):

```cpp
void Player::applyDamage(int amount)
{
    hp = std::max(0, hp - amount);
}

void Player::respawn(sf::Vector2f topLeft)
{
    body.position = topLeft;
    speed = {0.0f, 0.0f};
    grounded = false;
    hp = MAX_HEALTH;
}
```

- [ ] **Step 7: Run the tests**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: PASS — the four new fall/respawn tests and all existing tests.

- [ ] **Step 8: Commit**

```bash
git add src/Player/Player.h src/Player/Player.cpp tests/test_player.cpp
git commit -m "feat: player health with fall damage, death, and respawn

50 HP; falls past 7 tiles deal impact-scaled damage, a terminal-velocity fall
is lethal; respawn restores full health and clears velocity.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Lava damage

Damage over time while overlapping lava: 20 per 0.5 s, so three hits kill.

**Files:**
- Modify: `src/Player/Player.h`
- Modify: `src/Player/Player.cpp`
- Test: `tests/test_player.cpp`

**Interfaces:**
- Consumes: `physics::overlapsLava` (Task 1); `Player::applyDamage` (Task 2).
- Produces: private `void Player::applyLavaDamage(const World& world, float dt)`, private `float lavaTimer` member; `respawn` also resets `lavaTimer`.

- [ ] **Step 1: Write the failing tests**

First, if `tests/test_player.cpp` does not already `#include <cmath>` at the top, add it (the `tickFor` lambda below uses `std::lround` so `0.4 / STEP` rounds to 24 ticks rather than truncating to 23). Then append:

```cpp
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
```

- [ ] **Step 2: Run to verify failure**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: FAIL — the player takes no lava damage yet, so `health()` stays 50 where the test expects 30/10/dead.

- [ ] **Step 3: Add the lava state and helper declaration to `src/Player/Player.h`**

Declare the helper beside `applyDamage`:

```cpp
    void applyLavaDamage(const World& world, float dt);
```

Add the member beside `int hp`:

```cpp
    float lavaTimer = 0.0f;
```

- [ ] **Step 4: Add the lava constants to `src/Player/Player.cpp`**

In the anonymous namespace, after the fall constants from Task 2, add:

```cpp
// Lava: 20 damage per 0.5 s of contact, so three hits (0.5 / 1.0 / 1.5 s) kill.
constexpr int LAVA_DAMAGE = 20;
constexpr float LAVA_DAMAGE_INTERVAL = 0.5f;
```

- [ ] **Step 5: Call `applyLavaDamage` from `Player::update` in `src/Player/Player.cpp`**

Find:

```cpp
    move(input, world, dt);

    mine(input, world, result, dt);
    place(input, world, machines, result);
```

Replace with:

```cpp
    move(input, world, dt);
    applyLavaDamage(world, dt);

    mine(input, world, result, dt);
    place(input, world, machines, result);
```

- [ ] **Step 6: Implement `applyLavaDamage` and reset the timer in `respawn` (`src/Player/Player.cpp`)**

Add the definition (e.g. after `applyDamage`):

```cpp
void Player::applyLavaDamage(const World& world, float dt)
{
    if (physics::overlapsLava(body, world))
    {
        lavaTimer += dt;

        while (lavaTimer >= LAVA_DAMAGE_INTERVAL)
        {
            applyDamage(LAVA_DAMAGE);
            lavaTimer -= LAVA_DAMAGE_INTERVAL;
        }
    }
    else
    {
        lavaTimer = 0.0f;
    }
}
```

Then add the timer reset to `respawn` — change:

```cpp
    grounded = false;
    hp = MAX_HEALTH;
}
```

to:

```cpp
    grounded = false;
    hp = MAX_HEALTH;
    lavaTimer = 0.0f;
}
```

- [ ] **Step 7: Run the tests**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: PASS — both lava tests plus everything from Tasks 1-2.

- [ ] **Step 8: Commit**

```bash
git add src/Player/Player.h src/Player/Player.cpp tests/test_player.cpp
git commit -m "feat: lava deals 20 damage per 0.5s of contact (three hits kill)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Respawn wiring + HUD health bar

Wire death→respawn into the game loop and draw the health bar. This is graphics/integration (game target), verified by build + full suite (no regression) + a manual visual check.

**Files:**
- Modify: `src/Game/Game.cpp`
- Modify: `src/Hud/Hud.h`
- Modify: `src/Hud/Hud.cpp`

**Interfaces:**
- Consumes: `Player::isDead()`, `Player::respawn()`, `Player::health()`, `Player::MAX_HEALTH` (Task 2); `Game::findSpawn()`, `Camera::snapTo` (existing); `currentWindowView` (file-local helper in `Hud.cpp`).
- Produces: `void Hud::drawHealth(sf::RenderWindow& window, int health, int maxHealth);`

- [ ] **Step 1: Respawn on death in `Game::fixedUpdate` (`src/Game/Game.cpp`)**

Find:

```cpp
    const PlayerInput input = readInput();
    const ActionResult result = player.update(input, world, dt, &machines);
```

Insert the death check right after:

```cpp
    const PlayerInput input = readInput();
    const ActionResult result = player.update(input, world, dt, &machines);

    if (player.isDead())
    {
        player.respawn(findSpawn());
        camera.snapTo(player.center());
    }
```

- [ ] **Step 2: Declare `drawHealth` in `src/Hud/Hud.h`**

In the public section (e.g. after the `draw` declaration), add:

```cpp
    // A fixed health bar in the top-left corner: a red fill proportional to
    // health/maxHealth over a dark back. Shapes only, so it renders even with no
    // font loaded.
    void drawHealth(sf::RenderWindow& window, int health, int maxHealth);
```

- [ ] **Step 3: Implement `drawHealth` in `src/Hud/Hud.cpp`**

Add the definition (e.g. after `Hud::draw`). If `<algorithm>` is not already included at the top of `Hud.cpp`, add it.

```cpp
void Hud::drawHealth(sf::RenderWindow& window, int health, int maxHealth)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    constexpr float BAR_WIDTH = 200.0f;
    constexpr float BAR_HEIGHT = 18.0f;

    const float frac = maxHealth > 0
        ? std::clamp(static_cast<float>(health) / static_cast<float>(maxHealth), 0.0f, 1.0f)
        : 0.0f;

    sf::RectangleShape back({BAR_WIDTH, BAR_HEIGHT});
    back.setPosition({MARGIN, MARGIN});
    back.setFillColor(sf::Color(40, 20, 20));
    back.setOutlineThickness(2.0f);
    back.setOutlineColor(sf::Color(15, 8, 8));
    window.draw(back);

    if (frac > 0.0f)
    {
        sf::RectangleShape fill({BAR_WIDTH * frac, BAR_HEIGHT});
        fill.setPosition({MARGIN, MARGIN});
        fill.setFillColor(sf::Color(200, 50, 50));
        window.draw(fill);
    }

    window.setView(previous);
}
```

- [ ] **Step 4: Draw it in `Game::render` (`src/Game/Game.cpp`)**

Find:

```cpp
    hud.draw(window, player.inventory(), player.selectedSlot());
```

Add directly after:

```cpp
    hud.drawHealth(window, player.health(), Player::MAX_HEALTH);
```

- [ ] **Step 5: Build the whole project and run the suite**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug; & "build/Debug/Litharia_tests.exe"`
Expected: both `Litharia.exe` and `Litharia_tests.exe` build clean; the full suite still passes (this task adds no tests and must not regress any).

- [ ] **Step 6: Manual visual check (controller/user, optional for the subagent)**

Launch `& "build/Debug/Litharia.exe"`: a red health bar shows top-left; taking a big fall or standing in lava shrinks it; dying teleports the player to spawn with a full bar. (A subagent cannot see the window — note this as deferred.)

- [ ] **Step 7: Commit**

```bash
git add src/Game/Game.cpp src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "feat: respawn on death and draw a HUD health bar

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Retune underground pool counts

Water pools 10 → 25, lava pools 15 → 40. The terrain tests assert against the constants themselves (`TerrainGenerator::WATER_POOL_COUNT` / `LAVA_POOL_COUNT`), so they need no edits — but must still pass.

**Files:**
- Modify: `src/World/TerrainGenerator.h`

**Interfaces:**
- Consumes: nothing.
- Produces: updated `WATER_POOL_COUNT` / `LAVA_POOL_COUNT` values.

- [ ] **Step 1: Change the constants in `src/World/TerrainGenerator.h`**

Find:

```cpp
    static constexpr int SURFACE_LAKE_COUNT = 5;
    static constexpr int WATER_POOL_COUNT = 10;
    static constexpr int LAVA_POOL_COUNT = 15;
```

Replace with:

```cpp
    static constexpr int SURFACE_LAKE_COUNT = 5;
    static constexpr int WATER_POOL_COUNT = 25;
    static constexpr int LAVA_POOL_COUNT = 40;
```

- [ ] **Step 2: Build and run the full suite**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug; & "build/Debug/Litharia_tests.exe"`
Expected: `[doctest] Status: SUCCESS!`. The terrain pool-count/placement tests compare against the constants, so they pass at the new values (and confirm the extra pools still land inside their depth bands).

- [ ] **Step 3: Commit**

```bash
git add src/World/TerrainGenerator.h
git commit -m "tweak: more underground pools (water 10->25, lava 15->40)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- 50 HP, health API, death → Task 2 (field, `health`/`isDead`/`respawn`). ✓
- Fall damage past 7 tiles, terminal-lethal → Task 2 (constants + `move`). ✓
- Lava 20 per 0.5 s, three hits kill, timer reset → Task 3. ✓
- `overlapsLava` → Task 1. ✓
- Respawn at spawn + camera snap → Task 4. ✓
- HUD health bar → Task 4. ✓
- Pool counts 25 / 40 → Task 5. ✓
- Death keeps inventory → Task 4 respawn only moves/heals the player, never touches `bag`. ✓

**Placeholder scan:** No TBD/TODO; every code step has complete code; every command has expected output. ✓

**Type consistency:** `overlapsLava(const AABB&, const World&) -> bool` declared (T1 S3) / defined (T1 S4) / consumed (T3 S6, test T1 S1) identically. `Player::MAX_HEALTH` (int), `health()->int`, `isDead()->bool`, `respawn(sf::Vector2f)`, `applyDamage(int)`, `applyLavaDamage(const World&, float)`, `hp` (int), `lavaTimer` (float) consistent across T2/T3 and the Game/HUD call sites. `Hud::drawHealth(sf::RenderWindow&, int, int)` declared (T4 S2) / defined (T4 S3) / called (T4 S4) identically. ✓

**Note on the pool change (Task 5):** verified the terrain tests reference `TerrainGenerator::WATER_POOL_COUNT` / `LAVA_POOL_COUNT` rather than hardcoded 10/15, so bumping the constants keeps them green; Step 2 runs the suite to confirm.
