# Crafting Menu and Inventory-Filtered Build Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pressing E opens a hand-crafting menu (a bootstrap "Crafting Table" recipe reachable anywhere, and — once a Crafting Table is placed — an advanced menu of machine recipes on that table), and build mode's palette only ever shows machine types the player is actually holding, with counts, consuming one item per placement and refunding one on removal.

**Architecture:** Seven new `ItemType`s (one per placeable `MachineType`) link the bag to the build palette via a new `itemForMachine()` lookup. A new `CraftRecipe` table (mirroring the existing `SmeltRecipe` pattern) drives a single-slot, timed, background-ticking craft process on `Game`. The Crafting Table becomes the game's first multi-tile machine (2×1), which requires `Machines::place()`/`remove()` to become footprint-aware instead of assuming one tile per machine — the riskiest piece, isolated into its own task with dedicated swap-and-pop regression coverage.

**Tech Stack:** C++20, SFML 3, doctest, CMake + Visual Studio generator (existing project stack — no new dependencies).

## Global Constraints

- Build (tests): `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`. `cmake` is **not** on PATH; the full path is `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
- Build (game): `cmake --build C:\Litharia\build --config Debug --target Litharia`.
- Test binary: `C:\Litharia\build\Debug\Litharia_tests.exe`. Game binary: `C:\Litharia\build\Debug\Litharia.exe`.
- Baseline before this work: **186 test cases, 186 passed, 0 failed**.
- Registry arrays (`Items.cpp`'s `registry`, `MachineRegistry.cpp`'s `registry`) are positional aggregate-initialized `std::array`s indexed by their enum — **order must match the enum**, and every existing row must gain any new trailing field added to the struct.
- `Machines.cpp`, `Recipes.cpp`, `MachineRegistry.cpp`, `Items.cpp`, `Inventory.cpp` compile into `Litharia_core` and are reachable from `Litharia_tests` (no SFML Graphics/Window). `Hud.cpp`, `Game.cpp`, `MachineRenderer.cpp` compile only into the `Litharia` executable (they touch SFML Graphics/Window) and are **not** linked into the test binary — consistent with today's codebase, changes there are verified by running the game, not by doctest.
- Spec: `docs/superpowers/specs/2026-07-16-crafting-menu-and-build-filter-design.md`.

## Note on spec-listed tests that are deliberately manual, not doctest

The spec's Testing section asks for coverage of "recipe affordability gate,
ingredient deduction on start, progress-to-completion timing, output landing
in the bag, overflow-to-ground-drop when the bag is full, second-click-while-
crafting no-op" and the build-palette's filter/consume/refund behavior. All of
that logic lives in `Game::startCraft`/`updateCrafting`/`placeMachineAtCursor`/
`removeMachineAtCursor`/`cycleBuildType`, and its rendering counterpart in
`Hud::drawCraftPanel`/`drawBuildPalette` — both `Game.cpp` and `Hud.cpp` are
SFML-Graphics/Window code that only compiles into the `Litharia` executable,
never into `Litharia_core`/`Litharia_tests` (see Global Constraints above).
This is the existing project boundary, not a new one introduced by this
work — `hitTestChestButton`, `drawBuildPalette`, and `placeMachineAtCursor`'s
predecessor were never doctest-covered either. So every one of those
spec-listed behaviors is exercised instead by the manual-verification steps in
Tasks 7, 9, and 10, which walk through each scenario (afford/can't-afford,
mid-craft double-click, full-bag overflow, palette counts, place/destroy
refund) against the running game. Everything the spec asks to unit-test that
*does* live in `Litharia_core` — the recipe table's shape (Task 4) and the
multi-tile placement/removal invariants (Task 5) — keeps its doctest coverage
as specified.

---

## Task 1: Craftable items for every placeable machine

Adds the 7 new `ItemType`s (`CraftingTable`, `BurnerGenerator`, `Drill`, `Belt`, `Chute`, `Smelter`, `Chest`) that later tasks build on: recipes produce them, the build palette counts them, placing/removing a machine consumes/refunds them.

**Files:**
- Modify: `src/Items/Items.h` (`enum class ItemType`, ~line 11-26)
- Modify: `src/Items/Items.cpp` (`registry`, ~line 9-22)
- Test: `tests/test_recipes.cpp`

**Interfaces:**
- Produces: `ItemType::CraftingTable`, `ItemType::BurnerGenerator`, `ItemType::Drill`, `ItemType::Belt`, `ItemType::Chute`, `ItemType::Smelter`, `ItemType::Chest` — every later task's recipes/build-palette/consumption code references these by name.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("every placeable machine has a matching craftable item")
{
    CHECK(itemInfo(ItemType::CraftingTable).name == "Crafting Table");
    CHECK(itemInfo(ItemType::BurnerGenerator).name == "Burner Generator");
    CHECK(itemInfo(ItemType::Drill).name == "Drill");
    CHECK(itemInfo(ItemType::Belt).name == "Belt");
    CHECK(itemInfo(ItemType::Chute).name == "Chute");
    CHECK(itemInfo(ItemType::Smelter).name == "Smelter");
    CHECK(itemInfo(ItemType::Chest).name == "Chest");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: **compile error** — `'CraftingTable': is not a member of 'ItemType'` (and similarly for the others).

- [ ] **Step 3: Add the new enumerators**

In `src/Items/Items.h`, change the `ItemType` enum (append before `Count`, after `OakLog`):

```cpp
enum class ItemType : std::uint8_t
{
    None,
    Dirt,
    Stone,
    CopperOre,
    IronOre,
    Coal,
    CopperPlate,
    IronPlate,
    Pickaxe,
    Axe,
    OakLog,
    CraftingTable,
    BurnerGenerator,
    Drill,
    Belt,
    Chute,
    Smelter,
    Chest,

    Count
};
```

- [ ] **Step 4: Add matching registry rows**

In `src/Items/Items.cpp`, replace the `registry` array with (order matches the enum exactly; the 7 new rows are appended, colors match each machine's `MachineInfo::color` swatch so bag icon and placed machine read as the same object):

```cpp
constexpr std::array<ItemInfo, static_cast<std::size_t>(ItemType::Count)> registry = {{
    //  name                 maxStack  placeBlock            toolType           iconColor
    {"Nothing",           0,  BlockType::Air,       ToolType::None,    {  0,   0,   0}},
    {"Dirt",             99,  BlockType::Dirt,      ToolType::None,    {134,  89,  52}},
    {"Stone",            99,  BlockType::Stone,     ToolType::None,    {112, 112, 118}},
    {"Copper Ore",       99,  BlockType::CopperOre, ToolType::None,    {201, 116,  56}},
    {"Iron Ore",         99,  BlockType::IronOre,   ToolType::None,    {166, 174, 190}},
    {"Coal",             99,  BlockType::Coal,      ToolType::None,    { 44,  44,  50}},
    {"Copper Plate",     99,  BlockType::Air,       ToolType::None,    {224, 150,  90}},
    {"Iron Plate",       99,  BlockType::Air,       ToolType::None,    {205, 210, 218}},
    {"Pickaxe",           1,  BlockType::Air,       ToolType::Pickaxe, {170, 170, 180}},
    {"Axe",               1,  BlockType::Air,       ToolType::Axe,     {160,  90,  50}},
    {"Oak Log",          99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
    {"Crafting Table",   10,  BlockType::Air,       ToolType::None,    {120,  80,  40}},
    {"Burner Generator", 10,  BlockType::Air,       ToolType::None,    {190, 120,  60}},
    {"Drill",            10,  BlockType::Air,       ToolType::None,    {150, 150, 160}},
    {"Belt",             50,  BlockType::Air,       ToolType::None,    { 90,  90, 100}},
    {"Chute",            50,  BlockType::Air,       ToolType::None,    { 70,  70,  80}},
    {"Smelter",          10,  BlockType::Air,       ToolType::None,    {200,  90,  70}},
    {"Chest",            10,  BlockType::Air,       ToolType::None,    {140,  95,  50}},
}};
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: builds clean; `test cases: 187 | 187 passed | 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp tests/test_recipes.cpp
git commit -m "feat: add a craftable item for every placeable machine"
```

---

## Task 2: `Inventory::removeOne(ItemType)`

The by-type counterpart to the existing by-slot `removeOne(int)` and to `count(ItemType)`. Later tasks use it to deduct recipe ingredients and to consume the item a placed machine costs.

**Files:**
- Modify: `src/Items/Inventory.h` (~line 33, alongside the existing `removeOne(int)`)
- Modify: `src/Items/Inventory.cpp` (~line 68-84)
- Test: `tests/test_inventory.cpp`

**Interfaces:**
- Produces: `bool Inventory::removeOne(ItemType type)` — removes one unit of `type` from the first slot holding it (clearing the slot if it reaches zero), returns `false` and does nothing if the bag holds none.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_inventory.cpp`:

```cpp
TEST_CASE("removeOne(ItemType) decrements the first matching slot")
{
    Inventory bag;
    bag.add({ItemType::CopperOre, 3});

    CHECK(bag.removeOne(ItemType::CopperOre));
    CHECK(bag.count(ItemType::CopperOre) == 2);
}

TEST_CASE("removeOne(ItemType) clears a slot that reaches zero")
{
    Inventory bag;
    bag.add({ItemType::Stone, 1});

    CHECK(bag.removeOne(ItemType::Stone));
    CHECK(bag.slot(0).empty());
}

TEST_CASE("removeOne(ItemType) is a no-op when the bag doesn't hold it")
{
    Inventory bag;
    bag.add({ItemType::Dirt, 1});

    CHECK_FALSE(bag.removeOne(ItemType::Stone));
    CHECK(bag.count(ItemType::Dirt) == 1);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: **compile error** — `'removeOne': function does not take 1 arguments` / no overload takes `ItemType`.

- [ ] **Step 3: Declare the overload**

In `src/Items/Inventory.h`, directly below the existing `bool removeOne(int slot);` declaration and its comment, add:

```cpp
    // Removes one unit of `type` from the first slot holding it, clearing that
    // slot if it reaches zero. False and no-op if the bag holds none of it -
    // the by-type counterpart to the by-slot removeOne(int) above.
    bool removeOne(ItemType type);
```

- [ ] **Step 4: Implement it**

In `src/Items/Inventory.cpp`, directly below the existing `bool Inventory::removeOne(int slot)` definition, add:

```cpp
bool Inventory::removeOne(ItemType type)
{
    for (std::size_t i = 0; i < slots.size(); ++i)
        if (slots[i].type == type)
            return removeOne(static_cast<int>(i));

    return false;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 190 | 190 passed | 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/Items/Inventory.h src/Items/Inventory.cpp tests/test_inventory.cpp
git commit -m "feat: add Inventory::removeOne(ItemType)"
```

---

## Task 3: Crafting Table machine type, footprint width, and item linkage

Registers `MachineType::CraftingTable` (2-tile-wide — the data half of multi-tile support; the placement/removal *behavior* is Task 5), and adds `itemForMachine()`, the lookup every later task uses to go from a machine type to the item that represents it in the bag.

**Files:**
- Modify: `src/Machines/MachineType.h` (`enum class MachineType`, `MachineInfo`, ~line 25-56)
- Modify: `src/Machines/MachineRegistry.cpp` (`registry`, ~line 9-18)
- Test: `tests/test_machines.cpp`

**Interfaces:**
- Consumes: `ItemType::CraftingTable` / `BurnerGenerator` / `Drill` / `Belt` / `Chute` / `Smelter` / `Chest` (Task 1).
- Produces: `MachineType::CraftingTable`; `MachineInfo::width` (`int`, tile footprint along X, growing rightward from the placed `(x, y)` — `1` for every existing type, `2` for `CraftingTable`); `ItemType itemForMachine(MachineType type)`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("the machine registry declares a footprint width, 1 for every type except the Crafting Table")
{
    for (int i = 1; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineType type = static_cast<MachineType>(i);
        const int width = machineInfo(type).width;

        if (type == MachineType::CraftingTable)
            CHECK(width == 2);
        else
            CHECK(width == 1);
    }
}

TEST_CASE("itemForMachine maps every placeable machine to its own item")
{
    CHECK(itemForMachine(MachineType::BurnerGenerator) == ItemType::BurnerGenerator);
    CHECK(itemForMachine(MachineType::Drill) == ItemType::Drill);
    CHECK(itemForMachine(MachineType::Belt) == ItemType::Belt);
    CHECK(itemForMachine(MachineType::Chute) == ItemType::Chute);
    CHECK(itemForMachine(MachineType::Smelter) == ItemType::Smelter);
    CHECK(itemForMachine(MachineType::Chest) == ItemType::Chest);
    CHECK(itemForMachine(MachineType::CraftingTable) == ItemType::CraftingTable);
}

TEST_CASE("itemForMachine(None) has no matching item")
{
    CHECK(itemForMachine(MachineType::None) == ItemType::None);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: **compile error** — `'CraftingTable': is not a member of 'MachineType'` / `'width': is not a member of 'MachineInfo'` / `'itemForMachine': identifier not found`.

- [ ] **Step 3: Add the enumerator, the `width` field, and the `itemForMachine` declaration**

In `src/Machines/MachineType.h`, change the enum:

```cpp
enum class MachineType : std::uint8_t
{
    None,
    BurnerGenerator,
    Drill,
    Belt,
    Chute,
    Smelter,
    Chest,
    CraftingTable,

    Count
};
```

Change `MachineInfo` to add `width` as its last member:

```cpp
struct MachineInfo
{
    std::string_view name;
    BlockColor color;

    bool generator; // supplies power to the machines touching it
    bool consumer;  // draws power from a generator touching it
    bool transport; // belt/chute: carries one item toward its facing (chute: down)

    float powerRating; // supply if generator, demand if consumer
    float actionTime;  // drill: seconds per ore; transport: transfer interval

    // Tile footprint along X, starting at the machine's placed (x, y) and
    // growing rightward. 1 for every machine except the Crafting Table.
    int width;
};
```

Add the declaration below `const MachineInfo& machineInfo(MachineType type);`:

```cpp
// The item that represents `type` in the bag - what the build palette counts,
// what placing consumes, what removing refunds. None for MachineType::None.
ItemType itemForMachine(MachineType type);
```

- [ ] **Step 4: Update the registry and implement `itemForMachine`**

In `src/Machines/MachineRegistry.cpp`, replace the `registry` array (every existing row gains a trailing `width` of `1`; the new `CraftingTable` row is `2`):

```cpp
constexpr std::array<MachineInfo, static_cast<std::size_t>(MachineType::Count)> registry = {{
    //  name                 color            gen    con    trans  power  action  width
    {"None",              {  0,   0,   0}, false, false, false, 0.0f,  0.0f,  1},
    {"Burner Generator",  {190, 120,  60}, true,  false, false, 10.0f, 0.0f,  1},
    {"Drill",             {150, 150, 160}, false, true,  false, 5.0f,  3.0f,  1},
    {"Belt",              { 90,  90, 100}, false, false, true,  0.0f,  0.5f,  1},
    {"Chute",             { 70,  70,  80}, false, false, true,  0.0f,  0.5f,  1},
    {"Smelter",           {200,  90,  70}, false, true,  false, 5.0f,  0.0f,  1},
    {"Chest",             {140,  95,  50}, false, false, false, 0.0f,  0.0f,  1},
    {"Crafting Table",    {120,  80,  40}, false, false, false, 0.0f,  0.0f,  2},
}};
```

At the bottom of the same file, below `formatDrillOreList`, add:

```cpp
ItemType itemForMachine(MachineType type)
{
    switch (type)
    {
        case MachineType::BurnerGenerator: return ItemType::BurnerGenerator;
        case MachineType::Drill:           return ItemType::Drill;
        case MachineType::Belt:            return ItemType::Belt;
        case MachineType::Chute:           return ItemType::Chute;
        case MachineType::Smelter:         return ItemType::Smelter;
        case MachineType::Chest:           return ItemType::Chest;
        case MachineType::CraftingTable:   return ItemType::CraftingTable;
        default:                           return ItemType::None;
    }
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 193 | 193 passed | 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/MachineType.h src/Machines/MachineRegistry.cpp tests/test_machines.cpp
git commit -m "feat: register the Crafting Table machine and add itemForMachine"
```

---

## Task 4: Hand-craft recipe table

The `CraftRecipe` data — what each of the 7 items costs and how long it takes — that Task 9's crafting flow reads from.

**Files:**
- Modify: `src/Machines/Recipes.h` (add `CraftIngredient`, `CraftRecipe`, `allCraftRecipes()`)
- Modify: `src/Machines/Recipes.cpp` (add the table + accessor)
- Test: `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: `ItemType::OakLog`, `Stone`, `IronPlate`, `CopperPlate` (existing raw materials); the 7 items from Task 1.
- Produces: `struct CraftIngredient { ItemType item; int count; }`; `struct CraftRecipe { ItemType output; std::array<CraftIngredient, 2> ingredients; float seconds; bool requiresCraftingTable; }`; `std::span<const CraftRecipe> allCraftRecipes()`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_recipes.cpp` (add `#include <algorithm>` at the top alongside the existing `#include "doctest.h"` if not already present):

```cpp
TEST_CASE("allCraftRecipes exposes every craftable item exactly once")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    CHECK(all.size() == 7);
}

TEST_CASE("the Crafting Table recipe costs 15 oak logs and needs no table")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CraftingTable; });

    REQUIRE(it != all.end());
    CHECK_FALSE(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::OakLog);
    CHECK(it->ingredients[0].count == 15);
}

TEST_CASE("every recipe but the Crafting Table requires a placed table")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();

    for (const CraftRecipe& r : all)
        if (r.output != ItemType::CraftingTable)
            CHECK(r.requiresCraftingTable);
}

TEST_CASE("every recipe costs a positive amount of at least one ingredient")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();

    for (const CraftRecipe& r : all)
    {
        CHECK(r.ingredients[0].item != ItemType::None);
        CHECK(r.ingredients[0].count > 0);
        CHECK(r.seconds > 0.0f);
    }
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: **compile error** — `'CraftRecipe': undeclared identifier` / `'allCraftRecipes': identifier not found`.

- [ ] **Step 3: Declare the recipe types and accessor**

In `src/Machines/Recipes.h`, add `#include <array>` to the includes, then append below the existing `formatSmeltRecipeList` declaration:

```cpp
// One ingredient a hand-craft recipe consumes. count == 0 (with item ==
// ItemType::None) marks an unused slot - every recipe here needs at most 2.
struct CraftIngredient
{
    ItemType item = ItemType::None;
    int count = 0;
};

// One hand-craft recipe: up to 2 ingredients from the bag become one output
// item after `seconds`. requiresCraftingTable is false only for the Crafting
// Table itself - the one recipe reachable with no table placed yet.
struct CraftRecipe
{
    ItemType output;
    std::array<CraftIngredient, 2> ingredients;
    float seconds;
    bool requiresCraftingTable;
};

// Every defined hand-craft recipe.
std::span<const CraftRecipe> allCraftRecipes();
```

- [ ] **Step 4: Define the recipe table**

In `src/Machines/Recipes.cpp`, inside the existing anonymous namespace, below the `recipes` array, add:

```cpp
constexpr std::array<CraftRecipe, 7> craftRecipes = {{
    {ItemType::CraftingTable,   {{{ItemType::OakLog, 15}, {}}},                          3.0f, false},
    {ItemType::Chest,           {{{ItemType::OakLog, 8}, {}}},                           2.0f, true},
    {ItemType::Belt,            {{{ItemType::IronPlate, 1}, {ItemType::CopperPlate, 1}}}, 1.0f, true},
    {ItemType::Chute,           {{{ItemType::Stone, 2}, {}}},                            1.0f, true},
    {ItemType::BurnerGenerator, {{{ItemType::Stone, 5}, {ItemType::IronPlate, 2}}},       3.0f, true},
    {ItemType::Drill,           {{{ItemType::IronPlate, 5}, {ItemType::CopperPlate, 2}}}, 4.0f, true},
    {ItemType::Smelter,         {{{ItemType::Stone, 5}, {ItemType::CopperPlate, 3}}},     4.0f, true},
}};
```

Below `std::span<const SmeltRecipe> allSmeltRecipes() { return recipes; }`, add:

```cpp
std::span<const CraftRecipe> allCraftRecipes()
{
    return craftRecipes;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 197 | 197 passed | 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Recipes.h src/Machines/Recipes.cpp tests/test_recipes.cpp
git commit -m "feat: add the hand-craft recipe table"
```

---

## Task 5: Multi-tile placement and removal in `Machines`

The Crafting Table is the first machine wider than one tile. `Machines::place()`/`remove()` currently assume exactly one `byTile` entry per machine; this task makes both footprint-aware. This is the sharpest task in the plan — get the swap-and-pop fixup right or a relocated multi-tile machine silently leaks a stale `byTile` entry.

**Files:**
- Modify: `src/Machines/Machine.h` (comment only, ~line 9-10)
- Modify: `src/Machines/Machines.cpp` (`place()`, `remove()`, ~line 54-100)
- Test: `tests/test_machines.cpp`

**Interfaces:**
- Consumes: `MachineInfo::width` (Task 3).
- Produces: no signature changes — `Machines::place(MachineType, int, int, Direction)`, `canPlace(int, int)`, `remove(int, int)`, `at(int, int)` all keep their existing signatures and single-tile-query semantics; only their internal behavior for multi-tile types changes.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("a 2-wide machine occupies both tiles it spans")
{
    Machines machines;
    Machine* table = machines.place(MachineType::CraftingTable, 4, 4, Direction::Right);

    REQUIRE(table != nullptr);
    CHECK(machines.at(4, 4) == table);
    CHECK(machines.at(5, 4) == table);
    CHECK(machines.at(6, 4) == nullptr);
    CHECK_FALSE(machines.canPlace(4, 4));
    CHECK_FALSE(machines.canPlace(5, 4));
}

TEST_CASE("a 2-wide machine cannot be placed if either tile is taken")
{
    Machines machines;
    REQUIRE(machines.place(MachineType::Belt, 5, 4, Direction::Right) != nullptr);

    // (4,4) is free but (5,4) is not - the whole placement must fail, not
    // just settle for the tile that collided.
    CHECK(machines.place(MachineType::CraftingTable, 4, 4, Direction::Right) == nullptr);
    CHECK(machines.count() == 1);
    CHECK(machines.at(4, 4) == nullptr);
}

TEST_CASE("removing a 2-wide machine via either tile clears both")
{
    Machines machines;
    machines.place(MachineType::CraftingTable, 4, 4, Direction::Right);

    REQUIRE(machines.remove(5, 4)); // remove via the *second* tile, not the origin
    CHECK(machines.count() == 0);
    CHECK(machines.at(4, 4) == nullptr);
    CHECK(machines.at(5, 4) == nullptr);
}

TEST_CASE("swap-and-pop relocates every tile of a multi-tile machine, not just its origin")
{
    Machines machines;
    machines.place(MachineType::Belt, 0, 0, Direction::Right);          // index 0, doomed
    machines.place(MachineType::Belt, 1, 0, Direction::Right);          // index 1, untouched survivor
    machines.place(MachineType::CraftingTable, 8, 8, Direction::Right); // index 2 -> swapped into slot 0

    REQUIRE(machines.count() == 3);
    REQUIRE(machines.remove(0, 0));

    // The crafting table (previously last in the vector) now lives at index 0,
    // but must still be reachable from BOTH of its tiles.
    Machine* table = machines.at(8, 8);
    REQUIRE(table != nullptr);
    CHECK(table->type == MachineType::CraftingTable);
    CHECK(machines.at(9, 8) == table);

    // The untouched survivor is still exactly where it was.
    REQUIRE(machines.at(1, 0) != nullptr);
    CHECK(machines.at(1, 0)->type == MachineType::Belt);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: builds (no new symbols needed), but the new test cases **FAIL** — e.g. `CHECK(machines.at(5, 4) == table)` fails because `place()` only registers `(4,4)` today.

- [ ] **Step 3: Make `place()` footprint-aware**

In `src/Machines/Machines.cpp`, replace `Machines::place()`:

```cpp
Machine* Machines::place(MachineType type, int x, int y, Direction facing)
{
    const int width = machineInfo(type).width;

    for (int dx = 0; dx < width; ++dx)
        if (!canPlace(x + dx, y))
            return nullptr;

    Machine m;
    m.type = type;
    m.x = x;
    m.y = y;
    m.facing = facing;
    // facing itself is excluded from output (it's reserved for input); start the
    // rotation just past it.
    m.outputCursor = rotateCW(facing);

    m.placedSeq = nextSeq++;

    if (type == MachineType::Chest)
        m.storage = Inventory(CHEST_SLOTS);

    machines.push_back(m);
    const int index = static_cast<int>(machines.size()) - 1;

    for (int dx = 0; dx < width; ++dx)
        byTile[key(x + dx, y)] = index;

    return &machines[index];
}
```

- [ ] **Step 4: Make `remove()` footprint-aware**

Replace `Machines::remove()`:

```cpp
bool Machines::remove(int x, int y)
{
    const int index = indexAt(x, y);
    if (index < 0)
        return false;

    // Capture the doomed machine's own footprint before anything moves.
    const int doomedX = machines[index].x;
    const int doomedY = machines[index].y;
    const int doomedWidth = machineInfo(machines[index].type).width;

    const int last = static_cast<int>(machines.size()) - 1;

    // Swap the doomed machine with the last, so the vector stays dense, then fix
    // EVERY tile the moved machine occupies - not just its origin, or a
    // multi-tile machine relocated into the freed slot leaves a stale byTile
    // entry on its second tile.
    if (index != last)
    {
        machines[index] = machines[last];

        const int movedWidth = machineInfo(machines[index].type).width;
        for (int dx = 0; dx < movedWidth; ++dx)
            byTile[key(machines[index].x + dx, machines[index].y)] = index;
    }

    machines.pop_back();

    for (int dx = 0; dx < doomedWidth; ++dx)
        byTile.erase(key(doomedX + dx, doomedY));

    return true;
}
```

- [ ] **Step 5: Update the stale "single tile" comment on `Machine`**

In `src/Machines/Machine.h`, replace the comment above the struct:

```cpp
// One placed machine. Plain data: all behaviour lives in Machines. A machine
// occupies machineInfo(type).width tiles starting at (x, y) and growing
// rightward - 1 for everything except the Crafting Table, which is 2.
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 201 | 201 passed | 0 failed`.

- [ ] **Step 7: Commit**

```bash
git add src/Machines/Machine.h src/Machines/Machines.cpp tests/test_machines.cpp
git commit -m "feat: make Machines::place/remove footprint-aware for multi-tile machines"
```

---

## Task 6: Multi-tile rendering for the Crafting Table

`MachineRenderer` currently draws every machine as one fixed `TILE_SIZE` square and always draws I/O ticks, a bar, and a carried/output item glyph — none of which apply to the Crafting Table. `MachineRenderer.cpp` is only linked into the `Litharia` executable (not `Litharia_tests`), so this task is verified by running the game, matching how the rest of this file is covered.

**Files:**
- Modify: `src/Machines/MachineRenderer.cpp` (`draw()`, ~line 56-136)

**Interfaces:**
- Consumes: `MachineInfo::width` (Task 3), `Machines::place` now able to place a `CraftingTable` (Task 5).

- [ ] **Step 1: Widen the body rectangle and skip decorations for the Crafting Table**

In `src/Machines/MachineRenderer.cpp`, inside `MachineRenderer::draw()`:

- Change the `body` shape construction (currently `sf::RectangleShape body({static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)});`) to a default-sized shape, since its size now varies per machine:

```cpp
sf::RectangleShape body;
body.setOutlineThickness(-1.0f);
body.setOutlineColor(sf::Color(20, 20, 24));
```

- Inside the `for (const Machine& m : machines.all())` loop, right after computing `px`/`py`, size the body from the machine's own width and draw it, then skip the rest of the loop body for the Crafting Table (it has no I/O sides, bar, or carried/output item):

```cpp
        const float px = static_cast<float>(m.x * TILE_SIZE);
        const float py = static_cast<float>(m.y * TILE_SIZE);

        // Consumers dim when they have no power.
        const std::uint8_t alpha = (info.consumer && !m.powered) ? 120 : 255;

        body.setSize({static_cast<float>(info.width * TILE_SIZE), static_cast<float>(TILE_SIZE)});
        body.setPosition({px, py});
        body.setFillColor(toColor(info.color, alpha));
        target.draw(body);

        if (m.type == MachineType::CraftingTable)
            continue;

        const MachineStatus status = barStatus(m);
```

  (This replaces the existing `body.setPosition(...)`/`body.setFillColor(...)`/`target.draw(body)` three lines and the blank line before `const MachineStatus status = barStatus(m);` — everything below that in the loop is unchanged.)

- [ ] **Step 2: Build the game executable**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean, no errors.

- [ ] **Step 3: Manually verify rendering**

This step needs Task 5's `Machines::place` to accept `CraftingTable`, which it already does — but nothing in the game yet lets the *player* place one (that's Task 7/9). For now, confirm the renderer change compiles and doesn't break existing single-tile machines:

Run: `C:\Litharia\build\Debug\Litharia.exe`, press `B` to enter build mode, place a Drill, Belt, Chute, Smelter, Chest, and Burner Generator. Confirm each still renders as a single `TILE_SIZE` square with its usual I/O ticks and bar, exactly as before this change. (The Crafting Table's own rendering gets a full visual check in Task 10, once it's actually placeable.)

- [ ] **Step 4: Commit**

```bash
git add src/Machines/MachineRenderer.cpp
git commit -m "feat: render machines at their own footprint width"
```

---

## Task 7: Inventory-filtered, counted, consuming/refunding build mode

Build mode's palette currently lists every `MachineType` unconditionally and placing/destroying never touches the bag. This task makes the palette show only held types (with counts), consumes one item per placement, and refunds one on removal. `Hud.cpp` and `Game.cpp` are only linked into the `Litharia` executable, so this task is verified by running the game.

**Files:**
- Modify: `src/Hud/Hud.h` (`drawBuildPalette` signature, ~line 85)
- Modify: `src/Hud/Hud.cpp` (`drawBuildPalette`, ~line 352-405)
- Modify: `src/Game/Game.h` (method decls, ~line 47-59)
- Modify: `src/Game/Game.cpp` (`placeMachineAtCursor`, `removeMachineAtCursor`, `cycleBuildType`, `handleEvents`, `render`, ~line 148-186, 414-511, 576-624)

**Interfaces:**
- Consumes: `itemForMachine` (Task 3), `Inventory::removeOne(ItemType)` (Task 2), `Inventory::count(ItemType)` (existing), `ItemEntity` (existing, for the overflow-drop path).
- Produces: `Hud::drawBuildPalette(sf::RenderWindow&, MachineType, const Inventory&)` (signature changes — adds the bag parameter callers must now pass).

- [ ] **Step 1: Change `drawBuildPalette`'s signature and filter/count logic**

In `src/Hud/Hud.h`, change:

```cpp
    // The build-mode picker: a strip of machine-type swatches centered on
    // `selected`, plus a "left click: place / right click: destroy" caption.
    // Only lists MachineTypes the bag currently holds >= 1 of, labeling each
    // swatch with its count; empty if the bag holds none of anything craftable
    // yet.
    void drawBuildPalette(sf::RenderWindow& window, MachineType selected, const Inventory& bag);
```

In `src/Hud/Hud.cpp`, replace `Hud::drawBuildPalette`:

```cpp
void Hud::drawBuildPalette(sf::RenderWindow& window, MachineType selected, const Inventory& bag)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    constexpr int FIRST = 1; // skip MachineType::None

    std::vector<MachineType> held;
    for (int i = FIRST; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineType type = static_cast<MachineType>(i);
        if (bag.count(itemForMachine(type)) > 0)
            held.push_back(type);
    }

    if (held.empty())
    {
        window.setView(previous);
        return;
    }

    constexpr int VISIBLE = 5;
    constexpr float SWATCH = 40.0f;
    constexpr float GAP = 6.0f;

    const int total = static_cast<int>(held.size());
    const int visible = std::min(VISIBLE, total);

    const auto found = std::find(held.begin(), held.end(), selected);
    const int selectedIndex = (found != held.end()) ? static_cast<int>(found - held.begin()) : 0;
    const int half = visible / 2;

    const float totalWidth = visible * SWATCH + (visible - 1) * GAP;
    const sf::Vector2f windowSize(window.getSize());
    const float startX = (windowSize.x - totalWidth) * 0.5f;
    const float y = windowSize.y - SLOT_SIZE - MARGIN - SWATCH - MARGIN * 2.0f;

    const MachineType displayed = held[selectedIndex];

    for (int slot = 0; slot < visible; ++slot)
    {
        const int index = ((selectedIndex + slot - half) % total + total) % total;
        const MachineType type = held[index];
        const MachineInfo& info = machineInfo(type);
        const bool isSelected = (index == selectedIndex);

        const sf::Vector2f pos{startX + slot * (SWATCH + GAP), y};

        sf::RectangleShape swatch({SWATCH, SWATCH});
        swatch.setPosition(pos);
        swatch.setFillColor(toColor(info.color));
        swatch.setOutlineThickness(isSelected ? -3.0f : -1.0f);
        swatch.setOutlineColor(isSelected ? sf::Color(255, 236, 140) : sf::Color(90, 90, 105));
        window.draw(swatch);

        if (font)
        {
            sf::Text count(*font, "x" + std::to_string(bag.count(itemForMachine(type))), 12);
            count.setFillColor(sf::Color::White);
            count.setOutlineThickness(2.0f);
            count.setOutlineColor(sf::Color(10, 10, 12));
            const sf::FloatRect cb = count.getLocalBounds();
            count.setPosition({pos.x + SWATCH - cb.size.x - 3.0f, pos.y + SWATCH - cb.size.y - 6.0f});
            window.draw(count);
        }
    }

    if (!font)
    {
        window.setView(previous);
        return;
    }

    sf::Text name(*font, std::string(machineInfo(displayed).name), 16);
    const sf::FloatRect nameBounds = name.getLocalBounds();
    name.setFillColor(sf::Color::White);
    name.setPosition({(windowSize.x - nameBounds.size.x) * 0.5f, y - 22.0f});
    window.draw(name);

    sf::Text hint(*font, "Left click: place    Right click: destroy", 14);
    const sf::FloatRect hintBounds = hint.getLocalBounds();
    hint.setFillColor(sf::Color(220, 220, 220));
    hint.setPosition({(windowSize.x - hintBounds.size.x) * 0.5f, y + SWATCH + 6.0f});
    window.draw(hint);

    window.setView(previous);
}
```

- [ ] **Step 2: Build to confirm the signature change compiles in isolation**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: **build error** at the `hud.drawBuildPalette(window, buildType);` call site in `Game.cpp` — wrong number of arguments. This confirms the signature change took effect; fixed in the next step.

- [ ] **Step 3: Consume on place, refund on destroy, filter cycling — `Game.h`**

In `src/Game/Game.h`, no new methods are needed (existing `placeMachineAtCursor`, `removeMachineAtCursor`, `cycleBuildType` are reused) — only their bodies change in `Game.cpp` below.

- [ ] **Step 4: `Game.cpp` — `placeMachineAtCursor`**

Replace:

```cpp
void Game::placeMachineAtCursor()
{
    const sf::Vector2i tile = cursorTile();
    const int width = machineInfo(buildType).width;

    for (int dx = 0; dx < width; ++dx)
        if (world.isSolid(tile.x + dx, tile.y))
            return;

    const ItemType item = itemForMachine(buildType);
    if (player.inventory().count(item) <= 0)
        return;

    if (machines.place(buildType, tile.x, tile.y, buildFacing) == nullptr)
        return;

    player.inventory().removeOne(item);
}
```

- [ ] **Step 5: `Game.cpp` — `removeMachineAtCursor`**

Replace:

```cpp
void Game::removeMachineAtCursor()
{
    const sf::Vector2i tile = cursorTile();
    const Machine* target = machines.at(tile.x, tile.y);
    if (target == nullptr)
        return;

    const MachineType type = target->type;
    if (!machines.remove(tile.x, tile.y))
        return;

    const ItemType item = itemForMachine(type);
    const int leftover = player.inventory().add({item, 1});

    if (leftover > 0)
    {
        const sf::Vector2f position =
            player.center() - sf::Vector2f{ItemEntity::SIZE * 0.5f, ItemEntity::SIZE * 0.5f};
        drops.emplace_back(ItemStack{item, leftover}, position, sf::Vector2f{0.0f, -60.0f});
    }
}
```

- [ ] **Step 6: `Game.cpp` — `cycleBuildType`**

Replace:

```cpp
void Game::cycleBuildType(int delta)
{
    constexpr int first = 1; // skip MachineType::None
    const int count = static_cast<int>(MachineType::Count) - first;

    const Inventory& bag = player.inventory();
    int index = static_cast<int>(buildType) - first;

    for (int step = 0; step < count; ++step)
    {
        index = ((index + delta) % count + count) % count;
        const MachineType candidate = static_cast<MachineType>(first + index);

        if (bag.count(itemForMachine(candidate)) > 0)
        {
            setBuildType(candidate);
            return;
        }
    }
    // Nothing held: leave buildType where it is, and the palette draws empty.
}
```

- [ ] **Step 7: `Game.cpp` — fix the `drawBuildPalette` call site and add F7**

In `render()`, change:

```cpp
    if (buildMode)
        hud.drawBuildPalette(window, buildType, player.inventory());
```

In `handleEvents()`, directly below `if (key->code == Key::F6) setBuildType(MachineType::Chest);`, add:

```cpp
            if (key->code == Key::F7) setBuildType(MachineType::CraftingTable);
```

- [ ] **Step 8: Build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 9: Manually verify**

Run: `C:\Litharia\build\Debug\Litharia.exe`.

1. Fresh spawn, no crafted machine items yet: press `B`. Confirm the palette draws **nothing** (no swatches, no name/hint text) — you hold zero of everything.
2. Give yourself items for this check only by temporarily placing a machine the old way is no longer possible; instead confirm the *filtering* logic another way: since nothing is craftable yet in this task (Task 9 adds that), you cannot craft real items yet — skip to Task 10's full end-to-end check for the "3x Chest, 5x Drill" scenario. For this task, it's sufficient to confirm: (a) build mode with an empty bag shows an empty palette and does not crash, (b) pressing `F1`-`F7` while the bag is empty does not let you actually place anything (left-click in build mode is a silent no-op), confirming the new inventory guard in `placeMachineAtCursor` works.
3. Exit build mode (`B`), confirm the rest of the game (mining, movement) is unaffected.

- [ ] **Step 10: Commit**

```bash
git add src/Hud/Hud.h src/Hud/Hud.cpp src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: filter build palette to held items, consume on place, refund on destroy"
```

---

## Task 8: Crafting panel rendering and hit-testing in `Hud`

Adds the two crafting views (basic: Crafting Table only; advanced: the other 6 recipes) as a new `Hud` panel, plus its hit-test counterpart, following the exact pattern `hitTestChestButton`/chest-panel drawing already established. `Hud.cpp` is only linked into `Litharia`, so this task is verified by running the game (the panel won't be reachable from the player yet — that's Task 9 — so verification here is a build-only check; Task 10 exercises it for real).

**Files:**
- Modify: `src/Hud/Hud.h` (new methods + constants, ~line 30-90)
- Modify: `src/Hud/Hud.cpp` (new panel origin helper, `drawCraftPanel`, `hitTestCraftButton`)

**Interfaces:**
- Consumes: `CraftRecipe`, `CraftIngredient`, `allCraftRecipes()` (Task 4).
- Produces: `void Hud::drawCraftPanel(sf::RenderWindow&, const Inventory& bag, bool advanced, bool crafting, int craftingRecipeIndex, float craftProgress)`; `std::optional<int> Hud::hitTestCraftButton(sf::Vector2f screenPos, sf::Vector2f windowSize, bool advanced) const` — the returned `int` is an index into `allCraftRecipes()`.

- [ ] **Step 1: Add the new constants and method declarations**

In `src/Hud/Hud.h`, below the existing `CHEST_BUTTON_WIDTH`/`CHEST_BUTTON_HEIGHT` constants, add:

```cpp
    // Crafting recipe buttons: one per row, wide enough for a name + cost
    // string.
    static constexpr float CRAFT_BUTTON_WIDTH = 240.0f;
    static constexpr float CRAFT_BUTTON_HEIGHT = 40.0f;
```

Below the existing `hitTestChestButton` declaration, add:

```cpp
    // The hand-crafting panel: one button per visible recipe (basic: just the
    // Crafting Table; advanced: every recipe that requires one), each showing
    // its name and ingredient cost. Every button renders dimmed/disabled while
    // `crafting` is true (one craft at a time); the in-progress one additionally
    // shows a fill bar for craftProgress / that recipe's seconds.
    void drawCraftPanel(sf::RenderWindow& window, const Inventory& bag, bool advanced, bool crafting,
                         int craftingRecipeIndex, float craftProgress);

    // Screen position -> index into allCraftRecipes() for the button it lands
    // on, filtered identically to drawCraftPanel (same order, same `advanced`
    // split) so drawing and hit-testing can never disagree. nullopt if the
    // point misses every button.
    std::optional<int> hitTestCraftButton(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                           bool advanced) const;
```

- [ ] **Step 2: Add the panel origin helper and the two methods**

In `src/Hud/Hud.cpp`, inside the anonymous namespace, below `chestButtonsOrigin`, add:

```cpp
// Where the crafting panel's first button sits: directly below the bag panel,
// sharing its left edge - the same spot the chest panel would occupy (the two
// are mutually exclusive, so there's no visual collision).
sf::Vector2f craftPanelOrigin(sf::Vector2f windowSize)
{
    constexpr int BAG_ROWS = 3;
    const sf::Vector2f bagOrigin = bagPanelOrigin(windowSize);
    const float y =
        bagOrigin.y + BAG_ROWS * Hud::SLOT_SIZE + (BAG_ROWS - 1) * Hud::SLOT_GAP + Hud::MARGIN;

    return {bagOrigin.x, y};
}
```

At the end of `Hud.cpp`, add:

```cpp
void Hud::drawCraftPanel(sf::RenderWindow& window, const Inventory& bag, bool advanced, bool crafting,
                          int craftingRecipeIndex, float craftProgress)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    const std::span<const CraftRecipe> all = allCraftRecipes();
    const sf::Vector2f origin = craftPanelOrigin(sf::Vector2f(window.getSize()));

    int row = 0;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].requiresCraftingTable != advanced)
            continue;

        const CraftRecipe& recipe = all[i];
        const sf::Vector2f pos{origin.x, origin.y + row * (CRAFT_BUTTON_HEIGHT + SLOT_GAP)};
        ++row;

        bool affordable = true;
        for (const CraftIngredient& ing : recipe.ingredients)
            if (ing.item != ItemType::None && bag.count(ing.item) < ing.count)
                affordable = false;

        const bool disabled = crafting || !affordable;
        const bool inProgress = crafting && static_cast<int>(i) == craftingRecipeIndex;

        sf::RectangleShape button({CRAFT_BUTTON_WIDTH, CRAFT_BUTTON_HEIGHT});
        button.setPosition(pos);
        button.setFillColor(disabled ? sf::Color(40, 40, 46, 170) : BAG_SLOT_BACKGROUND);
        button.setOutlineThickness(-1.0f);
        button.setOutlineColor(sf::Color(90, 90, 105));
        window.draw(button);

        if (inProgress)
        {
            const float fraction = std::clamp(craftProgress / recipe.seconds, 0.0f, 1.0f);
            sf::RectangleShape fill({CRAFT_BUTTON_WIDTH * fraction, CRAFT_BUTTON_HEIGHT});
            fill.setPosition(pos);
            fill.setFillColor(sf::Color(90, 200, 230, 120));
            window.draw(fill);
        }

        if (!font)
            continue;

        std::string label = std::string(itemInfo(recipe.output).name) + " (";
        bool firstIngredient = true;
        for (const CraftIngredient& ing : recipe.ingredients)
        {
            if (ing.item == ItemType::None)
                continue;

            if (!firstIngredient)
                label += ", ";
            firstIngredient = false;

            label += std::to_string(ing.count) + "x " + std::string(itemInfo(ing.item).name);
        }
        label += ")";

        sf::Text text(*font, label, 13);
        text.setFillColor(disabled ? sf::Color(150, 150, 150) : sf::Color::White);
        text.setPosition({pos.x + 8.0f, pos.y + (CRAFT_BUTTON_HEIGHT - 13.0f) * 0.5f});
        window.draw(text);
    }

    window.setView(previous);
}

std::optional<int> Hud::hitTestCraftButton(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                            bool advanced) const
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const sf::Vector2f origin = craftPanelOrigin(windowSize);

    int row = 0;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].requiresCraftingTable != advanced)
            continue;

        const sf::Vector2f pos{origin.x, origin.y + row * (CRAFT_BUTTON_HEIGHT + SLOT_GAP)};
        ++row;

        const sf::FloatRect rect(pos, {CRAFT_BUTTON_WIDTH, CRAFT_BUTTON_HEIGHT});
        if (rect.contains(screenPos))
            return static_cast<int>(i);
    }

    return std::nullopt;
}
```

- [ ] **Step 3: Build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean (these two methods aren't called from anywhere yet — that's Task 9 — so no runtime behavior to observe here).

- [ ] **Step 4: Commit**

```bash
git add src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "feat: add the crafting panel's rendering and hit-testing to Hud"
```

---

## Task 9: Crafting flow wiring in `Game`

Wires everything together: the E key's three-way tile resolution (bag / chest / crafting table), the single-slot background-ticking craft process, and click dispatch to the new craft buttons. This is the task that makes the feature actually playable.

**Files:**
- Modify: `src/Game/Game.h` (new state + method decls, ~line 46-87)
- Modify: `src/Game/Game.cpp` (`toggleInventory`, `drawInventoryPanels`, `handleEvents`, `fixedUpdate`, new `startCraft`/`updateCrafting`)

**Interfaces:**
- Consumes: `Hud::drawCraftPanel`, `Hud::hitTestCraftButton` (Task 8); `CraftRecipe`, `allCraftRecipes()` (Task 4); `Inventory::removeOne(ItemType)` (Task 2); `ItemEntity` (existing).
- Produces: `Game::startCraft(int recipeIndex)`, `Game::updateCrafting(float dt)` — no other task depends on these; this is the top of the call chain.

- [ ] **Step 1: Add new state and method declarations**

In `src/Game/Game.h`, add `#include "../Machines/Recipes.h"` to the includes. Below the existing method declarations (after `void collectAllFromChest();`), add:

```cpp
    void startCraft(int recipeIndex);
    void updateCrafting(float dt);
```

In the private state section, below `std::optional<sf::Vector2i> openChestTile;`, add:

```cpp
    std::optional<sf::Vector2i> openCraftingTableTile;

    bool crafting = false;
    int craftingRecipeIndex = -1;
    float craftProgress = 0.0f;
```

- [ ] **Step 2: Rewrite `toggleInventory` for the three-way tile resolution**

In `src/Game/Game.cpp`, replace `Game::toggleInventory`:

```cpp
void Game::toggleInventory()
{
    if (dragging)
        return;

    if (inventoryOpen)
    {
        inventoryOpen = false;
        openChestTile.reset();
        openCraftingTableTile.reset();
        return;
    }

    const sf::Vector2i tile = cursorTile();
    const Machine* machine = machines.at(tile.x, tile.y);

    openChestTile.reset();
    openCraftingTableTile.reset();

    if (machine != nullptr && machine->type == MachineType::Chest)
        openChestTile = tile;
    else if (machine != nullptr && machine->type == MachineType::CraftingTable)
        openCraftingTableTile = tile;

    inventoryOpen = true;
    buildMode = false;
}
```

- [ ] **Step 3: Rewrite `drawInventoryPanels` to draw the craft panel when no chest is open**

Replace `Game::drawInventoryPanels`:

```cpp
void Game::drawInventoryPanels()
{
    if (openChestTile.has_value())
    {
        const Machine* chest = machines.at(openChestTile->x, openChestTile->y);

        // The chest might have vanished by other means while the panel was
        // open; fall back to the bag-only view rather than touch a stale tile.
        if (chest == nullptr || chest->type != MachineType::Chest)
            openChestTile.reset();
    }

    if (openCraftingTableTile.has_value())
    {
        const Machine* table = machines.at(openCraftingTableTile->x, openCraftingTableTile->y);

        if (table == nullptr || table->type != MachineType::CraftingTable)
            openCraftingTableTile.reset();
    }

    hud.drawInventoryPanel(window, player.inventory());

    if (openChestTile.has_value())
    {
        hud.drawChestPanel(window, machines.at(openChestTile->x, openChestTile->y)->storage);
        hud.drawChestButtons(window);
    }
    else
    {
        hud.drawCraftPanel(window, player.inventory(), openCraftingTableTile.has_value(), crafting,
                            craftingRecipeIndex, craftProgress);
    }
}
```

- [ ] **Step 4: Split the inventory click branch in `handleEvents` to dispatch to craft buttons**

In `src/Game/Game.cpp`'s `handleEvents()`, replace the `else if (inventoryOpen && mouse->button == sf::Mouse::Button::Left)` branch:

```cpp
            else if (inventoryOpen && mouse->button == sf::Mouse::Button::Left)
            {
                const sf::Vector2f screenPos(sf::Mouse::getPosition(window));
                const sf::Vector2f windowSize(window.getSize());

                if (openChestTile.has_value())
                {
                    const auto chestButton = hud.hitTestChestButton(screenPos, windowSize);

                    if (chestButton == Hud::ChestButton::DepositAll)
                        depositAllToChest();
                    else if (chestButton == Hud::ChestButton::CollectAll)
                        collectAllFromChest();
                    else
                        beginDrag();
                }
                else
                {
                    const auto craftHit =
                        hud.hitTestCraftButton(screenPos, windowSize, openCraftingTableTile.has_value());

                    if (craftHit.has_value())
                        startCraft(*craftHit);
                    else
                        beginDrag();
                }
            }
```

- [ ] **Step 5: Implement `startCraft` and `updateCrafting`**

In `src/Game/Game.cpp`, below `Game::collectAllFromChest`, add:

```cpp
void Game::startCraft(int recipeIndex)
{
    if (crafting)
        return;

    const std::span<const CraftRecipe> recipes = allCraftRecipes();
    if (recipeIndex < 0 || recipeIndex >= static_cast<int>(recipes.size()))
        return;

    const CraftRecipe& recipe = recipes[recipeIndex];
    Inventory& bag = player.inventory();

    for (const CraftIngredient& ing : recipe.ingredients)
        if (ing.item != ItemType::None && bag.count(ing.item) < ing.count)
            return;

    for (const CraftIngredient& ing : recipe.ingredients)
    {
        if (ing.item == ItemType::None)
            continue;

        for (int i = 0; i < ing.count; ++i)
            bag.removeOne(ing.item);
    }

    crafting = true;
    craftingRecipeIndex = recipeIndex;
    craftProgress = 0.0f;
}

void Game::updateCrafting(float dt)
{
    if (!crafting)
        return;

    const std::span<const CraftRecipe> recipes = allCraftRecipes();
    const CraftRecipe& recipe = recipes[craftingRecipeIndex];

    craftProgress += dt;
    if (craftProgress < recipe.seconds)
        return;

    Inventory& bag = player.inventory();
    const int leftover = bag.add({recipe.output, 1});

    if (leftover > 0)
    {
        const sf::Vector2f position =
            player.center() - sf::Vector2f{ItemEntity::SIZE * 0.5f, ItemEntity::SIZE * 0.5f};
        drops.emplace_back(ItemStack{recipe.output, leftover}, position, sf::Vector2f{0.0f, -60.0f});
    }

    crafting = false;
    craftingRecipeIndex = -1;
    craftProgress = 0.0f;
}
```

- [ ] **Step 6: Tick crafting every fixed step**

In `Game::fixedUpdate`, directly below `tickMachines(dt);`, add:

```cpp
    updateCrafting(dt);
```

- [ ] **Step 7: Build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 8: Manually verify the bootstrap craft**

Run: `C:\Litharia\build\Debug\Litharia.exe`.

1. Chop an oak tree with the starting Axe until you hold at least 15 Oak Log (check the window title bar, which shows `holding: ...`, or open the bag with `E` and count).
2. Press `E` while **not** standing over any machine. Confirm the bag panel opens alongside a single button reading `Crafting Table (15x Oak Log)`.
3. If you hold fewer than 15 logs, confirm the button renders dimmed/grey and clicking it does nothing.
4. With >= 15 logs, click the button. Confirm: the 15 logs are deducted from the bag immediately, the button now renders dimmed with a cyan fill bar growing left-to-right.
5. Close the panel (`E`) and walk around for ~3 seconds (the Crafting Table's recipe time) — confirm a Crafting Table item appears in your bag once the timer elapses, proving it progresses in the background.
6. Re-open `E`: confirm the button is clickable again (no longer greyed by an in-progress craft).

- [ ] **Step 9: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: wire up the crafting menu's E-key flow and background progress"
```

---

## Task 10: End-to-end verification

No code changes — this task exercises the full feature the way a player would, covering every path the earlier tasks' manual-verification steps deferred (build-palette counts/labels with real crafted items, the advanced table menu, footprint placement/removal/refund, and multi-tile rendering).

**Files:** none.

- [ ] **Step 1: Full run build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 2: Craft and place a Crafting Table**

Run `C:\Litharia\build\Debug\Litharia.exe`. Chop enough oak logs (>= 15), press `E` in the open, craft a Crafting Table (wait for it to finish). Press `B` to enter build mode: confirm the palette now shows exactly one swatch, "Crafting Table x1", and nothing else. Left-click to place it. Confirm it renders as a **2-tile-wide** block (twice the width of a Belt placed next to it) with no I/O ticks, bar, or item glyph. Confirm the palette now shows "Crafting Table x0" is gone entirely (you're back to an empty/no-swatch palette, since you no longer hold one).

- [ ] **Step 3: Advanced menu**

Walk the cursor over either tile of the placed table and press `E`. Confirm the bag panel opens alongside 6 recipe buttons (Chest, Belt, Chute, Burner Generator, Drill, Smelter), each labeled with its name and ingredient cost, each dimmed if unaffordable. Gather materials (mine stone/copper ore/iron ore, smelt ore into plates at a placed Smelter if you have one, or mine directly) until you can afford at least two different recipes — craft one, confirm it deducts ingredients and shows a progress bar; confirm every other button (including ones you can afford) is dimmed and unclickable while it's in progress; wait for completion and confirm the item lands in your bag.

- [ ] **Step 4: Build palette counts and filtering**

Craft 3 Chests and 5 Drills (or whatever quantities are convenient given gathered materials — at least 2 of two different types). Press `B`. Confirm the palette shows only the types you hold, each labeled `xN` matching what you crafted (e.g. "Chest x3", "Drill x5") — and nothing for types you hold zero of. Scroll the mouse wheel: confirm cycling only visits your held types and wraps around them, never landing on an unheld type.

- [ ] **Step 5: Placement consumes, removal refunds**

With Drill selected in the palette, left-click to place one. Confirm the count in the palette drops by exactly 1 (e.g. "Drill x5" -> "Drill x4"). Right-click the placed Drill to destroy it. Confirm: the machine disappears, and the palette count goes back up by 1 (e.g. back to "Drill x5"). Repeat placing until you hold 0 of a type and confirm that type then disappears from the palette entirely (matching Task 7's empty-bag behavior, now demonstrated with real items).

- [ ] **Step 6: Regression check — existing systems untouched**

Confirm mining, placing world blocks, the hotbar, chest deposit/collect, drag-and-drop between bag and chest, and machine power/smelting/drilling all still work exactly as before (spot-check one of each). This is a regression check, not new functionality — nothing in this plan should have touched their behavior, so any deviation here means an earlier task's change had an unintended side effect worth investigating before calling the feature done.

- [ ] **Step 7: Final automated test run**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 201 | 201 passed | 0 failed` (no change from Task 5 — Tasks 6-9 touch only executable-only files).

- [ ] **Step 8: Update the CLAUDE.md-adjacent doc trail (optional but consistent with prior plans)**

No action needed unless the project's `docs/` conventions require a changelog entry — check `docs/superpowers/plans/` for whether prior completed plans added one; if not, skip.
