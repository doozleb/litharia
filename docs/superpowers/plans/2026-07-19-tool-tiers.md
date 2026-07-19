# Tool Tiers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a five-tier progression (Wood, Stone, Copper, Iron, Obsidian) for the Pickaxe and Axe, gate what a pickaxe can mine behind its tier, give every tier a mining-speed bonus, give axes a tier-scaled log-yield bonus, and spawn a renewable "Sharp Rock" resource on the surface to bootstrap the Stone tier.

**Architecture:** One tier at a time, oldest to newest (Wood foundation, then Stone, Copper, Iron, Obsidian), each task adding that tier's item(s), recipe(s), and (where the design calls for it) bumping the next block's `requiredTier` gate. Two follow-up tasks add cross-cutting mechanics once every tier exists: the axe log-yield bonus, and the Sharp Rock world-spawn/respawn system. A tier's own `BlockInfo::requiredTier` bump lands in the task that makes that ore newly reachable (Stone unlocks Copper in Task 1 since "Wood can't mine Copper" is itself a Wood-tier property; Copper unlocks Iron in Task 3; Iron unlocks Obsidian in Task 4) - this avoids ever leaving an ore hand-unminable by every existing tool mid-plan.

**Tech Stack:** C++20, SFML 3 (System only for `Litharia_core`; Graphics/Window for the `Litharia` game exe), CMake + Visual Studio generator, doctest (vendored single header).

## Global Constraints

- **Build (tests):** `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:\litharia\build --config Debug --target Litharia_tests`. `cmake` is not on PATH; use the full path shown.
- **Build (game):** same command with `--target Litharia`.
- **Test binary:** `C:\litharia\build\Debug\Litharia_tests.exe`.
- **Baseline before this work:** 247 test cases, 247 passed, 0 failed, at commit `e216d5e`.
- **Simulation/rendering split:** `Blocks.cpp`, `Items.cpp`, `Player.cpp`, `Recipes.cpp`, `TerrainGenerator.cpp` compile into `Litharia_core`, reachable from `Litharia_tests`. `Hud.cpp`, `Game.cpp`/`Game.h` compile only into the `Litharia` executable and are **not** linked into the test binary - changes there are build-verified, not doctest-verified, matching every prior plan in this repo.
- **`ToolTier` ordering is the single source of tier comparisons:** gating and the log-yield bonus both read the enum's underlying value via `meetsTier`/a direct cast - never hand-write a second tier-ordering table.
- **`TOOL_TIER_SPEED_MULTIPLIER` is the single source of mining-speed differences between tiers.** Do not hardcode a per-block, per-tier timing literal anywhere else.
- **Recipe shape:** every tool recipe costs Sticks plus that tier's own raw material only - never a mixed-tier ingredient (e.g. never Copper Plate in an Iron tool's recipe).
- **This plan does not add a way to obtain Obsidian blocks in the world.** That is a separate, later liquid/lava-water reaction spec. Obsidian is added here purely as a minable block/item/tool tier so the recipes and gating are wired up ahead of it.
- **Commit after every task** with a `feat:` prefixed message.
- **Spec:** `docs/superpowers/specs/2026-07-19-tool-tiers-design.md`.

---

## File Structure

**Modified across most tasks:**
- `src/Blocks/Blocks.h` / `src/Blocks/Blocks.cpp` - `ToolTier` enum, `meetsTier`, `TOOL_TIER_SPEED_MULTIPLIER`/`toolTierSpeedMultiplier` (Task 1 only); `BlockInfo::requiredTier` field (Task 1); `BlockType::Obsidian` (Task 4).
- `src/Items/Items.h` / `src/Items/Items.cpp` - `ItemInfo::tier` field (Task 1); `ItemType::Pickaxe`/`Axe` renamed to `WoodPickaxe`/`WoodAxe` (Task 1); `Stick`, `SharpRock`, `StonePickaxe`, `StoneAxe` (Task 2); `CopperPickaxe`, `CopperAxe` (Task 3); `Obsidian` (material item), `IronPickaxe`, `IronAxe` (Task 4); `ObsidianPickaxe`, `ObsidianAxe` (Task 5); `itemForBlock()` gains an `Obsidian` case (Task 4).
- `src/Player/Player.h` / `src/Player/Player.cpp` - tier-aware gating and speed in `mine()`, renamed starting-inventory items (Task 1); axe log-yield bonus (Task 6).
- `src/Machines/Recipes.h` / `src/Machines/Recipes.cpp` - `CraftRecipe::outputCount` field (Task 2); one new recipe per new tool/material item (Tasks 2-5).
- `src/Hud/Hud.cpp` - craft label shows `x{count}` when `outputCount > 1` (Task 2 only; build-verified, not test-verified).
- `src/World/TerrainGenerator.h` / `src/World/TerrainGenerator.cpp` - `scatterSharpRocks`/`randomSurfaceSpot` (Task 7 only).
- `src/Game/Game.h` / `src/Game/Game.cpp` - Sharp Rock materialization at world load and the runtime respawn timer (Task 7 only).
- `tests/test_mining.cpp` - the bulk of the new tests: renames, gating, speed, log-yield bonus.
- `tests/test_recipes.cpp` - one new `TEST_CASE` per new recipe, recipe-count bumps.
- `tests/test_terrain.cpp` - Sharp Rock spawn tests (Task 7 only).

**No new files.**

---

## Canonical Interfaces (defined once, referenced by later tasks)

```cpp
// src/Blocks/Blocks.h (Task 1)
enum class ToolTier : std::uint8_t
{
    Wood,
    Stone,
    Copper,
    Iron,
    Obsidian,
};

// True if a tool of `held` tier can mine a block that requires `required`.
inline bool meetsTier(ToolTier held, ToolTier required)
{
    return static_cast<std::uint8_t>(held) >= static_cast<std::uint8_t>(required);
}

// Indexed by ToolTier. Multiplies mining speed - Wood is nerfed below 1x,
// every tier from Copper up is a bonus above Stone's baseline 1x.
inline constexpr std::array<float, 5> TOOL_TIER_SPEED_MULTIPLIER = {
    0.6f,  // Wood
    1.0f,  // Stone
    1.25f, // Copper
    1.5f,  // Iron
    1.75f, // Obsidian
};

inline float toolTierSpeedMultiplier(ToolTier tier)
{
    return TOOL_TIER_SPEED_MULTIPLIER[static_cast<std::size_t>(tier)];
}
```

```cpp
// src/Blocks/Blocks.h, BlockInfo struct (Task 1 appends the field so every
// existing 6-value row keeps compiling unchanged and defaults to Wood, i.e.
// "any tool of the right kind works" - today's behavior, preserved)
struct BlockInfo
{
    std::string_view name;
    BlockColor color;
    bool solid;
    float hardness;
    BlockType drop;
    ToolType requiredTool;
    ToolTier requiredTier = ToolTier::Wood; // meaningful only when requiredTool != ToolType::None
};
```

```cpp
// src/Items/Items.h, ItemInfo struct (Task 1 appends the field so every
// existing 5-value row keeps compiling unchanged and defaults to Wood -
// harmless for every non-tool item, since their toolType is already None)
struct ItemInfo
{
    std::string_view name;
    int maxStack;
    BlockType placeBlock;
    ToolType toolType;
    BlockColor iconColor;
    ToolTier tier = ToolTier::Wood; // meaningful only when toolType != ToolType::None
};
```

```cpp
// src/Machines/Recipes.h, CraftRecipe struct (Task 2 appends the field so
// every existing 4-value row keeps compiling unchanged and defaults to 1 -
// today's implicit behavior for every recipe but the new Stick recipe)
struct CraftRecipe
{
    ItemType output;
    std::array<CraftIngredient, 2> ingredients;
    float seconds;
    bool requiresCraftingTable;
    int outputCount = 1;
};
```

Colors for each new item/block are hand-picked to be a real, non-black, tier-appropriate swatch (checked generically by the existing "every item has a real icon color" test) - exact values are given in each task, not left to the implementer.

---

## Task 1: `ToolTier` foundation, Wood tier rename, gating + speed mechanism

**Files:**
- Modify: `src/Blocks/Blocks.h`, `src/Blocks/Blocks.cpp`
- Modify: `src/Items/Items.h:11-24`, `src/Items/Items.cpp:9-35`
- Modify: `src/Player/Player.cpp:83-89`, `src/Player/Player.cpp:180-229`
- Test: `tests/test_mining.cpp`

**Interfaces:**
- Consumes: nothing new from other tasks (this is the plan's first task).
- Produces: `ToolTier`, `meetsTier`, `TOOL_TIER_SPEED_MULTIPLIER`, `toolTierSpeedMultiplier`, `BlockInfo::requiredTier`, `ItemInfo::tier`, `ItemType::WoodPickaxe`, `ItemType::WoodAxe`. Every later task reuses all of these.

- [ ] **Step 1: Write the failing tests**

Replace the existing `TEST_CASE("pickaxe, axe, and oak log are correctly typed items")` in `tests/test_mining.cpp` with:

```cpp
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
```

Replace the existing `TEST_CASE("the player spawns already holding a pickaxe and an axe")` with:

```cpp
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
```

Replace the existing `TEST_CASE("holding mine breaks a block after its hardness, and it drops itself")` with (the only change is `hardness` becoming `expectedTime`, since Wood now mines at 0.6x speed rather than 1x):

```cpp
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
```

Append two new tests to `tests/test_mining.cpp`:

```cpp
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command from Global Constraints.
Expected: **compile error** - `'WoodPickaxe': is not a member of 'ItemType'` (and similarly for `WoodAxe`, `ToolTier`, `meetsTier`, `toolTierSpeedMultiplier`, `requiredTier`, `tier`).

- [ ] **Step 3: Add `ToolTier`, `meetsTier`, and the speed-multiplier table to `Blocks.h`**

In `src/Blocks/Blocks.h`, add `#include <array>` and `#include <cstddef>` alongside the existing includes:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
```

Immediately after the `ToolType` enum (right before `enum class BlockType`), add:

```cpp
// How advanced a tool is, independent of what kind it is (Pickaxe vs Axe).
// Ordered: a higher tier can always do everything a lower tier can.
enum class ToolTier : std::uint8_t
{
    Wood,
    Stone,
    Copper,
    Iron,
    Obsidian,
};

// True if a tool of `held` tier can mine a block that requires `required`.
inline bool meetsTier(ToolTier held, ToolTier required)
{
    return static_cast<std::uint8_t>(held) >= static_cast<std::uint8_t>(required);
}

// Indexed by ToolTier. Multiplies mining speed - Wood is nerfed below 1x,
// every tier from Copper up is a bonus above Stone's baseline 1x.
inline constexpr std::array<float, 5> TOOL_TIER_SPEED_MULTIPLIER = {
    0.6f,  // Wood
    1.0f,  // Stone
    1.25f, // Copper
    1.5f,  // Iron
    1.75f, // Obsidian
};

inline float toolTierSpeedMultiplier(ToolTier tier)
{
    return TOOL_TIER_SPEED_MULTIPLIER[static_cast<std::size_t>(tier)];
}
```

Add the new field to `BlockInfo` (append as the last member):

```cpp
struct BlockInfo
{
    std::string_view name;
    BlockColor color;
    bool solid;
    float hardness; // seconds of mining to break

    // What the block leaves behind when mined, named as a block. Items maps this
    // to an ItemType; keeping it a BlockType is what lets Items depend on Blocks
    // and not the other way round.
    BlockType drop;

    // What tool is needed to mine this block at all. A mismatched (or empty)
    // hand makes the block unbreakable, not just slower.
    ToolType requiredTool;

    // The minimum tier of that tool. Meaningless when requiredTool is None.
    // Defaults to Wood - "any tool of the right kind works" - so every
    // existing row keeps compiling unchanged.
    ToolTier requiredTier = ToolTier::Wood;
};
```

- [ ] **Step 4: Bump Copper Ore's `requiredTier` in `Blocks.cpp`**

In `src/Blocks/Blocks.cpp`, replace the `"Copper Ore"` row with:

```cpp
    {"Copper Ore",  {201, 116,  56}, true,  1.40f, BlockType::CopperOre, ToolType::Pickaxe, ToolTier::Stone},
```

Every other row is untouched - each still ends after `ToolType::...`, so `requiredTier` defaults to `ToolTier::Wood` for all of them, preserving today's behavior exactly.

- [ ] **Step 5: Add `tier` to `ItemInfo` and rename `Pickaxe`/`Axe` in `Items.h`**

In `src/Items/Items.h`, append the new field to `ItemInfo`:

```cpp
struct ItemInfo
{
    std::string_view name;
    int maxStack;

    // The block this item places, or Air if it is not placeable.
    BlockType placeBlock;

    // None for everything except tools.
    ToolType toolType;

    // What this item looks like in the hotbar/bag and on the ground. Independent
    // of placeBlock: a non-placeable item (a plate, a tool, a log) still needs a
    // color of its own to render as anything but a black square.
    BlockColor iconColor;

    // How advanced a tool item is. Meaningless when toolType is None.
    ToolTier tier = ToolTier::Wood;
};
```

Replace `Pickaxe,` and `Axe,` in the `ItemType` enum with:

```cpp
    WoodPickaxe,
    WoodAxe,
```

- [ ] **Step 6: Rename the registry rows in `Items.cpp`**

In `src/Items/Items.cpp`, replace:

```cpp
    {"Pickaxe",           1,  BlockType::Air,       ToolType::Pickaxe, {170, 170, 180}},
    {"Axe",               1,  BlockType::Air,       ToolType::Axe,     {160,  90,  50}},
```

with:

```cpp
    {"Wood Pickaxe",      1,  BlockType::Air,       ToolType::Pickaxe, {170, 170, 180}, ToolTier::Wood},
    {"Wood Axe",          1,  BlockType::Air,       ToolType::Axe,     {160,  90,  50}, ToolTier::Wood},
```

- [ ] **Step 7: Rename the starting-inventory items in `Player.cpp`**

In `src/Player/Player.cpp`, in the `Player::Player` constructor, replace:

```cpp
    bag.exchange(0, {ItemType::Pickaxe, 1});
    bag.exchange(1, {ItemType::Axe, 1});
```

with:

```cpp
    bag.exchange(0, {ItemType::WoodPickaxe, 1});
    bag.exchange(1, {ItemType::WoodAxe, 1});
```

- [ ] **Step 8: Apply tier gating and tier speed in `Player::mine()`**

In `src/Player/Player.cpp`, replace:

```cpp
    const ToolType heldTool = itemInfo(bag.slot(selected).type).toolType;
    const bool wrongTool = block != BlockType::Air && blockInfo(block).requiredTool != heldTool;

    // Not holding the button, nothing solid under the cursor, out of arm's
    // reach, or the wrong tool (including no tool at all) in hand: no
    // progress, and any progress already made is thrown away. A block cannot
    // be chipped away by hand or the wrong tool - it simply does not break.
    if (!input.mine || block == BlockType::Air || !inReach(tileX, tileY) || wrongTool)
    {
        mining = false;
        progress = 0.0f;
        return;
    }
```

with:

```cpp
    const ItemInfo& heldInfo = itemInfo(bag.slot(selected).type);
    const BlockInfo& targetInfo = blockInfo(block);

    const bool wrongKind = block != BlockType::Air && targetInfo.requiredTool != heldInfo.toolType;
    const bool tooLowTier = heldInfo.toolType == ToolType::Pickaxe &&
                             !meetsTier(heldInfo.tier, targetInfo.requiredTier);
    const bool wrongTool = wrongKind || tooLowTier;

    // Not holding the button, nothing solid under the cursor, out of arm's
    // reach, the wrong kind of tool, or a pickaxe below the block's required
    // tier: no progress, and any progress already made is thrown away. A
    // block cannot be chipped away by hand, the wrong tool, or an
    // underpowered one - it simply does not break. Axes never gate on tier:
    // every axe tier can chop any tree, tier only changes speed (and, later,
    // log yield).
    if (!input.mine || block == BlockType::Air || !inReach(tileX, tileY) || wrongTool)
    {
        mining = false;
        progress = 0.0f;
        return;
    }
```

Then replace:

```cpp
    targetHardness = blockInfo(block).hardness;
```

with:

```cpp
    targetHardness = targetInfo.hardness / toolTierSpeedMultiplier(heldInfo.tier);
```

- [ ] **Step 9: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all test cases pass, count now 247 + 3 new = 250 (2 `TEST_CASE`s were replaced in place, not added; 3 were appended: the Copper Ore tier test, the `meetsTier` test, and the wood-pickaxe-cannot-mine-copper test).

- [ ] **Step 10: Build the game executable too**

Run: the Build (game) command from Global Constraints.
Expected: builds clean.

- [ ] **Step 11: Commit**

```bash
git add src/Blocks/Blocks.h src/Blocks/Blocks.cpp src/Items/Items.h src/Items/Items.cpp \
        src/Player/Player.cpp tests/test_mining.cpp
git commit -m "feat: add ToolTier foundation, rename Pickaxe/Axe to Wood tier"
```

---

## Task 2: Sticks, Sharp Rocks, and the Stone tier

**Files:**
- Modify: `src/Items/Items.h:11-24`, `src/Items/Items.cpp:9-35`
- Modify: `src/Machines/Recipes.h:38-44`, `src/Machines/Recipes.cpp:13-27`
- Modify: `src/Hud/Hud.cpp` (build-verified only; see Global Constraints)
- Modify: `src/Game/Game.cpp:581-601` (`updateCrafting`)
- Test: `tests/test_mining.cpp`, `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: `ToolTier`, `ItemInfo::tier` (Task 1).
- Produces: `ItemType::Stick`, `ItemType::SharpRock`, `ItemType::StonePickaxe`, `ItemType::StoneAxe`, `CraftRecipe::outputCount`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_mining.cpp`:

```cpp
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
```

Append to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("the Stick recipe costs 1 oak log and yields 4 sticks")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::Stick; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::OakLog);
    CHECK(it->ingredients[0].count == 1);
    CHECK(it->outputCount == 4);
}

TEST_CASE("every recipe outputs a positive count, 1 by default")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();

    for (const CraftRecipe& r : all)
        CHECK(r.outputCount > 0);
}

TEST_CASE("the Stone Pickaxe recipe costs 2 sticks and 2 sharp rocks")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::StonePickaxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::SharpRock);
    CHECK(it->ingredients[1].count == 2);
}

TEST_CASE("the Stone Axe recipe costs 2 sticks and 1 sharp rock")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::StoneAxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::SharpRock);
    CHECK(it->ingredients[1].count == 1);
}
```

Bump the recipe count in `tests/test_recipes.cpp`: `CHECK(all.size() == 13);` becomes `CHECK(all.size() == 16);`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** - `'Stick': is not a member of 'ItemType'` (and similarly for `SharpRock`, `StonePickaxe`, `StoneAxe`, `outputCount`).

- [ ] **Step 3: Add the new `ItemType` entries**

In `src/Items/Items.h`, replace:

```cpp
    WoodPickaxe,
    WoodAxe,
    OakLog,
```

with:

```cpp
    WoodPickaxe,
    WoodAxe,
    StonePickaxe,
    StoneAxe,
    OakLog,
    Stick,
    SharpRock,
```

- [ ] **Step 4: Add the new registry rows**

In `src/Items/Items.cpp`, replace:

```cpp
    {"Wood Pickaxe",      1,  BlockType::Air,       ToolType::Pickaxe, {170, 170, 180}, ToolTier::Wood},
    {"Wood Axe",          1,  BlockType::Air,       ToolType::Axe,     {160,  90,  50}, ToolTier::Wood},
    {"Oak Log",          99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
```

with:

```cpp
    {"Wood Pickaxe",      1,  BlockType::Air,       ToolType::Pickaxe, {170, 170, 180}, ToolTier::Wood},
    {"Wood Axe",          1,  BlockType::Air,       ToolType::Axe,     {160,  90,  50}, ToolTier::Wood},
    {"Stone Pickaxe",     1,  BlockType::Air,       ToolType::Pickaxe, {130, 130, 135}, ToolTier::Stone},
    {"Stone Axe",         1,  BlockType::Air,       ToolType::Axe,     {120, 100,  80}, ToolTier::Stone},
    {"Oak Log",          99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
    {"Stick",             99,  BlockType::Air,       ToolType::None,    {170, 140,  90}},
    {"Sharp Rock",        99,  BlockType::Air,       ToolType::None,    {150, 145, 140}},
```

- [ ] **Step 5: Add `CraftRecipe::outputCount`**

In `src/Machines/Recipes.h`, append the field to `CraftRecipe`:

```cpp
// One hand-craft recipe: up to 2 ingredients from the bag become
// outputCount of the output item after `seconds`. requiresCraftingTable is
// false only for the Crafting Table itself - the one recipe reachable with
// no table placed yet.
struct CraftRecipe
{
    ItemType output;
    std::array<CraftIngredient, 2> ingredients;
    float seconds;
    bool requiresCraftingTable;
    int outputCount = 1;
};
```

- [ ] **Step 6: Add the new recipes**

In `src/Machines/Recipes.cpp`, replace:

```cpp
constexpr std::array<CraftRecipe, 13> craftRecipes = {{
    {ItemType::CraftingTable,   {{{ItemType::OakLog, 15}, {}}},                          3.0f, false},
```

with:

```cpp
constexpr std::array<CraftRecipe, 16> craftRecipes = {{
    {ItemType::Stick,           {{{ItemType::OakLog, 1}, {}}},                           1.0f, true, 4},
    {ItemType::StonePickaxe,    {{{ItemType::Stick, 2}, {ItemType::SharpRock, 2}}},       2.0f, true},
    {ItemType::StoneAxe,        {{{ItemType::Stick, 2}, {ItemType::SharpRock, 1}}},       2.0f, true},
    {ItemType::CraftingTable,   {{{ItemType::OakLog, 15}, {}}},                          3.0f, false},
```

- [ ] **Step 7: Show output count on the craft panel label**

In `src/Hud/Hud.cpp`'s `buildRecipeLabels()`, replace:

```cpp
    for (const CraftRecipe& recipe : allCraftRecipes())
        craftLabels.push_back(
            {sf::Text(*font, std::string(itemInfo(recipe.output).name), LABEL_TITLE_SIZE),
             sf::Text(*font, formatIngredientCost(recipe), LABEL_SUBTITLE_SIZE)});
```

with:

```cpp
    for (const CraftRecipe& recipe : allCraftRecipes())
    {
        std::string title(itemInfo(recipe.output).name);
        if (recipe.outputCount > 1)
            title += " x" + std::to_string(recipe.outputCount);

        craftLabels.push_back({sf::Text(*font, title, LABEL_TITLE_SIZE),
                                sf::Text(*font, formatIngredientCost(recipe), LABEL_SUBTITLE_SIZE)});
    }
```

- [ ] **Step 8: Grant `outputCount` instead of a hardcoded 1**

In `src/Game/Game.cpp`'s `updateCrafting()`, replace:

```cpp
    Inventory& bag = player.inventory();
    const int leftover = bag.add({recipe.output, 1});
```

with:

```cpp
    Inventory& bag = player.inventory();
    const int leftover = bag.add({recipe.output, recipe.outputCount});
```

- [ ] **Step 9: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 250 + 6 new = 256 (2 item-typing/mining tests in `test_mining.cpp`, 4 recipe tests in `test_recipes.cpp`).

- [ ] **Step 10: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 11: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/Recipes.h src/Machines/Recipes.cpp \
        src/Hud/Hud.cpp src/Game/Game.cpp tests/test_mining.cpp tests/test_recipes.cpp
git commit -m "feat: add sticks, sharp rocks, and the Stone tool tier"
```

---

## Task 3: Copper tier

**Files:**
- Modify: `src/Items/Items.h:11-27`, `src/Items/Items.cpp:9-38`
- Modify: `src/Blocks/Blocks.cpp` (Iron Ore row)
- Modify: `src/Machines/Recipes.cpp`
- Test: `tests/test_mining.cpp`, `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: `ToolTier`, `toolTierSpeedMultiplier`, `CraftRecipe::outputCount` (Tasks 1-2).
- Produces: `ItemType::CopperPickaxe`, `ItemType::CopperAxe`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_mining.cpp`:

```cpp
TEST_CASE("the copper tools are correctly typed items")
{
    CHECK(itemInfo(ItemType::CopperPickaxe).toolType == ToolType::Pickaxe);
    CHECK(itemInfo(ItemType::CopperPickaxe).tier == ToolTier::Copper);

    CHECK(itemInfo(ItemType::CopperAxe).toolType == ToolType::Axe);
    CHECK(itemInfo(ItemType::CopperAxe).tier == ToolTier::Copper);
}

TEST_CASE("iron ore now requires at least Copper tier")
{
    CHECK(blockInfo(BlockType::IronOre).requiredTier == ToolTier::Copper);
}

TEST_CASE("a stone pickaxe cannot mine iron ore, but a copper pickaxe can")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::IronOre);
    world.set(13, 29, BlockType::IronOre);

    Player player = standingAt(world, 10.0f, 30);

    player.inventory().exchange(2, {ItemType::StonePickaxe, 1});
    player.setSelectedSlot(2);

    PlayerInput stoneInput;
    stoneInput.mine = true;
    stoneInput.cursor = cursorOn(12, 29);

    for (int i = 0; i < 300; ++i)
    {
        const ActionResult result = player.update(stoneInput, world, STEP);
        REQUIRE_FALSE(result.broke);
    }
    CHECK(world.get(12, 29) == BlockType::IronOre);

    player.inventory().exchange(3, {ItemType::CopperPickaxe, 1});
    player.setSelectedSlot(3);

    PlayerInput copperInput;
    copperInput.mine = true;
    copperInput.cursor = cursorOn(13, 29);

    ActionResult result;
    for (int i = 0; i < 300 && !result.broke; ++i)
        result = player.update(copperInput, world, STEP);

    REQUIRE(result.broke);
    CHECK(world.get(13, 29) == BlockType::Air);
}
```

Append to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("the Copper Pickaxe recipe costs 2 sticks and 4 copper plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperPickaxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::CopperPlate);
    CHECK(it->ingredients[1].count == 4);
}

TEST_CASE("the Copper Axe recipe costs 2 sticks and 2 copper plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperAxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::CopperPlate);
    CHECK(it->ingredients[1].count == 2);
}
```

Bump the recipe count: `CHECK(all.size() == 16);` becomes `CHECK(all.size() == 18);`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** - `'CopperPickaxe': is not a member of 'ItemType'` (and similarly for `CopperAxe`).

- [ ] **Step 3: Add the new `ItemType` entries**

In `src/Items/Items.h`, replace:

```cpp
    StonePickaxe,
    StoneAxe,
    OakLog,
```

with:

```cpp
    StonePickaxe,
    StoneAxe,
    CopperPickaxe,
    CopperAxe,
    OakLog,
```

- [ ] **Step 4: Add the new registry rows**

In `src/Items/Items.cpp`, replace:

```cpp
    {"Stone Pickaxe",     1,  BlockType::Air,       ToolType::Pickaxe, {130, 130, 135}, ToolTier::Stone},
    {"Stone Axe",         1,  BlockType::Air,       ToolType::Axe,     {120, 100,  80}, ToolTier::Stone},
    {"Oak Log",          99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
```

with:

```cpp
    {"Stone Pickaxe",     1,  BlockType::Air,       ToolType::Pickaxe, {130, 130, 135}, ToolTier::Stone},
    {"Stone Axe",         1,  BlockType::Air,       ToolType::Axe,     {120, 100,  80}, ToolTier::Stone},
    {"Copper Pickaxe",    1,  BlockType::Air,       ToolType::Pickaxe, {195, 120,  70}, ToolTier::Copper},
    {"Copper Axe",        1,  BlockType::Air,       ToolType::Axe,     {190, 115,  65}, ToolTier::Copper},
    {"Oak Log",          99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
```

- [ ] **Step 5: Bump Iron Ore's `requiredTier`**

In `src/Blocks/Blocks.cpp`, replace the `"Iron Ore"` row:

```cpp
    {"Iron Ore",    {166, 174, 190}, true,  2.00f, BlockType::IronOre,   ToolType::Pickaxe},
```

with:

```cpp
    {"Iron Ore",    {166, 174, 190}, true,  2.00f, BlockType::IronOre,   ToolType::Pickaxe, ToolTier::Copper},
```

- [ ] **Step 6: Add the new recipes**

In `src/Machines/Recipes.cpp`, replace `constexpr std::array<CraftRecipe, 16> craftRecipes` with `constexpr std::array<CraftRecipe, 18> craftRecipes`, and add these two rows directly after the `StoneAxe` row:

```cpp
    {ItemType::CopperPickaxe,   {{{ItemType::Stick, 2}, {ItemType::CopperPlate, 4}}},   2.0f, true},
    {ItemType::CopperAxe,       {{{ItemType::Stick, 2}, {ItemType::CopperPlate, 2}}},   2.0f, true},
```

- [ ] **Step 7: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 256 + 5 new = 261.

- [ ] **Step 8: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 9: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Blocks/Blocks.cpp src/Machines/Recipes.cpp \
        tests/test_mining.cpp tests/test_recipes.cpp
git commit -m "feat: add the Copper tool tier"
```

---

## Task 4: Obsidian block, and the Iron tier

**Files:**
- Modify: `src/Blocks/Blocks.h`, `src/Blocks/Blocks.cpp`
- Modify: `src/Items/Items.h`, `src/Items/Items.cpp`
- Modify: `src/Machines/Recipes.cpp`
- Test: `tests/test_mining.cpp`, `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: `ToolTier`, `toolTierSpeedMultiplier` (Task 1).
- Produces: `BlockType::Obsidian`, `ItemType::Obsidian` (material), `ItemType::IronPickaxe`, `ItemType::IronAxe`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_mining.cpp`:

```cpp
TEST_CASE("obsidian is a real, high-hardness, Iron-tier-gated block")
{
    CHECK(blockInfo(BlockType::Obsidian).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::Obsidian).requiredTier == ToolTier::Iron);
    CHECK(blockInfo(BlockType::Obsidian).solid);
    CHECK(blockInfo(BlockType::Obsidian).drop == BlockType::Obsidian);
    CHECK(blockInfo(BlockType::Obsidian).hardness > blockInfo(BlockType::IronOre).hardness);

    CHECK(itemForBlock(BlockType::Obsidian) == ItemType::Obsidian);
}

TEST_CASE("the iron tools are correctly typed items")
{
    CHECK(itemInfo(ItemType::IronPickaxe).toolType == ToolType::Pickaxe);
    CHECK(itemInfo(ItemType::IronPickaxe).tier == ToolTier::Iron);

    CHECK(itemInfo(ItemType::IronAxe).toolType == ToolType::Axe);
    CHECK(itemInfo(ItemType::IronAxe).tier == ToolTier::Iron);
}

TEST_CASE("a copper pickaxe cannot mine obsidian, but an iron pickaxe can")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::Obsidian);
    world.set(13, 29, BlockType::Obsidian);

    Player player = standingAt(world, 10.0f, 30);

    player.inventory().exchange(2, {ItemType::CopperPickaxe, 1});
    player.setSelectedSlot(2);

    PlayerInput copperInput;
    copperInput.mine = true;
    copperInput.cursor = cursorOn(12, 29);

    for (int i = 0; i < 400; ++i)
    {
        const ActionResult result = player.update(copperInput, world, STEP);
        REQUIRE_FALSE(result.broke);
    }
    CHECK(world.get(12, 29) == BlockType::Obsidian);

    player.inventory().exchange(3, {ItemType::IronPickaxe, 1});
    player.setSelectedSlot(3);

    PlayerInput ironInput;
    ironInput.mine = true;
    ironInput.cursor = cursorOn(13, 29);

    ActionResult result;
    for (int i = 0; i < 400 && !result.broke; ++i)
        result = player.update(ironInput, world, STEP);

    REQUIRE(result.broke);
    CHECK(world.get(13, 29) == BlockType::Air);
}
```

Append to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("the Iron Pickaxe recipe costs 2 sticks and 4 iron plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronPickaxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::IronPlate);
    CHECK(it->ingredients[1].count == 4);
}

TEST_CASE("the Iron Axe recipe costs 2 sticks and 2 iron plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronAxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::IronPlate);
    CHECK(it->ingredients[1].count == 2);
}
```

Bump the recipe count: `CHECK(all.size() == 18);` becomes `CHECK(all.size() == 20);`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** - `'Obsidian': is not a member of 'BlockType'` (and similarly for the `ItemType` entries).

- [ ] **Step 3: Add `BlockType::Obsidian`**

In `src/Blocks/Blocks.h`, replace:

```cpp
enum class BlockType : std::uint8_t
{
    Air,
    Grass,
    Dirt,
    Stone,
    CopperOre,
    IronOre,
    Coal,
    OakLog,
    OakLeaves,

    Count
};
```

with:

```cpp
enum class BlockType : std::uint8_t
{
    Air,
    Grass,
    Dirt,
    Stone,
    CopperOre,
    IronOre,
    Coal,
    OakLog,
    OakLeaves,
    Obsidian,

    Count
};
```

- [ ] **Step 4: Add the Obsidian block registry row**

In `src/Blocks/Blocks.cpp`, append a new row right after `"Oak Leaves"`:

```cpp
    {"Oak Leaves",  { 60, 140,  50}, false, 0.15f, BlockType::Air,       ToolType::Axe},
    {"Obsidian",    { 40,  20,  55}, true,  3.00f, BlockType::Obsidian,  ToolType::Pickaxe, ToolTier::Iron},
```

- [ ] **Step 5: Add the new `ItemType` entries**

In `src/Items/Items.h`, replace:

```cpp
    None,
    Dirt,
    Stone,
    CopperOre,
    IronOre,
    Coal,
```

with:

```cpp
    None,
    Dirt,
    Stone,
    CopperOre,
    IronOre,
    Coal,
    Obsidian,
```

and replace:

```cpp
    CopperPickaxe,
    CopperAxe,
    OakLog,
```

with:

```cpp
    CopperPickaxe,
    CopperAxe,
    IronPickaxe,
    IronAxe,
    OakLog,
```

- [ ] **Step 6: Add the new registry rows and the `itemForBlock` case**

In `src/Items/Items.cpp`, replace:

```cpp
    {"Coal",             99,  BlockType::Coal,      ToolType::None,    { 44,  44,  50}},
```

with:

```cpp
    {"Coal",             99,  BlockType::Coal,      ToolType::None,    { 44,  44,  50}},
    {"Obsidian",         99,  BlockType::Obsidian,  ToolType::None,    { 40,  20,  55}},
```

Replace:

```cpp
    {"Copper Pickaxe",    1,  BlockType::Air,       ToolType::Pickaxe, {195, 120,  70}, ToolTier::Copper},
    {"Copper Axe",        1,  BlockType::Air,       ToolType::Axe,     {190, 115,  65}, ToolTier::Copper},
    {"Oak Log",          99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
```

with:

```cpp
    {"Copper Pickaxe",    1,  BlockType::Air,       ToolType::Pickaxe, {195, 120,  70}, ToolTier::Copper},
    {"Copper Axe",        1,  BlockType::Air,       ToolType::Axe,     {190, 115,  65}, ToolTier::Copper},
    {"Iron Pickaxe",      1,  BlockType::Air,       ToolType::Pickaxe, {175, 180, 190}, ToolTier::Iron},
    {"Iron Axe",          1,  BlockType::Air,       ToolType::Axe,     {170, 175, 185}, ToolTier::Iron},
    {"Oak Log",          99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
```

In `itemForBlock()`, replace:

```cpp
        case BlockType::OakLog:    return ItemType::OakLog;

        default: return ItemType::None;
```

with:

```cpp
        case BlockType::OakLog:    return ItemType::OakLog;
        case BlockType::Obsidian:  return ItemType::Obsidian;

        default: return ItemType::None;
```

- [ ] **Step 7: Add the new recipes**

In `src/Machines/Recipes.cpp`, replace `constexpr std::array<CraftRecipe, 18> craftRecipes` with `constexpr std::array<CraftRecipe, 20> craftRecipes`, and add these two rows directly after the `CopperAxe` row:

```cpp
    {ItemType::IronPickaxe,     {{{ItemType::Stick, 2}, {ItemType::IronPlate, 4}}},     2.0f, true},
    {ItemType::IronAxe,         {{{ItemType::Stick, 2}, {ItemType::IronPlate, 2}}},     2.0f, true},
```

- [ ] **Step 8: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 261 + 5 new = 266.

- [ ] **Step 9: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 10: Commit**

```bash
git add src/Blocks/Blocks.h src/Blocks/Blocks.cpp src/Items/Items.h src/Items/Items.cpp \
        src/Machines/Recipes.cpp tests/test_mining.cpp tests/test_recipes.cpp
git commit -m "feat: add the Obsidian block and the Iron tool tier"
```

---

## Task 5: Obsidian tier

**Files:**
- Modify: `src/Items/Items.h`, `src/Items/Items.cpp`
- Modify: `src/Machines/Recipes.cpp`
- Test: `tests/test_mining.cpp`, `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: `ToolTier`, `toolTierSpeedMultiplier`, `ItemType::Obsidian` (material, Task 4).
- Produces: `ItemType::ObsidianPickaxe`, `ItemType::ObsidianAxe`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_mining.cpp`:

```cpp
TEST_CASE("the obsidian tools are correctly typed items, and Obsidian is the top tier")
{
    CHECK(itemInfo(ItemType::ObsidianPickaxe).toolType == ToolType::Pickaxe);
    CHECK(itemInfo(ItemType::ObsidianPickaxe).tier == ToolTier::Obsidian);

    CHECK(itemInfo(ItemType::ObsidianAxe).toolType == ToolType::Axe);
    CHECK(itemInfo(ItemType::ObsidianAxe).tier == ToolTier::Obsidian);

    // Nothing outranks Obsidian: it meets its own tier requirement and
    // every requirement below it.
    CHECK(meetsTier(ToolTier::Obsidian, ToolTier::Obsidian));
    CHECK(meetsTier(ToolTier::Obsidian, ToolTier::Iron));
}

TEST_CASE("an obsidian pickaxe mines obsidian at Obsidian tier's own (fastest) speed")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::Obsidian);

    Player player = standingAt(world, 10.0f, 30);
    player.inventory().exchange(2, {ItemType::ObsidianPickaxe, 1});
    player.setSelectedSlot(2);

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    const float expectedTime =
        blockInfo(BlockType::Obsidian).hardness / toolTierSpeedMultiplier(ToolTier::Obsidian);

    ActionResult result;
    float elapsed = 0.0f;

    for (int i = 0; i < 400 && !result.broke; ++i)
    {
        result = player.update(input, world, STEP);
        elapsed += STEP;
    }

    REQUIRE(result.broke);
    CHECK(elapsed >= expectedTime);
    CHECK(elapsed < expectedTime + 0.05f);
}
```

Append to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("the Obsidian Pickaxe recipe costs 2 sticks and 3 obsidian")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ObsidianPickaxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Obsidian);
    CHECK(it->ingredients[1].count == 3);
}

TEST_CASE("the Obsidian Axe recipe costs 2 sticks and 2 obsidian")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ObsidianAxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Obsidian);
    CHECK(it->ingredients[1].count == 2);
}
```

Bump the recipe count: `CHECK(all.size() == 20);` becomes `CHECK(all.size() == 22);`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** - `'ObsidianPickaxe': is not a member of 'ItemType'` (and similarly for `ObsidianAxe`).

- [ ] **Step 3: Add the new `ItemType` entries**

In `src/Items/Items.h`, replace:

```cpp
    IronPickaxe,
    IronAxe,
    OakLog,
```

with:

```cpp
    IronPickaxe,
    IronAxe,
    ObsidianPickaxe,
    ObsidianAxe,
    OakLog,
```

- [ ] **Step 4: Add the new registry rows**

In `src/Items/Items.cpp`, replace:

```cpp
    {"Iron Pickaxe",      1,  BlockType::Air,       ToolType::Pickaxe, {175, 180, 190}, ToolTier::Iron},
    {"Iron Axe",          1,  BlockType::Air,       ToolType::Axe,     {170, 175, 185}, ToolTier::Iron},
    {"Oak Log",          99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
```

with:

```cpp
    {"Iron Pickaxe",      1,  BlockType::Air,       ToolType::Pickaxe, {175, 180, 190}, ToolTier::Iron},
    {"Iron Axe",          1,  BlockType::Air,       ToolType::Axe,     {170, 175, 185}, ToolTier::Iron},
    {"Obsidian Pickaxe",  1,  BlockType::Air,       ToolType::Pickaxe, { 60,  35,  80}, ToolTier::Obsidian},
    {"Obsidian Axe",      1,  BlockType::Air,       ToolType::Axe,     { 55,  30,  75}, ToolTier::Obsidian},
    {"Oak Log",          99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
```

- [ ] **Step 5: Add the new recipes**

In `src/Machines/Recipes.cpp`, replace `constexpr std::array<CraftRecipe, 20> craftRecipes` with `constexpr std::array<CraftRecipe, 22> craftRecipes`, and add these two rows directly after the `IronAxe` row:

```cpp
    {ItemType::ObsidianPickaxe, {{{ItemType::Stick, 2}, {ItemType::Obsidian, 3}}},       2.0f, true},
    {ItemType::ObsidianAxe,     {{{ItemType::Stick, 2}, {ItemType::Obsidian, 2}}},       2.0f, true},
```

- [ ] **Step 6: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 266 + 4 new = 270.

- [ ] **Step 7: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 8: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/Recipes.cpp \
        tests/test_mining.cpp tests/test_recipes.cpp
git commit -m "feat: add the Obsidian tool tier"
```

---

## Task 6: Axe log-yield bonus

**Files:**
- Modify: `src/Player/Player.cpp`
- Test: `tests/test_mining.cpp`

**Interfaces:**
- Consumes: `ToolTier` (Task 1), every axe `ItemType` (Tasks 1-5).
- Produces: no new public interface - a behavior change inside `Player::mine()`'s existing tree-break branch.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_mining.cpp` (the `buildTree` helper already builds a trunk of any height, already used by the existing cascade tests):

```cpp
TEST_CASE("felling 5 or more logs at once adds a per-tier bonus")
{
    auto logsFelledWith = [](ItemType axe) {
        World world;
        buildFloor(world, 30);
        buildTree(world, 12, 30, 5); // trunk rows 25..29 - a 5-log fell

        Player player = standingAt(world, 10.0f, 30);
        player.inventory().exchange(2, {axe, 1});
        player.setSelectedSlot(2);

        PlayerInput input;
        input.mine = true;
        input.cursor = cursorOn(12, 29); // the bottom log

        ActionResult result;
        for (int i = 0; i < 300 && !result.broke; ++i)
            result = player.update(input, world, STEP);

        int logCount = 0;
        for (const BrokenTile& tile : result.broken)
            if (tile.block == BlockType::OakLog)
                ++logCount;

        return logCount;
    };

    CHECK(logsFelledWith(ItemType::WoodAxe) == 5);
    CHECK(logsFelledWith(ItemType::StoneAxe) == 6);
    CHECK(logsFelledWith(ItemType::CopperAxe) == 7);
    CHECK(logsFelledWith(ItemType::IronAxe) == 8);
    CHECK(logsFelledWith(ItemType::ObsidianAxe) == 9);
}

TEST_CASE("a partial chop below the 4-log floor gets no tier bonus, regardless of axe")
{
    World world;
    buildFloor(world, 30);
    buildTree(world, 12, 30, 5); // trunk rows 25..29 (25 = top, 29 = bottom)

    Player player = standingAt(world, 10.0f, 30);
    player.inventory().exchange(2, {ItemType::ObsidianAxe, 1}); // the biggest possible bonus
    player.setSelectedSlot(2);

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 26); // second log from the top: a 2-log partial chop

    ActionResult result;
    for (int i = 0; i < 300 && !result.broke; ++i)
        result = player.update(input, world, STEP);

    REQUIRE(result.broke);

    int logCount = 0;
    for (const BrokenTile& tile : result.broken)
        if (tile.block == BlockType::OakLog)
            ++logCount;

    CHECK(logCount == 2);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: **compile succeeds, tests fail** - `logsFelledWith(ItemType::StoneAxe) == 6` etc. all report `5` (today's plain cascade count, no bonus applied yet).

- [ ] **Step 3: Add the bonus helper and call it from the tree-break branch**

In `src/Player/Player.cpp`, add a new helper function in the anonymous namespace, right after `collectTreeBreak`:

```cpp
// Felling 4+ logs in one cascade adds a flat bonus scaled by the axe's
// tier (Wood +0 ... Obsidian +4 - ToolTier's own underlying value is
// exactly this bonus, so no separate table is needed). Below 4 logs (a
// partial chop high up the trunk) there is no bonus at all - this closes
// the exploit of chopping one log at a time to farm the bonus repeatedly.
void applyAxeLogBonus(std::vector<BrokenTile>& broken, ToolTier axeTier)
{
    int logCount = 0;
    int lastLogX = 0;
    int lastLogY = 0;

    for (const BrokenTile& tile : broken)
    {
        if (tile.block != BlockType::OakLog)
            continue;

        ++logCount;
        lastLogX = tile.x;
        lastLogY = tile.y;
    }

    if (logCount < 4)
        return;

    const int bonus = static_cast<int>(axeTier);

    for (int i = 0; i < bonus; ++i)
        broken.push_back({BlockType::OakLog, lastLogX, lastLogY});
}
```

Then in `Player::mine()`, replace:

```cpp
    if (isTreePart(block))
        collectTreeBreak(world, tileX, tileY, result.broken);
```

with:

```cpp
    if (isTreePart(block))
    {
        collectTreeBreak(world, tileX, tileY, result.broken);
        applyAxeLogBonus(result.broken, heldInfo.tier);
    }
```

- [ ] **Step 4: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 270 + 2 new = 272.

Note: the pre-existing test `"breaking the bottom log fells the whole tree and drops every log"` fells a 5-log tree with `ItemType::WoodAxe` (via `setSelectedSlot(1)`, which still holds a Wood Axe) and asserts `logCount == 5` - Wood's bonus is `+0`, so that assertion still holds unchanged.

- [ ] **Step 5: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 6: Commit**

```bash
git add src/Player/Player.cpp tests/test_mining.cpp
git commit -m "feat: add axe tier log-yield bonus on big fells"
```

---

## Task 7: Sharp Rock world spawn and respawn

**Files:**
- Modify: `src/World/TerrainGenerator.h`, `src/World/TerrainGenerator.cpp`
- Modify: `src/Game/Game.h`, `src/Game/Game.cpp`
- Test: `tests/test_terrain.cpp`

**Interfaces:**
- Consumes: `ItemType::SharpRock` (Task 2).
- Produces: `TerrainGenerator::SHARP_ROCK_COUNT`, `TerrainGenerator::scatterSharpRocks`, `TerrainGenerator::randomSurfaceSpot`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_terrain.cpp`:

```cpp
TEST_CASE("world generation places exactly SHARP_ROCK_COUNT sharp rock spots on the surface, spread out")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generate(world);

    const std::vector<std::pair<int, int>> spots = generator.scatterSharpRocks(world);

    REQUIRE(spots.size() == static_cast<std::size_t>(TerrainGenerator::SHARP_ROCK_COUNT));

    std::vector<int> xs;
    for (const auto& [x, y] : spots)
    {
        REQUIRE(x >= 0);
        REQUIRE(x < WORLD_WIDTH);
        CHECK(y == generator.surfaceHeight(x));
        xs.push_back(x);
    }

    // Spread across the world rather than clumped together.
    std::sort(xs.begin(), xs.end());
    for (std::size_t i = 1; i < xs.size(); ++i)
        CHECK(xs[i] - xs[i - 1] > 10);
}

TEST_CASE("randomSurfaceSpot lands on the surface within world bounds")
{
    World world;
    TerrainGenerator generator(4242u);
    generator.generate(world);

    const auto [x, y] = generator.randomSurfaceSpot(world, 17u);

    REQUIRE(x >= 0);
    REQUIRE(x < WORLD_WIDTH);
    CHECK(y == generator.surfaceHeight(x));
}

TEST_CASE("randomSurfaceSpot gives different salts a chance to land on different spots")
{
    World world;
    TerrainGenerator generator(99u);
    generator.generate(world);

    bool sawDifferentSpot = false;
    const auto [firstX, firstY] = generator.randomSurfaceSpot(world, 0u);

    for (std::uint32_t salt = 1; salt < 20; ++salt)
    {
        const auto [x, y] = generator.randomSurfaceSpot(world, salt);
        if (x != firstX)
        {
            sawDifferentSpot = true;
            break;
        }
    }

    CHECK(sawDifferentSpot);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** - `'scatterSharpRocks': is not a member of 'TerrainGenerator'` (and similarly for `randomSurfaceSpot`, `SHARP_ROCK_COUNT`).

- [ ] **Step 3: Declare the new members**

In `src/World/TerrainGenerator.h`, add the constant next to `TREE_MIN_SPACING`:

```cpp
    // A finite early-game resource: exactly this many Sharp Rock spawn
    // points at world generation. Game tops it back up at runtime via
    // randomSurfaceSpot - see SHARP_ROCK_RESPAWN_INTERVAL in Game.h.
    static constexpr int SHARP_ROCK_COUNT = 9;
```

Add the two new public methods next to `surfaceHeight`:

```cpp
    // SHARP_ROCK_COUNT random surface spots, hashed from the world seed
    // alone - deterministic like every other pass. Used once at world
    // generation.
    std::vector<std::pair<int, int>> scatterSharpRocks(const World& world) const;

    // One more random surface spot, hashed from the world seed and `salt` -
    // vary `salt` per call (e.g. an incrementing counter) to get a
    // different spot each time. Used by Game's runtime respawn timer, since
    // that is not a generation pass and needs a fresh pick on demand.
    std::pair<int, int> randomSurfaceSpot(const World& world, std::uint32_t salt) const;
```

- [ ] **Step 4: Implement both methods**

In `src/World/TerrainGenerator.cpp`, add a new salt constant next to `SALT_TREE`:

```cpp
constexpr std::uint32_t SALT_SHARP_ROCK = 0x9000u;
```

Add both method definitions after `scatterTrees`:

```cpp
std::pair<int, int> TerrainGenerator::randomSurfaceSpot(const World& world, std::uint32_t salt) const
{
    constexpr int MAX_ATTEMPTS = 8;

    int x = 1;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
    {
        const float roll = noise::hashFloat(attempt, 0, worldSeed + SALT_SHARP_ROCK + salt);
        x = 1 + static_cast<int>(roll * (WORLD_WIDTH - 2));

        if (world.get(x, surfaceHeight(x)) == BlockType::Grass)
            break;
    }

    return {x, surfaceHeight(x)};
}

std::vector<std::pair<int, int>> TerrainGenerator::scatterSharpRocks(const World& world) const
{
    std::vector<std::pair<int, int>> spots;
    spots.reserve(SHARP_ROCK_COUNT);

    const int binWidth = (WORLD_WIDTH - 2) / SHARP_ROCK_COUNT;
    constexpr int MAX_ATTEMPTS = 8;

    for (int i = 0; i < SHARP_ROCK_COUNT; ++i)
    {
        const int binStart = 1 + i * binWidth;
        int x = binStart;

        for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
        {
            const float roll = noise::hashFloat(i, attempt, worldSeed + SALT_SHARP_ROCK);
            x = binStart + static_cast<int>(roll * binWidth);

            if (world.get(x, surfaceHeight(x)) == BlockType::Grass)
                break;
        }

        spots.push_back({x, surfaceHeight(x)});
    }

    return spots;
}
```

- [ ] **Step 5: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 272 + 3 new = 275.

- [ ] **Step 6: Materialize Sharp Rocks at world load**

In `src/Game/Game.h`, add the respawn constant and new members:

```cpp
    static constexpr float SHARP_ROCK_RESPAWN_INTERVAL = 300.0f; // 5 minutes
```

and, in the private method list, next to `spawnDrop`:

```cpp
    void spawnSharpRocks();
    void respawnSharpRocksIfNeeded(float dt);
```

and, next to the other simulation-state fields:

```cpp
    float sharpRockRespawnTimer = 0.0f;
    int sharpRockSpawnCounter = 0;
```

In `src/Game/Game.cpp`, add both method definitions right after `spawnDrop`:

```cpp
void Game::spawnSharpRocks()
{
    for (const auto& [x, y] : generator.scatterSharpRocks(world))
    {
        const sf::Vector2f position{(x + 0.5f) * TILE_SIZE, (y - 1.0f) * TILE_SIZE};
        drops.emplace_back(ItemStack{ItemType::SharpRock, 1}, position, sf::Vector2f{0.0f, 0.0f});
    }
}

void Game::respawnSharpRocksIfNeeded(float dt)
{
    sharpRockRespawnTimer += dt;

    if (sharpRockRespawnTimer < SHARP_ROCK_RESPAWN_INTERVAL)
        return;

    sharpRockRespawnTimer = 0.0f;

    int groundCount = 0;
    for (const ItemEntity& drop : drops)
        if (drop.stack().type == ItemType::SharpRock)
            ++groundCount;

    if (groundCount >= TerrainGenerator::SHARP_ROCK_COUNT)
        return;

    ++sharpRockSpawnCounter;
    const auto [x, y] =
        generator.randomSurfaceSpot(world, static_cast<std::uint32_t>(sharpRockSpawnCounter));

    const sf::Vector2f position{(x + 0.5f) * TILE_SIZE, (y - 1.0f) * TILE_SIZE};
    drops.emplace_back(ItemStack{ItemType::SharpRock, 1}, position, sf::Vector2f{0.0f, 0.0f});
}
```

In `Game::Game()`'s constructor, replace:

```cpp
    generator.generate(world);
    chunks.markAllDirty();
```

with:

```cpp
    generator.generate(world);
    chunks.markAllDirty();

    spawnSharpRocks();
```

(`spawnSharpRocks()` runs before `player = Player(findSpawn());`, so it does not depend on player state - it only needs `generator` and `world`, both already constructed by this point.)

In `Game::fixedUpdate()`, replace:

```cpp
    updateDrops(dt);
    tickMachines(dt);
```

with:

```cpp
    updateDrops(dt);
    respawnSharpRocksIfNeeded(dt);
    tickMachines(dt);
```

- [ ] **Step 7: Build the game executable and verify it still runs**

Run: the Build (game) command from Global Constraints.
Expected: builds clean. (`Game.cpp`/`Game.h` are not linked into the test binary, so this step is this task's only verification of the materialization/respawn wiring - matching how every other `Game.cpp`-only change in this repo's plans is verified.)

- [ ] **Step 8: Commit**

```bash
git add src/World/TerrainGenerator.h src/World/TerrainGenerator.cpp src/Game/Game.h src/Game/Game.cpp \
        tests/test_terrain.cpp
git commit -m "feat: spawn and respawn Sharp Rocks on the surface"
```

---

## Final check

After Task 7, run the full suite once more (`Litharia_tests.exe`) and confirm **275 test cases, 275 passed, 0 failed** - the plan's cumulative total (247 baseline + 3 + 6 + 5 + 5 + 4 + 2 + 3 = 275).
