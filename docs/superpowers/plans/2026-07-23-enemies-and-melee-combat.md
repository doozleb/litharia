# Enemies and Melee Combat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two enemy types (a common night/cave Nightstalker and a rare daytime Sunroamer) that spawn just outside the player's view and chase them with simple jump-capable physics AI, plus a five-tier sword line the player uses to fight back.

**Architecture:** A new `src/Enemies/` module (`Enemy` entity + `EnemySpawner` pure spawn-decision function) lives in the SFML-System-only `Litharia_core` library alongside `Player`/`ItemEntity`, so both are unit-testable without a window. Swords are a new parallel item category (`ItemInfo::isSword`/`meleeDamage`, reusing the existing `ToolTier` field) that turns the player's existing "hold mine to act" input into a swing state machine, reported back to `Game` through new `ActionResult` fields exactly like mining already reports `BrokenTile`s. `Game` (the only place that owns both `Player` and the enemy list) resolves contact damage and sword hits, and renders enemies/the swing as plain colored shapes, matching the game's current no-sprite style.

**Tech Stack:** C++20, SFML 3 (System/Physics core is graphics-free; Game/rendering links Graphics+Window), CMake (Visual Studio generator, multi-config), doctest.

## Global Constraints

- Core-testable code (`Enemy`, `EnemySpawner`, `Player` changes) must not include SFML Graphics/Window headers - only `SFML/System/Vector2.hpp`, matching `Player`/`ItemEntity`/`Physics`. It goes in `Litharia_core` in `CMakeLists.txt`.
- Game-layer wiring (`src/Game/Game.cpp`/`.h`) is integration code, not unit-tested here - verify it by building and a manual in-game check, the same convention the health-and-damage feature used.
- Every existing registry row (`ItemInfo`, `CraftRecipe`) must keep compiling unchanged - new fields are added with trailing defaults, never by reordering existing fields.
- Enemy stats, sword damage/timing, and spawn constants are exact values from the approved spec (`docs/superpowers/specs/2026-07-23-enemies-and-melee-combat-design.md`) - do not re-derive or "round" them differently.
- Manual in-game verification must use the **Release** build, not Debug - Debug is unplayable in this project (hangs within seconds).
- Follow this repo's existing test style: `constexpr float STEP = 1.0f / 60.0f;` fixed-timestep loops, `buildFloor`-style helpers, `doctest::Approx` for floats. Look at `tests/test_player.cpp` before writing new tests in this plan - it is the template every new test in this plan follows.

---

## Task 1: Sword items - registry fields, five new items, swing-duration table

**Files:**
- Modify: `src/Blocks/Blocks.h`
- Modify: `src/Items/Items.h`
- Modify: `src/Items/Items.cpp`
- Test: `tests/test_mining.cpp` (append)

**Interfaces:**
- Produces: `ItemInfo::isSword` (bool, default false), `ItemInfo::meleeDamage` (int, default 0); `ItemType::WoodSword/StoneSword/CopperSword/IronSword/ObsidianSword`; `SWORD_SWING_SECONDS` (`std::array<float, 5>`, indexed by `ToolTier`), `swordSwingSeconds(ToolTier)`, `SWORD_SWING_DELAY` (float, `0.5f`) in `Blocks.h`. Task 4 (`Player`'s swing state machine) consumes all of these.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_mining.cpp` (it already contains every `ToolTier`/`itemInfo` test in this codebase):

```cpp
TEST_CASE("the five sword tiers are correctly typed melee items")
{
    CHECK(itemInfo(ItemType::WoodSword).isSword);
    CHECK(itemInfo(ItemType::WoodSword).toolType == ToolType::None);
    CHECK(itemInfo(ItemType::WoodSword).tier == ToolTier::Wood);
    CHECK(itemInfo(ItemType::WoodSword).meleeDamage == 8);

    CHECK(itemInfo(ItemType::StoneSword).isSword);
    CHECK(itemInfo(ItemType::StoneSword).tier == ToolTier::Stone);
    CHECK(itemInfo(ItemType::StoneSword).meleeDamage == 13);

    CHECK(itemInfo(ItemType::CopperSword).isSword);
    CHECK(itemInfo(ItemType::CopperSword).tier == ToolTier::Copper);
    CHECK(itemInfo(ItemType::CopperSword).meleeDamage == 18);

    CHECK(itemInfo(ItemType::IronSword).isSword);
    CHECK(itemInfo(ItemType::IronSword).tier == ToolTier::Iron);
    CHECK(itemInfo(ItemType::IronSword).meleeDamage == 23);

    CHECK(itemInfo(ItemType::ObsidianSword).isSword);
    CHECK(itemInfo(ItemType::ObsidianSword).tier == ToolTier::Obsidian);
    CHECK(itemInfo(ItemType::ObsidianSword).meleeDamage == 28);
}

TEST_CASE("non-sword items still report isSword = false and meleeDamage = 0")
{
    CHECK_FALSE(itemInfo(ItemType::WoodPickaxe).isSword);
    CHECK(itemInfo(ItemType::WoodPickaxe).meleeDamage == 0);
    CHECK_FALSE(itemInfo(ItemType::Stone).isSword);
    CHECK_FALSE(itemInfo(ItemType::Torch).isSword);
}

TEST_CASE("sword swing duration decreases with tier, 0.8s at Wood down to 0.6s at Obsidian")
{
    CHECK(swordSwingSeconds(ToolTier::Wood) == doctest::Approx(0.8f));
    CHECK(swordSwingSeconds(ToolTier::Stone) == doctest::Approx(0.75f));
    CHECK(swordSwingSeconds(ToolTier::Copper) == doctest::Approx(0.7f));
    CHECK(swordSwingSeconds(ToolTier::Iron) == doctest::Approx(0.65f));
    CHECK(swordSwingSeconds(ToolTier::Obsidian) == doctest::Approx(0.6f));
    CHECK(SWORD_SWING_DELAY == doctest::Approx(0.5f));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL to compile - `ItemType::WoodSword`, `isSword`, `meleeDamage`, `swordSwingSeconds`, `SWORD_SWING_DELAY` are not declared.

- [ ] **Step 3: Add the swing-duration table to `Blocks.h`**

In `src/Blocks/Blocks.h`, immediately after the existing `toolTierSpeedMultiplier` function (around line 51):

```cpp
// Sword swing duration, indexed by ToolTier: 0.8s at Wood down to 0.6s at
// Obsidian, an even 0.05s faster per tier. A fixed delay follows every
// swing regardless of tier - see SWORD_SWING_DELAY.
inline constexpr std::array<float, 5> SWORD_SWING_SECONDS = {
    0.8f,  // Wood
    0.75f, // Stone
    0.7f,  // Copper
    0.65f, // Iron
    0.6f,  // Obsidian
};

inline float swordSwingSeconds(ToolTier tier)
{
    return SWORD_SWING_SECONDS[static_cast<std::size_t>(tier)];
}

// The fixed delay after any sword swing finishes, before a new one can
// start - the same regardless of tier.
inline constexpr float SWORD_SWING_DELAY = 0.5f;
```

- [ ] **Step 4: Add the new `ItemInfo` fields and `ItemType` values in `Items.h`**

In `src/Items/Items.h`, add five entries to the `ItemType` enum, immediately before `Count`:

```cpp
    Torch,

    WoodSword,
    StoneSword,
    CopperSword,
    IronSword,
    ObsidianSword,

    Count
```

And add two trailing fields to `ItemInfo` (after `tier`), so every existing registry row keeps compiling unchanged:

```cpp
struct ItemInfo
{
    std::string_view name;
    int maxStack;
    BlockType placeBlock;
    ToolType toolType;
    BlockColor iconColor;
    ToolTier tier = ToolTier::Wood;

    // A melee weapon: toolType stays None (it doesn't mine), but it reuses
    // tier for its swing-duration/damage progression. Selecting one in the
    // hotbar turns the "mine" input into a swing instead of mining - see
    // Player::swing. Meaningless when isSword is false.
    bool isSword = false;
    int meleeDamage = 0;
};
```

- [ ] **Step 5: Add the five registry rows in `Items.cpp`**

In `src/Items/Items.cpp`, append to the `registry` array, immediately after the `"Torch"` row:

```cpp
    {"Torch",             50,  BlockType::Air,       ToolType::None,    {230, 170,  60}},
    {"Wood Sword",         1,  BlockType::Air,       ToolType::None,    {200, 170, 120}, ToolTier::Wood,     true,  8},
    {"Stone Sword",        1,  BlockType::Air,       ToolType::None,    {150, 150, 155}, ToolTier::Stone,    true, 13},
    {"Copper Sword",       1,  BlockType::Air,       ToolType::None,    {205, 130,  75}, ToolTier::Copper,   true, 18},
    {"Iron Sword",         1,  BlockType::Air,       ToolType::None,    {190, 195, 205}, ToolTier::Iron,     true, 23},
    {"Obsidian Sword",     1,  BlockType::Air,       ToolType::None,    { 70,  40,  90}, ToolTier::Obsidian, true, 28},
}};
```

(Replace the existing closing `}};` after `"Torch"` with the five new rows followed by `}};`.)

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `build/Debug/Litharia_tests.exe`
Expected: PASS, all three new test cases green, no existing test broken.

- [ ] **Step 7: Commit**

```bash
git add src/Blocks/Blocks.h src/Items/Items.h src/Items/Items.cpp tests/test_mining.cpp
git commit -m "feat: add five sword tiers as a new item category"
```

---

## Task 2: Sword recipes

**Files:**
- Modify: `src/Machines/Recipes.cpp`
- Test: `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: `ItemType::WoodSword/StoneSword/CopperSword/IronSword/ObsidianSword` (Task 1).
- Produces: nothing new consumed elsewhere - this task only adds data to the existing `allCraftRecipes()` registry.

- [ ] **Step 1: Write the failing tests**

In `tests/test_recipes.cpp`, change the existing count assertion (around line 71):

```cpp
TEST_CASE("allCraftRecipes exposes every craftable item exactly once")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    CHECK(all.size() == 32);
}
```

(Was `27`; five sword recipes are being added.)

Then append two new test cases:

```cpp
TEST_CASE("the Wood Sword recipe costs 2 sticks and 1 oak log - no starting freebie like the pickaxe/axe")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::WoodSword; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::OakLog);
    CHECK(it->ingredients[1].count == 1);
    CHECK(it->seconds == doctest::Approx(1.5f));
}

TEST_CASE("the Obsidian Sword recipe costs 2 sticks and 2 obsidian")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ObsidianSword; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Obsidian);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(2.0f));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `build/Debug/Litharia_tests.exe`
Expected: FAIL - size is 27 not 32, and the two `find_if` results are `all.end()`.

- [ ] **Step 3: Add the five recipes**

In `src/Machines/Recipes.cpp`, change `constexpr std::array<CraftRecipe, 27> craftRecipes` to `constexpr std::array<CraftRecipe, 32> craftRecipes`, and append these five rows immediately before the closing `}};`:

```cpp
    {ItemType::Torch,           {{{ItemType::Stone, 2}, {ItemType::Stick, 1}}},            1.0f, true, 2},
    {ItemType::WoodSword,       {{{ItemType::Stick, 2}, {ItemType::OakLog, 1}}},            1.5f, true},
    {ItemType::StoneSword,      {{{ItemType::Stick, 2}, {ItemType::SharpRock, 2}}},         2.0f, true},
    {ItemType::CopperSword,     {{{ItemType::Stick, 2}, {ItemType::CopperPlate, 3}}},       2.0f, true},
    {ItemType::IronSword,       {{{ItemType::Stick, 2}, {ItemType::IronPlate, 3}}},         2.0f, true},
    {ItemType::ObsidianSword,   {{{ItemType::Stick, 2}, {ItemType::Obsidian, 2}}},          2.0f, true},
}};
```

(The `Torch` line is shown for context/anchor - it is unchanged, only the five `*Sword` lines after it and the array size are new.)

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/Machines/Recipes.cpp tests/test_recipes.cpp
git commit -m "feat: make swords craftable at the Crafting Table"
```

---

## Task 3: Player facing direction

**Files:**
- Modify: `src/Player/Player.h`
- Modify: `src/Player/Player.cpp`
- Test: `tests/test_player.cpp`

**Interfaces:**
- Consumes: `Direction` enum (`src/Core/Direction.h`, already exists: `Up, Down, Left, Right`).
- Produces: `Player::facing() const -> Direction`. Task 4 (swing) and Task 8 (Game's hit resolution and blade rendering) both consume this.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_player.cpp`:

```cpp
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
```

This needs `#include "Core/Direction.h"` added to `tests/test_player.cpp`'s include block.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL to compile - `Player::facing()` is not declared.

- [ ] **Step 3: Add `facing()` to `Player.h`**

In `src/Player/Player.h`, add the include:

```cpp
#include "../Core/Direction.h"
```

Add the public accessor (near `isGrounded()`):

```cpp
    bool isGrounded() const { return grounded; }

    // Left/Right only, updated whenever horizontal input is nonzero and
    // held otherwise. Used for the sword swing's hit-side and render angle.
    Direction facing() const { return facingDir; }
```

Add the private member (near `grounded`):

```cpp
    bool grounded = false;
    Direction facingDir = Direction::Right;
```

- [ ] **Step 4: Update `facingDir` in `Player::move`**

In `src/Player/Player.cpp`, at the very top of `Player::move` (before the `steer` calculation):

```cpp
void Player::move(const PlayerInput& input, const World& world, float dt)
{
    if (input.left != input.right)
        facingDir = input.right ? Direction::Right : Direction::Left;

    const float steer = (input.right ? 1.0f : 0.0f) - (input.left ? 1.0f : 0.0f);
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/Player/Player.h src/Player/Player.cpp tests/test_player.cpp
git commit -m "feat: track which way the player is facing"
```

---

## Task 4: Player swing state machine

**Files:**
- Modify: `src/Player/Player.h`
- Modify: `src/Player/Player.cpp`
- Test: `tests/test_player.cpp`

**Interfaces:**
- Consumes: `ItemInfo::isSword`/`meleeDamage` (Task 1), `swordSwingSeconds`/`SWORD_SWING_DELAY` (Task 1), `Player::facing()` (Task 3, not directly used by the state machine itself but documented together).
- Produces: `ActionResult::meleeHit` (bool), `ActionResult::meleeDamage` (int); `Player::isSwinging() const -> bool`; `Player::swingProgress() const -> float` (0..1). Task 8 (Game's hit resolution and blade rendering) consumes all of these.

- [ ] **Step 1: Write the failing tests**

Add `#include "Items/Items.h"` to `tests/test_player.cpp`'s include block (it currently reaches `ItemType`/`itemInfo` only transitively through `Player.h`; every other test file in this codebase that uses them includes `Items.h` directly).

Append to `tests/test_player.cpp`:

```cpp
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

    int hitTicks = 0;
    int reportedDamage = 0;
    for (int i = 0; i < totalTicks; ++i)
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL to compile - `ActionResult::meleeHit`/`meleeDamage`, `Player::isSwinging()` are not declared.

- [ ] **Step 3: Add the new `ActionResult` fields and `Player` swing API in `Player.h`**

In `src/Player/Player.h`, add to `ActionResult` (after `damageTaken`):

```cpp
    // Actual health lost this tick (fall and/or lava), already clamped to
    // what the player had left. 0 most ticks. Game reads this to spawn a
    // floating damage popup.
    int damageTaken = 0;

    // True only on the single tick a sword swing's hit-resolution point is
    // crossed (the swing's midpoint - see Player::swing). meleeDamage is
    // only meaningful that same tick.
    bool meleeHit = false;
    int meleeDamage = 0;
};
```

Add to the public section (near `isMining()`/`miningProgress()`):

```cpp
    // True while a sword swing is in progress (not during its post-swing delay).
    bool isSwinging() const { return swingPhase == SwingPhase::Swinging; }

    // 0 to 1 through the current swing; 0 when not swinging.
    float swingProgress() const;
```

Add the private state machine (near `mining`/`target`/`progress`):

```cpp
    // A sword swing: Idle (nothing happening) -> Swinging (progress runs
    // 0..swingDuration) -> Delay (a fixed SWORD_SWING_DELAY) -> back to
    // Idle. Driven by the same "mine" input bool that drives mining -
    // whichever one applies depends on whether the held item isSword.
    enum class SwingPhase { Idle, Swinging, Delay };

    void swing(const PlayerInput& input, float dt, ActionResult& result);

    SwingPhase swingPhase = SwingPhase::Idle;
    float swingTimer = 0.0f;
    float swingDuration = 0.0f;
    int swingDamage = 0;
    bool swingHitDelivered = false;
```

- [ ] **Step 4: Implement `swing()` and `swingProgress()`, and branch `update()` between mining and swinging, in `Player.cpp`**

Add near the bottom of `Player.cpp` (after `mine()`, before `applyDamage()`):

```cpp
float Player::swingProgress() const
{
    if (swingPhase != SwingPhase::Swinging || swingDuration <= 0.0f)
        return 0.0f;

    return std::clamp(swingTimer / swingDuration, 0.0f, 1.0f);
}

void Player::swing(const PlayerInput& input, float dt, ActionResult& result)
{
    switch (swingPhase)
    {
        case SwingPhase::Idle:
            if (input.mine)
            {
                const ItemInfo& heldInfo = itemInfo(bag.slot(selected).type);
                swingDamage = heldInfo.meleeDamage;
                swingDuration = swordSwingSeconds(heldInfo.tier);
                swingTimer = 0.0f;
                swingHitDelivered = false;
                swingPhase = SwingPhase::Swinging;
            }
            break;

        case SwingPhase::Swinging:
        {
            const float before = swingTimer;
            swingTimer += dt;

            // One hit per swing, delivered exactly at the midpoint (the
            // blade's full-extension point in its 10deg-to-10deg arc) -
            // never re-triggering across multiple ticks.
            const float midpoint = swingDuration * 0.5f;
            if (!swingHitDelivered && before < midpoint && swingTimer >= midpoint)
            {
                result.meleeHit = true;
                result.meleeDamage = swingDamage;
                swingHitDelivered = true;
            }

            if (swingTimer >= swingDuration)
            {
                swingPhase = SwingPhase::Delay;
                swingTimer = 0.0f;
            }
            break;
        }

        case SwingPhase::Delay:
            swingTimer += dt;
            if (swingTimer >= SWORD_SWING_DELAY)
            {
                swingPhase = SwingPhase::Idle;
                swingTimer = 0.0f;
            }
            break;
    }
}
```

Replace `Player::update` with:

```cpp
ActionResult Player::update(const PlayerInput& input, World& world, float dt,
                             const Machines* machines)
{
    ActionResult result;

    damageTakenThisTick = 0;

    move(input, world, dt);
    applyLavaDamage(world, dt);

    const ItemInfo& heldInfo = itemInfo(bag.slot(selected).type);

    if (heldInfo.isSword)
    {
        // A sword can't mine - reset any mining state left over from a
        // previously held tool, so switching back to it later doesn't
        // resume stale progress on whatever tile the cursor happens to be on.
        mining = false;
        progress = 0.0f;

        swing(input, dt, result);
    }
    else
    {
        swingPhase = SwingPhase::Idle;
        swingTimer = 0.0f;

        mine(input, world, result, dt);
    }

    place(input, world, machines, result);

    result.damageTaken = damageTakenThisTick;

    return result;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `build/Debug/Litharia_tests.exe`
Expected: PASS, and every pre-existing mining test in `test_mining.cpp` still passes (they never select a sword, so `mine()` still runs exactly as before).

- [ ] **Step 6: Commit**

```bash
git add src/Player/Player.h src/Player/Player.cpp tests/test_player.cpp
git commit -m "feat: add the sword swing state machine to Player"
```

---

## Task 5: Enemy entity - stats, chase/jump AI, contact damage

**Files:**
- Create: `src/Enemies/Enemy.h`
- Create: `src/Enemies/Enemy.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_enemies.cpp` (new)

**Interfaces:**
- Consumes: `physics::moveAndCollide`, `AABB` (`src/Physics/Physics.h`), `World::isSolid`/`inBounds` (not called directly - collision is all through `moveAndCollide`), `TILE_SIZE` (`src/Core/Constants.h`).
- Produces: `EnemyType` enum (`Nightstalker`, `Sunroamer`); `EnemyInfo` struct + `enemyInfo(EnemyType)`; `Enemy` class with `update`, `type()`, `box()`/`position()`/`center()`/`velocity()`/`isGrounded()`, `health()`/`isDead()`/`applyDamage()`, `tickContactDamage()`. Task 6 (spawner) constructs `Enemy` instances; Task 7 (Game wiring) drives `update`/`tickContactDamage`/`applyDamage` and reads `type()`/`box()`/`center()` for rendering and hit resolution.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_enemies.cpp`:

```cpp
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
// `standing()` test helper - the player being far away (dx, dy both large
// negative) keeps it from being pulled off the floor while settling.
Enemy standing(EnemyType type, World& world, float tileX, float floorRow)
{
    Enemy enemy(type, {tileX * TILE_SIZE, (floorRow - 4.0f) * TILE_SIZE});

    const sf::Vector2f farAway{tileX * TILE_SIZE - 1000.0f, floorRow * TILE_SIZE - 1000.0f};
    for (int i = 0; i < 120; ++i)
        enemy.update(world, farAway, STEP);

    return enemy;
}

} // namespace

TEST_CASE("enemyInfo reports the documented stats for each type, Sunroamer weaker and slower")
{
    const EnemyInfo& night = enemyInfo(EnemyType::Nightstalker);
    CHECK(night.maxHealth == 40);
    CHECK(night.moveSpeed == doctest::Approx(190.0f));
    CHECK(night.jumpSpeed == doctest::Approx(470.0f));
    CHECK(night.contactDamage == 8);
    CHECK(night.contactInterval == doctest::Approx(0.6f));

    const EnemyInfo& day = enemyInfo(EnemyType::Sunroamer);
    CHECK(day.maxHealth == 20);
    CHECK(day.moveSpeed == doctest::Approx(140.0f));
    CHECK(day.contactDamage == 4);

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

    // Come back down while the player stays overhead the entire time.
    for (int i = 0; i < 200; ++i)
        enemy.update(world, playerAbove, STEP);

    // A re-triggering jump would never let it actually descend back to the
    // floor - it must land, and its apex must have been meaningfully above
    // the ground (a real jump happened, exactly once).
    CHECK(enemy.isGrounded());
    CHECK(enemy.position().y == doctest::Approx(groundY).epsilon(0.01));
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `Enemy.h` doesn't exist yet, and `test_enemies.cpp` isn't in the build (fix in the next steps).

- [ ] **Step 3: Create `src/Enemies/Enemy.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <SFML/System/Vector2.hpp>

#include "../Physics/Physics.h"

class World;

enum class EnemyType : std::uint8_t
{
    Nightstalker,
    Sunroamer,
};

struct EnemyInfo
{
    std::string_view name;
    float width;
    float height;
    int maxHealth;
    float moveSpeed;
    float jumpSpeed;
    int contactDamage;
    float contactInterval;
};

const EnemyInfo& enemyInfo(EnemyType type);

// A mob that always chases the single player: no pathfinding, just a
// direct vector toward them plus an auto-jump when grounded and either
// blocked sideways or the player is above. Falls through
// physics::moveAndCollide, the same function Player and ItemEntity use.
class Enemy
{
public:
    Enemy(EnemyType type, sf::Vector2f topLeft);

    void update(const World& world, sf::Vector2f playerCenter, float dt);

    EnemyType type() const { return kind; }
    const AABB& box() const { return body; }
    sf::Vector2f position() const { return body.position; }
    sf::Vector2f center() const { return body.center(); }
    sf::Vector2f velocity() const { return speed; }
    bool isGrounded() const { return grounded; }

    int health() const { return hp; }
    bool isDead() const { return hp <= 0; }
    void applyDamage(int amount);

    // Accumulates dt while touchingPlayer is true, resets to 0 the instant
    // it's false. Returns the total contact damage to apply this call (0,
    // one interval, or more if dt is unusually large) - Game feeds the
    // result into Player::takeDamage.
    int tickContactDamage(bool touchingPlayer, float dt);

private:
    EnemyType kind;
    AABB body;
    sf::Vector2f speed{0.0f, 0.0f};
    bool grounded = false;
    bool blockedHorizontally = false;
    int hp;
    float contactTimer = 0.0f;
};
```

- [ ] **Step 4: Create `src/Enemies/Enemy.cpp`**

```cpp
#include "Enemy.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include "../Core/Constants.h"
#include "../World/World.h"

namespace
{

// File-local, matching Player.cpp's own GRAVITY/TERMINAL_VELOCITY constants
// (no shared physics-constants header exists in this codebase yet).
constexpr float GRAVITY = 1800.0f;           // px/s^2
constexpr float TERMINAL_VELOCITY = 1100.0f; // px/s

constexpr std::array<EnemyInfo, 2> registry = {{
    {"Nightstalker", 28.0f, 42.0f, 40, 190.0f, 470.0f, 8, 0.6f},
    {"Sunroamer",     24.0f, 34.0f, 20, 140.0f, 420.0f, 4, 0.6f},
}};

} // namespace

const EnemyInfo& enemyInfo(EnemyType type)
{
    return registry[static_cast<std::size_t>(type)];
}

Enemy::Enemy(EnemyType type, sf::Vector2f topLeft)
    : kind(type)
    , body{topLeft, {enemyInfo(type).width, enemyInfo(type).height}}
    , hp(enemyInfo(type).maxHealth)
{
}

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

void Enemy::applyDamage(int amount)
{
    hp = std::max(0, hp - amount);
}

int Enemy::tickContactDamage(bool touchingPlayer, float dt)
{
    if (!touchingPlayer)
    {
        contactTimer = 0.0f;
        return 0;
    }

    contactTimer += dt;

    const EnemyInfo& info = enemyInfo(kind);
    int totalDamage = 0;

    while (contactTimer >= info.contactInterval)
    {
        totalDamage += info.contactDamage;
        contactTimer -= info.contactInterval;
    }

    return totalDamage;
}
```

- [ ] **Step 5: Wire the new files into `CMakeLists.txt`**

In `Litharia_core`'s source list, add `src/Enemies/Enemy.cpp` (alongside `src/Player/Player.cpp`):

```cmake
    src/Physics/Physics.cpp
    src/Player/Player.cpp

    src/Enemies/Enemy.cpp

    src/Items/Items.cpp
```

In `Litharia_tests`'s source list, add `tests/test_enemies.cpp` (alongside `tests/test_player.cpp`):

```cmake
    tests/test_player.cpp
    tests/test_enemies.cpp
    tests/test_spawn.cpp
```

- [ ] **Step 6: Re-run CMake and run tests to verify they pass**

Run: `cmake -S . -B build`
Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `build/Debug/Litharia_tests.exe`
Expected: PASS, all new `test_enemies.cpp` cases green.

- [ ] **Step 7: Commit**

```bash
git add src/Enemies/Enemy.h src/Enemies/Enemy.cpp CMakeLists.txt tests/test_enemies.cpp
git commit -m "feat: add the Enemy entity - stats, chase/jump AI, contact damage"
```

---

## Task 6: Enemy spawning - just outside the view, night/cave/day rules

**Files:**
- Create: `src/Enemies/EnemySpawner.h`
- Create: `src/Enemies/EnemySpawner.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_enemy_spawn.cpp` (new)

**Interfaces:**
- Consumes: `Enemy.h`'s `EnemyType` (Task 5); `TerrainGenerator::surfaceHeight`/`generateBase` (`src/World/TerrainGenerator.h`, pre-existing); `World::isSolid` (pre-existing); `noise::hashFloat` (`src/Core/Noise.h`, pre-existing).
- Produces: `ViewBounds` struct (`left, top, right, bottom`, world pixels); `EnemySpawn` struct (`type`, `position`); `attemptSpawn(world, generator, view, daylightFactor, currentEnemyCount, salt) -> std::optional<EnemySpawn>`; public constants `MAX_ENEMIES`, `SPAWN_MARGIN_TILES`, `SPAWN_VERTICAL_SEARCH_TILES`, `SUNROAMER_SPAWN_CHANCE`, `SPAWN_ATTEMPT_INTERVAL`, `NIGHT_THRESHOLD`. Task 7 (Game wiring) calls `attemptSpawn` on a timer and reuses `NIGHT_THRESHOLD` for the despawn check.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_enemy_spawn.cpp`:

```cpp
#include "doctest.h"

#include <cstdint>

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "Enemies/Enemy.h"
#include "Enemies/EnemySpawner.h"
#include "World/TerrainGenerator.h"
#include "World/World.h"

namespace
{

// Deep enough that TerrainGenerator::SURFACE_MAX (260) can never reach it
// for any column - guarantees "underground" no matter where the real
// surface rolls for that seed.
constexpr int DEEP_Y = 400;

// Shallow enough that TerrainGenerator::SURFACE_MIN (80) is always at least
// this far below it, and well within SPAWN_VERTICAL_SEARCH_TILES.
constexpr int SHALLOW_Y = 50;

ViewBounds viewCenteredOn(int tileY, float leftTileX = 500.0f, float rightTileX = 520.0f)
{
    return ViewBounds{leftTileX * TILE_SIZE,
                       static_cast<float>(tileY - 5) * TILE_SIZE,
                       rightTileX * TILE_SIZE,
                       static_cast<float>(tileY + 5) * TILE_SIZE};
}

// Carves an open-pocket foothold at (x, DEEP_Y) on both of attemptSpawn's
// two possible candidate columns (view.left - margin, view.right + margin) -
// the test doesn't need to know which edge the implementation's internal
// coin-flip lands on.
void carveDeepFootholdOnBothEdges(World& world, const ViewBounds& view)
{
    const int leftX = static_cast<int>(view.left / TILE_SIZE) - SPAWN_MARGIN_TILES;
    const int rightX = static_cast<int>(view.right / TILE_SIZE) + SPAWN_MARGIN_TILES;

    for (int x : {leftX, rightX})
    {
        world.set(x, DEEP_Y, BlockType::Air);
        world.set(x, DEEP_Y + 1, BlockType::Stone);
    }
}

} // namespace

TEST_CASE("an underground foothold always spawns a Nightstalker, day or night")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(DEEP_Y);
    carveDeepFootholdOnBothEdges(world, view);

    const auto nightResult = attemptSpawn(world, generator, view, 0.0f, 0, 1u);
    REQUIRE(nightResult.has_value());
    CHECK(nightResult->type == EnemyType::Nightstalker);

    const auto dayResult = attemptSpawn(world, generator, view, 1.0f, 0, 2u);
    REQUIRE(dayResult.has_value());
    CHECK(dayResult->type == EnemyType::Nightstalker);
}

TEST_CASE("an above-ground foothold at night spawns a Nightstalker")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(SHALLOW_Y);
    const auto result = attemptSpawn(world, generator, view, 0.0f, 0, 3u);

    REQUIRE(result.has_value());
    CHECK(result->type == EnemyType::Nightstalker);
}

TEST_CASE("an above-ground foothold by day never spawns a Nightstalker, and only rarely spawns a Sunroamer")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(SHALLOW_Y);

    int sunroamerCount = 0;
    for (std::uint32_t salt = 0; salt < 500; ++salt)
    {
        const auto result = attemptSpawn(world, generator, view, 1.0f, 0, salt);
        if (!result.has_value())
            continue;

        CHECK(result->type == EnemyType::Sunroamer);
        ++sunroamerCount;
    }

    // ~8% (SUNROAMER_SPAWN_CHANCE) of 500 is ~40; a wide band avoids
    // flakiness while still catching an "always" or "never" bug.
    CHECK(sunroamerCount > 10);
    CHECK(sunroamerCount < 150);
}

TEST_CASE("no foothold within the search range returns nullopt")
{
    World world; // default-constructed: every tile is Air, so no foothold ever exists.
    TerrainGenerator generator(2026u);

    const ViewBounds view = viewCenteredOn(SHALLOW_Y);

    CHECK_FALSE(attemptSpawn(world, generator, view, 0.0f, 0, 4u).has_value());
    CHECK_FALSE(attemptSpawn(world, generator, view, 1.0f, 0, 5u).has_value());
}

TEST_CASE("attemptSpawn never spawns at or above MAX_ENEMIES")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(DEEP_Y);
    carveDeepFootholdOnBothEdges(world, view);

    CHECK_FALSE(attemptSpawn(world, generator, view, 0.0f, MAX_ENEMIES, 6u).has_value());
    CHECK_FALSE(attemptSpawn(world, generator, view, 0.0f, MAX_ENEMIES + 5, 7u).has_value());
}

TEST_CASE("attemptSpawn is a pure function: identical inputs always give the identical result")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world);

    const ViewBounds view = viewCenteredOn(DEEP_Y);
    carveDeepFootholdOnBothEdges(world, view);

    const auto first = attemptSpawn(world, generator, view, 0.3f, 2, 42u);
    const auto second = attemptSpawn(world, generator, view, 0.3f, 2, 42u);

    REQUIRE(first.has_value() == second.has_value());
    if (first.has_value())
    {
        CHECK(first->type == second->type);
        CHECK(first->position == second->position);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `EnemySpawner.h` doesn't exist yet.

- [ ] **Step 3: Create `src/Enemies/EnemySpawner.h`**

```cpp
#pragma once

#include <cstdint>
#include <optional>

#include <SFML/System/Vector2.hpp>

#include "Enemy.h"

class World;
class TerrainGenerator;

// The camera's current visible rectangle, in world pixels - same convention
// as sf::View (top < bottom, left < right), but plain floats so this stays
// SFML-System-only and testable without a window.
struct ViewBounds
{
    float left;
    float top;
    float right;
    float bottom;
};

struct EnemySpawn
{
    EnemyType type;
    sf::Vector2f position;
};

inline constexpr int MAX_ENEMIES = 10;
inline constexpr int SPAWN_MARGIN_TILES = 3;
inline constexpr int SPAWN_VERTICAL_SEARCH_TILES = 40;
inline constexpr float SUNROAMER_SPAWN_CHANCE = 0.08f;
inline constexpr float SPAWN_ATTEMPT_INTERVAL = 3.0f; // seconds, Game's own spawn timer
inline constexpr float NIGHT_THRESHOLD = 0.5f;        // daylightFactor() below this counts as night

// Deterministic given `salt` (vary per call, e.g. an incrementing counter -
// the same convention TerrainGenerator::randomSurfaceSpot uses). Picks a
// point just outside `view`, searches for a foothold, and rolls the
// night/cave/day spawn rules from the design doc. Returns nullopt if no
// foothold was found or nothing rolled eligible this attempt.
std::optional<EnemySpawn> attemptSpawn(const World& world,
                                        const TerrainGenerator& generator,
                                        ViewBounds view,
                                        float daylightFactor,
                                        int currentEnemyCount,
                                        std::uint32_t salt);
```

- [ ] **Step 4: Create `src/Enemies/EnemySpawner.cpp`**

```cpp
#include "EnemySpawner.h"

#include <algorithm>

#include "../Core/Constants.h"
#include "../Core/Noise.h"
#include "../World/TerrainGenerator.h"
#include "../World/World.h"

std::optional<EnemySpawn> attemptSpawn(const World& world,
                                        const TerrainGenerator& generator,
                                        ViewBounds view,
                                        float daylightFactor,
                                        int currentEnemyCount,
                                        std::uint32_t salt)
{
    if (currentEnemyCount >= MAX_ENEMIES)
        return std::nullopt;

    // Pick the left or right edge of the view, offset further out by the
    // margin - "just outside the visible screen".
    const bool useRightEdge = noise::hashFloat(0, static_cast<int>(salt), salt) >= 0.5f;
    const float edgeX = useRightEdge ? view.right + SPAWN_MARGIN_TILES * TILE_SIZE
                                      : view.left - SPAWN_MARGIN_TILES * TILE_SIZE;
    const int x = std::clamp(static_cast<int>(edgeX / TILE_SIZE), 0, WORLD_WIDTH - 1);

    // Start the vertical search from the view's own vertical center - the
    // camera follows the player, so this naturally covers the cave case: if
    // the player is deep underground, "just outside their view" at the
    // view's own depth is still inside/near the same cave system.
    const int startY = static_cast<int>((view.top + view.bottom) * 0.5f / TILE_SIZE);

    int foundY = -1;
    for (int i = 0; i <= SPAWN_VERTICAL_SEARCH_TILES; ++i)
    {
        const int y = startY + i;
        if (y < 0 || y >= WORLD_HEIGHT - 1)
            continue;

        if (!world.isSolid(x, y) && world.isSolid(x, y + 1))
        {
            foundY = y;
            break;
        }
    }

    if (foundY < 0)
        return std::nullopt;

    const bool underground = foundY > generator.surfaceHeight(x);
    const sf::Vector2f position{static_cast<float>(x) * TILE_SIZE, static_cast<float>(foundY) * TILE_SIZE};

    if (underground)
        return EnemySpawn{EnemyType::Nightstalker, position};

    if (daylightFactor < NIGHT_THRESHOLD)
        return EnemySpawn{EnemyType::Nightstalker, position};

    // Daytime, above ground: Sunroamer, and only rarely.
    const float roll = noise::hashFloat(x, foundY, salt);
    if (roll < SUNROAMER_SPAWN_CHANCE)
        return EnemySpawn{EnemyType::Sunroamer, position};

    return std::nullopt;
}
```

- [ ] **Step 5: Wire the new files into `CMakeLists.txt`**

In `Litharia_core`'s source list, add `src/Enemies/EnemySpawner.cpp` next to `src/Enemies/Enemy.cpp`:

```cmake
    src/Enemies/Enemy.cpp
    src/Enemies/EnemySpawner.cpp
```

In `Litharia_tests`'s source list, add `tests/test_enemy_spawn.cpp` next to `tests/test_enemies.cpp`:

```cmake
    tests/test_enemies.cpp
    tests/test_enemy_spawn.cpp
    tests/test_spawn.cpp
```

- [ ] **Step 6: Re-run CMake and run tests to verify they pass**

Run: `cmake -S . -B build`
Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `build/Debug/Litharia_tests.exe`
Expected: PASS, all new `test_enemy_spawn.cpp` cases green.

- [ ] **Step 7: Commit**

```bash
git add src/Enemies/EnemySpawner.h src/Enemies/EnemySpawner.cpp CMakeLists.txt tests/test_enemy_spawn.cpp
git commit -m "feat: add the just-outside-view enemy spawn algorithm"
```

---

## Task 7: Game wiring - Player::takeDamage, enemy spawn/update/contact/despawn

**Files:**
- Modify: `src/Player/Player.h`
- Modify: `src/Player/Player.cpp`
- Modify: `src/Game/Game.h`
- Modify: `src/Game/Game.cpp`
- Test: `tests/test_player.cpp` (append, for `takeDamage` only)

**Interfaces:**
- Consumes: `Enemy`/`EnemyType`/`enemyInfo` (Task 5), `attemptSpawn`/`ViewBounds`/`MAX_ENEMIES`/`SPAWN_ATTEMPT_INTERVAL`/`NIGHT_THRESHOLD` (Task 6).
- Produces: `Player::takeDamage(int) `. Task 8 doesn't need this task's Game-side methods directly (it adds its own), but shares the same `enemies` vector this task introduces on `Game`.

- [ ] **Step 1: Write the failing test for `Player::takeDamage`**

Append to `tests/test_player.cpp`:

```cpp
TEST_CASE("takeDamage reduces health and clamps at 0, same as fall/lava damage")
{
    World world;
    buildFloor(world, 30);

    Player player = standing(world, 10.0f, 30.0f);
    REQUIRE(player.health() == Player::MAX_HEALTH);

    player.takeDamage(8);
    CHECK(player.health() == Player::MAX_HEALTH - 8);
    CHECK_FALSE(player.isDead());

    player.takeDamage(1000);
    CHECK(player.health() == 0);
    CHECK(player.isDead());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL to compile - `Player::takeDamage` is not declared.

- [ ] **Step 3: Add `takeDamage` to `Player`**

In `src/Player/Player.h`, add to the public section (near `respawn`):

```cpp
    // Restores full health at `topLeft`, velocity cleared. Called by Game on death.
    void respawn(sf::Vector2f topLeft);

    // Damage from a source Player doesn't compute itself - currently only
    // enemy contact damage, resolved in Game since Player doesn't know
    // about the enemy list. Routes through the same clamped applyDamage
    // fall/lava damage already uses.
    void takeDamage(int amount);
```

In `src/Player/Player.cpp`, add near `applyDamage`:

```cpp
void Player::takeDamage(int amount)
{
    applyDamage(amount);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 5: Commit the core-library change**

```bash
git add src/Player/Player.h src/Player/Player.cpp tests/test_player.cpp
git commit -m "feat: add Player::takeDamage, the mob-damage extension point"
```

- [ ] **Step 6: Add enemy state and update/spawn/despawn methods to `Game.h`**

In `src/Game/Game.h`, add the includes (alongside the other `World`/`Items` includes):

```cpp
#include "../Enemies/Enemy.h"
#include "../Enemies/EnemySpawner.h"
```

Add private method declarations (near `updateDrops`):

```cpp
    void updateDrops(float dt);

    void spawnEnemiesIfNeeded(float dt);
    void updateEnemies(float dt);
    void despawnEnemies();
```

Add member state (near `drops`):

```cpp
    std::vector<ItemEntity> drops;
    std::vector<Enemy> enemies;
    int enemySpawnCounter = 0;
    float enemySpawnTimer = 0.0f;
```

- [ ] **Step 7: Implement the new methods in `Game.cpp`**

Add near `Game::updateDrops` (which they parallel):

```cpp
void Game::spawnEnemiesIfNeeded(float dt)
{
    enemySpawnTimer += dt;
    if (enemySpawnTimer < SPAWN_ATTEMPT_INTERVAL)
        return;

    enemySpawnTimer = 0.0f;
    ++enemySpawnCounter;

    const sf::Vector2f viewSize = camera.view().getSize();
    const sf::Vector2f viewCenter = camera.center();
    const ViewBounds view{viewCenter.x - viewSize.x * 0.5f,
                           viewCenter.y - viewSize.y * 0.5f,
                           viewCenter.x + viewSize.x * 0.5f,
                           viewCenter.y + viewSize.y * 0.5f};

    const auto spawn = attemptSpawn(world, generator, view, dayNightClock.daylightFactor(),
                                     static_cast<int>(enemies.size()),
                                     static_cast<std::uint32_t>(enemySpawnCounter));

    if (spawn.has_value())
        enemies.emplace_back(spawn->type, spawn->position);
}

void Game::updateEnemies(float dt)
{
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

    // Dead enemies just vanish - no drops yet, see the design doc's Scope section.
    std::erase_if(enemies, [](const Enemy& e) { return e.isDead(); });

    despawnEnemies();
}

void Game::despawnEnemies()
{
    const bool isDay = dayNightClock.daylightFactor() >= NIGHT_THRESHOLD;
    if (!isDay)
        return;

    const sf::FloatRect viewRect(camera.center() - camera.view().getSize() * 0.5f,
                                  camera.view().getSize());

    std::erase_if(enemies, [&](const Enemy& e) {
        // Cave Nightstalkers are never "above ground", so this never
        // touches them regardless of the surface daylight clock. Sunroamers
        // never despawn from time-of-day at all - see the design doc.
        if (e.type() != EnemyType::Nightstalker)
            return false;

        const int tileX = static_cast<int>(e.center().x / TILE_SIZE);
        const bool aboveGround = e.position().y <= generator.surfaceHeight(tileX) * TILE_SIZE;
        if (!aboveGround)
            return false;

        const sf::FloatRect enemyRect(e.position(), e.box().size);
        const bool onScreen = viewRect.findIntersection(enemyRect).has_value();

        return !onScreen;
    });
}
```

- [ ] **Step 8: Call the new methods from `Game::fixedUpdate`**

In `src/Game/Game.cpp`, in `fixedUpdate`, add the spawn/update call next to the existing `updateDrops(dt);` line:

```cpp
    updateDrops(dt);
    spawnEnemiesIfNeeded(dt);
    updateEnemies(dt);
    updateDamagePopups(dt);
```

(Was just `updateDrops(dt); updateDamagePopups(dt);` - the two new calls are inserted between them.)

- [ ] **Step 9: Build**

Run: `cmake --build build --config Debug --target Litharia`
Expected: builds clean (this task's code is integration-only, not unit-tested - the compile itself is the check here, plus the manual playtest in Task 9).

- [ ] **Step 10: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: wire enemy spawning, chase AI, contact damage, and despawn into Game"
```

---

## Task 8: Game wiring - sword hit resolution and rendering

**Files:**
- Modify: `src/Game/Game.h`
- Modify: `src/Game/Game.cpp`

**Interfaces:**
- Consumes: `ActionResult::meleeHit`/`meleeDamage` (Task 4), `Player::facing()`/`isSwinging()`/`swingProgress()` (Tasks 3-4), `enemies` (Task 7), `Enemy::applyDamage`/`center()`/`type()` (Task 5).

- [ ] **Step 1: Add `resolveMeleeHit` to `Game.h`**

In `src/Game/Game.h`, add the private method declaration (near `spawnDrop`, since it plays the same "Player reported it, Game acts on it" role):

```cpp
    // A mined block becomes a stack on the ground.
    void spawnDrop(const ActionResult& result);

    // A sword's hit-frame reported through ActionResult::meleeHit damages
    // whatever enemies are in reach, on the side the player is facing.
    void resolveMeleeHit(const ActionResult& result);
```

- [ ] **Step 2: Implement `resolveMeleeHit` in `Game.cpp`**

Add near `Game::spawnDrop`:

```cpp
void Game::resolveMeleeHit(const ActionResult& result)
{
    constexpr float SWORD_REACH_TILES = 2.5f;
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

- [ ] **Step 3: Call it from `fixedUpdate`**

In `src/Game/Game.cpp`'s `fixedUpdate`, right after the existing damage-popup check:

```cpp
    if (result.damageTaken > 0)
        spawnDamagePopup(result.damageTaken);

    if (result.meleeHit)
        resolveMeleeHit(result);
```

- [ ] **Step 4: Render enemies and the swing blade in `Game::render`**

In `src/Game/Game.cpp`'s `render()`, add the enemy draw loop right after the item-drop loop and before the player's own `body` rectangle:

```cpp
    // Enemies.
    for (const Enemy& enemy : enemies)
    {
        const EnemyInfo& info = enemyInfo(enemy.type());

        sf::RectangleShape enemyShape({info.width, info.height});
        enemyShape.setPosition(enemy.position());
        enemyShape.setFillColor(enemy.type() == EnemyType::Nightstalker
                                     ? sf::Color(45, 35, 60)
                                     : sf::Color(225, 200, 120));
        enemyShape.setOutlineThickness(-2.0f);
        enemyShape.setOutlineColor(sf::Color(20, 15, 25));

        window.draw(enemyShape);
    }
```

Then, right after the existing `window.draw(body);` line that draws the player, add the swing blade:

```cpp
    window.draw(body);

    // The sword's swing arc, drawn only while actively swinging: sweeps
    // 10deg off vertical (top) to 10deg off vertical (bottom), passing
    // through horizontal (full extension, facing direction) at the
    // midpoint - see the enemies-and-melee-combat design.
    if (player.isSwinging())
    {
        constexpr float BLADE_LENGTH = 2.5f * TILE_SIZE;
        constexpr float BLADE_THICKNESS = 4.0f;

        const float armDeg = -80.0f + player.swingProgress() * 160.0f;
        const float renderDeg = player.facing() == Direction::Right ? armDeg : 180.0f - armDeg;

        sf::RectangleShape blade({BLADE_LENGTH, BLADE_THICKNESS});
        blade.setOrigin({0.0f, BLADE_THICKNESS * 0.5f});
        blade.setPosition(player.center());
        blade.setRotation(sf::degrees(renderDeg));
        blade.setFillColor(sf::Color(200, 200, 210));

        window.draw(blade);
    }
```

- [ ] **Step 5: Build**

Run: `cmake --build build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 6: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: resolve sword hits against enemies and render enemies/the swing"
```

---

## Task 9: Manual verification pass

**Files:** none (verification only).

- [ ] **Step 1: Full test suite, one last time**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `build/Debug/Litharia_tests.exe`
Expected: PASS, every test in the suite (not just this feature's new ones).

- [ ] **Step 2: Build the Release binary**

Run: `cmake --build build --config Release --target Litharia`
Expected: builds clean. (Manual play-testing must use this Release build - Debug is unplayable in this project, it hangs within seconds.)

- [ ] **Step 3: Launch and check enemy spawning/AI**

Run `build/Release/Litharia.exe`. Wait for night (or dig underground into a cave) and confirm:
- Nightstalkers appear just past the edge of the screen, not popping into view.
- They walk toward the player continuously, including across the whole visible area.
- They jump over a 1-tile obstacle placed in their path.
- Standing under an overhang/ceiling with a Nightstalker on the other side: it jumps repeatedly against the ceiling rather than routing around.
- Waiting through dawn while a Nightstalker is above ground and off-screen: it's gone when it comes back into view. One that's on-screen at dawn does not disappear while visible.
- During the day, above ground, a Sunroamer occasionally appears (much less often than Nightstalkers at night).

- [ ] **Step 4: Check contact damage**

Let an enemy touch the player and confirm health drops (with the existing floating damage-popup) roughly every 0.6s of continuous contact, and stops when the player steps away.

- [ ] **Step 5: Check sword crafting and swinging**

At a Crafting Table, craft a Wood Sword (2 Stick + 1 Oak Log) and equip it. Confirm:
- Holding left-click swings a visible blade sweeping from near-overhead down to near-straight-down, mirrored correctly when facing left vs right.
- The swing connects with a nearby enemy roughly at the midpoint of the swing, and its health drops.
- After a swing finishes, there's a brief pause (~0.5s) before the next one starts even if the mouse is still held.
- Craft and equip an Obsidian Sword and confirm its swing is visibly faster and hits harder.

- [ ] **Step 6: Report results to the user**

Summarize what was verified and flag anything that looked wrong before considering this feature done.
