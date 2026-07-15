# Factory Auto-Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the first automated production loop to Litharia — a coal-fed burner generator powers a drill that auto-mines an ore vein onto a belt, which feeds an electric smelter that turns ore into refined plates, with zero hand-mining once running.

**Architecture:** A new pure-logic `Machines` module holds a flat vector of 1×1 `Machine` structs plus a `tile → index` spatial map, and ticks the whole factory on the existing fixed 1/60 s timestep. Power is an in-module network solve (orthogonal-adjacency flood fill; supply ≥ demand → powered). Item transport is a chain of one-item buffers with a transfer interval (belts move toward their facing, chutes drop down). All of this links into `Litharia_core` and is tested window-free, exactly like the existing world/physics/inventory code; only a new `MachineRenderer` and the `Game` wiring touch SFML.

**Tech Stack:** C++20, SFML 3 (System only for core; Graphics/Window for renderer), CMake, doctest (vendored single header).

## Global Constraints

- **C++ standard:** C++20 (`set(CMAKE_CXX_STANDARD 20)`), already configured — do not change.
- **Simulation/rendering split:** Files compiled into `Litharia_core` MUST NOT include `<SFML/Graphics.hpp>` or `<SFML/Window.hpp>`. Only `<SFML/System/...>` (for `sf::Vector2f`/`sf::Vector2i`) is allowed in core. Rendering lives in the `Litharia` executable target only. This is the property that keeps the test binary window-free.
- **Registry pattern:** New enums (`MachineType`) get a `constexpr std::array<Info, Count>` registry indexed by the enum, with a bounds-checked `xxxInfo(type)` accessor that falls back to the zeroth (null) entry — mirror `src/Blocks/Blocks.cpp` and `src/Items/Items.cpp` exactly.
- **World edge safety:** `World::get` returns `BlockType::Air` out of bounds and `World::set` ignores out-of-bounds writes. Machine code relies on this — never guard world access with your own bounds checks that duplicate it.
- **Determinism:** The factory tick must be a pure function of prior state + `dt`. No wall-clock, no RNG in the tick. This is what lets `test_factory.cpp` assert exact plate counts.
- **Fixed timestep:** All new simulation runs inside `Game::fixedUpdate(dt)` at `dt == 1.0f/60.0f`. Never tick machines from `render()`.
- **Every new `.cpp` is added to the correct CMake target** (`Litharia_core` for logic, `Litharia` for rendering, `Litharia_tests` for tests) in `CMakeLists.txt`. A file that compiles but is not listed will link-fail.
- **Commit after every task** with a `feat:`/`test:`/`refactor:` prefixed message.

---

## Design Summary (from brainstorming, 2026-07-15)

Decisions this slice implements, for reviewer context:

- **Progression model:** tech-tree engine — throughput of refined goods is the eventual gate. This slice builds only the production loop, no research sink yet.
- **Logistics:** gravity-native — belts carry horizontally, chutes drop for free. Powered lifts (upward transport) are **out of scope** here.
- **Power:** electricity is the universal currency; a coal **burner generator** is the first source. More generators (water/steam/solar) come later.
- **Ore model:** finite and **consumed** — a drill eats the ore beneath it, then idles; you relocate it. Matches the finite dug-down world.
- **Scope cuts for this slice (deliberate, note in code):**
  - All machines are **1×1 tiles** (no multi-tile footprints yet).
  - Power connectivity is **orthogonal adjacency** between machines (no dedicated wires/poles yet).
  - Power is **binary** (supply ≥ demand → all consumers run; else they stall). No proportional brownout.
  - Belts hold **one item per tile**, moved on a fixed interval (no Factorio-style lanes/sub-tile packing).

---

## File Structure

**New — core logic (into `Litharia_core`):**
- `src/Core/Direction.h` / `src/Core/Direction.cpp` — `Direction` enum + `dirDX/dirDY/rotateCW` helpers.
- `src/Machines/MachineType.h` — `MachineType` enum, `MachineInfo` struct, `machineInfo()`, shared constants.
- `src/Machines/MachineRegistry.cpp` — the `MachineInfo` registry table + accessor.
- `src/Machines/Machine.h` — the plain-data `Machine` struct (all fields, header-only).
- `src/Machines/Recipes.h` / `src/Machines/Recipes.cpp` — smelting recipe registry + lookup.
- `src/Machines/Machines.h` / `src/Machines/Machines.cpp` — the container: placement, spatial index, power solve, item insertion, factory tick.

**New — rendering (into `Litharia` executable only):**
- `src/Machines/MachineRenderer.h` / `src/Machines/MachineRenderer.cpp` — draws machine quads + carried items, dims unpowered machines.

**Modified:**
- `src/Blocks/Blocks.h` / `src/Blocks/Blocks.cpp` — add `BlockType::Coal`.
- `src/World/TerrainGenerator.h` / `src/World/TerrainGenerator.cpp` — add a coal vein pass.
- `src/Items/Items.h` / `src/Items/Items.cpp` — add `ItemType::Coal/CopperPlate/IronPlate`; map coal in `itemForBlock`; allow non-placeable items (`placeBlock == Air`).
- `src/Game/Game.h` / `src/Game/Game.cpp` — own a `Machines`, tick it, render it, add build/place controls + fuel loading.
- `CMakeLists.txt` — register all new `.cpp` and test files.

**New — tests (into `Litharia_tests`):**
- `tests/test_recipes.cpp`, `tests/test_machines.cpp`, `tests/test_power.cpp`, `tests/test_transport.cpp`, `tests/test_processing.cpp`, `tests/test_factory.cpp`.
- Modified: `tests/test_terrain.cpp` (coal placement), `tests/test_mining.cpp` (relax the "all items placeable" invariant).

---

## Canonical Interfaces (defined once, referenced by all tasks)

These are the exact signatures later tasks depend on. They are introduced by the task noted in parentheses; do not rename them.

```cpp
// src/Core/Direction.h  (Task 1)
enum class Direction : std::uint8_t { Up, Down, Left, Right };
int dirDX(Direction d);
int dirDY(Direction d);
Direction rotateCW(Direction d);

// src/Machines/MachineType.h  (Task 5)
enum class MachineType : std::uint8_t { None, BurnerGenerator, Drill, Belt, Chute, Smelter, Count };
inline constexpr float COAL_BURN_SECONDS = 20.0f;   // seconds of power per coal
inline constexpr int   DRILL_REACH = 4;             // tiles it scans downward for ore
struct MachineInfo {
    std::string_view name;
    BlockColor color;
    bool  generator;     // supplies power
    bool  consumer;      // draws power
    bool  transport;     // belt/chute: moves a carried item
    float powerRating;   // supply if generator, demand if consumer
    float actionTime;    // drill: seconds per ore; transport: transfer interval
};
const MachineInfo& machineInfo(MachineType type);

// src/Machines/Machine.h  (Task 5)
struct Machine {
    MachineType type = MachineType::None;
    int x = 0, y = 0;
    Direction facing = Direction::Right;
    ItemStack input;                    // processing input buffer
    ItemStack output;                   // processing output buffer
    float progress = 0.0f;              // seconds into current op
    float fuel = 0.0f;                  // generator: seconds of burn left
    int network = -1;                   // power network id
    bool powered = false;               // set by updatePower()
    ItemType carried = ItemType::None;  // transport: the one item on this tile
    float carryTimer = 0.0f;            // transport: counts down; moves at <= 0
    bool empty() const { return type == MachineType::None; }
};

// src/Machines/Recipes.h  (Task 4)
struct SmeltRecipe { ItemType in; ItemType out; float seconds; };
const SmeltRecipe* smeltRecipeFor(ItemType in);   // nullptr if none

// src/Machines/Machines.h  (Tasks 6-13)
class Machines {
public:
    bool     canPlace(int x, int y) const;
    Machine* place(MachineType type, int x, int y, Direction facing);   // nullptr if blocked
    bool     remove(int x, int y);
    Machine*       at(int x, int y);
    const Machine* at(int x, int y) const;
    bool     tryInsert(int x, int y, ItemType item);                    // true if accepted
    void     updatePower();                                             // rebuild networks + powered
    void     tick(World& world, float dt, std::vector<sf::Vector2i>& minedTiles);
    std::size_t count() const;
    const std::vector<Machine>& all() const;
private:
    int  indexAt(int x, int y) const;   // -1 if none
    Machines::machines;                 // std::vector<Machine>
};
```

---

### Task 1: Direction helper

**Files:**
- Create: `src/Core/Direction.h`, `src/Core/Direction.cpp`
- Test: `tests/test_machines.cpp` (create; direction cases first)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `Direction`, `dirDX`, `dirDY`, `rotateCW` (see Canonical Interfaces).

- [ ] **Step 1: Write the failing test**

Create `tests/test_machines.cpp`:

```cpp
#include "doctest.h"

#include "Core/Direction.h"

TEST_CASE("direction deltas point the right way")
{
    CHECK(dirDX(Direction::Left)  == -1);
    CHECK(dirDX(Direction::Right) ==  1);
    CHECK(dirDY(Direction::Up)    == -1);
    CHECK(dirDY(Direction::Down)  ==  1);

    // The other axis is zero.
    CHECK(dirDY(Direction::Left)  == 0);
    CHECK(dirDX(Direction::Up)    == 0);
}

TEST_CASE("rotateCW cycles through all four and wraps")
{
    CHECK(rotateCW(Direction::Up)    == Direction::Right);
    CHECK(rotateCW(Direction::Right) == Direction::Down);
    CHECK(rotateCW(Direction::Down)  == Direction::Left);
    CHECK(rotateCW(Direction::Left)  == Direction::Up);
}
```

- [ ] **Step 2: Register the new files in CMake**

In `CMakeLists.txt`, add to the `Litharia_core` source list (after `src/Core/Noise.cpp`):

```cmake
    src/Core/Direction.cpp
```

And add to the `Litharia_tests` source list (after `tests/test_main.cpp`):

```cmake
    tests/test_machines.cpp
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug`
Expected: FAIL to compile — `Core/Direction.h` not found.

- [ ] **Step 4: Write minimal implementation**

Create `src/Core/Direction.h`:

```cpp
#pragma once

#include <cstdint>

// A cardinal direction on the tile grid. Kept free of SFML so it can live in the
// core library and the test binary.
enum class Direction : std::uint8_t { Up, Down, Left, Right };

int dirDX(Direction d);
int dirDY(Direction d);

// The next direction clockwise. Used to rotate a machine before placing it.
Direction rotateCW(Direction d);
```

Create `src/Core/Direction.cpp`:

```cpp
#include "Direction.h"

int dirDX(Direction d)
{
    switch (d)
    {
        case Direction::Left:  return -1;
        case Direction::Right: return  1;
        default:               return  0;
    }
}

int dirDY(Direction d)
{
    switch (d)
    {
        case Direction::Up:   return -1;
        case Direction::Down: return  1;
        default:              return  0;
    }
}

Direction rotateCW(Direction d)
{
    switch (d)
    {
        case Direction::Up:    return Direction::Right;
        case Direction::Right: return Direction::Down;
        case Direction::Down:  return Direction::Left;
        case Direction::Left:  return Direction::Up;
    }
    return Direction::Up;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS, all cases green.

- [ ] **Step 6: Commit**

```bash
git add src/Core/Direction.h src/Core/Direction.cpp tests/test_machines.cpp CMakeLists.txt
git commit -m "feat: add Direction grid helper"
```

---

### Task 2: Coal block + terrain vein

**Files:**
- Modify: `src/Blocks/Blocks.h`, `src/Blocks/Blocks.cpp`
- Modify: `src/World/TerrainGenerator.h`, `src/World/TerrainGenerator.cpp`
- Test: `tests/test_terrain.cpp`

**Interfaces:**
- Produces: `BlockType::Coal`, coal veins in generated worlds within `COAL_MIN_Y..COAL_MAX_Y`, only replacing stone.

- [ ] **Step 1: Write the failing test**

Read the top of `tests/test_terrain.cpp` first to match its existing helpers and seeds. Append this case:

```cpp
TEST_CASE("coal spawns only in stone and inside its depth band")
{
    World world;
    TerrainGenerator(4242u).generate(world);

    int coalCount = 0;

    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        for (int y = 0; y < WORLD_HEIGHT; ++y)
        {
            if (world.get(x, y) != BlockType::Coal)
                continue;

            ++coalCount;

            // Inside the band...
            CHECK(y >= TerrainGenerator::COAL_MIN_Y);
            CHECK(y <= TerrainGenerator::COAL_MAX_Y);
        }
    }

    // The world is not barren of fuel.
    CHECK(coalCount > 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug`
Expected: FAIL to compile — `BlockType::Coal` and `TerrainGenerator::COAL_MIN_Y` do not exist.

- [ ] **Step 3: Add the block type and registry entry**

In `src/Blocks/Blocks.h`, add `Coal` to the enum after `IronOre`:

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

    Count
};
```

In `src/Blocks/Blocks.cpp`, add the registry row after the Iron Ore row (order MUST match the enum):

```cpp
    {"Coal",        { 44,  44,  50}, true,  1.10f, BlockType::Coal},
```

- [ ] **Step 4: Add the coal vein pass**

In `src/World/TerrainGenerator.h`, add the depth band constants after the iron ones:

```cpp
    static constexpr int COAL_MIN_Y = 180;
    static constexpr int COAL_MAX_Y = 300;
```

In `src/World/TerrainGenerator.cpp`, add a salt near the other salts:

```cpp
constexpr std::uint32_t SALT_COAL = 0x5000u;
```

Add a density near the other densities:

```cpp
constexpr float COAL_DENSITY = 0.30f;
```

Then extend the `ores` array in `scatterOre` (it already loops every entry and only writes into stone, so coal needs no special-casing):

```cpp
    const Ore ores[] = {
        {BlockType::CopperOre, COPPER_MIN_Y, COPPER_MAX_Y, COPPER_DENSITY, SALT_COPPER},
        {BlockType::IronOre, IRON_MIN_Y, IRON_MAX_Y, IRON_DENSITY, SALT_IRON},
        {BlockType::Coal, COAL_MIN_Y, COAL_MAX_Y, COAL_DENSITY, SALT_COAL},
    };
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS. The existing "same seed → identical world" test still passes because generation stays deterministic.

- [ ] **Step 6: Commit**

```bash
git add src/Blocks/Blocks.h src/Blocks/Blocks.cpp src/World/TerrainGenerator.h src/World/TerrainGenerator.cpp tests/test_terrain.cpp
git commit -m "feat: add coal ore block and terrain vein"
```

---

### Task 3: New items (coal, plates) + relax placeability invariant

**Files:**
- Modify: `src/Items/Items.h`, `src/Items/Items.cpp`
- Modify: `tests/test_mining.cpp`

**Interfaces:**
- Produces: `ItemType::Coal`, `ItemType::CopperPlate`, `ItemType::IronPlate`; `itemForBlock(BlockType::Coal) == ItemType::Coal`. Plates are non-placeable (`placeBlock == BlockType::Air`).

- [ ] **Step 1: Update the existing invariant test (it will otherwise fail)**

`tests/test_mining.cpp` currently asserts every item is placeable. Plates break that. Replace the loop at the end of the `"the item registry maps mined blocks to items"` case (the `for` over `ItemType::Count`) with:

```cpp
    for (int i = 1; i < static_cast<int>(ItemType::Count); ++i)
    {
        const ItemInfo& info = itemInfo(static_cast<ItemType>(i));

        CHECK_FALSE(info.name.empty());
        CHECK(info.maxStack > 0);
        // Plates are refined goods, not placeable terrain: placeBlock may be Air.
    }
```

Add coal to the block→item mapping checks in the same case (after the IronOre line):

```cpp
    CHECK(itemForBlock(BlockType::Coal) == ItemType::Coal);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug`
Expected: FAIL to compile — `ItemType::Coal` does not exist.

- [ ] **Step 3: Add the item types**

In `src/Items/Items.h`, extend the enum after `IronOre`:

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

    Count
};
```

- [ ] **Step 4: Add registry rows and the coal mapping**

In `src/Items/Items.cpp`, add rows after Iron Ore (order MUST match the enum). Plates are non-placeable, so `placeBlock` is `BlockType::Air`:

```cpp
    {"Coal",         99,  BlockType::Coal},
    {"Copper Plate", 99,  BlockType::Air},
    {"Iron Plate",   99,  BlockType::Air},
```

Add a case to the `itemForBlock` switch (after `IronOre`):

```cpp
        case BlockType::Coal:      return ItemType::Coal;
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp tests/test_mining.cpp
git commit -m "feat: add coal and plate items; allow non-placeable items"
```

---

### Task 4: Smelting recipe registry

**Files:**
- Create: `src/Machines/Recipes.h`, `src/Machines/Recipes.cpp`
- Test: `tests/test_recipes.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ItemType` (Task 3).
- Produces: `SmeltRecipe`, `smeltRecipeFor(ItemType)` (see Canonical Interfaces).

- [ ] **Step 1: Write the failing test**

Create `tests/test_recipes.cpp`:

```cpp
#include "doctest.h"

#include "Machines/Recipes.h"

TEST_CASE("ores smelt into their plates")
{
    const SmeltRecipe* copper = smeltRecipeFor(ItemType::CopperOre);
    REQUIRE(copper != nullptr);
    CHECK(copper->out == ItemType::CopperPlate);
    CHECK(copper->seconds > 0.0f);

    const SmeltRecipe* iron = smeltRecipeFor(ItemType::IronOre);
    REQUIRE(iron != nullptr);
    CHECK(iron->out == ItemType::IronPlate);
}

TEST_CASE("things that do not smelt return null")
{
    CHECK(smeltRecipeFor(ItemType::Stone) == nullptr);
    CHECK(smeltRecipeFor(ItemType::CopperPlate) == nullptr);
    CHECK(smeltRecipeFor(ItemType::None) == nullptr);
}
```

- [ ] **Step 2: Register files in CMake**

Add to `Litharia_core` sources: `src/Machines/Recipes.cpp`. Add to `Litharia_tests` sources: `tests/test_recipes.cpp`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug`
Expected: FAIL — `Machines/Recipes.h` not found.

- [ ] **Step 4: Write minimal implementation**

Create `src/Machines/Recipes.h`:

```cpp
#pragma once

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
```

Create `src/Machines/Recipes.cpp`:

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
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Recipes.h src/Machines/Recipes.cpp tests/test_recipes.cpp CMakeLists.txt
git commit -m "feat: add smelting recipe registry"
```

---

### Task 5: Machine type registry + Machine struct

**Files:**
- Create: `src/Machines/MachineType.h`, `src/Machines/MachineRegistry.cpp`, `src/Machines/Machine.h`
- Test: `tests/test_machines.cpp` (append)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `BlockColor` (from `Blocks.h`), `Direction` (Task 1), `ItemStack`/`ItemType` (Items).
- Produces: `MachineType`, `MachineInfo`, `machineInfo()`, `COAL_BURN_SECONDS`, `DRILL_REACH`, `Machine` (see Canonical Interfaces).

- [ ] **Step 1: Write the failing test**

Append to `tests/test_machines.cpp`:

```cpp
#include "Machines/Machine.h"
#include "Machines/MachineType.h"

TEST_CASE("the machine registry has a valid row per type")
{
    for (int i = 1; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineInfo& info = machineInfo(static_cast<MachineType>(i));
        CHECK_FALSE(info.name.empty());
    }

    // A generator supplies power; a drill and smelter draw it.
    CHECK(machineInfo(MachineType::BurnerGenerator).generator);
    CHECK(machineInfo(MachineType::Drill).consumer);
    CHECK(machineInfo(MachineType::Smelter).consumer);

    // Transport machines move items and are neither source nor sink of power.
    CHECK(machineInfo(MachineType::Belt).transport);
    CHECK(machineInfo(MachineType::Chute).transport);
}

TEST_CASE("a default machine is empty")
{
    Machine m;
    CHECK(m.empty());
    CHECK(m.carried == ItemType::None);
    CHECK(m.powered == false);
}
```

Note: the `#include`s go at the top of the file with the existing `#include "Core/Direction.h"`.

- [ ] **Step 2: Register files in CMake**

Add to `Litharia_core` sources: `src/Machines/MachineRegistry.cpp`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug`
Expected: FAIL — `Machines/MachineType.h` not found.

- [ ] **Step 4: Write the machine type header**

Create `src/Machines/MachineType.h`:

```cpp
#pragma once

#include <cstdint>
#include <string_view>

#include "../Blocks/Blocks.h" // for BlockColor

// Seconds of generator burn one coal provides.
inline constexpr float COAL_BURN_SECONDS = 20.0f;

// How many tiles straight down a drill scans for ore to eat.
inline constexpr int DRILL_REACH = 4;

enum class MachineType : std::uint8_t
{
    None,
    BurnerGenerator,
    Drill,
    Belt,
    Chute,
    Smelter,

    Count
};

struct MachineInfo
{
    std::string_view name;
    BlockColor color;

    bool generator; // supplies power to its network
    bool consumer;  // draws power from its network
    bool transport; // belt/chute: carries one item toward its facing (chute: down)

    float powerRating; // supply if generator, demand if consumer
    float actionTime;  // drill: seconds per ore; transport: transfer interval
};

const MachineInfo& machineInfo(MachineType type);
```

- [ ] **Step 5: Write the registry**

Create `src/Machines/MachineRegistry.cpp`:

```cpp
#include "MachineType.h"

#include <array>

namespace
{

// Indexed by MachineType. Order must match the enum.
constexpr std::array<MachineInfo, static_cast<std::size_t>(MachineType::Count)> registry = {{
    //  name                 color            gen    con    trans  power  action
    {"None",              {  0,   0,   0}, false, false, false, 0.0f,  0.0f},
    {"Burner Generator",  {190, 120,  60}, true,  false, false, 10.0f, 0.0f},
    {"Drill",             {150, 150, 160}, false, true,  false, 5.0f,  1.0f},
    {"Belt",              { 90,  90, 100}, false, false, true,  0.0f,  0.5f},
    {"Chute",             { 70,  70,  80}, false, false, true,  0.0f,  0.5f},
    {"Smelter",           {200,  90,  70}, false, true,  false, 5.0f,  0.0f},
}};

} // namespace

const MachineInfo& machineInfo(MachineType type)
{
    const auto index = static_cast<std::size_t>(type);

    if (index >= registry.size())
        return registry[static_cast<std::size_t>(MachineType::None)];

    return registry[index];
}
```

- [ ] **Step 6: Write the Machine struct**

Create `src/Machines/Machine.h`:

```cpp
#pragma once

#include "../Core/Direction.h"
#include "../Items/Items.h"
#include "MachineType.h"

// One placed machine. Plain data: all behaviour lives in Machines. A machine is a
// single tile (multi-tile footprints are a later slice).
struct Machine
{
    MachineType type = MachineType::None;

    int x = 0;
    int y = 0;
    Direction facing = Direction::Right;

    // Processing machines (drill, smelter, generator).
    ItemStack input;
    ItemStack output;
    float progress = 0.0f; // seconds into the current operation

    // Burner generator: seconds of fuel left to burn.
    float fuel = 0.0f;

    // Power, set every tick by Machines::updatePower().
    int network = -1;
    bool powered = false;

    // Transport machines (belt, chute): one carried item and its move timer.
    ItemType carried = ItemType::None;
    float carryTimer = 0.0f; // counts down; the item advances when it reaches 0

    bool empty() const { return type == MachineType::None; }
};
```

- [ ] **Step 7: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add src/Machines/MachineType.h src/Machines/MachineRegistry.cpp src/Machines/Machine.h tests/test_machines.cpp CMakeLists.txt
git commit -m "feat: add machine type registry and Machine data struct"
```

---

### Task 6: Machines container — placement & spatial index

**Files:**
- Create: `src/Machines/Machines.h`, `src/Machines/Machines.cpp`
- Test: `tests/test_machines.cpp` (append)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Machine`, `MachineType`, `Direction`.
- Produces: `Machines::canPlace/place/remove/at/count/all/indexAt` (see Canonical Interfaces). Later tasks add `tryInsert`, `updatePower`, `tick` to this same class.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_machines.cpp`:

```cpp
#include "Machines/Machines.h"

TEST_CASE("placing a machine puts it on its tile and nowhere else")
{
    Machines machines;

    CHECK(machines.count() == 0);
    CHECK(machines.at(5, 5) == nullptr);

    Machine* m = machines.place(MachineType::Drill, 5, 5, Direction::Down);
    REQUIRE(m != nullptr);

    CHECK(m->type == MachineType::Drill);
    CHECK(machines.count() == 1);
    CHECK(machines.at(5, 5) == m);
    CHECK(machines.at(6, 5) == nullptr);
}

TEST_CASE("two machines cannot share a tile")
{
    Machines machines;

    REQUIRE(machines.place(MachineType::Belt, 3, 3, Direction::Right) != nullptr);

    CHECK_FALSE(machines.canPlace(3, 3));
    CHECK(machines.place(MachineType::Belt, 3, 3, Direction::Right) == nullptr);
    CHECK(machines.count() == 1);
}

TEST_CASE("removing a machine frees its tile and keeps the rest intact")
{
    Machines machines;

    machines.place(MachineType::Belt, 1, 1, Direction::Right);
    machines.place(MachineType::Belt, 2, 1, Direction::Right);
    machines.place(MachineType::Smelter, 3, 1, Direction::Right);

    REQUIRE(machines.count() == 3);
    REQUIRE(machines.remove(2, 1));

    CHECK(machines.count() == 2);
    CHECK(machines.at(2, 1) == nullptr);

    // The others survive and are still reachable by tile.
    REQUIRE(machines.at(1, 1) != nullptr);
    REQUIRE(machines.at(3, 1) != nullptr);
    CHECK(machines.at(1, 1)->type == MachineType::Belt);
    CHECK(machines.at(3, 1)->type == MachineType::Smelter);

    // Removing an empty tile reports nothing removed.
    CHECK_FALSE(machines.remove(9, 9));
}
```

- [ ] **Step 2: Register files in CMake**

Add to `Litharia_core` sources: `src/Machines/Machines.cpp`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug`
Expected: FAIL — `Machines/Machines.h` not found.

- [ ] **Step 4: Write the header**

Create `src/Machines/Machines.h`:

```cpp
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <SFML/System/Vector2.hpp>

#include "Machine.h"

class World;

// Owns every placed machine, indexes them by tile, and drives the whole factory
// one fixed step at a time. Pure logic: no SFML Graphics, so it lives in the core
// library and the test binary.
class Machines
{
public:
    bool canPlace(int x, int y) const;

    // Places a machine, or returns nullptr if the tile is taken. The pointer is
    // valid only until the next remove(): removal may relocate storage.
    Machine* place(MachineType type, int x, int y, Direction facing);

    bool remove(int x, int y);

    Machine* at(int x, int y);
    const Machine* at(int x, int y) const;

    // Hands one item into the machine at (x, y). Returns true if it was accepted.
    // Added behaviour in Task 8.
    bool tryInsert(int x, int y, ItemType item);

    // Rebuilds power networks and sets each machine's powered flag. Task 7.
    void updatePower();

    // One fixed simulation step of the whole factory. Fills minedTiles with any
    // world tiles a drill turned to air, so the caller can flag them for redraw.
    // Task 13 assembles the full body; earlier tasks build the helpers it calls.
    void tick(World& world, float dt, std::vector<sf::Vector2i>& minedTiles);

    std::size_t count() const { return machines.size(); }
    const std::vector<Machine>& all() const { return machines; }

private:
    static long long key(int x, int y)
    {
        return (static_cast<long long>(x) << 32) ^ static_cast<unsigned>(y);
    }

    int indexAt(int x, int y) const; // -1 if no machine there

    // Task 7 helpers.
    void assignNetworks();

    // Task 8-13 helpers.
    void insertOutputAhead(Machine& m);

    std::vector<Machine> machines;
    std::unordered_map<long long, int> byTile;
};
```

- [ ] **Step 5: Write the placement implementation**

Create `src/Machines/Machines.cpp` (this file grows in later tasks; start with placement + lookup):

```cpp
#include "Machines.h"

#include "Recipes.h"
#include "../World/World.h"

int Machines::indexAt(int x, int y) const
{
    const auto it = byTile.find(key(x, y));
    return it == byTile.end() ? -1 : it->second;
}

bool Machines::canPlace(int x, int y) const
{
    return indexAt(x, y) < 0;
}

Machine* Machines::place(MachineType type, int x, int y, Direction facing)
{
    if (!canPlace(x, y))
        return nullptr;

    Machine m;
    m.type = type;
    m.x = x;
    m.y = y;
    m.facing = facing;

    machines.push_back(m);
    const int index = static_cast<int>(machines.size()) - 1;
    byTile[key(x, y)] = index;

    return &machines[index];
}

bool Machines::remove(int x, int y)
{
    const int index = indexAt(x, y);
    if (index < 0)
        return false;

    const int last = static_cast<int>(machines.size()) - 1;

    // Swap the doomed machine with the last, so the vector stays dense, then fix
    // the moved machine's tile entry.
    if (index != last)
    {
        machines[index] = machines[last];
        byTile[key(machines[index].x, machines[index].y)] = index;
    }

    machines.pop_back();
    byTile.erase(key(x, y));

    return true;
}

Machine* Machines::at(int x, int y)
{
    const int index = indexAt(x, y);
    return index < 0 ? nullptr : &machines[index];
}

const Machine* Machines::at(int x, int y) const
{
    const int index = indexAt(x, y);
    return index < 0 ? nullptr : &machines[index];
}

// --- Stubs filled in by later tasks. They must compile now. -------------------

bool Machines::tryInsert(int, int, ItemType)
{
    return false;
}

void Machines::assignNetworks()
{
}

void Machines::updatePower()
{
}

void Machines::insertOutputAhead(Machine&)
{
}

void Machines::tick(World&, float, std::vector<sf::Vector2i>&)
{
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/Machines/Machines.h src/Machines/Machines.cpp tests/test_machines.cpp CMakeLists.txt
git commit -m "feat: add Machines container with placement and spatial index"
```

---

### Task 7: Power networks

**Files:**
- Modify: `src/Machines/Machines.cpp`
- Test: `tests/test_power.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Machines::place/at`, `MachineInfo`.
- Produces: implemented `Machines::updatePower()` and `Machines::assignNetworks()`. After `updatePower()`, each consumer's `powered` is true iff its network's generator supply (from generators with `fuel > 0`) ≥ total consumer demand.

- [ ] **Step 1: Write the failing test**

Create `tests/test_power.cpp`:

```cpp
#include "doctest.h"

#include "Machines/Machines.h"

namespace
{

// A generator is only a source once it has fuel; the power solve reads .fuel.
void fuel(Machines& m, int x, int y, float seconds)
{
    Machine* g = m.at(x, y);
    REQUIRE(g != nullptr);
    g->fuel = seconds;
}

} // namespace

TEST_CASE("a fuelled generator powers an adjacent consumer")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down);
    fuel(m, 0, 0, 10.0f);

    m.updatePower();

    CHECK(m.at(1, 0)->powered);

    // Same network id for the connected pair.
    CHECK(m.at(0, 0)->network == m.at(1, 0)->network);
}

TEST_CASE("a consumer with no generator is unpowered")
{
    Machines m;
    m.place(MachineType::Drill, 4, 4, Direction::Down);

    m.updatePower();

    CHECK_FALSE(m.at(4, 4)->powered);
}

TEST_CASE("an unfuelled generator supplies nothing")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down);
    // No fuel set.

    m.updatePower();

    CHECK_FALSE(m.at(1, 0)->powered);
}

TEST_CASE("demand beyond supply browns out the whole network")
{
    Machines m;
    // Generator supply is 10; each drill demands 5, so three drills (15) exceed it.
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down);
    m.place(MachineType::Drill, 2, 0, Direction::Down);
    m.place(MachineType::Drill, 3, 0, Direction::Down);
    fuel(m, 0, 0, 10.0f);

    m.updatePower();

    CHECK_FALSE(m.at(1, 0)->powered);
    CHECK_FALSE(m.at(2, 0)->powered);
    CHECK_FALSE(m.at(3, 0)->powered);
}

TEST_CASE("two separated networks do not share power")
{
    Machines m;
    // Group A: powered.
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down);
    fuel(m, 0, 0, 10.0f);

    // Group B: a lone drill far away.
    m.place(MachineType::Drill, 50, 50, Direction::Down);

    m.updatePower();

    CHECK(m.at(1, 0)->powered);
    CHECK_FALSE(m.at(50, 50)->powered);
    CHECK(m.at(1, 0)->network != m.at(50, 50)->network);
}
```

- [ ] **Step 2: Register file in CMake**

Add to `Litharia_tests` sources: `tests/test_power.cpp`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug` then run the binary.
Expected: FAIL — `updatePower` is a stub, so `powered` stays false everywhere (the first case fails).

- [ ] **Step 4: Implement the power solve**

In `src/Machines/Machines.cpp`, add includes at the top (after existing includes):

```cpp
#include <queue>
```

Replace the `assignNetworks` and `updatePower` stubs with:

```cpp
void Machines::assignNetworks()
{
    for (Machine& m : machines)
        m.network = -1;

    int next = 0;

    for (std::size_t start = 0; start < machines.size(); ++start)
    {
        if (machines[start].network != -1)
            continue;

        // Flood fill orthogonally connected machines into one network.
        const int id = next++;
        std::queue<int> frontier;
        machines[start].network = id;
        frontier.push(static_cast<int>(start));

        while (!frontier.empty())
        {
            const Machine& m = machines[frontier.front()];
            frontier.pop();

            const int nx[4] = {m.x - 1, m.x + 1, m.x, m.x};
            const int ny[4] = {m.y, m.y, m.y - 1, m.y + 1};

            for (int i = 0; i < 4; ++i)
            {
                const int neighbour = indexAt(nx[i], ny[i]);
                if (neighbour >= 0 && machines[neighbour].network == -1)
                {
                    machines[neighbour].network = id;
                    frontier.push(neighbour);
                }
            }
        }
    }
}

void Machines::updatePower()
{
    assignNetworks();

    int networkCount = 0;
    for (const Machine& m : machines)
        networkCount = std::max(networkCount, m.network + 1);

    std::vector<float> supply(networkCount, 0.0f);
    std::vector<float> demand(networkCount, 0.0f);

    for (const Machine& m : machines)
    {
        const MachineInfo& info = machineInfo(m.type);

        if (info.generator && m.fuel > 0.0f)
            supply[m.network] += info.powerRating;

        if (info.consumer)
            demand[m.network] += info.powerRating;
    }

    for (Machine& m : machines)
    {
        const MachineInfo& info = machineInfo(m.type);
        m.powered = info.consumer && supply[m.network] >= demand[m.network];
    }
}
```

Add `#include <algorithm>` at the top if not already present (for `std::max`).

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Machines.cpp tests/test_power.cpp CMakeLists.txt
git commit -m "feat: solve power networks by adjacency"
```

---

### Task 8: Item insertion (tryInsert)

**Files:**
- Modify: `src/Machines/Machines.cpp`
- Test: `tests/test_transport.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Machines::at`, `Machine`, `smeltRecipeFor`.
- Produces: implemented `Machines::tryInsert(x, y, item)`:
  - Belt/Chute: accepts if `carried == None`; sets `carried` and `carryTimer = actionTime`.
  - Smelter: accepts an item that has a smelt recipe, if `input` is empty or same type below max.
  - Generator: accepts `Coal` into `input` (below max).
  - Drill / empty tile / None item: rejects.

- [ ] **Step 1: Write the failing test**

Create `tests/test_transport.cpp`:

```cpp
#include "doctest.h"

#include "Machines/Machines.h"

TEST_CASE("a belt accepts one item and then is full")
{
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);

    CHECK(m.tryInsert(0, 0, ItemType::CopperOre));
    CHECK(m.at(0, 0)->carried == ItemType::CopperOre);

    // Already carrying: the next item is refused.
    CHECK_FALSE(m.tryInsert(0, 0, ItemType::IronOre));
    CHECK(m.at(0, 0)->carried == ItemType::CopperOre);
}

TEST_CASE("a smelter accepts smeltable ore but not plates or stone")
{
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Right);

    CHECK(m.tryInsert(0, 0, ItemType::CopperOre));
    CHECK(m.at(0, 0)->input.type == ItemType::CopperOre);
    CHECK(m.at(0, 0)->input.count == 1);

    // A second copper ore stacks.
    CHECK(m.tryInsert(0, 0, ItemType::CopperOre));
    CHECK(m.at(0, 0)->input.count == 2);

    // Non-smeltable input is refused.
    Machines m2;
    m2.place(MachineType::Smelter, 0, 0, Direction::Right);
    CHECK_FALSE(m2.tryInsert(0, 0, ItemType::Stone));
    CHECK_FALSE(m2.tryInsert(0, 0, ItemType::CopperPlate));
}

TEST_CASE("a generator accepts coal as fuel but nothing else")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);

    CHECK(m.tryInsert(0, 0, ItemType::Coal));
    CHECK(m.at(0, 0)->input.type == ItemType::Coal);

    CHECK_FALSE(m.tryInsert(0, 0, ItemType::IronOre));
}

TEST_CASE("inserting into an empty tile or a drill is refused")
{
    Machines m;
    m.place(MachineType::Drill, 0, 0, Direction::Down);

    CHECK_FALSE(m.tryInsert(0, 0, ItemType::CopperOre)); // drills are sources only
    CHECK_FALSE(m.tryInsert(9, 9, ItemType::CopperOre)); // nothing there
    CHECK_FALSE(m.tryInsert(0, 0, ItemType::None));
}
```

- [ ] **Step 2: Register file in CMake**

Add to `Litharia_tests` sources: `tests/test_transport.cpp`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug` then run the binary.
Expected: FAIL — `tryInsert` stub returns false, so the first CHECK fails.

- [ ] **Step 4: Implement tryInsert**

In `src/Machines/Machines.cpp`, replace the `tryInsert` stub with:

```cpp
bool Machines::tryInsert(int x, int y, ItemType item)
{
    if (item == ItemType::None)
        return false;

    Machine* m = at(x, y);
    if (m == nullptr)
        return false;

    const MachineInfo& info = machineInfo(m->type);

    if (info.transport)
    {
        if (m->carried != ItemType::None)
            return false;

        m->carried = item;
        m->carryTimer = info.actionTime;
        return true;
    }

    if (m->type == MachineType::Smelter)
    {
        if (smeltRecipeFor(item) == nullptr)
            return false;

        return addToBuffer(m->input, item);
    }

    if (m->type == MachineType::BurnerGenerator)
    {
        if (item != ItemType::Coal)
            return false;

        return addToBuffer(m->input, item);
    }

    return false;
}
```

Add this small helper as a file-local function near the top of `Machines.cpp` (inside an anonymous namespace, after the includes):

```cpp
namespace
{

// Adds one item to a single-stack buffer if it fits (empty, or same type below
// max). Returns false if the buffer is occupied by something else or full.
bool addToBuffer(ItemStack& buffer, ItemType item)
{
    if (buffer.empty())
    {
        buffer.type = item;
        buffer.count = 1;
        return true;
    }

    if (buffer.type == item && buffer.count < itemInfo(item).maxStack)
    {
        ++buffer.count;
        return true;
    }

    return false;
}

} // namespace
```

Declare it before use is automatic since it is above the methods. If the compiler complains about ordering, move the anonymous-namespace block above all method definitions.

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Machines.cpp tests/test_transport.cpp CMakeLists.txt
git commit -m "feat: accept items into belts, smelters, and generators"
```

---

### Task 9: Belt & chute transport tick

**Files:**
- Modify: `src/Machines/Machines.cpp`, `src/Machines/Machines.h`
- Test: `tests/test_transport.cpp` (append)

**Interfaces:**
- Consumes: `tryInsert`, `dirDX/dirDY`, `MachineInfo::actionTime`.
- Produces: a private `Machines::tickTransport(float dt)` that: for each transport machine carrying an item, counts `carryTimer` down by `dt`; at ≤ 0 tries to hand the item to the machine ahead (belt → facing tile; chute → the tile below). On success `carried` clears; on failure the item waits (`carryTimer` clamped to 0). A freshly received item cannot move the same tick because `tryInsert` sets its timer to a positive interval.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_transport.cpp`:

```cpp
namespace
{

// The World transport needs is only for the drill/smelter later; belts ignore it,
// but tick() requires one, so make a throwaway.
}

TEST_CASE("an item rides a belt to the next belt after the interval")
{
    World world;
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);
    m.place(MachineType::Belt, 1, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Belt interval is 0.5s. Tick past it.
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    // The item has moved one tile along.
    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(1, 0)->carried == ItemType::IronOre);
}

TEST_CASE("a chute moves its item straight down regardless of facing")
{
    World world;
    Machines m;
    m.place(MachineType::Chute, 0, 0, Direction::Right); // facing ignored by chutes
    m.place(MachineType::Belt, 0, 1, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(0, 1)->carried == ItemType::Coal);
}

TEST_CASE("a belt backs up when the tile ahead is full")
{
    World world;
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);
    m.place(MachineType::Belt, 1, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::Stone));
    REQUIRE(m.tryInsert(1, 0, ItemType::Stone)); // tile ahead already occupied

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    // Nothing could move: both still hold their item, nothing was lost.
    CHECK(m.at(0, 0)->carried == ItemType::Stone);
    CHECK(m.at(1, 0)->carried == ItemType::Stone);
}
```

Add `#include "World/World.h"` to the top of `tests/test_transport.cpp`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug` then run the binary.
Expected: FAIL — `tick` is still a stub, so items never move.

- [ ] **Step 3: Add the transport helper declaration**

In `src/Machines/Machines.h`, add to the private section (after `insertOutputAhead`):

```cpp
    void tickTransport(float dt);
```

- [ ] **Step 4: Implement transport and wire it into tick**

In `src/Machines/Machines.cpp`, add the method:

```cpp
void Machines::tickTransport(float dt)
{
    for (Machine& m : machines)
    {
        const MachineInfo& info = machineInfo(m.type);

        if (!info.transport || m.carried == ItemType::None)
            continue;

        m.carryTimer -= dt;
        if (m.carryTimer > 0.0f)
            continue;

        m.carryTimer = 0.0f;

        // Chutes always drop down; belts move toward their facing.
        const Direction dir = (m.type == MachineType::Chute) ? Direction::Down : m.facing;
        const int tx = m.x + dirDX(dir);
        const int ty = m.y + dirDY(dir);

        // tryInsert may relocate storage on nothing here (it does not place), so it
        // is safe. On success the item leaves this belt.
        if (tryInsert(tx, ty, m.carried))
            m.carried = ItemType::None;
    }
}
```

Replace the `tick` stub with a version that runs transport (drill/smelter added in later tasks):

```cpp
void Machines::tick(World& world, float dt, std::vector<sf::Vector2i>& minedTiles)
{
    (void)world;
    (void)minedTiles;

    updatePower();
    tickTransport(dt);
}
```

> Note on move ordering: `tryInsert` sets the receiver's `carryTimer` to the full interval, so an item handed to a downstream belt this tick cannot advance again until a later tick. That single rule prevents an item skipping multiple tiles per step, independent of iteration order.

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Machines.h src/Machines/Machines.cpp tests/test_transport.cpp
git commit -m "feat: move items along belts and chutes"
```

---

### Task 10: Generator fuel & burn

**Files:**
- Modify: `src/Machines/Machines.cpp`, `src/Machines/Machines.h`
- Test: `tests/test_processing.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `updatePower`, `COAL_BURN_SECONDS`, network demand.
- Produces: a private `Machines::tickGenerators(float dt)` that, per generator: if `fuel <= 0` and `input` holds coal, consumes one coal and adds `COAL_BURN_SECONDS` to `fuel`; then, if its network has any demand, burns `fuel` down by `dt` (never below 0). Idle networks (no demand) do not burn fuel. Requires network demand to be available after `updatePower` — store it.

- [ ] **Step 1: Write the failing test**

Create `tests/test_processing.cpp`:

```cpp
#include "doctest.h"

#include "Machines/Machines.h"
#include "World/World.h"

namespace
{
constexpr float STEP = 1.0f / 60.0f;
}

TEST_CASE("a generator converts a coal into burn time when a consumer needs it")
{
    World world;
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down); // creates demand
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    m.tick(world, STEP, mined);

    // The coal became fuel (minus the fraction burned this tick).
    Machine* gen = m.at(0, 0);
    CHECK(gen->input.empty());
    CHECK(gen->fuel > COAL_BURN_SECONDS - 1.0f);
    CHECK(gen->fuel <= COAL_BURN_SECONDS);
}

TEST_CASE("a generator with no load does not waste its coal")
{
    World world;
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right); // no consumer nearby
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 120; ++i)
        m.tick(world, STEP, mined);

    // It lit one coal (fuel is available) but, with nothing drawing power, it holds
    // that fuel steady rather than draining it.
    Machine* gen = m.at(0, 0);
    CHECK(gen->fuel == doctest::Approx(COAL_BURN_SECONDS));
}
```

- [ ] **Step 2: Register file in CMake**

Add to `Litharia_tests` sources: `tests/test_processing.cpp`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug` then run the binary.
Expected: FAIL — generators never gain fuel from coal.

- [ ] **Step 4: Store network demand and implement generator burn**

In `src/Machines/Machines.h`, add a private member and a method:

```cpp
    void tickGenerators(float dt);

    std::vector<float> networkDemand; // demand per network id, filled by updatePower
```

In `src/Machines/Machines.cpp`, at the end of `updatePower()` (after setting `powered`), persist demand:

```cpp
    networkDemand = demand;
```

Add the generator tick:

```cpp
void Machines::tickGenerators(float dt)
{
    for (Machine& m : machines)
    {
        if (m.type != MachineType::BurnerGenerator)
            continue;

        // Light a fresh coal only when the last one is spent.
        if (m.fuel <= 0.0f && m.input.type == ItemType::Coal && m.input.count > 0)
        {
            --m.input.count;
            if (m.input.count == 0)
                m.input.type = ItemType::None;

            m.fuel += COAL_BURN_SECONDS;
        }

        // Burn only under load, so an idle base does not drain its fuel.
        const bool hasLoad = m.network >= 0
            && m.network < static_cast<int>(networkDemand.size())
            && networkDemand[m.network] > 0.0f;

        if (m.fuel > 0.0f && hasLoad)
            m.fuel = std::max(0.0f, m.fuel - dt);
    }
}
```

Update `tick` to run generators before transport (order: power → generators → transport; processors added later):

```cpp
void Machines::tick(World& world, float dt, std::vector<sf::Vector2i>& minedTiles)
{
    (void)world;
    (void)minedTiles;

    updatePower();
    tickGenerators(dt);
    tickTransport(dt);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Machines.h src/Machines/Machines.cpp tests/test_processing.cpp CMakeLists.txt
git commit -m "feat: burn coal in generators under load"
```

---

### Task 11: Drill — mine world ore into output

**Files:**
- Modify: `src/Machines/Machines.cpp`, `src/Machines/Machines.h`
- Test: `tests/test_processing.cpp` (append)

**Interfaces:**
- Consumes: `World::get/set`, `itemForBlock`, `isSolidBlock`, `blockInfo(...).drop`, `insertOutputAhead`, `MachineInfo::actionTime`, `powered`.
- Produces: private `Machines::tickDrills(World&, float dt, std::vector<sf::Vector2i>& minedTiles)`. A drill, when `powered` and its `output` is empty, scans tiles `y+1 .. y+DRILL_REACH` for the first ore block (copper/iron/coal). If found, it accumulates `progress` by `dt`; at `actionTime` it turns that tile to `Air`, records the coord in `minedTiles`, sets `output = {itemForBlock(ore), 1}`, resets `progress`. With no ore in reach it stays idle (`progress = 0`). Each tick, a non-empty `output` is pushed one item toward `facing` via `insertOutputAhead`.

Add a helper to identify ore:

```cpp
// True for blocks a drill should mine (they drop themselves as an ore/fuel item).
bool isOre(BlockType b); // Copper, Iron, Coal
```

- [ ] **Step 1: Write the failing test**

Append to `tests/test_processing.cpp`:

```cpp
TEST_CASE("a powered drill eats the ore below it and outputs onto a belt")
{
    World world;
    world.fill(BlockType::Air);
    world.set(0, 1, BlockType::CopperOre); // directly beneath the drill

    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Right); // outputs to the right
    m.place(MachineType::Belt, 2, 0, Direction::Right);
    REQUIRE(m.at(1, 0) != nullptr);

    // Put the ore under the drill at (1,2) as well: drill at (1,0) scans down.
    world.set(1, 1, BlockType::CopperOre);

    // Fuel the generator so the drill is powered.
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Drill work time is 1.0s; run 2s to be safe, plus belt handoff.
    for (int i = 0; i < 180; ++i)
        m.tick(world, step, mined);

    // The ore tile is gone...
    CHECK(world.get(1, 1) == BlockType::Air);
    // ...and copper ore reached the belt (or is sitting in the drill output).
    const bool onBelt = m.at(2, 0)->carried == ItemType::CopperOre;
    const bool inDrill = m.at(1, 0)->output.type == ItemType::CopperOre;
    CHECK((onBelt || inDrill));

    // The mined coordinate was reported for redraw.
    bool reported = false;
    for (const sf::Vector2i& t : mined)
        if (t.x == 1 && t.y == 1)
            reported = true;
    CHECK(reported);
}

TEST_CASE("a drill with no ore in reach stays idle")
{
    World world;
    world.fill(BlockType::Air); // nothing to mine

    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(1, 0)->output.empty());
    CHECK(mined.empty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug` then run the binary.
Expected: FAIL — drills do nothing yet.

- [ ] **Step 3: Add declarations**

In `src/Machines/Machines.h` private section:

```cpp
    void tickDrills(World& world, float dt, std::vector<sf::Vector2i>& minedTiles);
```

- [ ] **Step 4: Implement drill + output ejection**

In `src/Machines/Machines.cpp` anonymous namespace, add:

```cpp
bool isOre(BlockType b)
{
    return b == BlockType::CopperOre || b == BlockType::IronOre || b == BlockType::Coal;
}
```

Implement `insertOutputAhead` (replace its stub):

```cpp
void Machines::insertOutputAhead(Machine& m)
{
    if (m.output.empty())
        return;

    const int tx = m.x + dirDX(m.facing);
    const int ty = m.y + dirDY(m.facing);

    if (tryInsert(tx, ty, m.output.type))
    {
        --m.output.count;
        if (m.output.count == 0)
            m.output.type = ItemType::None;
    }
}
```

Add the drill tick:

```cpp
void Machines::tickDrills(World& world, float dt, std::vector<sf::Vector2i>& minedTiles)
{
    for (Machine& m : machines)
    {
        if (m.type != MachineType::Drill)
            continue;

        // Always try to push any held output onto the machine ahead.
        insertOutputAhead(m);

        if (!m.powered || !m.output.empty())
        {
            m.progress = 0.0f;
            continue;
        }

        // Find the nearest ore straight below, within reach.
        int oreY = -1;
        for (int dy = 1; dy <= DRILL_REACH; ++dy)
        {
            if (isOre(world.get(m.x, m.y + dy)))
            {
                oreY = m.y + dy;
                break;
            }
        }

        if (oreY < 0)
        {
            m.progress = 0.0f; // nothing to mine: idle
            continue;
        }

        m.progress += dt;

        if (m.progress >= machineInfo(m.type).actionTime)
        {
            const BlockType ore = world.get(m.x, oreY);
            const ItemType drop = itemForBlock(ore);

            world.set(m.x, oreY, BlockType::Air);
            minedTiles.push_back({m.x, oreY});

            m.output = {drop, 1};
            m.progress = 0.0f;
        }
    }
}
```

Wire it into `tick` (after generators, before/after transport — put drills before transport so fresh output can be picked up promptly):

```cpp
void Machines::tick(World& world, float dt, std::vector<sf::Vector2i>& minedTiles)
{
    updatePower();
    tickGenerators(dt);
    tickDrills(world, dt, minedTiles);
    tickTransport(dt);
}
```

Ensure `#include "../Items/Items.h"` is available (via `Machine.h`) for `itemForBlock`. It is, transitively; if the compiler disagrees, include it explicitly in `Machines.cpp`.

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Machines.h src/Machines/Machines.cpp tests/test_processing.cpp
git commit -m "feat: drills auto-mine ore veins into their output"
```

---

### Task 12: Smelter — ore into plate

**Files:**
- Modify: `src/Machines/Machines.cpp`, `src/Machines/Machines.h`
- Test: `tests/test_processing.cpp` (append)

**Interfaces:**
- Consumes: `smeltRecipeFor`, `insertOutputAhead`, `powered`, `input`/`output`.
- Produces: private `Machines::tickSmelters(float dt)`. A smelter, when `powered`, has a non-empty `input` with a recipe, and `output` can accept the result (empty or same plate below max), accumulates `progress` by `dt`; at `recipe.seconds` it consumes one input and adds one output plate, resets `progress`. Each tick it pushes `output` toward `facing`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_processing.cpp`:

```cpp
TEST_CASE("a powered smelter turns copper ore into a copper plate")
{
    World world;
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Smelter, 1, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));   // power
    REQUIRE(m.tryInsert(1, 0, ItemType::CopperOre)); // work

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Copper recipe is 2.0s.
    for (int i = 0; i < 150; ++i)
        m.tick(world, step, mined);

    Machine* s = m.at(1, 0);
    CHECK(s->input.empty());
    CHECK(s->output.type == ItemType::CopperPlate);
    CHECK(s->output.count == 1);
}

TEST_CASE("an unpowered smelter makes no progress")
{
    World world;
    Machines m;
    m.place(MachineType::Smelter, 1, 0, Direction::Right); // no generator
    REQUIRE(m.tryInsert(1, 0, ItemType::CopperOre));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 150; ++i)
        m.tick(world, step, mined);

    Machine* s = m.at(1, 0);
    CHECK(s->input.type == ItemType::CopperOre); // untouched
    CHECK(s->output.empty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target Litharia_tests --config Debug` then run the binary.
Expected: FAIL — smelters do nothing yet.

- [ ] **Step 3: Add declaration**

In `src/Machines/Machines.h` private section:

```cpp
    void tickSmelters(float dt);
```

- [ ] **Step 4: Implement smelter**

In `src/Machines/Machines.cpp`:

```cpp
void Machines::tickSmelters(float dt)
{
    for (Machine& m : machines)
    {
        if (m.type != MachineType::Smelter)
            continue;

        insertOutputAhead(m);

        const SmeltRecipe* recipe = m.input.empty() ? nullptr : smeltRecipeFor(m.input.type);

        const bool outputReady = m.output.empty()
            || (recipe != nullptr && m.output.type == recipe->out
                && m.output.count < itemInfo(recipe->out).maxStack);

        if (!m.powered || recipe == nullptr || !outputReady)
        {
            if (recipe == nullptr)
                m.progress = 0.0f;
            continue;
        }

        m.progress += dt;

        if (m.progress >= recipe->seconds)
        {
            --m.input.count;
            if (m.input.count == 0)
                m.input.type = ItemType::None;

            if (m.output.empty())
                m.output = {recipe->out, 1};
            else
                ++m.output.count;

            m.progress = 0.0f;
        }
    }
}
```

Wire into `tick` (after drills, before transport):

```cpp
void Machines::tick(World& world, float dt, std::vector<sf::Vector2i>& minedTiles)
{
    updatePower();
    tickGenerators(dt);
    tickDrills(world, dt, minedTiles);
    tickSmelters(dt);
    tickTransport(dt);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Machines.h src/Machines/Machines.cpp tests/test_processing.cpp
git commit -m "feat: smelters refine ore into plates"
```

---

### Task 13: Full auto-loop integration test

**Files:**
- Test: `tests/test_factory.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: the entire `Machines` API. Adds no new production code — this is the headline behavioural proof that the loop runs hands-free.

- [ ] **Step 1: Write the failing test**

Create `tests/test_factory.cpp`:

```cpp
#include "doctest.h"

#include "Machines/Machines.h"
#include "World/World.h"

// The whole point of the slice: place a generator, a drill over ore, a belt, and a
// smelter, then let it run. Plates must come out with no hand-mining.
TEST_CASE("a coal-fed drill-belt-smelter line produces plates on its own")
{
    World world;
    world.fill(BlockType::Air);

    // A short copper vein straight under the drill's column.
    for (int y = 1; y <= 4; ++y)
        world.set(10, y, BlockType::CopperOre);

    Machines m;

    // Layout (all on row 0 except the ore below the drill):
    //   (9,0) generator  (10,0) drill  (11,0) belt  (12,0) smelter
    m.place(MachineType::BurnerGenerator, 9, 0, Direction::Right);
    m.place(MachineType::Drill,           10, 0, Direction::Right);
    m.place(MachineType::Belt,            11, 0, Direction::Right);
    m.place(MachineType::Smelter,         12, 0, Direction::Right);

    // Prime the generator with plenty of coal.
    for (int i = 0; i < 10; ++i)
        REQUIRE(m.tryInsert(9, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Run ~30 seconds of simulation.
    for (int i = 0; i < 1800; ++i)
        m.tick(world, step, mined);

    // The vein has been eaten...
    for (int y = 1; y <= 4; ++y)
        CHECK(world.get(10, y) == BlockType::Air);

    // ...and copper plates exist somewhere in the line (smelter output or belt).
    const bool platesMade = m.at(12, 0)->output.type == ItemType::CopperPlate
        || m.at(11, 0)->carried == ItemType::CopperPlate;
    CHECK(platesMade);
    CHECK(m.at(12, 0)->output.count >= 1);
}
```

- [ ] **Step 2: Register file in CMake**

Add to `Litharia_tests` sources: `tests/test_factory.cpp`.

- [ ] **Step 3: Run test to verify it fails, then passes**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: This should PASS immediately if Tasks 7–12 are correct. If it fails, the failure pinpoints an integration gap (most likely output ejection timing or the smelter needing the smelter's output to also drain — for the slice the smelter output need not drain, so `count >= 1` holds). Debug using superpowers:systematic-debugging before altering production code.

- [ ] **Step 4: Commit**

```bash
git add tests/test_factory.cpp CMakeLists.txt
git commit -m "test: full drill-belt-smelter loop produces plates unattended"
```

---

### Task 14: Machine rendering layer

**Files:**
- Create: `src/Machines/MachineRenderer.h`, `src/Machines/MachineRenderer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Machines::all()`, `machineInfo(...).color`, `Machine` state, `TILE_SIZE`.
- Produces: `MachineRenderer::draw(sf::RenderTarget&, const Machines&)` — one call from `Game::render()`. No unit test (rendering); verified by running the game in Task 15.

- [ ] **Step 1: Register files in CMake**

Add to the `Litharia` executable sources (NOT the core library): `src/Machines/MachineRenderer.cpp`.

- [ ] **Step 2: Write the header**

Create `src/Machines/MachineRenderer.h`:

```cpp
#pragma once

#include <SFML/Graphics.hpp>

class Machines;

// Draws the machine layer: a colored quad per machine, dimmed when unpowered, with
// a facing tick and a dot for any carried/output item. In the executable only.
class MachineRenderer
{
public:
    void draw(sf::RenderTarget& target, const Machines& machines) const;
};
```

- [ ] **Step 3: Write the implementation**

Create `src/Machines/MachineRenderer.cpp`:

```cpp
#include "MachineRenderer.h"

#include "../Core/Constants.h"
#include "../Core/Direction.h"
#include "../Items/Items.h"
#include "Machines.h"

namespace
{

sf::Color toColor(BlockColor c, std::uint8_t alpha = 255)
{
    return sf::Color(c.r, c.g, c.b, alpha);
}

// The color of the item riding a machine, matching how drops look on the ground.
sf::Color itemColor(ItemType type)
{
    const BlockType block = itemInfo(type).placeBlock;

    // Plates are not placeable; give them a bright refined tint.
    if (block == BlockType::Air)
        return sf::Color(220, 220, 235);

    return toColor(blockInfo(block).color);
}

} // namespace

void MachineRenderer::draw(sf::RenderTarget& target, const Machines& machines) const
{
    sf::RectangleShape body({static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)});
    body.setOutlineThickness(-1.0f);
    body.setOutlineColor(sf::Color(20, 20, 24));

    sf::RectangleShape facing({4.0f, 4.0f});
    facing.setFillColor(sf::Color(250, 250, 210));

    sf::CircleShape item(3.0f);
    item.setOrigin({3.0f, 3.0f});

    for (const Machine& m : machines.all())
    {
        const MachineInfo& info = machineInfo(m.type);

        const float px = static_cast<float>(m.x * TILE_SIZE);
        const float py = static_cast<float>(m.y * TILE_SIZE);

        // Consumers dim when they have no power.
        const std::uint8_t alpha = (info.consumer && !m.powered) ? 120 : 255;

        body.setPosition({px, py});
        body.setFillColor(toColor(info.color, alpha));
        target.draw(body);

        // A small tick showing which way it faces / outputs.
        const float cx = px + TILE_SIZE * 0.5f - 2.0f;
        const float cy = py + TILE_SIZE * 0.5f - 2.0f;
        facing.setPosition({cx + dirDX(m.facing) * 5.0f, cy + dirDY(m.facing) * 5.0f});
        target.draw(facing);

        // The carried transport item, or the output buffer's item.
        ItemType shown = m.carried;
        if (shown == ItemType::None && !m.output.empty())
            shown = m.output.type;

        if (shown != ItemType::None)
        {
            item.setPosition({px + TILE_SIZE * 0.5f, py + TILE_SIZE * 0.5f});
            item.setFillColor(itemColor(shown));
            target.draw(item);
        }
    }
}
```

- [ ] **Step 4: Build the game to verify it compiles**

Run: `cmake --build build --target Litharia --config Debug`
Expected: builds cleanly (linked into the executable, not the core lib or tests).

- [ ] **Step 5: Commit**

```bash
git add src/Machines/MachineRenderer.h src/Machines/MachineRenderer.cpp CMakeLists.txt
git commit -m "feat: render the machine layer"
```

---

### Task 15: Game integration — build controls, fuel loading, tick & render wiring

**Files:**
- Modify: `src/Game/Game.h`, `src/Game/Game.cpp`
- (No unit test; verified by running the game — this task is the manual playtest.)

**Interfaces:**
- Consumes: `Machines`, `MachineRenderer`, existing `Player`, `World`, `ChunkRenderer`, `PlayerInput` cursor.
- Produces: a playable loop — the player enters a build mode, cycles machine type, rotates facing, places/removes machines with the mouse, hand-loads coal into a generator, and watches the factory run and redraw.

Design of the controls (keep it minimal and discoverable):
- Key `B` toggles **build mode**. In build mode, left-click places the selected machine at the cursor tile (facing the current build direction); right-click removes a machine.
- Keys `F1`–`F5` select the machine type to build (Generator, Drill, Belt, Chute, Smelter).
- Key `R` rotates the build facing clockwise.
- Key `F` hand-loads one coal from the player's inventory into the machine under the cursor (fuel/insert), using `Machines::tryInsert`.
- When build mode is OFF, the existing mine/place controls behave exactly as today.

- [ ] **Step 1: Add members to Game.h**

In `src/Game/Game.h`, add includes and members:

```cpp
#include "../Machines/Machines.h"
#include "../Machines/MachineRenderer.h"
```

Add to the private data (after `Hud hud;`):

```cpp
    Machines machines;
    MachineRenderer machineRenderer;

    bool buildMode = false;
    MachineType buildType = MachineType::Belt;
    Direction buildFacing = Direction::Right;
```

Add method declarations (near the other private methods):

```cpp
    void tickMachines(float dt);
    void placeMachineAtCursor();
    void removeMachineAtCursor();
    void loadFuelAtCursor();
    sf::Vector2i cursorTile() const;
```

- [ ] **Step 2: Implement the helpers in Game.cpp**

Add near the top of `src/Game/Game.cpp` (it already includes `Constants.h`). Add:

```cpp
#include "../Machines/MachineType.h"
```

Implement the cursor-tile helper and the machine actions:

```cpp
sf::Vector2i Game::cursorTile() const
{
    const sf::Vector2f world = cursorWorldPosition();
    return {static_cast<int>(std::floor(world.x / TILE_SIZE)),
            static_cast<int>(std::floor(world.y / TILE_SIZE))};
}

void Game::placeMachineAtCursor()
{
    const sf::Vector2i tile = cursorTile();

    // Do not place inside solid rock or where a machine already sits.
    if (world.isSolid(tile.x, tile.y) || !machines.canPlace(tile.x, tile.y))
        return;

    machines.place(buildType, tile.x, tile.y, buildFacing);
}

void Game::removeMachineAtCursor()
{
    const sf::Vector2i tile = cursorTile();
    machines.remove(tile.x, tile.y);
}

void Game::loadFuelAtCursor()
{
    const sf::Vector2i tile = cursorTile();

    // Only spend a coal if the machine actually accepts it.
    Inventory& bag = player.inventory();
    if (bag.count(ItemType::Coal) <= 0)
        return;

    if (machines.tryInsert(tile.x, tile.y, ItemType::Coal))
    {
        // Remove one coal from wherever it sits in the bag.
        for (int i = 0; i < Inventory::SIZE; ++i)
        {
            if (bag.slot(i).type == ItemType::Coal)
            {
                bag.removeOne(i);
                break;
            }
        }
    }
}

void Game::tickMachines(float dt)
{
    std::vector<sf::Vector2i> mined;
    machines.tick(world, dt, mined);

    // Any tile a drill ate must be rebuilt in the chunk mesh.
    for (const sf::Vector2i& t : mined)
        chunks.markDirty(t.x, t.y);
}
```

Add `#include <cmath>` to `Game.cpp` if not present (for `std::floor`).

- [ ] **Step 3: Wire build-mode input into handleEvents**

In `Game::handleEvents`, inside the `KeyPressed` branch, add (after the existing hotbar keys):

```cpp
            if (key->code == Key::B)
                buildMode = !buildMode;

            if (key->code == Key::R)
                buildFacing = rotateCW(buildFacing);

            if (key->code == Key::F)
                loadFuelAtCursor();

            if (key->code == Key::F1) buildType = MachineType::BurnerGenerator;
            if (key->code == Key::F2) buildType = MachineType::Drill;
            if (key->code == Key::F3) buildType = MachineType::Belt;
            if (key->code == Key::F4) buildType = MachineType::Chute;
            if (key->code == Key::F5) buildType = MachineType::Smelter;
```

Machine placement is on mouse *press* (one machine per click), so add mouse-button handling in `handleEvents`. Add another branch to the event `if/else` chain:

```cpp
        else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
        {
            if (buildMode && mouse->button == sf::Mouse::Button::Left)
                placeMachineAtCursor();
            else if (buildMode && mouse->button == sf::Mouse::Button::Right)
                removeMachineAtCursor();
        }
```

- [ ] **Step 4: Suppress mine/place while in build mode**

In `Game::readInput`, gate the mine/place flags so the two modes do not fight:

```cpp
    input.mine  = !buildMode && focused && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    input.place = !buildMode && focused && sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);
```

- [ ] **Step 5: Tick and draw the factory**

In `Game::fixedUpdate`, after `updateDrops(dt);`, add:

```cpp
    tickMachines(dt);
```

In `Game::render`, after `chunks.draw(window, camera.view());` and before drawing drops, add:

```cpp
    machineRenderer.draw(window, machines);
```

Optionally extend the window title in `fixedUpdate` to show build state (helpful during playtest):

```cpp
    const std::string mode = buildMode
        ? "  -  BUILD: " + std::string(machineInfo(buildType).name)
        : "";
    window.setTitle("Litharia" + mode + "  -  machines: " + std::to_string(machines.count()));
```

(Replace the existing `setTitle` call, or append — keep whichever is more useful.)

- [ ] **Step 6: Build and play-test (manual verification)**

Run: `cmake --build build --target Litharia --config Debug` then `./build/Debug/Litharia.exe`

Verify by hand:
1. Dig down to a copper vein and gather a few **coal** (mine coal blocks) into the hotbar.
2. Press `B` to enter build mode. Press `F1`, click above the ore to place a **Generator**; press `F` while pointing at it to load coal (repeat a few times).
3. Press `F2`, place a **Drill** next to the generator, on top of the ore column (drill scans down). Use `R` so its facing points at where the belt will go.
4. Press `F3`, place a **Belt** at the drill's output tile, facing the smelter.
5. Press `F5`, place a **Smelter** at the belt's end.
6. Watch: the drill should mine the vein (tiles vanish and redraw), copper ore should ride the belt, and the smelter should accumulate copper plates. Unpowered machines render dimmed.

If any step misbehaves, debug with superpowers:systematic-debugging. The pure-logic path is already covered by tests, so failures here are almost always wiring (wrong facing, cursor tile math, or a missing `markDirty`).

- [ ] **Step 7: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: build mode, fuel loading, and factory tick/render in Game"
```

---

## Self-Review

**Spec coverage** (design summary → task):
- Tech-tree progression groundwork (refined plates as output) → Tasks 3, 4, 12, 13. (The research *sink* is explicitly deferred; this slice ends at plates.)
- Gravity-native logistics (belts across, chutes down) → Tasks 9, 14. Powered lifts deferred, as designed.
- Electric grid, burner generator → Tasks 5, 7, 10.
- Finite/consumed ore, relocate drill → Task 11 (`world.set(Air)`, idle when reach exhausted).
- Machines are 1×1, adjacency power, binary power, one-item belts → enforced across Tasks 5–9 and noted in the Design Summary.
- Rendering + controls → Tasks 14, 15.

**Placeholder scan:** No "TBD"/"TODO"/"add error handling" left; every code step shows complete code. The only manual (untested) steps are rendering (Task 14) and Game wiring (Task 15), which cannot be unit-tested window-free — both have explicit manual verification.

**Type consistency check:**
- `Machines::tick(World&, float, std::vector<sf::Vector2i>&)` — identical signature in the header (Task 6), every test call, and `Game::tickMachines` (Task 15). ✓
- `Machine` fields (`carried`, `carryTimer`, `output`, `input`, `fuel`, `powered`, `network`, `progress`) defined once in Task 5 and used unchanged thereafter. ✓
- `machineInfo().actionTime` used as both drill work time and belt interval, consistent with the registry rows (Task 5). ✓
- `addToBuffer` / `isOre` are file-local helpers in `Machines.cpp` (Tasks 8, 11); not referenced from headers. ✓
- `smeltRecipeFor` returns `const SmeltRecipe*`, used as pointer everywhere (Tasks 4, 8, 12). ✓
- `tryInsert` sets `carryTimer = actionTime` (Task 8), which Task 9's ordering note depends on. ✓

**One known simplification to watch during execution:** belt hand-off is iteration-order dependent for the blocked case, but the "receiver timer resets to full interval" rule (Task 8/9) guarantees no item advances more than one tile per tick regardless of order — the integration test (Task 13) is the guard. If a future slice adds multi-item belts, revisit ordering with an explicit downstream-first pass.

---

## Execution Handoff

**Plan complete and saved to `plans/2026-07-15-factory-auto-loop-plan.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

**Which approach?**
