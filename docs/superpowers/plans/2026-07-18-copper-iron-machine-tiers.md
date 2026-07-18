# Copper/Iron Machine Tiers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `Drill`, `Belt`, `Chute`, and `Smelter` into a slower Copper tier and a faster Iron tier each (today's numbers become the Iron tier), with Copper-tier recipes reachable from Copper Plate alone.

**Architecture:** One family at a time (Drill, then Belt, then Chute, then Smelter), each task: replace the family's single `MachineType`/`ItemType` enum entry with two (`Copper<X>`/`Iron<X>`), add a family-check helper (`isDrill`/`isBelt`/`isChute`/`isSmelter`) that every existing single-type-equality check gets swapped for, give each new registry row its own speed/cost, and fix every test file that constructs the old type name. The Smelter task additionally introduces one new `MachineInfo` field (`speedMultiplier`) since its timing comes from the shared `SmeltRecipe` table rather than `MachineInfo::actionTime` directly.

**Tech Stack:** C++20, SFML 3 (System only for `Litharia_core`; Graphics/Window for the `Litharia` game exe), CMake + Visual Studio generator, doctest (vendored single header).

## Global Constraints

- **Build (tests):** `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:\litharia\build --config Debug --target Litharia_tests`. `cmake` is not on PATH; use the full path shown.
- **Build (game):** same command with `--target Litharia`.
- **Test binary:** `C:\litharia\build\Debug\Litharia_tests.exe`.
- **Baseline before this work:** 222 test cases, 222 passed, 0 failed, at commit `c4a0f59`.
- **Simulation/rendering split:** `Machines.cpp`, `MachineStatus.cpp`, `MachineRegistry.cpp`, `Recipes.cpp`, `Items.cpp` compile into `Litharia_core`, reachable from `Litharia_tests`. `MachineRenderer.cpp`, `Hud.cpp`, `Game.cpp`/`Game.h` compile only into the `Litharia` executable and are **not** linked into the test binary — those changes are build-verified, not doctest-verified, matching every prior plan in this repo.
- **`COPPER_TIER_SLOWDOWN` is the single source of the tier gap:** every Copper-tier timing is `<iron value> * COPPER_TIER_SLOWDOWN`, written as that expression in the registry, never as a separately hand-typed literal. Do not hardcode `5.25f`, `0.875f`, or `6.125f` anywhere.
- **Power draw does not change between tiers.** Only speed and craft cost differ. Do not touch any `powerRating` value in this plan.
- **Recipe shape:** every Copper/Iron recipe costs exactly its own tier's plate plus Stone — never a mix of Copper Plate and Iron Plate in the same recipe.
- **Commit after every task** with a `feat:` prefixed message.
- **Spec:** `docs/superpowers/specs/2026-07-18-copper-iron-machine-tiers-design.md`.

---

## File Structure

**Modified across all four tasks:**
- `src/Items/Items.h` — `ItemType` enum: each of `Drill`/`Belt`/`Chute`/`Smelter` becomes two entries (`Copper<X>`, `Iron<X>`), in place.
- `src/Items/Items.cpp` — `registry` (the `ItemInfo` table): each old row becomes two.
- `src/Machines/MachineType.h` — `MachineType` enum (same split); adds `COPPER_TIER_SLOWDOWN` (Task 1), `isDrill`/`isBelt`/`isChute`/`isSmelter` declarations (one per task), and `MachineInfo::speedMultiplier` (Task 4).
- `src/Machines/MachineRegistry.cpp` — `registry` (the `MachineInfo` table): each old row becomes two; `itemForMachine()`'s switch gains a case per new type; `isDrill`/`isBelt`/`isChute`/`isSmelter` are defined here, next to `isFurniture`.
- `src/Machines/Recipes.cpp` — `craftRecipes`: each old entry becomes two (own-tier plate + Stone).
- `src/Machines/Machines.cpp` — every `m.type == MachineType::<X>` (or `!=`) becomes a call to the matching family helper; the Smelter tick (`tickSmelters`) gains the `speedMultiplier` factor (Task 4 only).
- `src/Machines/MachineStatus.cpp` — `barStatus()`'s per-family branches become family-helper calls; the Drill branch stops hardcoding `MachineType::Drill` in its own `machineInfo()` lookup; the Smelter branch gains the `speedMultiplier` factor (Task 4 only).
- `src/Machines/MachineRenderer.cpp` — `isOutputSide()`'s switch gains a `case` label per new type, in the same family groupings as today.
- `src/Hud/Hud.cpp` — the tooltip builder's "Smelts:"/"Mines:" branches become family-helper calls.
- `src/Game/Game.h` — default `buildType` becomes `MachineType::CopperBelt` (Task 2 only).
- `src/Game/Game.cpp` — the belt-facing-reset guard becomes `isBelt(buildType)` (Task 2 only); the `F2`-`F5` direct-select bindings are removed one at a time as each task retires the type they pointed at.
- Ten test files get old-type-name renames (see per-task lists below); `test_machines.cpp`, `test_machine_status.cpp`, `test_recipes.cpp`, and (Task 4 only) `test_processing.cpp` also gain new test cases.

**No new files.**

---

## Canonical Interfaces (defined once, referenced by all tasks)

```cpp
// src/Machines/MachineType.h (Task 1 adds the constant; each task adds one helper)
inline constexpr float COPPER_TIER_SLOWDOWN = 1.75f; // Copper tier runs this much slower than Iron.

bool isDrill(MachineType type);   // CopperDrill or IronDrill      (Task 1)
bool isBelt(MachineType type);    // CopperBelt or IronBelt        (Task 2)
bool isChute(MachineType type);   // CopperChute or IronChute      (Task 3)
bool isSmelter(MachineType type); // CopperSmelter or IronSmelter  (Task 4)
```

```cpp
// src/Machines/MachineType.h, MachineInfo struct (Task 4 only — appended as the LAST
// field so every existing 9-value row keeps compiling unchanged and defaults to 1.0f)
struct MachineInfo
{
    std::string_view name;
    BlockColor color;
    bool generator;
    bool consumer;
    bool transport;
    float powerRating;
    float actionTime;
    int width;
    int height;
    float speedMultiplier = 1.0f; // Smelter-family only: recipe->seconds * this.
};
```

Colors for each new row are the midpoint between the family's current color and its
tier's ore color (Copper Ore `{201,116,56}`, Iron Ore `{166,174,190}`), rounded to the
nearest integer per channel — used identically for both the `ItemInfo::iconColor` row
and the `MachineInfo::color` row of the same type, matching the existing convention
that a machine's bag icon and its placed-body color are the same swatch.

---

## Task 1: Drill tier split (`CopperDrill` / `IronDrill`)

**Files:**
- Modify: `src/Items/Items.h:11-35`, `src/Items/Items.cpp:9-31`
- Modify: `src/Machines/MachineType.h:12-42`
- Modify: `src/Machines/MachineRegistry.cpp:9-71`
- Modify: `src/Machines/Recipes.cpp:13-23`
- Modify: `src/Machines/Machines.cpp:279`, `src/Machines/Machines.cpp:497`
- Modify: `src/Machines/MachineStatus.cpp:17-22`
- Modify: `src/Machines/MachineRenderer.cpp:37-52`
- Modify: `src/Hud/Hud.cpp:609-610`
- Modify: `src/Game/Game.cpp:767` (delete the line)
- Test: `tests/test_machines.cpp`, `tests/test_machine_status.cpp`, `tests/test_recipes.cpp`
- Test (mechanical rename only): `tests/test_extraction.cpp`, `tests/test_factory.cpp`, `tests/test_machine_inspect.cpp`, `tests/test_power.cpp`, `tests/test_processing.cpp`, `tests/test_transport.cpp`

**Interfaces:**
- Consumes: nothing new from other tasks (this is the plan's first task).
- Produces: `MachineType::CopperDrill`, `MachineType::IronDrill`, `ItemType::CopperDrill`, `ItemType::IronDrill`, `bool isDrill(MachineType)`, `inline constexpr float COPPER_TIER_SLOWDOWN`. Tasks 2-4 reuse `COPPER_TIER_SLOWDOWN` directly.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp` (anywhere after the existing `#include`s):

```cpp
TEST_CASE("a Copper Drill is COPPER_TIER_SLOWDOWN times slower than an Iron Drill")
{
    const float iron = machineInfo(MachineType::IronDrill).actionTime;
    const float copper = machineInfo(MachineType::CopperDrill).actionTime;

    CHECK(iron == doctest::Approx(3.0f));
    CHECK(copper == doctest::Approx(iron * COPPER_TIER_SLOWDOWN));
}

TEST_CASE("isDrill is true for exactly Copper Drill and Iron Drill")
{
    CHECK(isDrill(MachineType::CopperDrill));
    CHECK(isDrill(MachineType::IronDrill));
    CHECK_FALSE(isDrill(MachineType::None));
    CHECK_FALSE(isDrill(MachineType::BurnerGenerator));
    CHECK_FALSE(isDrill(MachineType::Belt));
    CHECK_FALSE(isDrill(MachineType::Smelter));
}
```

Replace the two Drill lines inside the existing `TEST_CASE("the machine registry has a valid row per type")`:

```cpp
    // OLD:
    // CHECK(machineInfo(MachineType::Drill).consumer);
    // NEW:
    CHECK(machineInfo(MachineType::CopperDrill).consumer);
    CHECK(machineInfo(MachineType::IronDrill).consumer);
```

Replace the Drill line inside `TEST_CASE("itemForMachine maps every placeable machine to its own item")`:

```cpp
    // OLD:
    // CHECK(itemForMachine(MachineType::Drill) == ItemType::Drill);
    // NEW:
    CHECK(itemForMachine(MachineType::CopperDrill) == ItemType::CopperDrill);
    CHECK(itemForMachine(MachineType::IronDrill) == ItemType::IronDrill);
```

Replace the Drill line inside `TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, and Furnace")`:

```cpp
    // OLD:
    // CHECK_FALSE(isFurniture(MachineType::Drill));
    // NEW:
    CHECK_FALSE(isFurniture(MachineType::CopperDrill));
    CHECK_FALSE(isFurniture(MachineType::IronDrill));
```

Append to `tests/test_machine_status.cpp`:

```cpp
TEST_CASE("a Copper Drill mid-mining shows progress against its own (slower) actionTime")
{
    Machine m;
    m.type = MachineType::CopperDrill;
    m.progress = machineInfo(MachineType::CopperDrill).actionTime * 0.25f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Progress);
    CHECK(status.fraction == doctest::Approx(0.25f));
}
```

Change the existing drill test's type in `tests/test_machine_status.cpp` (2 occurrences, lines 44-45):

```cpp
    // OLD:
    // m.type = MachineType::Drill;
    // m.progress = machineInfo(MachineType::Drill).actionTime * 0.25f;
    // NEW:
    m.type = MachineType::IronDrill;
    m.progress = machineInfo(MachineType::IronDrill).actionTime * 0.25f;
```

Replace the Drill line inside `tests/test_recipes.cpp`'s `TEST_CASE("every placeable machine has a matching craftable item")`:

```cpp
    // OLD:
    // CHECK(itemInfo(ItemType::Drill).name == "Drill");
    // NEW:
    CHECK(itemInfo(ItemType::CopperDrill).name == "Copper Drill");
    CHECK(itemInfo(ItemType::IronDrill).name == "Iron Drill");
```

Change `TEST_CASE("allCraftRecipes exposes every craftable item exactly once")`'s count:

```cpp
    // OLD: CHECK(all.size() == 9);
    // NEW:
    CHECK(all.size() == 10);
```

Append two new recipe-cost tests to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("the Copper Drill recipe costs 4 copper plates and 2 stone, no iron needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperDrill; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::CopperPlate);
    CHECK(it->ingredients[0].count == 4);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(4.0f));
}

TEST_CASE("the Iron Drill recipe costs 4 iron plates and 2 stone, no copper needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronDrill; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::IronPlate);
    CHECK(it->ingredients[0].count == 4);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(4.0f));
}
```

Append one more behavioral test to `tests/test_processing.cpp` (`MachineType`/`machineInfo` are already
available there transitively via the existing `Machines/Machines.h` include - no new `#include` needed):

```cpp
TEST_CASE("a Copper Drill takes COPPER_TIER_SLOWDOWN times longer to mine than an Iron Drill")
{
    World world;
    world.fill(BlockType::Air);
    world.set(1, 1, BlockType::CopperOre);

    Machines ironMachines;
    ironMachines.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    ironMachines.place(MachineType::IronDrill, 1, 0, Direction::Up); // nothing to receive output
    REQUIRE(ironMachines.tryInsert(0, 0, ItemType::Coal));

    Machines copperMachines;
    copperMachines.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    copperMachines.place(MachineType::CopperDrill, 1, 0, Direction::Up);
    REQUIRE(copperMachines.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Just past the Iron-tier finish line: Iron is done, Copper is not yet.
    const int ironTicks = static_cast<int>(machineInfo(MachineType::IronDrill).actionTime / step) + 1;
    for (int i = 0; i < ironTicks; ++i)
    {
        ironMachines.tick(world, step, mined);
        copperMachines.tick(world, step, mined);
    }

    CHECK_FALSE(ironMachines.at(1, 0)->output.empty());
    CHECK(copperMachines.at(1, 0)->output.empty());

    // Past its own (1.75x longer) time: now the Copper Drill is done too.
    const int extraTicks = static_cast<int>(
        (machineInfo(MachineType::CopperDrill).actionTime
         - machineInfo(MachineType::IronDrill).actionTime) / step) + 1;
    for (int i = 0; i < extraTicks; ++i)
        copperMachines.tick(world, step, mined);

    CHECK_FALSE(copperMachines.at(1, 0)->output.empty());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command from Global Constraints.
Expected: **compile error** — `'CopperDrill': is not a member of 'MachineType'` (and similarly for `ItemType`, `isDrill`, `COPPER_TIER_SLOWDOWN`).

- [ ] **Step 3: Split the enums**

In `src/Items/Items.h`, replace `Drill,` (line 26) with:

```cpp
    CopperDrill,
    IronDrill,
```

In `src/Items/Items.cpp`, replace the `"Drill"` row (line 24) with:

```cpp
    {"Copper Drill",      10,  BlockType::Air,       ToolType::None,    {176, 133, 108}},
    {"Iron Drill",        10,  BlockType::Air,       ToolType::None,    {158, 162, 175}},
```

In `src/Machines/MachineType.h`, add the constant right below `DRILL_REACH` (line 16):

```cpp
// Copper-tier machines run this much slower than their Iron-tier equivalent.
inline constexpr float COPPER_TIER_SLOWDOWN = 1.75f;
```

then replace `Drill,` (line 32) with:

```cpp
    CopperDrill,
    IronDrill,
```

then add the declaration next to `isFurniture`'s (line 72):

```cpp
// True for CopperDrill or IronDrill - the two speed tiers of the same machine.
bool isDrill(MachineType type);
```

- [ ] **Step 4: Split the registry row and `itemForMachine` case, define `isDrill`**

In `src/Machines/MachineRegistry.cpp`, replace the `"Drill"` row (line 13) with:

```cpp
    {"Copper Drill",      {176, 133, 108}, false, true,  false, 5.0f,  3.0f * COPPER_TIER_SLOWDOWN, 1, 1},
    {"Iron Drill",        {158, 162, 175}, false, true,  false, 5.0f,  3.0f,                         1, 1},
```

Replace the `Drill` case in `itemForMachine()` (line 55) with:

```cpp
        case MachineType::CopperDrill:     return ItemType::CopperDrill;
        case MachineType::IronDrill:       return ItemType::IronDrill;
```

Add the helper right after `isFurniture()`'s definition:

```cpp
bool isDrill(MachineType type)
{
    return type == MachineType::CopperDrill || type == MachineType::IronDrill;
}
```

- [ ] **Step 5: Split the craft recipe**

In `src/Machines/Recipes.cpp`, replace the `Drill` row (line 19) with:

```cpp
    {ItemType::CopperDrill, {{{ItemType::CopperPlate, 4}, {ItemType::Stone, 2}}}, 4.0f, true},
    {ItemType::IronDrill,   {{{ItemType::IronPlate, 4}, {ItemType::Stone, 2}}},   4.0f, true},
```

- [ ] **Step 6: Swap behavior-dispatch checks to `isDrill()`**

In `src/Machines/Machines.cpp:279`, change `if (m.type == MachineType::Drill)` to `if (isDrill(m.type))`.

In `src/Machines/Machines.cpp:497`, change `if (m.type != MachineType::Drill)` to `if (!isDrill(m.type))`.

In `src/Machines/MachineStatus.cpp`, change:

```cpp
    // OLD:
    // else if (m.type == MachineType::Drill)
    // {
    //     status.bar = MachineBar::Progress;
    //     status.fraction =
    //         std::clamp(m.progress / machineInfo(MachineType::Drill).actionTime, 0.0f, 1.0f);
    // }
    // NEW:
    else if (isDrill(m.type))
    {
        status.bar = MachineBar::Progress;
        status.fraction = std::clamp(m.progress / machineInfo(m.type).actionTime, 0.0f, 1.0f);
    }
```

In `src/Machines/MachineRenderer.cpp`, change the `isOutputSide()` switch's Drill/Smelter case group:

```cpp
    // OLD:
    // case MachineType::Drill:
    // case MachineType::Smelter:
    //     return side != m.facing;
    // NEW:
    case MachineType::CopperDrill:
    case MachineType::IronDrill:
    case MachineType::Smelter:
        return side != m.facing;
```

In `src/Hud/Hud.cpp`, change:

```cpp
    // OLD:
    // else if (machine.type == MachineType::Drill)
    // NEW:
    else if (isDrill(machine.type))
```

In `src/Game/Game.cpp`, delete line 767 entirely:

```cpp
    if (key->code == Key::F2) setBuildType(MachineType::Drill);
```

(F2 is no longer bound to anything - Copper/Iron Drill selection is scroll-cycle/palette-click only, per the design spec's hotkey decision.)

- [ ] **Step 7: Fix the remaining test files (mechanical rename, no behavior change)**

Using the Edit tool with `replace_all: true`, replace `MachineType::Drill` with `MachineType::IronDrill` in each of:

- `tests/test_machines.cpp` (the remaining generic placement/footprint/`placedSeq` tests that use Drill only as an arbitrary machine type)
- `tests/test_extraction.cpp`
- `tests/test_factory.cpp`
- `tests/test_machine_inspect.cpp`
- `tests/test_power.cpp`
- `tests/test_processing.cpp`
- `tests/test_transport.cpp`

(Iron tier keeps today's exact `3.0f` `actionTime`, so every existing timing-dependent comment and tick count in these files - e.g. "Drill work time is 3.0s; run 4s to be safe" - stays numerically correct; only the type name changes.)

- [ ] **Step 8: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all test cases pass, count now 222 + 6 new = 228 (2 registry/helper tests in `test_machines.cpp`, 1 status test, 2 recipe-cost tests, 1 tick-behavior test in `test_processing.cpp` - the "every placeable machine", "isFurniture", and "itemForMachine" tests keep the same case count, just more assertions inside).

- [ ] **Step 9: Build the game executable too**

Run: the Build (game) command from Global Constraints.
Expected: builds clean (`MachineRenderer.cpp`/`Hud.cpp`/`Game.cpp` are only compiled here, not in the test binary).

- [ ] **Step 10: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h \
        src/Machines/MachineRegistry.cpp src/Machines/Recipes.cpp \
        src/Machines/Machines.cpp src/Machines/MachineStatus.cpp \
        src/Machines/MachineRenderer.cpp src/Hud/Hud.cpp src/Game/Game.cpp \
        tests/test_machines.cpp tests/test_machine_status.cpp tests/test_recipes.cpp \
        tests/test_extraction.cpp tests/test_factory.cpp tests/test_machine_inspect.cpp \
        tests/test_power.cpp tests/test_processing.cpp tests/test_transport.cpp
git commit -m "feat: split Drill into Copper Drill (slower) and Iron Drill (today's speed) tiers"
```

---

## Task 2: Belt tier split (`CopperBelt` / `IronBelt`)

**Files:**
- Modify: `src/Items/Items.h`, `src/Items/Items.cpp`
- Modify: `src/Machines/MachineType.h`
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Recipes.cpp`
- Modify: `src/Machines/MachineRenderer.cpp`
- Modify: `src/Game/Game.h`, `src/Game/Game.cpp`
- Test: `tests/test_machines.cpp`, `tests/test_machine_status.cpp`, `tests/test_recipes.cpp`
- Test (mechanical rename only): `tests/test_extraction.cpp`, `tests/test_factory.cpp`, `tests/test_placing.cpp`, `tests/test_power.cpp`, `tests/test_processing.cpp`, `tests/test_transport.cpp`

**Interfaces:**
- Consumes: `COPPER_TIER_SLOWDOWN` (Task 1).
- Produces: `MachineType::CopperBelt`, `MachineType::IronBelt`, `ItemType::CopperBelt`, `ItemType::IronBelt`, `bool isBelt(MachineType)`.

`Machines.cpp`'s `tickTransport()` already dispatches on the generic `info.transport` flag with no Belt-specific type check, so **no change is needed there** - this is the one family whose core tick behavior needs zero touch.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("a Copper Belt is COPPER_TIER_SLOWDOWN times slower than an Iron Belt")
{
    const float iron = machineInfo(MachineType::IronBelt).actionTime;
    const float copper = machineInfo(MachineType::CopperBelt).actionTime;

    CHECK(iron == doctest::Approx(0.5f));
    CHECK(copper == doctest::Approx(iron * COPPER_TIER_SLOWDOWN));
}

TEST_CASE("isBelt is true for exactly Copper Belt and Iron Belt")
{
    CHECK(isBelt(MachineType::CopperBelt));
    CHECK(isBelt(MachineType::IronBelt));
    CHECK_FALSE(isBelt(MachineType::None));
    CHECK_FALSE(isBelt(MachineType::Chute));
    CHECK_FALSE(isBelt(MachineType::IronDrill));
}
```

Replace the Belt line inside `TEST_CASE("the machine registry has a valid row per type")`:

```cpp
    // OLD: CHECK(machineInfo(MachineType::Belt).transport);
    // NEW:
    CHECK(machineInfo(MachineType::CopperBelt).transport);
    CHECK(machineInfo(MachineType::IronBelt).transport);
```

Replace the Belt line inside `TEST_CASE("itemForMachine maps every placeable machine to its own item")`:

```cpp
    // OLD: CHECK(itemForMachine(MachineType::Belt) == ItemType::Belt);
    // NEW:
    CHECK(itemForMachine(MachineType::CopperBelt) == ItemType::CopperBelt);
    CHECK(itemForMachine(MachineType::IronBelt) == ItemType::IronBelt);
```

Replace the Belt line inside `TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, and Furnace")`:

```cpp
    // OLD: CHECK_FALSE(isFurniture(MachineType::Belt));
    // NEW:
    CHECK_FALSE(isFurniture(MachineType::CopperBelt));
    CHECK_FALSE(isFurniture(MachineType::IronBelt));
```

In `tests/test_machine_status.cpp`, change the existing belt test's type:

```cpp
    // OLD: m.type = MachineType::Belt;
    // NEW:
    m.type = MachineType::IronBelt;
```

and append:

```cpp
TEST_CASE("a copper belt never shows a bar")
{
    Machine m;
    m.type = MachineType::CopperBelt;

    CHECK(barStatus(m).bar == MachineBar::None);
}
```

Replace the Belt line inside `tests/test_recipes.cpp`'s `TEST_CASE("every placeable machine has a matching craftable item")`:

```cpp
    // OLD: CHECK(itemInfo(ItemType::Belt).name == "Belt");
    // NEW:
    CHECK(itemInfo(ItemType::CopperBelt).name == "Copper Belt");
    CHECK(itemInfo(ItemType::IronBelt).name == "Iron Belt");
```

Bump the recipe count: `CHECK(all.size() == 10);` becomes `CHECK(all.size() == 11);`.

Append:

```cpp
TEST_CASE("the Copper Belt recipe costs 2 copper plates and 2 stone, no iron needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperBelt; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::CopperPlate);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(1.0f));
}

TEST_CASE("the Iron Belt recipe costs 2 iron plates and 2 stone, no copper needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronBelt; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::IronPlate);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(1.0f));
}
```

Append one more behavioral test to `tests/test_transport.cpp`:

```cpp
TEST_CASE("a Copper Belt carries an item to the machine ahead, just slower than an Iron Belt would")
{
    Machines m;
    World world;
    m.place(MachineType::CopperBelt, 0, 0, Direction::Right);
    m.place(MachineType::IronBelt, 1, 0, Direction::Right); // just needs to accept the handoff

    REQUIRE(m.tryInsert(0, 0, ItemType::Stone));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Shorter than the Iron Belt's actionTime: neither belt has moved its item yet.
    const int ironTicks = static_cast<int>(machineInfo(MachineType::IronBelt).actionTime / step);
    for (int i = 0; i < ironTicks; ++i)
        m.tick(world, step, mined);
    CHECK(m.at(0, 0)->carried == ItemType::Stone);

    // Comfortably past the Copper Belt's own (slower) actionTime - a full extra
    // 0.2s of margin, not just +1 tick, so float accumulation over the run can
    // never make this assertion flaky.
    const int totalTicksForCopper =
        static_cast<int>((machineInfo(MachineType::CopperBelt).actionTime + 0.2f) / step);
    for (int i = ironTicks; i < totalTicksForCopper; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(1, 0)->carried == ItemType::Stone);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** — `'CopperBelt': is not a member of 'MachineType'` (and similarly for `ItemType`, `isBelt`).

- [ ] **Step 3: Split the enums**

In `src/Items/Items.h`, replace `Belt,` with:

```cpp
    CopperBelt,
    IronBelt,
```

In `src/Items/Items.cpp`, replace the `"Belt"` row with:

```cpp
    {"Copper Belt",       50,  BlockType::Air,       ToolType::None,    {146, 103,  78}},
    {"Iron Belt",         50,  BlockType::Air,       ToolType::None,    {128, 132, 145}},
```

In `src/Machines/MachineType.h`, replace `Belt,` with:

```cpp
    CopperBelt,
    IronBelt,
```

then add next to `isDrill`'s declaration:

```cpp
// True for CopperBelt or IronBelt - the two speed tiers of the same machine.
bool isBelt(MachineType type);
```

- [ ] **Step 4: Split the registry row and `itemForMachine` case, define `isBelt`**

In `src/Machines/MachineRegistry.cpp`, replace the `"Belt"` row with:

```cpp
    {"Copper Belt",       {146, 103,  78}, false, false, true,  0.0f,  0.5f * COPPER_TIER_SLOWDOWN, 1, 1},
    {"Iron Belt",         {128, 132, 145}, false, false, true,  0.0f,  0.5f,                         1, 1},
```

Replace the `Belt` case in `itemForMachine()` with:

```cpp
        case MachineType::CopperBelt:      return ItemType::CopperBelt;
        case MachineType::IronBelt:        return ItemType::IronBelt;
```

Add the helper next to `isDrill()`:

```cpp
bool isBelt(MachineType type)
{
    return type == MachineType::CopperBelt || type == MachineType::IronBelt;
}
```

- [ ] **Step 5: Split the craft recipe**

In `src/Machines/Recipes.cpp`, replace the `Belt` row with:

```cpp
    {ItemType::CopperBelt, {{{ItemType::CopperPlate, 2}, {ItemType::Stone, 2}}}, 1.0f, true},
    {ItemType::IronBelt,   {{{ItemType::IronPlate, 2}, {ItemType::Stone, 2}}},   1.0f, true},
```

- [ ] **Step 6: Swap the belt-facing guard, default `buildType`, and rendering case; remove F3**

In `src/Machines/MachineRenderer.cpp`, change the Belt case:

```cpp
    // OLD:
    // case MachineType::Belt:
    //     return side == m.facing;
    // NEW:
    case MachineType::CopperBelt:
    case MachineType::IronBelt:
        return side == m.facing;
```

In `src/Game/Game.h`, change:

```cpp
    // OLD: MachineType buildType = MachineType::Belt;
    // NEW:
    MachineType buildType = MachineType::CopperBelt;
```

In `src/Game/Game.cpp`, change **both** occurrences of the facing-reset guard (lines 333 and 754):

```cpp
    // OLD: if (buildType == MachineType::Belt)
    // NEW:
    if (isBelt(buildType))
```

Delete line 768 entirely:

```cpp
    if (key->code == Key::F3) setBuildType(MachineType::Belt);
```

- [ ] **Step 7: Fix the remaining test files (mechanical rename, no behavior change)**

Using the Edit tool with `replace_all: true`, replace `MachineType::Belt` with `MachineType::IronBelt` in each of:

- `tests/test_machines.cpp` (remaining generic placement/footprint/swap-and-pop tests using Belt as an arbitrary machine type)
- `tests/test_extraction.cpp`
- `tests/test_factory.cpp`
- `tests/test_placing.cpp`
- `tests/test_power.cpp`
- `tests/test_processing.cpp`
- `tests/test_transport.cpp`

- [ ] **Step 8: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 228 + 6 new = 234 (2 registry/helper tests, 1 status test, 2 recipe-cost tests, 1 belt-carry-timing test in `test_transport.cpp`).

- [ ] **Step 9: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 10: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h \
        src/Machines/MachineRegistry.cpp src/Machines/Recipes.cpp \
        src/Machines/MachineRenderer.cpp src/Game/Game.h src/Game/Game.cpp \
        tests/test_machines.cpp tests/test_machine_status.cpp tests/test_recipes.cpp \
        tests/test_extraction.cpp tests/test_factory.cpp tests/test_placing.cpp \
        tests/test_power.cpp tests/test_processing.cpp tests/test_transport.cpp
git commit -m "feat: split Belt into Copper Belt (slower) and Iron Belt (today's speed) tiers"
```

---

## Task 3: Chute tier split (`CopperChute` / `IronChute`)

**Files:**
- Modify: `src/Items/Items.h`, `src/Items/Items.cpp`
- Modify: `src/Machines/MachineType.h`
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Recipes.cpp`
- Modify: `src/Machines/Machines.cpp:457`
- Modify: `src/Machines/MachineRenderer.cpp`
- Modify: `src/Game/Game.cpp:769` (delete the line)
- Test: `tests/test_machines.cpp`, `tests/test_recipes.cpp`
- Test (mechanical rename only): `tests/test_transport.cpp`

Chute has the smallest test footprint of the four families - only `test_machines.cpp` (registry/helper/`itemForMachine`/`isFurniture` checks) and `test_transport.cpp` (one placement, "facing ignored by chutes") reference it today.

**Interfaces:**
- Consumes: `COPPER_TIER_SLOWDOWN` (Task 1).
- Produces: `MachineType::CopperChute`, `MachineType::IronChute`, `ItemType::CopperChute`, `ItemType::IronChute`, `bool isChute(MachineType)`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("a Copper Chute is COPPER_TIER_SLOWDOWN times slower than an Iron Chute")
{
    const float iron = machineInfo(MachineType::IronChute).actionTime;
    const float copper = machineInfo(MachineType::CopperChute).actionTime;

    CHECK(iron == doctest::Approx(0.5f));
    CHECK(copper == doctest::Approx(iron * COPPER_TIER_SLOWDOWN));
}

TEST_CASE("isChute is true for exactly Copper Chute and Iron Chute")
{
    CHECK(isChute(MachineType::CopperChute));
    CHECK(isChute(MachineType::IronChute));
    CHECK_FALSE(isChute(MachineType::None));
    CHECK_FALSE(isChute(MachineType::IronBelt));
}
```

Replace the Chute line inside `TEST_CASE("the machine registry has a valid row per type")`:

```cpp
    // OLD: CHECK(machineInfo(MachineType::Chute).transport);
    // NEW:
    CHECK(machineInfo(MachineType::CopperChute).transport);
    CHECK(machineInfo(MachineType::IronChute).transport);
```

`itemForMachine` today has no dedicated Chute assertion beyond the generic loop test - add one explicitly:

```cpp
TEST_CASE("itemForMachine maps both Chute tiers to their own item")
{
    CHECK(itemForMachine(MachineType::CopperChute) == ItemType::CopperChute);
    CHECK(itemForMachine(MachineType::IronChute) == ItemType::IronChute);
}
```

Replace the Chute line inside `TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, and Furnace")`:

```cpp
    // OLD: CHECK_FALSE(isFurniture(MachineType::Chute));
    // NEW:
    CHECK_FALSE(isFurniture(MachineType::CopperChute));
    CHECK_FALSE(isFurniture(MachineType::IronChute));
```

Replace the Chute line inside `tests/test_recipes.cpp`'s `TEST_CASE("every placeable machine has a matching craftable item")`:

```cpp
    // OLD: CHECK(itemInfo(ItemType::Chute).name == "Chute");
    // NEW:
    CHECK(itemInfo(ItemType::CopperChute).name == "Copper Chute");
    CHECK(itemInfo(ItemType::IronChute).name == "Iron Chute");
```

Bump the recipe count: `CHECK(all.size() == 11);` becomes `CHECK(all.size() == 12);`.

Append:

```cpp
TEST_CASE("the Copper Chute recipe costs 1 copper plate and 2 stone, no iron needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperChute; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::CopperPlate);
    CHECK(it->ingredients[0].count == 1);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(1.0f));
}

TEST_CASE("the Iron Chute recipe costs 1 iron plate and 2 stone, no copper needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronChute; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::IronPlate);
    CHECK(it->ingredients[0].count == 1);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(1.0f));
}
```

Append one more behavioral test to `tests/test_transport.cpp`:

```cpp
TEST_CASE("a Copper Chute drops its item straight down regardless of facing, just slower than an Iron Chute would")
{
    Machines m;
    World world;
    m.place(MachineType::CopperChute, 0, 0, Direction::Right); // facing ignored by chutes
    m.place(MachineType::IronBelt, 0, 1, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::Stone));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // A full extra 0.2s of margin past the Copper Chute's own actionTime, not
    // just +1 tick, so float accumulation over the run can never make this
    // assertion flaky.
    const int ticks =
        static_cast<int>((machineInfo(MachineType::CopperChute).actionTime + 0.2f) / step);
    for (int i = 0; i < ticks; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(0, 1)->carried == ItemType::Stone);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** — `'CopperChute': is not a member of 'MachineType'` (and similarly for `ItemType`, `isChute`).

- [ ] **Step 3: Split the enums**

In `src/Items/Items.h`, replace `Chute,` with:

```cpp
    CopperChute,
    IronChute,
```

In `src/Items/Items.cpp`, replace the `"Chute"` row with:

```cpp
    {"Copper Chute",      50,  BlockType::Air,       ToolType::None,    {136,  93,  68}},
    {"Iron Chute",        50,  BlockType::Air,       ToolType::None,    {118, 122, 135}},
```

In `src/Machines/MachineType.h`, replace `Chute,` with:

```cpp
    CopperChute,
    IronChute,
```

then add next to `isBelt`'s declaration:

```cpp
// True for CopperChute or IronChute - the two speed tiers of the same machine.
bool isChute(MachineType type);
```

- [ ] **Step 4: Split the registry row and `itemForMachine` case, define `isChute`**

In `src/Machines/MachineRegistry.cpp`, replace the `"Chute"` row with:

```cpp
    {"Copper Chute",      {136,  93,  68}, false, false, true,  0.0f,  0.5f * COPPER_TIER_SLOWDOWN, 1, 1},
    {"Iron Chute",        {118, 122, 135}, false, false, true,  0.0f,  0.5f,                         1, 1},
```

Replace the `Chute` case in `itemForMachine()` with:

```cpp
        case MachineType::CopperChute:     return ItemType::CopperChute;
        case MachineType::IronChute:       return ItemType::IronChute;
```

Add the helper next to `isBelt()`:

```cpp
bool isChute(MachineType type)
{
    return type == MachineType::CopperChute || type == MachineType::IronChute;
}
```

- [ ] **Step 5: Split the craft recipe**

In `src/Machines/Recipes.cpp`, replace the `Chute` row with:

```cpp
    {ItemType::CopperChute, {{{ItemType::CopperPlate, 1}, {ItemType::Stone, 2}}}, 1.0f, true},
    {ItemType::IronChute,   {{{ItemType::IronPlate, 1}, {ItemType::Stone, 2}}},    1.0f, true},
```

- [ ] **Step 6: Swap the direction check and rendering case; remove F4**

In `src/Machines/Machines.cpp:457`, change:

```cpp
    // OLD: const Direction dir = (m.type == MachineType::Chute) ? Direction::Down : m.facing;
    // NEW:
    const Direction dir = isChute(m.type) ? Direction::Down : m.facing;
```

In `src/Machines/MachineRenderer.cpp`, change the Chute case:

```cpp
    // OLD:
    // case MachineType::Chute:
    //     return side == Direction::Down;
    // NEW:
    case MachineType::CopperChute:
    case MachineType::IronChute:
        return side == Direction::Down;
```

In `src/Game/Game.cpp`, delete line 769 entirely:

```cpp
    if (key->code == Key::F4) setBuildType(MachineType::Chute);
```

- [ ] **Step 7: Fix the remaining test file (mechanical rename, no behavior change)**

Using the Edit tool with `replace_all: true`, replace `MachineType::Chute` with `MachineType::IronChute` in `tests/test_transport.cpp`.

- [ ] **Step 8: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 234 + 6 new = 240 (2 registry/helper tests, 1 `itemForMachine` test, 2 recipe-cost tests, 1 chute-direction-timing test in `test_transport.cpp`).

- [ ] **Step 9: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 10: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h \
        src/Machines/MachineRegistry.cpp src/Machines/Recipes.cpp \
        src/Machines/Machines.cpp src/Machines/MachineRenderer.cpp src/Game/Game.cpp \
        tests/test_machines.cpp tests/test_recipes.cpp tests/test_transport.cpp
git commit -m "feat: split Chute into Copper Chute (slower) and Iron Chute (today's speed) tiers"
```

---

## Task 4: Smelter tier split (`CopperSmelter` / `IronSmelter`) and the `speedMultiplier` field

**Files:**
- Modify: `src/Items/Items.h`, `src/Items/Items.cpp`
- Modify: `src/Machines/MachineType.h`
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Recipes.cpp`
- Modify: `src/Machines/Machines.cpp:163`, `:291`, `:542`, `:562`
- Modify: `src/Machines/MachineStatus.cpp:23-31`
- Modify: `src/Machines/MachineRenderer.cpp`
- Modify: `src/Hud/Hud.cpp:607-608`
- Modify: `src/Game/Game.cpp:770` (delete the line)
- Test: `tests/test_machines.cpp`, `tests/test_machine_status.cpp`, `tests/test_recipes.cpp`, `tests/test_processing.cpp`
- Test (mechanical rename only): `tests/test_extraction.cpp`, `tests/test_factory.cpp`, `tests/test_machine_inspect.cpp`, `tests/test_transport.cpp`

**Interfaces:**
- Consumes: `COPPER_TIER_SLOWDOWN` (Task 1).
- Produces: `MachineType::CopperSmelter`, `MachineType::IronSmelter`, `ItemType::CopperSmelter`, `ItemType::IronSmelter`, `bool isSmelter(MachineType)`, `MachineInfo::speedMultiplier`.

Unlike Drill/Belt/Chute, the Smelter's per-tick timing comes from the shared `SmeltRecipe` table (`recipe->seconds`), not from `MachineInfo::actionTime` directly - one Smelter processes either Copper Ore or Iron Ore. So this task adds one new `MachineInfo` field, `speedMultiplier` (default `1.0f`), applied at the point `recipe->seconds` is compared against `m.progress`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("Copper Smelter and Iron Smelter carry the expected speedMultiplier")
{
    CHECK(machineInfo(MachineType::IronSmelter).speedMultiplier == doctest::Approx(1.0f));
    CHECK(machineInfo(MachineType::CopperSmelter).speedMultiplier == doctest::Approx(COPPER_TIER_SLOWDOWN));
}

TEST_CASE("every non-Smelter machine defaults to a speedMultiplier of 1.0")
{
    for (int i = 1; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineType type = static_cast<MachineType>(i);
        if (isSmelter(type))
            continue;

        CHECK(machineInfo(type).speedMultiplier == doctest::Approx(1.0f));
    }
}

TEST_CASE("isSmelter is true for exactly Copper Smelter and Iron Smelter")
{
    CHECK(isSmelter(MachineType::CopperSmelter));
    CHECK(isSmelter(MachineType::IronSmelter));
    CHECK_FALSE(isSmelter(MachineType::None));
    CHECK_FALSE(isSmelter(MachineType::IronDrill));
}
```

Replace the Smelter line inside `TEST_CASE("the machine registry has a valid row per type")`:

```cpp
    // OLD: CHECK(machineInfo(MachineType::Smelter).consumer);
    // NEW:
    CHECK(machineInfo(MachineType::CopperSmelter).consumer);
    CHECK(machineInfo(MachineType::IronSmelter).consumer);
```

Replace the Smelter line inside `TEST_CASE("itemForMachine maps every placeable machine to its own item")`:

```cpp
    // OLD: CHECK(itemForMachine(MachineType::Smelter) == ItemType::Smelter);
    // NEW:
    CHECK(itemForMachine(MachineType::CopperSmelter) == ItemType::CopperSmelter);
    CHECK(itemForMachine(MachineType::IronSmelter) == ItemType::IronSmelter);
```

Replace the Smelter line inside `TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, and Furnace")`:

```cpp
    // OLD: CHECK_FALSE(isFurniture(MachineType::Smelter));
    // NEW:
    CHECK_FALSE(isFurniture(MachineType::CopperSmelter));
    CHECK_FALSE(isFurniture(MachineType::IronSmelter));
```

In `tests/test_machine_status.cpp`, change the two existing Smelter tests' type (lines 56 and 64):

```cpp
    // OLD: m.type = MachineType::Smelter; // input left empty
    // NEW:
    m.type = MachineType::IronSmelter; // input left empty
```

```cpp
    // OLD: m.type = MachineType::Smelter;
    // NEW:
    m.type = MachineType::IronSmelter;
```

and append a Copper-tier equivalent of `"a smelter mid-smelt shows progress against its recipe's time"`:

```cpp
TEST_CASE("a Copper Smelter mid-smelt shows progress against recipe seconds times its own multiplier")
{
    Machine m;
    m.type = MachineType::CopperSmelter;
    m.input = {ItemType::CopperOre, 1};

    const SmeltRecipe* recipe = smeltRecipeFor(ItemType::CopperOre);
    REQUIRE(recipe != nullptr);
    const float fullTime = recipe->seconds * machineInfo(MachineType::CopperSmelter).speedMultiplier;
    m.progress = fullTime * 0.5f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Progress);
    CHECK(status.fraction == doctest::Approx(0.5f));
}
```

Replace the Smelter line inside `tests/test_recipes.cpp`'s `TEST_CASE("every placeable machine has a matching craftable item")`:

```cpp
    // OLD: CHECK(itemInfo(ItemType::Smelter).name == "Smelter");
    // NEW:
    CHECK(itemInfo(ItemType::CopperSmelter).name == "Copper Smelter");
    CHECK(itemInfo(ItemType::IronSmelter).name == "Iron Smelter");
```

Bump the recipe count: `CHECK(all.size() == 12);` becomes `CHECK(all.size() == 13);`.

Append:

```cpp
TEST_CASE("the Copper Smelter recipe costs 3 copper plates and 5 stone, no iron needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperSmelter; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::CopperPlate);
    CHECK(it->ingredients[0].count == 3);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 5);
    CHECK(it->seconds == doctest::Approx(4.0f));
}

TEST_CASE("the Iron Smelter recipe costs 3 iron plates and 5 stone, no copper needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronSmelter; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::IronPlate);
    CHECK(it->ingredients[0].count == 3);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 5);
    CHECK(it->seconds == doctest::Approx(4.0f));
}
```

Append to `tests/test_processing.cpp` (needs `smeltRecipeFor`/`SmeltRecipe`, so add `#include "Machines/Recipes.h"` at the top alongside the existing includes - `MachineType`/`machineInfo`/`COPPER_TIER_SLOWDOWN` are already available transitively via `Machines/Machines.h`):

```cpp
TEST_CASE("a Copper Smelter takes COPPER_TIER_SLOWDOWN times longer than an Iron Smelter on the same recipe")
{
    World world;

    Machines ironMachines;
    ironMachines.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    ironMachines.place(MachineType::IronSmelter, 1, 0, Direction::Right);
    REQUIRE(ironMachines.tryInsert(0, 0, ItemType::Coal));
    REQUIRE(ironMachines.tryInsert(1, 0, ItemType::CopperOre));

    Machines copperMachines;
    copperMachines.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    copperMachines.place(MachineType::CopperSmelter, 1, 0, Direction::Right);
    REQUIRE(copperMachines.tryInsert(0, 0, ItemType::Coal));
    REQUIRE(copperMachines.tryInsert(1, 0, ItemType::CopperOre));

    const SmeltRecipe* recipe = smeltRecipeFor(ItemType::CopperOre);
    REQUIRE(recipe != nullptr);

    const float ironTime = recipe->seconds * machineInfo(MachineType::IronSmelter).speedMultiplier;
    const float copperTime = recipe->seconds * machineInfo(MachineType::CopperSmelter).speedMultiplier;

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Comfortably past the Iron Smelter's own cycle (a full extra second of
    // margin for the power-on lag - Machines::tick() runs updatePower() before
    // tickGenerators(), so a freshly-fuelled generator has a one-tick lag - and
    // for float accumulation), but still comfortably short of the Copper
    // Smelter's longer cycle.
    const int phase1Ticks = static_cast<int>((ironTime + 1.0f) / step);
    for (int i = 0; i < phase1Ticks; ++i)
    {
        ironMachines.tick(world, step, mined);
        copperMachines.tick(world, step, mined);
    }

    CHECK_FALSE(ironMachines.at(1, 0)->output.empty());
    CHECK(copperMachines.at(1, 0)->output.empty());

    // Comfortably past the Copper Smelter's own (longer) cycle too.
    const int totalTicksForCopper = static_cast<int>((copperTime + 1.0f) / step);
    for (int i = phase1Ticks; i < totalTicksForCopper; ++i)
        copperMachines.tick(world, step, mined);

    CHECK_FALSE(copperMachines.at(1, 0)->output.empty());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** — `'CopperSmelter': is not a member of 'MachineType'` (and similarly for `ItemType`, `isSmelter`, `speedMultiplier`).

- [ ] **Step 3: Split the enums**

In `src/Items/Items.h`, replace `Smelter,` with:

```cpp
    CopperSmelter,
    IronSmelter,
```

In `src/Items/Items.cpp`, replace the `"Smelter"` row with:

```cpp
    {"Copper Smelter",    10,  BlockType::Air,       ToolType::None,    {201, 103,  63}},
    {"Iron Smelter",      10,  BlockType::Air,       ToolType::None,    {183, 132, 130}},
```

In `src/Machines/MachineType.h`, replace `Smelter,` with:

```cpp
    CopperSmelter,
    IronSmelter,
```

then add next to `isChute`'s declaration:

```cpp
// True for CopperSmelter or IronSmelter - the two speed tiers of the same machine.
bool isSmelter(MachineType type);
```

then add the new field as the **last** member of `MachineInfo` (after `height`), so every existing 9-value row keeps compiling unchanged:

```cpp
struct MachineInfo
{
    std::string_view name;
    BlockColor color;

    bool generator;
    bool consumer;
    bool transport;

    float powerRating;
    float actionTime;

    int width;
    int height;

    // Smelter-family only: a Smelter's per-recipe seconds (from SmeltRecipe, not
    // actionTime) are multiplied by this. Every other machine type leaves it at
    // the default 1.0 and is unaffected.
    float speedMultiplier = 1.0f;
};
```

- [ ] **Step 4: Split the registry row (with explicit `speedMultiplier`) and `itemForMachine` case, define `isSmelter`**

In `src/Machines/MachineRegistry.cpp`, replace the `"Smelter"` row with:

```cpp
    {"Copper Smelter",    {201, 103,  63}, false, true,  false, 5.0f,  0.0f, 1, 1, COPPER_TIER_SLOWDOWN},
    {"Iron Smelter",      {183, 132, 130}, false, true,  false, 5.0f,  0.0f, 1, 1, 1.0f},
```

(Every other row in this table is untouched - it keeps its original 9 values and picks up `speedMultiplier`'s `1.0f` default automatically.)

Replace the `Smelter` case in `itemForMachine()` with:

```cpp
        case MachineType::CopperSmelter:   return ItemType::CopperSmelter;
        case MachineType::IronSmelter:     return ItemType::IronSmelter;
```

Add the helper next to `isChute()`:

```cpp
bool isSmelter(MachineType type)
{
    return type == MachineType::CopperSmelter || type == MachineType::IronSmelter;
}
```

- [ ] **Step 5: Split the craft recipe**

In `src/Machines/Recipes.cpp`, replace the `Smelter` row with:

```cpp
    {ItemType::CopperSmelter, {{{ItemType::CopperPlate, 3}, {ItemType::Stone, 5}}}, 4.0f, true},
    {ItemType::IronSmelter,   {{{ItemType::IronPlate, 3}, {ItemType::Stone, 5}}},   4.0f, true},
```

- [ ] **Step 6: Swap the tryInsert/idleReason/tick guards to `isSmelter()`, apply the multiplier**

In `src/Machines/Machines.cpp:163`, change:

```cpp
    // OLD: if (m->type == MachineType::Smelter)
    // NEW:
    if (isSmelter(m->type))
```

In `src/Machines/Machines.cpp:291`, change:

```cpp
    // OLD: if (m.type == MachineType::Smelter)
    // NEW:
    if (isSmelter(m.type))
```

In `src/Machines/Machines.cpp:542`, change:

```cpp
    // OLD: if (m.type != MachineType::Smelter)
    // NEW:
    if (!isSmelter(m.type))
```

In `src/Machines/Machines.cpp:562`, change:

```cpp
    // OLD: if (m.progress >= recipe->seconds)
    // NEW:
    if (m.progress >= recipe->seconds * machineInfo(m.type).speedMultiplier)
```

In `src/Machines/MachineStatus.cpp`, change the Smelter branch:

```cpp
    // OLD:
    // else if (m.type == MachineType::Smelter)
    // {
    //     const SmeltRecipe* recipe = m.input.empty() ? nullptr : smeltRecipeFor(m.input.type);
    //
    //     if (recipe != nullptr)
    //     {
    //         status.bar = MachineBar::Progress;
    //         status.fraction = std::clamp(m.progress / recipe->seconds, 0.0f, 1.0f);
    //     }
    // }
    // NEW:
    else if (isSmelter(m.type))
    {
        const SmeltRecipe* recipe = m.input.empty() ? nullptr : smeltRecipeFor(m.input.type);

        if (recipe != nullptr)
        {
            status.bar = MachineBar::Progress;
            status.fraction = std::clamp(
                m.progress / (recipe->seconds * machineInfo(m.type).speedMultiplier), 0.0f, 1.0f);
        }
    }
```

In `src/Machines/MachineRenderer.cpp`, change the Drill/Smelter case group one more time (it already has the two Drill entries from Task 1):

```cpp
    // OLD:
    // case MachineType::CopperDrill:
    // case MachineType::IronDrill:
    // case MachineType::Smelter:
    //     return side != m.facing;
    // NEW:
    case MachineType::CopperDrill:
    case MachineType::IronDrill:
    case MachineType::CopperSmelter:
    case MachineType::IronSmelter:
        return side != m.facing;
```

In `src/Hud/Hud.cpp`, change:

```cpp
    // OLD: if (machine.type == MachineType::Smelter)
    // NEW:
    if (isSmelter(machine.type))
```

In `src/Game/Game.cpp`, delete line 770 entirely:

```cpp
    if (key->code == Key::F5) setBuildType(MachineType::Smelter);
```

- [ ] **Step 7: Fix the remaining test files (mechanical rename, no behavior change)**

Using the Edit tool with `replace_all: true`, replace `MachineType::Smelter` with `MachineType::IronSmelter` in each of:

- `tests/test_machines.cpp` (remaining generic placement tests using Smelter as an arbitrary machine type)
- `tests/test_extraction.cpp`
- `tests/test_factory.cpp`
- `tests/test_machine_inspect.cpp`
- `tests/test_processing.cpp` (the two pre-existing Smelter tick tests, not the new multiplier test just added, which already uses the correct names)
- `tests/test_transport.cpp`

- [ ] **Step 8: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 240 + 7 new = 247 (2 registry/helper tests, 1 `isSmelter` test, 1 status test, 2 recipe-cost tests, 1 smelt-multiplier-timing test in `test_processing.cpp`).

- [ ] **Step 9: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 10: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h \
        src/Machines/MachineRegistry.cpp src/Machines/Recipes.cpp \
        src/Machines/Machines.cpp src/Machines/MachineStatus.cpp \
        src/Machines/MachineRenderer.cpp src/Hud/Hud.cpp src/Game/Game.cpp \
        tests/test_machines.cpp tests/test_machine_status.cpp tests/test_recipes.cpp \
        tests/test_processing.cpp tests/test_extraction.cpp tests/test_factory.cpp \
        tests/test_machine_inspect.cpp tests/test_transport.cpp
git commit -m "feat: split Smelter into Copper Smelter (slower) and Iron Smelter (today's speed) tiers"
```

---

## Manual verification (after all four tasks)

`MachineRenderer.cpp`/`Hud.cpp`/`Game.cpp` are not linked into the test binary, so the following need a manual pass via this project's run/verify workflow:

- Build mode palette shows all 8 new machine types (Copper/Iron × Drill/Belt/Chute/Smelter) once held, with distinguishable tier colors.
- Placing a Copper Belt/Iron Belt, rotating with `R`, and confirming the belt-facing restriction (horizontal-only) still applies to both.
- A Copper Smelter and an Iron Smelter placed side by side, both fed coal and ore, visibly finish at different rates.
- Tooltips ("Mines:"/"Smelts:") still render for all four Drill/Smelter variants.
- F1 (Burner Generator) and F6 (Item Acceptor) still work; F2-F5 no longer select anything (scroll-cycle/palette-click is the only way to pick a Drill/Belt/Chute/Smelter tier).
- Crafting each of the 8 new items at a placed Crafting Table with only the stated ingredients in the bag (no cross-tier metal) succeeds; missing the required plate blocks it.
