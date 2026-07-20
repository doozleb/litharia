# Obsidian Machine Tier Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a third, faster tier - Obsidian - above the existing Copper/Iron tiers for `Drill`, `Belt`, `Chute`, and `Smelter`, using raw `Obsidian` (not a new "plate") as the tier's crafting material.

**Architecture:** One family at a time (Drill, then Belt, then Chute, then Smelter), each task adds one `MachineType`/`ItemType` enum value, one `MachineRegistry` row, one `Items` row, one craft recipe row, extends that family's existing `isDrill`/`isBelt`/`isChute`/`isSmelter` helper to also match the new value, and adds the new value to `MachineRenderer.cpp`'s `isOutputSide()` switch. Because this *adds* a third value rather than *splitting* an existing one (unlike the 2026-07-18 Copper/Iron plan), no existing call site or test that already names `MachineType::CopperX`/`IronX` needs to change - this plan is purely additive.

**Tech Stack:** C++20, SFML 3 (System only for `Litharia_core`; Graphics/Window for the `Litharia` game exe), CMake + Visual Studio generator, doctest (vendored single header).

## Global Constraints

- **Build (tests):** `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:\Litharia\build --config Debug --target Litharia_tests`. `cmake` is not on PATH; use the full path shown.
- **Build (game):** same command with `--target Litharia`.
- **Test binary:** `C:\Litharia\build\Debug\Litharia_tests.exe`.
- **Baseline before this work:** 325 test cases, 325 passed, 0 failed, at commit `d77249a`.
- **Simulation/rendering split:** `Machines.cpp`, `MachineStatus.cpp`, `MachineRegistry.cpp`, `Recipes.cpp`, `Items.cpp` compile into `Litharia_core`, reachable from `Litharia_tests`. `MachineRenderer.cpp`, `Hud.cpp`, `Game.cpp`/`Game.h` compile only into the `Litharia` executable and are **not** linked into the test binary - those changes are build-verified, not doctest-verified, matching every prior plan in this repo.
- **`OBSIDIAN_TIER_SPEEDUP` is the single source of the tier gap:** every Obsidian-tier timing is `<iron value> * OBSIDIAN_TIER_SPEEDUP`, written as that expression in the registry, never as a separately hand-typed literal. Do not hardcode `1.8f`, `0.3f`, or the smelter recipe products.
- **Power draw does not change between tiers.** Only speed and craft cost differ. Do not touch any `powerRating` value in this plan.
- **Recipe shape:** every Obsidian recipe costs raw `Obsidian` plus `Stone` - never `CopperPlate`/`IronPlate`, and no new `ObsidianPlate` item or `SmeltRecipe` entry (Obsidian needs no smelting, same as its existing Pickaxe/Axe recipes already assume).
- **`Recipes.cpp`'s `craftRecipes` array has an explicit literal size** (`std::array<CraftRecipe, N>`) that must be bumped by exactly 1 every time a row is added, or the file fails to compile ("too many initializers"). `MachineRegistry.cpp`'s and `Items.cpp`'s registries are sized off `MachineType::Count`/`ItemType::Count` automatically and need no such bump.
- **Commit after every task** with a `feat:` prefixed message.
- **Spec:** `docs/superpowers/specs/2026-07-20-obsidian-machine-tier-design.md`.

---

## File Structure

**Modified across all four tasks:**
- `src/Items/Items.h` - `ItemType` enum: each of `Drill`/`Belt`/`Chute`/`Smelter` gains one `Obsidian<X>` entry, placed right after its `Iron<X>` entry.
- `src/Items/Items.cpp` - `registry` (the `ItemInfo` table): each family gains one row, placed right after its Iron row.
- `src/Machines/MachineType.h` - `MachineType` enum (same insertion pattern); adds `OBSIDIAN_TIER_SPEEDUP` (Task 1).
- `src/Machines/MachineRegistry.cpp` - `registry` (the `MachineInfo` table): each family gains one row; `itemForMachine()`'s switch gains one case per new type; `isDrill`/`isBelt`/`isChute`/`isSmelter` each gain one `||` arm.
- `src/Machines/Recipes.cpp` - `craftRecipes`: each family gains one row; the array's literal size bumps by 1 each task.
- `src/Machines/MachineRenderer.cpp` - `isOutputSide()`'s switch gains one `case` label per new type, in the same family groupings as today.
- Four test files gain new/extended test cases (see per-task lists below). No test file needs a mechanical rename - this plan only adds references, never changes what an existing `MachineType::CopperX`/`IronX` reference means.

**No new files.**

---

## Canonical Interfaces (defined once, referenced by all tasks)

```cpp
// src/Machines/MachineType.h (Task 1 adds this, next to COPPER_TIER_SLOWDOWN)
// Obsidian-tier machines run this much faster than their Iron-tier
// equivalent (multiplies actionTime/speedMultiplier down, not up).
inline constexpr float OBSIDIAN_TIER_SPEEDUP = 0.6f;
```

No new struct fields are needed - `MachineInfo::speedMultiplier` already exists (added by the Copper/Iron plan) and Task 4 just gives `ObsidianSmelter` a third value in circulation (`0.6f`, alongside `1.0f` and `1.75f`).

Colors (from the approved design spec, one per family, tinted toward Obsidian's own hue `{40, 20, 55}`):

| Family | Color |
|---|---|
| Obsidian Drill | `{90, 70, 100}` |
| Obsidian Belt | `{80, 60, 95}` |
| Obsidian Chute | `{75, 55, 90}` |
| Obsidian Smelter | `{95, 65, 100}` |

Used identically for both the `ItemInfo::iconColor` row and the `MachineInfo::color` row of the same type, matching the existing convention.

---

## Task 1: Drill tier addition (`ObsidianDrill`)

**Files:**
- Modify: `src/Items/Items.h`, `src/Items/Items.cpp`
- Modify: `src/Machines/MachineType.h`
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Recipes.cpp`
- Modify: `src/Machines/MachineRenderer.cpp`
- Test: `tests/test_machines.cpp`, `tests/test_recipes.cpp`, `tests/test_processing.cpp`

**Interfaces:**
- Consumes: nothing new from other tasks (this is the plan's first task).
- Produces: `MachineType::ObsidianDrill`, `ItemType::ObsidianDrill`, `inline constexpr float OBSIDIAN_TIER_SPEEDUP`. Tasks 2-4 reuse `OBSIDIAN_TIER_SPEEDUP` directly. `isDrill()` now also matches `ObsidianDrill`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("an Obsidian Drill is OBSIDIAN_TIER_SPEEDUP times faster than an Iron Drill")
{
    const float iron = machineInfo(MachineType::IronDrill).actionTime;
    const float obsidian = machineInfo(MachineType::ObsidianDrill).actionTime;

    CHECK(iron == doctest::Approx(3.0f));
    CHECK(obsidian == doctest::Approx(iron * OBSIDIAN_TIER_SPEEDUP));
}
```

Replace the existing `TEST_CASE("isDrill is true for exactly Copper Drill and Iron Drill")` with:

```cpp
TEST_CASE("isDrill is true for exactly Copper Drill, Iron Drill, and Obsidian Drill")
{
    CHECK(isDrill(MachineType::CopperDrill));
    CHECK(isDrill(MachineType::IronDrill));
    CHECK(isDrill(MachineType::ObsidianDrill));
    CHECK_FALSE(isDrill(MachineType::None));
    CHECK_FALSE(isDrill(MachineType::BurnerGenerator));
    CHECK_FALSE(isDrill(MachineType::IronSmelter));
    CHECK_FALSE(isDrill(MachineType::CopperBelt));
    CHECK_FALSE(isDrill(MachineType::IronBelt));
}
```

Add one more line inside the existing `TEST_CASE("the machine registry has a valid row per type")`, right after the two existing Drill `consumer` checks:

```cpp
    CHECK(machineInfo(MachineType::CopperDrill).consumer);
    CHECK(machineInfo(MachineType::IronDrill).consumer);
    CHECK(machineInfo(MachineType::ObsidianDrill).consumer); // <-- new line
```

Add one more line inside the existing `TEST_CASE("itemForMachine maps every placeable machine to its own item")`, right after the two existing Drill lines:

```cpp
    CHECK(itemForMachine(MachineType::CopperDrill) == ItemType::CopperDrill);
    CHECK(itemForMachine(MachineType::IronDrill) == ItemType::IronDrill);
    CHECK(itemForMachine(MachineType::ObsidianDrill) == ItemType::ObsidianDrill); // <-- new line
```

Add one more line inside the existing `TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, and Furnace")`, right after the two existing Drill lines:

```cpp
    CHECK_FALSE(isFurniture(MachineType::CopperDrill));
    CHECK_FALSE(isFurniture(MachineType::IronDrill));
    CHECK_FALSE(isFurniture(MachineType::ObsidianDrill)); // <-- new line
```

Add one more line inside `tests/test_recipes.cpp`'s `TEST_CASE("every placeable machine has a matching craftable item")`, right after the two existing Drill lines:

```cpp
    CHECK(itemInfo(ItemType::CopperDrill).name == "Copper Drill");
    CHECK(itemInfo(ItemType::IronDrill).name == "Iron Drill");
    CHECK(itemInfo(ItemType::ObsidianDrill).name == "Obsidian Drill"); // <-- new line
```

Change the recipe count: `CHECK(all.size() == 22);` becomes `CHECK(all.size() == 23);`.

Append a new recipe-cost test to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("the Obsidian Drill recipe costs 4 obsidian and 2 stone, no plate needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ObsidianDrill; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Obsidian);
    CHECK(it->ingredients[0].count == 4);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(4.0f));
}
```

Append one more behavioral test to `tests/test_processing.cpp` (`MachineType`/`machineInfo`/`OBSIDIAN_TIER_SPEEDUP` are already available there transitively via the existing `Machines/Machines.h` include - no new `#include` needed):

```cpp
TEST_CASE("an Obsidian Drill takes OBSIDIAN_TIER_SPEEDUP times less time to mine than an Iron Drill")
{
    World world;
    world.fill(BlockType::Air);
    world.set(1, 1, BlockType::CopperOre);

    Machines ironMachines;
    ironMachines.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    ironMachines.place(MachineType::IronDrill, 1, 0, Direction::Up); // nothing to receive output
    REQUIRE(ironMachines.tryInsert(0, 0, ItemType::Coal));

    Machines obsidianMachines;
    obsidianMachines.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    obsidianMachines.place(MachineType::ObsidianDrill, 1, 0, Direction::Up);
    REQUIRE(obsidianMachines.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Comfortably past the Obsidian Drill's own (shorter) time, but nowhere
    // near the Iron Drill's: Obsidian is done, Iron is not yet.
    const int obsidianTicks = static_cast<int>(
        (machineInfo(MachineType::ObsidianDrill).actionTime + 0.2f) / step);
    for (int i = 0; i < obsidianTicks; ++i)
    {
        ironMachines.tick(world, step, mined);
        obsidianMachines.tick(world, step, mined);
    }

    CHECK_FALSE(obsidianMachines.at(1, 0)->output.empty());
    CHECK(ironMachines.at(1, 0)->output.empty());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command from Global Constraints.
Expected: **compile error** - `'ObsidianDrill': is not a member of 'MachineType'` (and similarly for `ItemType`, `OBSIDIAN_TIER_SPEEDUP`).

- [ ] **Step 3: Add the enum values and constant**

In `src/Items/Items.h`, insert `ObsidianDrill,` right after `IronDrill,`:

```cpp
    CopperDrill,
    IronDrill,
    ObsidianDrill,
```

In `src/Items/Items.cpp`, insert a new row right after the `"Iron Drill"` row:

```cpp
    {"Iron Drill",        10,  BlockType::Air,       ToolType::None,    {158, 162, 175}},
    {"Obsidian Drill",    10,  BlockType::Air,       ToolType::None,    { 90,  70, 100}},
```

In `src/Machines/MachineType.h`, add the constant right below `COPPER_TIER_SLOWDOWN`:

```cpp
// Copper-tier machines run this much slower than their Iron-tier equivalent.
inline constexpr float COPPER_TIER_SLOWDOWN = 1.75f;

// Obsidian-tier machines run this much faster than their Iron-tier
// equivalent (multiplies actionTime/speedMultiplier down, not up).
inline constexpr float OBSIDIAN_TIER_SPEEDUP = 0.6f;
```

then insert `ObsidianDrill,` right after `IronDrill,`:

```cpp
    CopperDrill,
    IronDrill,
    ObsidianDrill,
```

- [ ] **Step 4: Add the registry row and `itemForMachine` case, extend `isDrill`**

In `src/Machines/MachineRegistry.cpp`, insert a new row right after the `"Iron Drill"` row:

```cpp
    {"Iron Drill",        {158, 162, 175}, false, true,  false, 5.0f,  3.0f,                          1, 1},
    {"Obsidian Drill",    { 90,  70, 100}, false, true,  false, 5.0f,  3.0f * OBSIDIAN_TIER_SPEEDUP,   1, 1},
```

Add a case in `itemForMachine()` right after the `IronDrill` case:

```cpp
        case MachineType::IronDrill:       return ItemType::IronDrill;
        case MachineType::ObsidianDrill:   return ItemType::ObsidianDrill;
```

Change `isDrill()`:

```cpp
bool isDrill(MachineType type)
{
    return type == MachineType::CopperDrill || type == MachineType::IronDrill
        || type == MachineType::ObsidianDrill;
}
```

- [ ] **Step 5: Add the craft recipe**

In `src/Machines/Recipes.cpp`, change the array size and insert a new row right after the `IronDrill` row:

```cpp
constexpr std::array<CraftRecipe, 23> craftRecipes = {{
```

```cpp
    {ItemType::IronDrill,   {{{ItemType::IronPlate, 4}, {ItemType::Stone, 2}}},   4.0f, true},
    {ItemType::ObsidianDrill, {{{ItemType::Obsidian, 4}, {ItemType::Stone, 2}}}, 4.0f, true},
```

- [ ] **Step 6: Add the rendering case**

In `src/Machines/MachineRenderer.cpp`, add `ObsidianDrill` to the Drill/Smelter case group:

```cpp
        case MachineType::CopperDrill:
        case MachineType::IronDrill:
        case MachineType::ObsidianDrill:
        case MachineType::CopperSmelter:
        case MachineType::IronSmelter:
            return side != m.facing;
```

- [ ] **Step 7: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all test cases pass (325 baseline + 3 new cases: the `OBSIDIAN_TIER_SPEEDUP` speed-ratio test, the recipe-cost test, and the processing behavioral test - the other five checks above are new assertions inside existing test cases, not new cases).

- [ ] **Step 8: Build the game executable too**

Run: the Build (game) command from Global Constraints.
Expected: builds clean (`MachineRenderer.cpp` is only compiled here, not in the test binary).

- [ ] **Step 9: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h \
        src/Machines/MachineRegistry.cpp src/Machines/Recipes.cpp \
        src/Machines/MachineRenderer.cpp \
        tests/test_machines.cpp tests/test_recipes.cpp tests/test_processing.cpp
git commit -m "feat: add Obsidian Drill, a faster tier above Iron"
```

---

## Task 2: Belt tier addition (`ObsidianBelt`)

**Files:**
- Modify: `src/Items/Items.h`, `src/Items/Items.cpp`
- Modify: `src/Machines/MachineType.h`
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Recipes.cpp`
- Modify: `src/Machines/MachineRenderer.cpp`
- Test: `tests/test_machines.cpp`, `tests/test_recipes.cpp`, `tests/test_transport.cpp`

**Interfaces:**
- Consumes: `OBSIDIAN_TIER_SPEEDUP` (Task 1).
- Produces: `MachineType::ObsidianBelt`, `ItemType::ObsidianBelt`. `isBelt()` now also matches `ObsidianBelt`.

`Machines.cpp`'s `tickTransport()` dispatches on the generic `info.transport` flag with no Belt-specific type check, so **no change is needed there** - same as the original Copper/Iron plan found for this family.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("an Obsidian Belt is OBSIDIAN_TIER_SPEEDUP times faster than an Iron Belt")
{
    const float iron = machineInfo(MachineType::IronBelt).actionTime;
    const float obsidian = machineInfo(MachineType::ObsidianBelt).actionTime;

    CHECK(iron == doctest::Approx(0.5f));
    CHECK(obsidian == doctest::Approx(iron * OBSIDIAN_TIER_SPEEDUP));
}
```

Replace the existing `TEST_CASE("isBelt is true for exactly Copper Belt and Iron Belt")` with:

```cpp
TEST_CASE("isBelt is true for exactly Copper Belt, Iron Belt, and Obsidian Belt")
{
    CHECK(isBelt(MachineType::CopperBelt));
    CHECK(isBelt(MachineType::IronBelt));
    CHECK(isBelt(MachineType::ObsidianBelt));
    CHECK_FALSE(isBelt(MachineType::None));
    CHECK_FALSE(isBelt(MachineType::CopperChute));
    CHECK_FALSE(isBelt(MachineType::IronChute));
    CHECK_FALSE(isBelt(MachineType::IronDrill));
}
```

Add one more line inside `TEST_CASE("the machine registry has a valid row per type")`, right after the two existing Belt `transport` checks:

```cpp
    CHECK(machineInfo(MachineType::CopperBelt).transport);
    CHECK(machineInfo(MachineType::IronBelt).transport);
    CHECK(machineInfo(MachineType::ObsidianBelt).transport); // <-- new line
```

Add one more line inside `TEST_CASE("itemForMachine maps every placeable machine to its own item")`, right after the two existing Belt lines:

```cpp
    CHECK(itemForMachine(MachineType::CopperBelt) == ItemType::CopperBelt);
    CHECK(itemForMachine(MachineType::IronBelt) == ItemType::IronBelt);
    CHECK(itemForMachine(MachineType::ObsidianBelt) == ItemType::ObsidianBelt); // <-- new line
```

Add one more line inside `TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, and Furnace")`, right after the two existing Belt lines:

```cpp
    CHECK_FALSE(isFurniture(MachineType::CopperBelt));
    CHECK_FALSE(isFurniture(MachineType::IronBelt));
    CHECK_FALSE(isFurniture(MachineType::ObsidianBelt)); // <-- new line
```

Add one more line inside `tests/test_recipes.cpp`'s `TEST_CASE("every placeable machine has a matching craftable item")`, right after the two existing Belt lines:

```cpp
    CHECK(itemInfo(ItemType::CopperBelt).name == "Copper Belt");
    CHECK(itemInfo(ItemType::IronBelt).name == "Iron Belt");
    CHECK(itemInfo(ItemType::ObsidianBelt).name == "Obsidian Belt"); // <-- new line
```

Change the recipe count: `CHECK(all.size() == 23);` becomes `CHECK(all.size() == 24);`.

Append a new recipe-cost test to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("the Obsidian Belt recipe costs 2 obsidian and 2 stone, no plate needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ObsidianBelt; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Obsidian);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(1.0f));
}
```

Append one more behavioral test to `tests/test_transport.cpp`:

```cpp
TEST_CASE("an Obsidian Belt carries an item to the machine ahead faster than an Iron Belt would")
{
    Machines m;
    World world;
    m.place(MachineType::ObsidianBelt, 0, 0, Direction::Right);
    m.place(MachineType::IronBelt, 1, 0, Direction::Right); // just needs to accept the handoff

    REQUIRE(m.tryInsert(0, 0, ItemType::Stone));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // A full extra 0.2s of margin past the Obsidian Belt's own actionTime,
    // not just +1 tick, so float accumulation over the run can never make
    // this assertion flaky - and comfortably short of the Iron Belt's time.
    const int ticks =
        static_cast<int>((machineInfo(MachineType::ObsidianBelt).actionTime + 0.2f) / step);
    for (int i = 0; i < ticks; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(1, 0)->carried == ItemType::Stone);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** - `'ObsidianBelt': is not a member of 'MachineType'` (and similarly for `ItemType`).

- [ ] **Step 3: Add the enum values**

In `src/Items/Items.h`, insert `ObsidianBelt,` right after `IronBelt,`:

```cpp
    CopperBelt,
    IronBelt,
    ObsidianBelt,
```

In `src/Items/Items.cpp`, insert a new row right after the `"Iron Belt"` row:

```cpp
    {"Iron Belt",         50,  BlockType::Air,       ToolType::None,    {128, 132, 145}},
    {"Obsidian Belt",     50,  BlockType::Air,       ToolType::None,    { 80,  60,  95}},
```

In `src/Machines/MachineType.h`, insert `ObsidianBelt,` right after `IronBelt,`:

```cpp
    CopperBelt,
    IronBelt,
    ObsidianBelt,
```

- [ ] **Step 4: Add the registry row and `itemForMachine` case, extend `isBelt`**

In `src/Machines/MachineRegistry.cpp`, insert a new row right after the `"Iron Belt"` row:

```cpp
    {"Iron Belt",         {128, 132, 145}, false, false, true,  0.0f,  0.5f,                          1, 1},
    {"Obsidian Belt",     { 80,  60,  95}, false, false, true,  0.0f,  0.5f * OBSIDIAN_TIER_SPEEDUP,   1, 1},
```

Add a case in `itemForMachine()` right after the `IronBelt` case:

```cpp
        case MachineType::IronBelt:        return ItemType::IronBelt;
        case MachineType::ObsidianBelt:    return ItemType::ObsidianBelt;
```

Change `isBelt()`:

```cpp
bool isBelt(MachineType type)
{
    return type == MachineType::CopperBelt || type == MachineType::IronBelt
        || type == MachineType::ObsidianBelt;
}
```

- [ ] **Step 5: Add the craft recipe**

In `src/Machines/Recipes.cpp`, change the array size and insert a new row right after the `IronBelt` row:

```cpp
constexpr std::array<CraftRecipe, 24> craftRecipes = {{
```

```cpp
    {ItemType::IronBelt,   {{{ItemType::IronPlate, 2}, {ItemType::Stone, 2}}},   1.0f, true},
    {ItemType::ObsidianBelt, {{{ItemType::Obsidian, 2}, {ItemType::Stone, 2}}}, 1.0f, true},
```

- [ ] **Step 6: Add the rendering case**

In `src/Machines/MachineRenderer.cpp`, add `ObsidianBelt` to the Belt case group:

```cpp
        case MachineType::CopperBelt:
        case MachineType::IronBelt:
        case MachineType::ObsidianBelt:
            return side == m.facing;
```

- [ ] **Step 7: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass (previous total + 3 new cases: speed-ratio test, recipe-cost test, transport behavioral test).

- [ ] **Step 8: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 9: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h \
        src/Machines/MachineRegistry.cpp src/Machines/Recipes.cpp \
        src/Machines/MachineRenderer.cpp \
        tests/test_machines.cpp tests/test_recipes.cpp tests/test_transport.cpp
git commit -m "feat: add Obsidian Belt, a faster tier above Iron"
```

---

## Task 3: Chute tier addition (`ObsidianChute`)

**Files:**
- Modify: `src/Items/Items.h`, `src/Items/Items.cpp`
- Modify: `src/Machines/MachineType.h`
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Recipes.cpp`
- Modify: `src/Machines/MachineRenderer.cpp`
- Test: `tests/test_machines.cpp`, `tests/test_recipes.cpp`, `tests/test_transport.cpp`

**Interfaces:**
- Consumes: `OBSIDIAN_TIER_SPEEDUP` (Task 1).
- Produces: `MachineType::ObsidianChute`, `ItemType::ObsidianChute`. `isChute()` now also matches `ObsidianChute`.

`Machines.cpp:457`'s `isChute(m.type) ? Direction::Down : m.facing` already dispatches through the helper, so **no change is needed there** - extending `isChute()` in Step 4 is sufficient.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("an Obsidian Chute is OBSIDIAN_TIER_SPEEDUP times faster than an Iron Chute")
{
    const float iron = machineInfo(MachineType::IronChute).actionTime;
    const float obsidian = machineInfo(MachineType::ObsidianChute).actionTime;

    CHECK(iron == doctest::Approx(0.5f));
    CHECK(obsidian == doctest::Approx(iron * OBSIDIAN_TIER_SPEEDUP));
}
```

Replace the existing `TEST_CASE("isChute is true for exactly Copper Chute and Iron Chute")` with:

```cpp
TEST_CASE("isChute is true for exactly Copper Chute, Iron Chute, and Obsidian Chute")
{
    CHECK(isChute(MachineType::CopperChute));
    CHECK(isChute(MachineType::IronChute));
    CHECK(isChute(MachineType::ObsidianChute));
    CHECK_FALSE(isChute(MachineType::None));
    CHECK_FALSE(isChute(MachineType::IronBelt));
}
```

Add one more line inside `TEST_CASE("the machine registry has a valid row per type")`, right after the two existing Chute `transport` checks:

```cpp
    CHECK(machineInfo(MachineType::CopperChute).transport);
    CHECK(machineInfo(MachineType::IronChute).transport);
    CHECK(machineInfo(MachineType::ObsidianChute).transport); // <-- new line
```

Add one more line inside the existing `TEST_CASE("itemForMachine maps both Chute tiers to their own item")` (rename it too):

```cpp
TEST_CASE("itemForMachine maps every Chute tier to its own item")
{
    CHECK(itemForMachine(MachineType::CopperChute) == ItemType::CopperChute);
    CHECK(itemForMachine(MachineType::IronChute) == ItemType::IronChute);
    CHECK(itemForMachine(MachineType::ObsidianChute) == ItemType::ObsidianChute);
}
```

Add one more line inside `TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, and Furnace")`, right after the two existing Chute lines:

```cpp
    CHECK_FALSE(isFurniture(MachineType::CopperChute));
    CHECK_FALSE(isFurniture(MachineType::IronChute));
    CHECK_FALSE(isFurniture(MachineType::ObsidianChute)); // <-- new line
```

Add one more line inside `tests/test_recipes.cpp`'s `TEST_CASE("every placeable machine has a matching craftable item")`, right after the two existing Chute lines:

```cpp
    CHECK(itemInfo(ItemType::CopperChute).name == "Copper Chute");
    CHECK(itemInfo(ItemType::IronChute).name == "Iron Chute");
    CHECK(itemInfo(ItemType::ObsidianChute).name == "Obsidian Chute"); // <-- new line
```

Change the recipe count: `CHECK(all.size() == 24);` becomes `CHECK(all.size() == 25);`.

Append a new recipe-cost test to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("the Obsidian Chute recipe costs 1 obsidian and 2 stone, no plate needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ObsidianChute; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Obsidian);
    CHECK(it->ingredients[0].count == 1);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(1.0f));
}
```

Append one more behavioral test to `tests/test_transport.cpp`:

```cpp
TEST_CASE("an Obsidian Chute drops its item straight down faster than an Iron Chute would")
{
    Machines m;
    World world;
    m.place(MachineType::ObsidianChute, 0, 0, Direction::Right); // facing ignored by chutes
    m.place(MachineType::IronBelt, 0, 1, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::Stone));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // A full extra 0.2s of margin past the Obsidian Chute's own actionTime,
    // not just +1 tick, so float accumulation over the run can never make
    // this assertion flaky.
    const int ticks =
        static_cast<int>((machineInfo(MachineType::ObsidianChute).actionTime + 0.2f) / step);
    for (int i = 0; i < ticks; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(0, 1)->carried == ItemType::Stone);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** - `'ObsidianChute': is not a member of 'MachineType'` (and similarly for `ItemType`).

- [ ] **Step 3: Add the enum values**

In `src/Items/Items.h`, insert `ObsidianChute,` right after `IronChute,`:

```cpp
    CopperChute,
    IronChute,
    ObsidianChute,
```

In `src/Items/Items.cpp`, insert a new row right after the `"Iron Chute"` row:

```cpp
    {"Iron Chute",        50,  BlockType::Air,       ToolType::None,    {118, 122, 135}},
    {"Obsidian Chute",    50,  BlockType::Air,       ToolType::None,    { 75,  55,  90}},
```

In `src/Machines/MachineType.h`, insert `ObsidianChute,` right after `IronChute,`:

```cpp
    CopperChute,
    IronChute,
    ObsidianChute,
```

- [ ] **Step 4: Add the registry row and `itemForMachine` case, extend `isChute`**

In `src/Machines/MachineRegistry.cpp`, insert a new row right after the `"Iron Chute"` row:

```cpp
    {"Iron Chute",        {118, 122, 135}, false, false, true,  0.0f,  0.5f,                          1, 1},
    {"Obsidian Chute",    { 75,  55,  90}, false, false, true,  0.0f,  0.5f * OBSIDIAN_TIER_SPEEDUP,   1, 1},
```

Add a case in `itemForMachine()` right after the `IronChute` case:

```cpp
        case MachineType::IronChute:       return ItemType::IronChute;
        case MachineType::ObsidianChute:   return ItemType::ObsidianChute;
```

Change `isChute()`:

```cpp
bool isChute(MachineType type)
{
    return type == MachineType::CopperChute || type == MachineType::IronChute
        || type == MachineType::ObsidianChute;
}
```

- [ ] **Step 5: Add the craft recipe**

In `src/Machines/Recipes.cpp`, change the array size and insert a new row right after the `IronChute` row:

```cpp
constexpr std::array<CraftRecipe, 25> craftRecipes = {{
```

```cpp
    {ItemType::IronChute,   {{{ItemType::IronPlate, 1}, {ItemType::Stone, 2}}},    1.0f, true},
    {ItemType::ObsidianChute, {{{ItemType::Obsidian, 1}, {ItemType::Stone, 2}}}, 1.0f, true},
```

- [ ] **Step 6: Add the rendering case**

In `src/Machines/MachineRenderer.cpp`, add `ObsidianChute` to the Chute case group:

```cpp
        case MachineType::CopperChute:
        case MachineType::IronChute:
        case MachineType::ObsidianChute:
            return side == Direction::Down;
```

- [ ] **Step 7: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass (previous total + 3 new cases: speed-ratio test, recipe-cost test, transport behavioral test).

- [ ] **Step 8: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 9: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h \
        src/Machines/MachineRegistry.cpp src/Machines/Recipes.cpp \
        src/Machines/MachineRenderer.cpp \
        tests/test_machines.cpp tests/test_recipes.cpp tests/test_transport.cpp
git commit -m "feat: add Obsidian Chute, a faster tier above Iron"
```

---

## Task 4: Smelter tier addition (`ObsidianSmelter`)

**Files:**
- Modify: `src/Items/Items.h`, `src/Items/Items.cpp`
- Modify: `src/Machines/MachineType.h`
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Recipes.cpp`
- Modify: `src/Machines/MachineRenderer.cpp`
- Test: `tests/test_machines.cpp`, `tests/test_machine_status.cpp`, `tests/test_recipes.cpp`, `tests/test_processing.cpp`

**Interfaces:**
- Consumes: `OBSIDIAN_TIER_SPEEDUP` (Task 1), `MachineInfo::speedMultiplier` (pre-existing field from the Copper/Iron plan).
- Produces: `MachineType::ObsidianSmelter`, `ItemType::ObsidianSmelter`. `isSmelter()` now also matches `ObsidianSmelter`.

`Machines.cpp`'s smelt tick and `MachineStatus.cpp`'s `barStatus()` both already compute `recipe->seconds * machineInfo(m.type).speedMultiplier` generically, so **no change is needed in either file** - giving `ObsidianSmelter` its own registry row is sufficient.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("an Obsidian Smelter smelts OBSIDIAN_TIER_SPEEDUP times faster than an Iron Smelter")
{
    CHECK(machineInfo(MachineType::ObsidianSmelter).speedMultiplier ==
          doctest::Approx(OBSIDIAN_TIER_SPEEDUP));
}
```

Replace the existing `TEST_CASE("Copper Smelter and Iron Smelter carry the expected speedMultiplier")` with:

```cpp
TEST_CASE("Copper, Iron, and Obsidian Smelters carry the expected speedMultiplier")
{
    CHECK(machineInfo(MachineType::IronSmelter).speedMultiplier == doctest::Approx(1.0f));
    CHECK(machineInfo(MachineType::CopperSmelter).speedMultiplier == doctest::Approx(COPPER_TIER_SLOWDOWN));
    CHECK(machineInfo(MachineType::ObsidianSmelter).speedMultiplier == doctest::Approx(OBSIDIAN_TIER_SPEEDUP));
}
```

(`TEST_CASE("every non-Smelter machine defaults to a speedMultiplier of 1.0")` loops over every `MachineType` and skips `isSmelter(type)` - it needs no edit and will pass unchanged once `isSmelter()` correctly excludes `ObsidianSmelter` from the "must default to 1.0" check.)

Replace the existing `TEST_CASE("isSmelter is true for exactly Copper Smelter and Iron Smelter")` with:

```cpp
TEST_CASE("isSmelter is true for exactly Copper Smelter, Iron Smelter, and Obsidian Smelter")
{
    CHECK(isSmelter(MachineType::CopperSmelter));
    CHECK(isSmelter(MachineType::IronSmelter));
    CHECK(isSmelter(MachineType::ObsidianSmelter));
    CHECK_FALSE(isSmelter(MachineType::None));
    CHECK_FALSE(isSmelter(MachineType::IronDrill));
}
```

Add one more line inside `TEST_CASE("the machine registry has a valid row per type")`, right after the two existing Smelter `consumer` checks:

```cpp
    CHECK(machineInfo(MachineType::CopperSmelter).consumer);
    CHECK(machineInfo(MachineType::IronSmelter).consumer);
    CHECK(machineInfo(MachineType::ObsidianSmelter).consumer); // <-- new line
```

Add one more line inside `TEST_CASE("itemForMachine maps every placeable machine to its own item")`, right after the two existing Smelter lines:

```cpp
    CHECK(itemForMachine(MachineType::CopperSmelter) == ItemType::CopperSmelter);
    CHECK(itemForMachine(MachineType::IronSmelter) == ItemType::IronSmelter);
    CHECK(itemForMachine(MachineType::ObsidianSmelter) == ItemType::ObsidianSmelter); // <-- new line
```

Add one more line inside `TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, and Furnace")`, right after the two existing Smelter lines:

```cpp
    CHECK_FALSE(isFurniture(MachineType::CopperSmelter));
    CHECK_FALSE(isFurniture(MachineType::IronSmelter));
    CHECK_FALSE(isFurniture(MachineType::ObsidianSmelter)); // <-- new line
```

Append to `tests/test_machine_status.cpp`:

```cpp
TEST_CASE("an Obsidian Smelter mid-smelt shows progress against recipe seconds times its own multiplier")
{
    Machine m;
    m.type = MachineType::ObsidianSmelter;
    m.input = {ItemType::CopperOre, 1};

    const SmeltRecipe* recipe = smeltRecipeFor(ItemType::CopperOre);
    REQUIRE(recipe != nullptr);
    const float fullTime = recipe->seconds * machineInfo(MachineType::ObsidianSmelter).speedMultiplier;
    m.progress = fullTime * 0.5f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Progress);
    CHECK(status.fraction == doctest::Approx(0.5f));
}
```

Add one more line inside `tests/test_recipes.cpp`'s `TEST_CASE("every placeable machine has a matching craftable item")`, right after the two existing Smelter lines:

```cpp
    CHECK(itemInfo(ItemType::CopperSmelter).name == "Copper Smelter");
    CHECK(itemInfo(ItemType::IronSmelter).name == "Iron Smelter");
    CHECK(itemInfo(ItemType::ObsidianSmelter).name == "Obsidian Smelter"); // <-- new line
```

Change the recipe count: `CHECK(all.size() == 25);` becomes `CHECK(all.size() == 26);`.

Append a new recipe-cost test to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("the Obsidian Smelter recipe costs 3 obsidian and 5 stone, no plate needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ObsidianSmelter; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Obsidian);
    CHECK(it->ingredients[0].count == 3);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 5);
    CHECK(it->seconds == doctest::Approx(4.0f));
}
```

Append one more behavioral test to `tests/test_processing.cpp` (needs `smeltRecipeFor`/`SmeltRecipe` - `#include "Machines/Recipes.h"` should already be present from the Copper/Iron plan's Task 4; if it is not, add it alongside the existing includes):

```cpp
TEST_CASE("an Obsidian Smelter finishes a recipe in less time than an Iron Smelter")
{
    World world;

    Machines ironMachines;
    ironMachines.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    ironMachines.place(MachineType::IronSmelter, 1, 0, Direction::Right);
    REQUIRE(ironMachines.tryInsert(0, 0, ItemType::Coal));
    REQUIRE(ironMachines.tryInsert(1, 0, ItemType::CopperOre));

    Machines obsidianMachines;
    obsidianMachines.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    obsidianMachines.place(MachineType::ObsidianSmelter, 1, 0, Direction::Right);
    REQUIRE(obsidianMachines.tryInsert(0, 0, ItemType::Coal));
    REQUIRE(obsidianMachines.tryInsert(1, 0, ItemType::CopperOre));

    const SmeltRecipe* recipe = smeltRecipeFor(ItemType::CopperOre);
    REQUIRE(recipe != nullptr);

    const float obsidianTime = recipe->seconds * machineInfo(MachineType::ObsidianSmelter).speedMultiplier;

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Comfortably past the Obsidian Smelter's own (shorter) time, but well
    // short of the Iron Smelter's: Obsidian is done, Iron is not yet.
    const int obsidianTicks = static_cast<int>((obsidianTime + 0.2f) / step);
    for (int i = 0; i < obsidianTicks; ++i)
    {
        ironMachines.tick(world, step, mined);
        obsidianMachines.tick(world, step, mined);
    }

    CHECK_FALSE(obsidianMachines.at(1, 0)->output.empty());
    CHECK(ironMachines.at(1, 0)->output.empty());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** - `'ObsidianSmelter': is not a member of 'MachineType'` (and similarly for `ItemType`).

- [ ] **Step 3: Add the enum values**

In `src/Items/Items.h`, insert `ObsidianSmelter,` right after `IronSmelter,`:

```cpp
    CopperSmelter,
    IronSmelter,
    ObsidianSmelter,
```

In `src/Items/Items.cpp`, insert a new row right after the `"Iron Smelter"` row:

```cpp
    {"Iron Smelter",      10,  BlockType::Air,       ToolType::None,    {183, 132, 130}},
    {"Obsidian Smelter",  10,  BlockType::Air,       ToolType::None,    { 95,  65, 100}},
```

In `src/Machines/MachineType.h`, insert `ObsidianSmelter,` right after `IronSmelter,`:

```cpp
    CopperSmelter,
    IronSmelter,
    ObsidianSmelter,
```

- [ ] **Step 4: Add the registry row and `itemForMachine` case, extend `isSmelter`**

In `src/Machines/MachineRegistry.cpp`, insert a new row right after the `"Iron Smelter"` row:

```cpp
    {"Iron Smelter",      {183, 132, 130}, false, true,  false, 5.0f,  0.0f, 1, 1, 1.0f},
    {"Obsidian Smelter",  { 95,  65, 100}, false, true,  false, 5.0f,  0.0f, 1, 1, OBSIDIAN_TIER_SPEEDUP},
```

Add a case in `itemForMachine()` right after the `IronSmelter` case:

```cpp
        case MachineType::IronSmelter:     return ItemType::IronSmelter;
        case MachineType::ObsidianSmelter: return ItemType::ObsidianSmelter;
```

Change `isSmelter()`:

```cpp
bool isSmelter(MachineType type)
{
    return type == MachineType::CopperSmelter || type == MachineType::IronSmelter
        || type == MachineType::ObsidianSmelter;
}
```

- [ ] **Step 5: Add the craft recipe**

In `src/Machines/Recipes.cpp`, change the array size and insert a new row right after the `IronSmelter` row:

```cpp
constexpr std::array<CraftRecipe, 26> craftRecipes = {{
```

```cpp
    {ItemType::IronSmelter,   {{{ItemType::IronPlate, 3}, {ItemType::Stone, 5}}},   4.0f, true},
    {ItemType::ObsidianSmelter, {{{ItemType::Obsidian, 3}, {ItemType::Stone, 5}}}, 4.0f, true},
```

- [ ] **Step 6: Add the rendering case**

In `src/Machines/MachineRenderer.cpp`, add `ObsidianSmelter` to the Drill/Smelter case group (the same group Task 1 already added `ObsidianDrill` to):

```cpp
        case MachineType::CopperDrill:
        case MachineType::IronDrill:
        case MachineType::ObsidianDrill:
        case MachineType::CopperSmelter:
        case MachineType::IronSmelter:
        case MachineType::ObsidianSmelter:
            return side != m.facing;
```

- [ ] **Step 7: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass (previous total + 4 new cases: `speedMultiplier` test, status-bar test, recipe-cost test, processing behavioral test).

- [ ] **Step 8: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 9: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h \
        src/Machines/MachineRegistry.cpp src/Machines/Recipes.cpp \
        src/Machines/MachineRenderer.cpp \
        tests/test_machines.cpp tests/test_machine_status.cpp \
        tests/test_recipes.cpp tests/test_processing.cpp
git commit -m "feat: add Obsidian Smelter, a faster tier above Iron"
```

---

## Final Verification

- [ ] **Step 1: Run the full test suite one more time**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, total test cases = 325 (baseline) + 13 new cases across the four tasks (3 each for Drill/Belt/Chute, 4 for Smelter - it additionally gets a `MachineStatus` bar test the other three families don't need) = 338.

- [ ] **Step 2: Build the game executable**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 3: Manual visual check**

`MachineRenderer.cpp`'s new case labels and the four new item/machine colors are not covered by any test (same as every prior tier-related rendering change in this codebase). Launch the game, open the build palette, and confirm all four Obsidian machines appear with their tinted-purple colors and place/tick correctly. This is a manual step for the human, not something to automate here.
