# Knockback, Slower Enemies, Denser Cave Spawns Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the player and enemies knock each other back on hit, make both enemy types slow enough that the player can always outrun them, and make enemies spawn much more often while the player is underground.

**Architecture:** Add a `knockbackTimer` + `applyKnockback(vx)` pair to both `Enemy` and `Player`, mirroring their existing per-tick control logic (chase AI / steering input) but suppressing it for a short window after an impulse so the impulse isn't instantly overwritten. Wire the impulses into `Game.cpp`'s existing melee-hit and contact-damage-tick events. Separately, lower both enemies' `moveSpeed` in the registry, and give `Game::spawnEnemiesIfNeeded` a second, shorter interval used whenever the player's current tile is underground.

**Tech Stack:** C++20, SFML 3, CMake + Visual Studio generator, doctest.

## Global Constraints

- Nightstalker `moveSpeed`: 190 -> 100 px/s.
- Sunroamer `moveSpeed`: 140 -> 75 px/s.
- Player `MAX_RUN_SPEED` (230 px/s) is unchanged.
- Knockback control lockout: `KNOCKBACK_LOCK_SECONDS = 0.25f` (file-local constant, duplicated in `Enemy.cpp` and `Player.cpp` — this codebase already duplicates GRAVITY/TERMINAL_VELOCITY the same way rather than sharing a physics-constants header).
- Knockback is horizontal-only (no vertical/"pop" component).
- `ENEMY_KNOCKBACK_SPEED = 260.0f` (applied to an enemy on a landed sword hit).
- `PLAYER_KNOCKBACK_SPEED = 220.0f` (applied to the player on a contact-damage tick).
- `CAVE_SPAWN_ATTEMPT_INTERVAL = 1.0f` seconds (new; used while the player is underground).
- `SPAWN_ATTEMPT_INTERVAL = 3.0f` seconds (existing; unchanged, used everywhere else).
- `MAX_ENEMIES`: 10 -> 16.
- `attemptSpawn`'s own eligibility/classification logic is untouched — only the cadence `Game` calls it at changes.

---

### Task 1: Slow down both enemy types

**Files:**
- Modify: `src/Enemies/Enemy.cpp:18-21`
- Test: `tests/test_enemies.cpp:58-76`

**Interfaces:**
- Consumes: existing `EnemyInfo` struct, `enemyInfo(EnemyType)` (no signature changes).
- Produces: `enemyInfo(EnemyType::Nightstalker).moveSpeed == 100.0f`, `enemyInfo(EnemyType::Sunroamer).moveSpeed == 75.0f` — later tasks' knockback tests rely on these exact values.

- [ ] **Step 1: Update the failing test expectations**

In `tests/test_enemies.cpp`, change the existing `TEST_CASE("enemyInfo reports the documented stats for each type, Sunroamer weaker and slower")` (currently lines 58-76) so the two `moveSpeed` checks read:

```cpp
    CHECK(night.moveSpeed == doctest::Approx(100.0f));
```

and

```cpp
    CHECK(day.moveSpeed == doctest::Approx(75.0f));
```

Leave every other line in that test case (maxHealth, jumpSpeed, contactDamage, contactInterval, the `day.moveSpeed < night.moveSpeed` comparison) untouched.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug --target Litharia_tests && ./build/Debug/Litharia_tests.exe --test-case="enemyInfo reports the documented stats for each type, Sunroamer weaker and slower"`
Expected: FAIL — the two `moveSpeed` checks report actual values 190.0 and 140.0, not the new 100.0/75.0.

- [ ] **Step 3: Update the registry**

In `src/Enemies/Enemy.cpp`, change the `registry` array (lines 18-21) from:

```cpp
constexpr std::array<EnemyInfo, 2> registry = {{
    {"Nightstalker", 28.0f, 42.0f, 40, 190.0f, 470.0f, 8, 0.6f},
    {"Sunroamer",     24.0f, 34.0f, 20, 140.0f, 420.0f, 4, 0.6f},
}};
```

to:

```cpp
constexpr std::array<EnemyInfo, 2> registry = {{
    {"Nightstalker", 28.0f, 42.0f, 40, 100.0f, 470.0f, 8, 0.6f},
    {"Sunroamer",     24.0f, 34.0f, 20, 75.0f, 420.0f, 4, 0.6f},
}};
```

(Only the fifth field — `moveSpeed` — changes on each row; `width, height, maxHealth, jumpSpeed, contactDamage, contactInterval` are untouched.)

- [ ] **Step 4: Run the full test suite to verify it passes**

Run: `cmake --build build --config Debug --target Litharia_tests && ./build/Debug/Litharia_tests.exe`
Expected: PASS — all tests green, including the updated `enemyInfo` case. (The "walks toward the player" and other chase-AI tests only assert direction/sign of velocity, not the exact speed, so they are unaffected.)

- [ ] **Step 5: Commit**

```bash
git add src/Enemies/Enemy.cpp tests/test_enemies.cpp
git commit -m "feat: slow down Nightstalker and Sunroamer so the player can outrun them"
```

---

### Task 2: Enemy knockback

**Files:**
- Modify: `src/Enemies/Enemy.h:37-69`
- Modify: `src/Enemies/Enemy.cpp:1-67`
- Test: `tests/test_enemies.cpp`

**Interfaces:**
- Consumes: nothing new from other tasks.
- Produces: `void Enemy::applyKnockback(float vx)` (public) — Task 4 (Game.cpp wiring) calls this on every enemy a sword hit damages.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_enemies.cpp`, after the existing `TEST_CASE("applyDamage clamps at 0 and isDead reports it")` case (end of file):

```cpp
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: BUILD FAILURE — `Enemy` has no member `applyKnockback` yet.

- [ ] **Step 3: Add the knockback state and method to Enemy.h**

In `src/Enemies/Enemy.h`, add a public method declaration right after `applyDamage` (currently line 53):

```cpp
    int health() const { return hp; }
    bool isDead() const { return hp <= 0; }
    void applyDamage(int amount);

    // Overrides horizontal velocity and suspends chase AI for
    // KNOCKBACK_LOCK_SECONDS (see Enemy.cpp), so the impulse isn't
    // instantly overwritten by the next tick's chase logic.
    void applyKnockback(float vx);
```

and add a private field right after `contactTimer` (currently line 68):

```cpp
    int hp;
    float contactTimer = 0.0f;
    float knockbackTimer = 0.0f;
```

- [ ] **Step 4: Implement the lock in Enemy::update and add applyKnockback**

In `src/Enemies/Enemy.cpp`, add the lock constant to the anonymous namespace (alongside `GRAVITY`/`TERMINAL_VELOCITY`, lines 15-16):

```cpp
constexpr float GRAVITY = 1800.0f;           // px/s^2
constexpr float TERMINAL_VELOCITY = 1100.0f; // px/s
constexpr float KNOCKBACK_LOCK_SECONDS = 0.25f;
```

Then change `Enemy::update` (lines 37-67) from:

```cpp
void Enemy::update(const World& world, sf::Vector2f playerCenter, float dt)
{
    const EnemyInfo& info = enemyInfo(kind);

    const float dx = playerCenter.x - center().x;
    if (dx > 1.0f)
        speed.x = info.moveSpeed;
    else if (dx < -1.0f)
        speed.x = -info.moveSpeed;
    else
        speed.x = 0.0f;

    // "Automatically jump if needed": jump whenever grounded and either the
    // last move was blocked sideways, or the player sits more than a tile
    // above. No pathfinding beyond this - if a solid ceiling separates the
    // two, this condition keeps firing every time the enemy lands from its
    // last hop, so it just keeps jumping into the ceiling's underside
    // rather than ever routing around it.
    const bool playerAbove = (playerCenter.y - center().y) < -static_cast<float>(TILE_SIZE);

    if (grounded && (blockedHorizontally || playerAbove))
        speed.y = -info.jumpSpeed;

    speed.y += GRAVITY * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);

    const physics::CollisionResult result = physics::moveAndCollide(body, speed, world, dt);

    grounded = result.grounded;
    blockedHorizontally = result.hitX;
}
```

to:

```cpp
void Enemy::update(const World& world, sf::Vector2f playerCenter, float dt)
{
    const EnemyInfo& info = enemyInfo(kind);

    knockbackTimer = std::max(0.0f, knockbackTimer - dt);

    // While a knockback impulse is still in effect, the chase/auto-jump
    // logic below is skipped entirely so it can't immediately overwrite
    // the impulse - gravity and collision still run every tick regardless.
    if (knockbackTimer <= 0.0f)
    {
        const float dx = playerCenter.x - center().x;
        if (dx > 1.0f)
            speed.x = info.moveSpeed;
        else if (dx < -1.0f)
            speed.x = -info.moveSpeed;
        else
            speed.x = 0.0f;

        // "Automatically jump if needed": jump whenever grounded and either
        // the last move was blocked sideways, or the player sits more than
        // a tile above. No pathfinding beyond this - if a solid ceiling
        // separates the two, this condition keeps firing every time the
        // enemy lands from its last hop, so it just keeps jumping into the
        // ceiling's underside rather than ever routing around it.
        const bool playerAbove = (playerCenter.y - center().y) < -static_cast<float>(TILE_SIZE);

        if (grounded && (blockedHorizontally || playerAbove))
            speed.y = -info.jumpSpeed;
    }

    speed.y += GRAVITY * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);

    const physics::CollisionResult result = physics::moveAndCollide(body, speed, world, dt);

    grounded = result.grounded;
    blockedHorizontally = result.hitX;
}
```

Finally, add the new method at the end of the file, after `tickContactDamage`:

```cpp
void Enemy::applyKnockback(float vx)
{
    speed.x = vx;
    knockbackTimer = KNOCKBACK_LOCK_SECONDS;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests && ./build/Debug/Litharia_tests.exe`
Expected: PASS — all tests green, including the two new knockback cases.

- [ ] **Step 6: Commit**

```bash
git add src/Enemies/Enemy.h src/Enemies/Enemy.cpp tests/test_enemies.cpp
git commit -m "feat: add Enemy::applyKnockback with a brief chase-AI lockout"
```

---

### Task 3: Player knockback

**Files:**
- Modify: `src/Player/Player.h:104-186`
- Modify: `src/Player/Player.cpp:1-277`
- Test: `tests/test_player.cpp`

**Interfaces:**
- Consumes: nothing new from other tasks.
- Produces: `void Player::applyKnockback(float vx)` (public) — Task 4 (Game.cpp wiring) calls this whenever an enemy's contact-damage tick fires.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_player.cpp`, after the existing `TEST_CASE("run speed is capped")` case:

```cpp
TEST_CASE("player applyKnockback sets velocity immediately and holds through the lock window")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);

    player.applyKnockback(-220.0f);
    CHECK(player.velocity().x == doctest::Approx(-220.0f));

    PlayerInput right;
    right.right = true;

    // 10 ticks (~0.167s) is still inside the 0.25s lock - steering must not
    // have reclaimed velocity.x despite holding right.
    for (int i = 0; i < 10; ++i)
        player.update(right, world, STEP);

    CHECK(player.velocity().x == doctest::Approx(-220.0f));
}

TEST_CASE("player steering resumes once the knockback lock has elapsed")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);

    player.applyKnockback(-220.0f);

    PlayerInput right;
    right.right = true;

    // 40 ticks (~0.667s) clears the 0.25s lock with room to spare; holding
    // right should have pulled velocity.x positive again by then.
    for (int i = 0; i < 40; ++i)
        player.update(right, world, STEP);

    CHECK(player.velocity().x > 0.0f);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: BUILD FAILURE — `Player` has no member `applyKnockback` yet.

- [ ] **Step 3: Add the knockback state and method to Player.h**

In `src/Player/Player.h`, add a public method declaration right after `takeDamage` (currently lines 107-111):

```cpp
    // Damage from a source Player doesn't compute itself - currently only
    // enemy contact damage, resolved in Game since Player doesn't know
    // about the enemy list. Routes through the same clamped applyDamage
    // fall/lava damage already uses.
    void takeDamage(int amount);

    // Overrides horizontal velocity and suspends steering input for
    // KNOCKBACK_LOCK_SECONDS (see Player.cpp), so the impulse isn't
    // instantly overwritten by the next tick's input handling.
    void applyKnockback(float vx);
```

and add a private field right after `lavaTimer` (currently line 152):

```cpp
    int hp = MAX_HEALTH;
    float lavaTimer = 0.0f;
    float knockbackTimer = 0.0f;
```

- [ ] **Step 4: Implement the lock in Player::move and add applyKnockback**

In `src/Player/Player.cpp`, add the lock constant to the anonymous namespace (alongside `GRAVITY`/`TERMINAL_VELOCITY`, lines 20-21):

```cpp
constexpr float GRAVITY = 1800.0f;           // px/s^2
constexpr float TERMINAL_VELOCITY = 1100.0f; // px/s
constexpr float KNOCKBACK_LOCK_SECONDS = 0.25f;
```

Then change the top of `Player::move` (lines 217-235) from:

```cpp
void Player::move(const PlayerInput& input, const World& world, float dt)
{
    if (input.left != input.right)
        facingDir = input.right ? Direction::Right : Direction::Left;

    const float steer = (input.right ? 1.0f : 0.0f) - (input.left ? 1.0f : 0.0f);

    if (steer != 0.0f)
    {
        const float control = grounded ? 1.0f : AIR_CONTROL;

        speed.x += steer * MOVE_ACCELERATION * control * dt;
        speed.x = std::clamp(speed.x, -MAX_RUN_SPEED, MAX_RUN_SPEED);
    }
    else if (grounded)
    {
        // Friction only bites on the ground; in the air you keep your momentum.
        speed.x = applyFriction(speed.x, GROUND_FRICTION * dt);
    }
```

to:

```cpp
void Player::move(const PlayerInput& input, const World& world, float dt)
{
    if (input.left != input.right)
        facingDir = input.right ? Direction::Right : Direction::Left;

    knockbackTimer = std::max(0.0f, knockbackTimer - dt);

    // While a knockback impulse is still in effect, steering is skipped
    // entirely so it can't immediately overwrite the impulse - jump,
    // gravity, and collision still run every tick regardless.
    if (knockbackTimer <= 0.0f)
    {
        const float steer = (input.right ? 1.0f : 0.0f) - (input.left ? 1.0f : 0.0f);

        if (steer != 0.0f)
        {
            const float control = grounded ? 1.0f : AIR_CONTROL;

            speed.x += steer * MOVE_ACCELERATION * control * dt;
            speed.x = std::clamp(speed.x, -MAX_RUN_SPEED, MAX_RUN_SPEED);
        }
        else if (grounded)
        {
            // Friction only bites on the ground; in the air you keep your momentum.
            speed.x = applyFriction(speed.x, GROUND_FRICTION * dt);
        }
    }
```

(The rest of `move` — the fluid check, jump, gravity, `moveAndCollide`, fall damage — is unchanged, and stays outside this `if` block exactly as it already sits after the block being replaced.)

Finally, add the new method near the existing `takeDamage`/`applyDamage` definitions:

```cpp
void Player::applyKnockback(float vx)
{
    speed.x = vx;
    knockbackTimer = KNOCKBACK_LOCK_SECONDS;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests && ./build/Debug/Litharia_tests.exe`
Expected: PASS — all tests green, including the two new knockback cases.

- [ ] **Step 6: Commit**

```bash
git add src/Player/Player.h src/Player/Player.cpp tests/test_player.cpp
git commit -m "feat: add Player::applyKnockback with a brief steering lockout"
```

---

### Task 4: Wire knockback into Game.cpp's melee and contact-damage events

**Files:**
- Modify: `src/Game/Game.cpp:47-51` (constants), `:180-198` (`resolveMeleeHit`), `:286-310` (`updateEnemies`)

**Interfaces:**
- Consumes: `Enemy::applyKnockback(float)` (Task 2), `Player::applyKnockback(float)` (Task 3).
- Produces: nothing new for later tasks (this is the last piece of the knockback feature).

- [ ] **Step 1: Add the knockback-speed constants**

In `src/Game/Game.cpp`'s anonymous namespace, add alongside `SWORD_REACH_TILES` (currently lines 47-50):

```cpp
// The sword's reach, in tiles - shared by resolveMeleeHit's actual hitbox
// check and render's drawn blade length, so the visible sword can never
// drift out of sync with what it actually hits.
constexpr float SWORD_REACH_TILES = 2.5f;

// Horizontal-only knockback impulses (px/s), applied on top of whatever
// velocity the target already had. See Enemy::applyKnockback /
// Player::applyKnockback for the brief AI/steering lockout that lets the
// impulse actually survive a tick instead of being instantly overwritten.
constexpr float ENEMY_KNOCKBACK_SPEED = 260.0f;
constexpr float PLAYER_KNOCKBACK_SPEED = 220.0f;
```

- [ ] **Step 2: Apply knockback to the enemy on a landed sword hit**

Change `Game::resolveMeleeHit` (currently lines 180-198) from:

```cpp
void Game::resolveMeleeHit(const ActionResult& result)
{
    const float reachPx = SWORD_REACH_TILES * TILE_SIZE;

    for (Enemy& enemy : enemies)
    {
        const sf::Vector2f offset = enemy.center() - player.center();

        const bool onFacingSide =
            player.facing() == Direction::Right ? offset.x >= 0.0f : offset.x <= 0.0f;
        if (!onFacingSide)
            continue;

        if (offset.x * offset.x + offset.y * offset.y > reachPx * reachPx)
            continue;

        enemy.applyDamage(result.meleeDamage);
    }
}
```

to:

```cpp
void Game::resolveMeleeHit(const ActionResult& result)
{
    const float reachPx = SWORD_REACH_TILES * TILE_SIZE;

    for (Enemy& enemy : enemies)
    {
        const sf::Vector2f offset = enemy.center() - player.center();

        const bool onFacingSide =
            player.facing() == Direction::Right ? offset.x >= 0.0f : offset.x <= 0.0f;
        if (!onFacingSide)
            continue;

        if (offset.x * offset.x + offset.y * offset.y > reachPx * reachPx)
            continue;

        enemy.applyDamage(result.meleeDamage);

        // Away from the player, along the facing side already established
        // above - falls back to the player's own facing direction only in
        // the (essentially impossible, given onFacingSide) case the enemy
        // sits exactly on the player's center.
        const float direction = offset.x != 0.0f
            ? (offset.x > 0.0f ? 1.0f : -1.0f)
            : (player.facing() == Direction::Right ? 1.0f : -1.0f);

        enemy.applyKnockback(direction * ENEMY_KNOCKBACK_SPEED);
    }
}
```

- [ ] **Step 3: Apply knockback to the player on a contact-damage tick**

Change `Game::updateEnemies` (currently lines 286-310) from:

```cpp
void Game::updateEnemies(float dt)
{
    // Dead enemies just vanish - no drops yet, see the design doc's Scope
    // section. Erased before the loop below (not after) so an enemy killed
    // by resolveMeleeHit earlier this same tick is already gone and cannot
    // still act - one more frame of AI movement plus a contact-damage tick
    // it has no business getting.
    std::erase_if(enemies, [](const Enemy& e) { return e.isDead(); });

    for (Enemy& enemy : enemies)
    {
        enemy.update(world, player.center(), dt);

        const bool touching = physics::overlaps(enemy.box(), player.box());
        const int contactDamage = enemy.tickContactDamage(touching, dt);

        if (contactDamage > 0)
        {
            player.takeDamage(contactDamage);
            spawnDamagePopup(contactDamage);
        }
    }

    despawnEnemies();
}
```

to:

```cpp
void Game::updateEnemies(float dt)
{
    // Dead enemies just vanish - no drops yet, see the design doc's Scope
    // section. Erased before the loop below (not after) so an enemy killed
    // by resolveMeleeHit earlier this same tick is already gone and cannot
    // still act - one more frame of AI movement plus a contact-damage tick
    // it has no business getting.
    std::erase_if(enemies, [](const Enemy& e) { return e.isDead(); });

    for (Enemy& enemy : enemies)
    {
        enemy.update(world, player.center(), dt);

        const bool touching = physics::overlaps(enemy.box(), player.box());
        const int contactDamage = enemy.tickContactDamage(touching, dt);

        if (contactDamage > 0)
        {
            player.takeDamage(contactDamage);
            spawnDamagePopup(contactDamage);

            // Away from the enemy that just hit them.
            const sf::Vector2f offset = player.center() - enemy.center();
            const float direction = offset.x != 0.0f
                ? (offset.x > 0.0f ? 1.0f : -1.0f)
                : (player.facing() == Direction::Right ? 1.0f : -1.0f);

            player.applyKnockback(direction * PLAYER_KNOCKBACK_SPEED);
        }
    }

    despawnEnemies();
}
```

- [ ] **Step 4: Build and run the full test suite**

Run: `cmake --build build --config Debug --target Litharia_tests && ./build/Debug/Litharia_tests.exe`
Expected: PASS — all tests green. This wiring has no window-independent unit-test seam of its own (it reaches into the live `enemies`/`player` members `Game` owns), so this is a build + regression check, not a new test.

- [ ] **Step 5: Build the game binary too**

Run: `cmake --build build --config Debug --target Litharia`
Expected: builds cleanly, confirming `Game.cpp` itself compiles with the new calls.

- [ ] **Step 6: Commit**

```bash
git add src/Game/Game.cpp
git commit -m "feat: knock the player and enemies back off each other's hits"
```

---

### Task 5: Cave spawn constants

**Files:**
- Modify: `src/Enemies/EnemySpawner.h:30-35`

**Interfaces:**
- Consumes: nothing.
- Produces: `CAVE_SPAWN_ATTEMPT_INTERVAL` (float, 1.0f) and `MAX_ENEMIES` (int, 16) — Task 6 (`Game::spawnEnemiesIfNeeded`) reads both.

- [ ] **Step 1: Confirm the existing MAX_ENEMIES tests are parameterized, not literal**

Run: `grep -n "MAX_ENEMIES" tests/test_enemy_spawn.cpp`
Expected output includes lines like:
```
CHECK_FALSE(attemptSpawn(world, generator, view, 0.0f, MAX_ENEMIES, 6u).has_value());
CHECK_FALSE(attemptSpawn(world, generator, view, 0.0f, MAX_ENEMIES + 5, 7u).has_value());
```
These reference the symbol, not the literal `10`, so raising the constant needs no test changes — confirm this before proceeding (if a literal `10` is found instead, stop and update this task before continuing).

- [ ] **Step 2: Update the constants**

In `src/Enemies/EnemySpawner.h`, change:

```cpp
inline constexpr int MAX_ENEMIES = 10;
inline constexpr int SPAWN_MARGIN_TILES = 3;
inline constexpr int SPAWN_VERTICAL_SEARCH_TILES = 40;
inline constexpr float SUNROAMER_SPAWN_CHANCE = 0.08f;
inline constexpr float SPAWN_ATTEMPT_INTERVAL = 3.0f; // seconds, Game's own spawn timer
inline constexpr float NIGHT_THRESHOLD = 0.5f;        // daylightFactor() below this counts as night
```

to:

```cpp
inline constexpr int MAX_ENEMIES = 16;
inline constexpr int SPAWN_MARGIN_TILES = 3;
inline constexpr int SPAWN_VERTICAL_SEARCH_TILES = 40;
inline constexpr float SUNROAMER_SPAWN_CHANCE = 0.08f;
inline constexpr float SPAWN_ATTEMPT_INTERVAL = 3.0f;      // seconds, Game's own spawn timer
inline constexpr float CAVE_SPAWN_ATTEMPT_INTERVAL = 1.0f; // seconds, used while the player is underground
inline constexpr float NIGHT_THRESHOLD = 0.5f;             // daylightFactor() below this counts as night
```

- [ ] **Step 3: Build and run the full test suite**

Run: `cmake --build build --config Debug --target Litharia_tests && ./build/Debug/Litharia_tests.exe`
Expected: PASS — all tests green, including every `MAX_ENEMIES`-based check in `test_enemy_spawn.cpp`, now exercised at 16 instead of 10.

- [ ] **Step 4: Commit**

```bash
git add src/Enemies/EnemySpawner.h
git commit -m "feat: raise the enemy cap and add a cave spawn-interval constant"
```

---

### Task 6: Wire the cave spawn cadence into Game::spawnEnemiesIfNeeded

**Files:**
- Modify: `src/Game/Game.cpp:267-284`

**Interfaces:**
- Consumes: `CAVE_SPAWN_ATTEMPT_INTERVAL`, `SPAWN_ATTEMPT_INTERVAL` (Task 5), `TerrainGenerator::surfaceHeight(int) const` (existing), `player.center()` (existing).
- Produces: nothing new for later tasks — this is the last task in the plan.

- [ ] **Step 1: Change the interval selection**

Change `Game::spawnEnemiesIfNeeded` (currently lines 267-284) from:

```cpp
void Game::spawnEnemiesIfNeeded(float dt)
{
    enemySpawnTimer += dt;
    if (enemySpawnTimer < SPAWN_ATTEMPT_INTERVAL)
        return;

    enemySpawnTimer = 0.0f;
    ++enemySpawnCounter;

    const ViewBounds view = currentViewBounds();

    const auto spawn = attemptSpawn(world, generator, view, dayNightClock.daylightFactor(),
                                     static_cast<int>(enemies.size()),
                                     static_cast<std::uint32_t>(enemySpawnCounter));

    if (spawn.has_value())
        enemies.emplace_back(spawn->type, spawn->position);
}
```

to:

```cpp
void Game::spawnEnemiesIfNeeded(float dt)
{
    enemySpawnTimer += dt;

    // Same "underground" test despawnEnemies already uses: the player's own
    // column compared against the generator's surface height for it. Caves
    // get a much shorter attempt interval so they feel denser to explore;
    // everywhere else keeps the original cadence.
    const int playerTileX = static_cast<int>(player.center().x / TILE_SIZE);
    const int playerTileY = static_cast<int>(player.center().y / TILE_SIZE);
    const bool playerUnderground = playerTileY > generator.surfaceHeight(playerTileX);

    const float interval = playerUnderground ? CAVE_SPAWN_ATTEMPT_INTERVAL : SPAWN_ATTEMPT_INTERVAL;

    if (enemySpawnTimer < interval)
        return;

    enemySpawnTimer = 0.0f;
    ++enemySpawnCounter;

    const ViewBounds view = currentViewBounds();

    const auto spawn = attemptSpawn(world, generator, view, dayNightClock.daylightFactor(),
                                     static_cast<int>(enemies.size()),
                                     static_cast<std::uint32_t>(enemySpawnCounter));

    if (spawn.has_value())
        enemies.emplace_back(spawn->type, spawn->position);
}
```

- [ ] **Step 2: Build and run the full test suite**

Run: `cmake --build build --config Debug --target Litharia_tests && ./build/Debug/Litharia_tests.exe`
Expected: PASS — all tests green. `spawnEnemiesIfNeeded` is `Game`-layer code with no window-independent seam (same category as the rest of `Game`'s wiring, per the design doc's Testing section), so this is a build + regression check, not a new unit test.

- [ ] **Step 3: Build the game binary too**

Run: `cmake --build build --config Debug --target Litharia`
Expected: builds cleanly.

- [ ] **Step 4: Commit**

```bash
git add src/Game/Game.cpp
git commit -m "feat: spawn enemies much more often while the player is underground"
```

---

## Final verification

- [ ] Run the complete test suite one more time end to end: `cmake --build build --config Debug --target Litharia_tests && ./build/Debug/Litharia_tests.exe`
- [ ] Build the game binary: `cmake --build build --config Debug --target Litharia`
- [ ] Per your standing preference, no automated live-game input verification is run — hand off to you to playtest knockback feel, outrunning enemies, and cave spawn density directly.
