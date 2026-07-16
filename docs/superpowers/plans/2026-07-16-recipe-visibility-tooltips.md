# Recipe Visibility in Smelter/Drill Tooltips Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Smelter's tooltip always shows an "accepts" line listing every ore→plate recipe, and a Drill's tooltip always shows a "mines" line listing every ore it can dig, so a first-time player can learn both without reading source or trial-and-error.

**Architecture:** Two small, independent read-only additions to the simulation core (`Litharia_core`, no window dependency, fully unit-tested), followed by one purely additive UI change in `Hud.cpp` (not linked into the test binary, verified manually). `Recipes.h/.cpp` gains a way to enumerate every smelt recipe; `MachineType.h`/`MachineRegistry.cpp` gains a named, shared list of mineable ores that both the Drill's mining logic and the tooltip read from, so they can't drift apart. Both gain a small pure string-formatting function callable from `Hud::drawMachineTooltip` without touching any existing conditional tooltip logic.

**Tech Stack:** C++20 (`std::span`), SFML 3 (System only for the two core tasks; Graphics/Window for the Hud task), doctest, CMake + Visual Studio generator.

## Global Constraints

- Recipe/ore lines use `->` (ASCII), never `→`, per the design spec (avoids non-ASCII glyphs in the bundled font).
- The lines are always shown for their machine type — not conditioned on idle/empty state — matching the existing "name" line's unconditional behavior.
- Scope is exactly two machine types: Smelter ("Smelts: ...") and Drill ("Mines: ..."). No other machine type gets a new line.
- `isOre(BlockType)` in `Machines.cpp` must keep accepting exactly the same three block types after its rewrite — behavior-preserving, not a logic change.
- Build: `cmake --build build --config Debug --target Litharia_tests` (core + tests) and `cmake --build build --config Debug --target Litharia` (game, for Task 3 only). Tests: `build/Debug/Litharia_tests.exe` (currently 160 cases, all green — confirm this before Task 1).

---

### Task 1: Smelter recipes become enumerable

**Files:**
- Modify: `src/Machines/Recipes.h`
- Modify: `src/Machines/Recipes.cpp`
- Test: `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: existing `SmeltRecipe` struct, existing `itemInfo(ItemType)` from `Items.h`.
- Produces: `std::span<const SmeltRecipe> allSmeltRecipes()` and `std::string formatSmeltRecipeList(std::span<const SmeltRecipe> list)`. Task 3 calls both together as `formatSmeltRecipeList(allSmeltRecipes())`. No other task depends on anything else here.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_recipes.cpp`:

```cpp
TEST_CASE("allSmeltRecipes exposes both defined recipes")
{
    const std::span<const SmeltRecipe> all = allSmeltRecipes();

    REQUIRE(all.size() == 2);
    CHECK(all[0].in == ItemType::CopperOre);
    CHECK(all[1].in == ItemType::IronOre);
}

TEST_CASE("formatSmeltRecipeList joins every recipe as \"In -> Out\"")
{
    CHECK(formatSmeltRecipeList(allSmeltRecipes()) ==
          "Copper Ore -> Copper Plate, Iron Ore -> Iron Plate");
}

TEST_CASE("formatSmeltRecipeList on an empty span yields an empty string")
{
    CHECK(formatSmeltRecipeList({}) == "");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAILS to compile — `allSmeltRecipes` and `formatSmeltRecipeList` don't exist yet.

- [ ] **Step 3: Declare the two new functions**

Replace the full contents of `src/Machines/Recipes.h`:

```cpp
#pragma once

#include <span>
#include <string>

#include "../Items/Items.h"

// One smelter recipe: an input item becomes an output item after a fixed time.
struct SmeltRecipe
{
    ItemType in;
    ItemType out;
    float seconds;
};

// The recipe whose input is `in`, or nullptr if that item does not smelt.
const SmeltRecipe* smeltRecipeFor(ItemType in);

// Every defined smelter recipe, e.g. for a tooltip's "accepts" listing.
std::span<const SmeltRecipe> allSmeltRecipes();

// "Copper Ore -> Copper Plate, Iron Ore -> Iron Plate" - one segment per
// recipe, joined by ", ". An empty span yields "".
std::string formatSmeltRecipeList(std::span<const SmeltRecipe> list);
```

- [ ] **Step 4: Implement them**

In `src/Machines/Recipes.cpp`, the existing `recipes` array and `smeltRecipeFor` are unchanged. Append after `smeltRecipeFor`'s closing brace:

```cpp
std::span<const SmeltRecipe> allSmeltRecipes()
{
    return recipes;
}

std::string formatSmeltRecipeList(std::span<const SmeltRecipe> list)
{
    std::string result;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (i > 0)
            result += ", ";

        result += std::string(itemInfo(list[i].in).name) + " -> " +
                   std::string(itemInfo(list[i].out).name);
    }

    return result;
}
```

The full file now reads:

```cpp
#include "Recipes.h"

#include <array>

namespace
{

constexpr std::array<SmeltRecipe, 2> recipes = {{
    {ItemType::CopperOre, ItemType::CopperPlate, 2.0f},
    {ItemType::IronOre,   ItemType::IronPlate,   3.5f},
}};

} // namespace

const SmeltRecipe* smeltRecipeFor(ItemType in)
{
    for (const SmeltRecipe& r : recipes)
        if (r.in == in)
            return &r;

    return nullptr;
}

std::span<const SmeltRecipe> allSmeltRecipes()
{
    return recipes;
}

std::string formatSmeltRecipeList(std::span<const SmeltRecipe> list)
{
    std::string result;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (i > 0)
            result += ", ";

        result += std::string(itemInfo(list[i].in).name) + " -> " +
                   std::string(itemInfo(list[i].out).name);
    }

    return result;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Run: `build/Debug/Litharia_tests.exe`
Expected: all cases pass, including the three new ones (163 total).

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Recipes.h src/Machines/Recipes.cpp tests/test_recipes.cpp
git commit -m "feat: expose all smelt recipes for UI enumeration"
```

---

### Task 2: Drill's mineable ore list becomes a named, shared constant

**Files:**
- Modify: `src/Machines/MachineType.h`
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Machines.cpp`
- Test: `tests/test_machines.cpp`

**Interfaces:**
- Consumes: existing `itemInfo(ItemType)`, existing `itemForBlock(BlockType)` (both from `Items.h`, already visible in `Machines.cpp` via `Machine.h`'s include chain).
- Produces: `inline constexpr std::array<ItemType, 3> DRILL_ORES` and `std::string formatDrillOreList(std::span<const ItemType> ores)`, both declared in `MachineType.h`. Task 3 calls `formatDrillOreList(DRILL_ORES)`. `Machines.cpp`'s private `isOre(BlockType)` is rewritten to derive from `DRILL_ORES` instead of hardcoding the same three types a second time — this is the change that makes `DRILL_ORES` the single source of truth, but no other task depends on `isOre` itself.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("DRILL_ORES lists exactly the three mineable ore types")
{
    CHECK(DRILL_ORES.size() == 3);
    CHECK(DRILL_ORES[0] == ItemType::CopperOre);
    CHECK(DRILL_ORES[1] == ItemType::IronOre);
    CHECK(DRILL_ORES[2] == ItemType::Coal);
}

TEST_CASE("formatDrillOreList joins every ore's name")
{
    CHECK(formatDrillOreList(DRILL_ORES) == "Copper Ore, Iron Ore, Coal");
}

TEST_CASE("formatDrillOreList on an empty span yields an empty string")
{
    CHECK(formatDrillOreList({}) == "");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAILS to compile — `DRILL_ORES` and `formatDrillOreList` don't exist yet.

- [ ] **Step 3: Add `DRILL_ORES` and declare `formatDrillOreList`**

In `src/Machines/MachineType.h`, replace the top of the file (includes and the three existing constants) with:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "../Blocks/Blocks.h" // for BlockColor
#include "../Items/Items.h"   // for ItemType

// Seconds of generator burn one coal provides.
inline constexpr float COAL_BURN_SECONDS = 20.0f;

// How many tiles straight down a drill scans for ore to eat.
inline constexpr int DRILL_REACH = 4;

// How many item slots a chest holds (2 rows of 10 in the UI).
inline constexpr int CHEST_SLOTS = 20;

// Ore item types a Drill can mine, in display order.
inline constexpr std::array<ItemType, 3> DRILL_ORES = {
    ItemType::CopperOre, ItemType::IronOre, ItemType::Coal};
```

The `enum class MachineType` and `struct MachineInfo` below are unchanged. Add the new declaration right after `const MachineInfo& machineInfo(MachineType type);` at the bottom of the file:

```cpp
const MachineInfo& machineInfo(MachineType type);

// "Copper Ore, Iron Ore, Coal" - one segment per entry, joined by ", ". An
// empty span yields "".
std::string formatDrillOreList(std::span<const ItemType> ores);
```

- [ ] **Step 4: Implement `formatDrillOreList`**

In `src/Machines/MachineRegistry.cpp`, append after `machineInfo`'s closing brace:

```cpp
std::string formatDrillOreList(std::span<const ItemType> ores)
{
    std::string result;

    for (std::size_t i = 0; i < ores.size(); ++i)
    {
        if (i > 0)
            result += ", ";

        result += std::string(itemInfo(ores[i]).name);
    }

    return result;
}
```

- [ ] **Step 5: Run tests to verify the new ones pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Run: `build/Debug/Litharia_tests.exe`
Expected: all cases pass, including the three new ones (166 total). `isOre` hasn't been touched yet, so drill-mining behavior is unchanged.

- [ ] **Step 6: Rewrite `isOre` to derive from `DRILL_ORES`**

In `src/Machines/Machines.cpp`, replace:

```cpp
// True for blocks a drill should mine (they drop themselves as an ore/fuel item).
bool isOre(BlockType b)
{
    return b == BlockType::CopperOre || b == BlockType::IronOre || b == BlockType::Coal;
}
```

with:

```cpp
// True for blocks a drill should mine (they drop themselves as an ore/fuel item).
// Derives from DRILL_ORES so the mining logic and the tooltip's "Mines: ..."
// line can never list different ores.
bool isOre(BlockType b)
{
    const ItemType item = itemForBlock(b);
    return std::find(DRILL_ORES.begin(), DRILL_ORES.end(), item) != DRILL_ORES.end();
}
```

No new includes are needed: `<algorithm>` (for `std::find`) and `itemForBlock`/`ItemType`/`DRILL_ORES` (via `Machine.h`'s existing include chain) are already available in this file.

- [ ] **Step 7: Run the full test suite to confirm no regression**

Run: `cmake --build build --config Debug --target Litharia_tests`
Run: `build/Debug/Litharia_tests.exe`
Expected: all cases still pass (166 total), including the existing drill-mining tests in `tests/test_mining.cpp` and `tests/test_factory.cpp` — this is the regression check confirming `isOre`'s rewrite accepts exactly the same three block types as before.

- [ ] **Step 8: Commit**

```bash
git add src/Machines/MachineType.h src/Machines/MachineRegistry.cpp src/Machines/Machines.cpp tests/test_machines.cpp
git commit -m "feat: share the Drill's mineable-ore list with the UI"
```

---

### Task 3: Tooltip gains "Smelts:" / "Mines:" lines

**Files:**
- Modify: `src/Hud/Hud.cpp`

**Interfaces:**
- Consumes: `allSmeltRecipes()`/`formatSmeltRecipeList()` (Task 1), `DRILL_ORES`/`formatDrillOreList()` (Task 2), existing `TooltipLine` struct and `Hud::drawMachineTooltip` in `Hud.cpp`.
- Produces: no new public interface — this is the terminal, purely additive UI task. Nothing else depends on it.

- [ ] **Step 1: Add the include**

In `src/Hud/Hud.cpp`, the include block currently reads:

```cpp
#include "../Blocks/Blocks.h"
#include "../Items/Inventory.h"
#include "../Machines/MachineType.h"
#include "HudLayout.h"
```

Change to:

```cpp
#include "../Blocks/Blocks.h"
#include "../Items/Inventory.h"
#include "../Machines/MachineType.h"
#include "../Machines/Recipes.h"
#include "HudLayout.h"
```

- [ ] **Step 2: Add the two conditional lines**

In `src/Hud/Hud.cpp`, `Hud::drawMachineTooltip` currently starts:

```cpp
    std::vector<TooltipLine> lines;
    lines.push_back({std::string(info.name), sf::Color::White});

    if (info.generator)
```

Change to:

```cpp
    std::vector<TooltipLine> lines;
    lines.push_back({std::string(info.name), sf::Color::White});

    if (machine.type == MachineType::Smelter)
        lines.push_back({"Smelts: " + formatSmeltRecipeList(allSmeltRecipes()), sf::Color::White});
    else if (machine.type == MachineType::Drill)
        lines.push_back({"Mines: " + formatDrillOreList(DRILL_ORES), sf::Color::White});

    if (info.generator)
```

Every other line in the function (Fuel/Powered, Input, Output, bar percentage, idle reason) is unchanged — both new lines are unconditional for their machine type, same as the existing name line.

- [ ] **Step 3: Build both targets**

Run: `cmake --build build --config Debug --target Litharia_tests`
Run: `build/Debug/Litharia_tests.exe`
Expected: all 166 cases still pass (`Hud.cpp` isn't part of `Litharia_tests`, so this just confirms Tasks 1-2 are still intact).

Run: `cmake --build build --config Debug --target Litharia`
Expected: builds cleanly — this is the first compile of `Hud.cpp` against the new symbols.

- [ ] **Step 4: Manually verify the tooltip**

Use the project's `run` skill to launch `build/Debug/Litharia.exe`. In build mode, place a Smelter and a Drill. Hover the cursor over the Smelter and confirm the tooltip shows a line reading exactly `Smelts: Copper Ore -> Copper Plate, Iron Ore -> Iron Plate` right below its name. Hover the Drill and confirm `Mines: Copper Ore, Iron Ore, Coal` right below its name. Then hover a Belt, a Chest, and a Burner Generator (place one of each if needed) and confirm none of them show a "Smelts:"/"Mines:" line — the change should be scoped to exactly the two machine types.

- [ ] **Step 5: Commit**

```bash
git add src/Hud/Hud.cpp
git commit -m "feat: show accepted recipes/ores in the Smelter and Drill tooltips"
```

---

## Out of scope

- Palette machine descriptions, control legend, on-tile status glyphs, power network overlay, and guided first-machine nudges — later plans in this beginner-clarity arc.
- Recipe/ore lines for the BurnerGenerator or Chest (per the design spec, neither has the same gap).
- Any change to how recipes are authored, added, or balanced.
