# Fluids and Obsidian Generation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a real fluid simulation (Water and Lava, 8 discrete levels each, falling/spreading/settling) with finite world-generated pools (surface lakes, underground water pools, deep lava pools), and a Water+Lava→Obsidian reaction, closing the gap the tool-tiers work left (Obsidian existed as a block/item/tool tier with nothing able to produce it).

**Architecture:** Five tasks, each independently buildable and testable. Task 1 extends `BlockType` with 16 new fluid levels (`Water1..8`, `Lava1..8`) and the small helper functions that make the rest of the code level-agnostic - this is a pure data-model foundation, same shape as the tool-tiers plan's Task 1. Task 2 builds the `FluidSim` engine (an active-tile-queue-driven fall/spread/react simulation) as a new, self-contained component with no dependency on `Game`, `Player`, or `TerrainGenerator` - fully testable with hand-built `World`s. Task 3 wires fluid-overlap into the player's gravity. Task 4 adds the `scatterFluids` world-generation pass. Task 5 wires `FluidSim` into `Game`'s tick loop and world-load bootstrap. This ordering means every task after Task 2 is additive integration against an already-tested engine, not a task that could still be discovering engine bugs.

**Tech Stack:** C++20, SFML 3 (System only for `Litharia_core`; Graphics/Window for the `Litharia` game exe), CMake + Visual Studio generator, doctest (vendored single header).

## Global Constraints

- **Build (tests):** `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:\litharia\build --config Debug --target Litharia_tests`. `cmake` is not on PATH; use the full path shown.
- **Build (game):** same command with `--target Litharia`.
- **Test binary:** `C:\litharia\build\Debug\Litharia_tests.exe`.
- **Baseline before this work:** 275 test cases passing at commit `ad61b2b`.
- **`Tile` must stay exactly 1 byte** (`static_assert(sizeof(Tile) == 1, ...)` in `src/Tile/Tile.h`). This is why a fluid's level is encoded as which specific `BlockType` occupies the tile, never as a separate per-tile field. Do not touch `Tile.h`.
- **Fluid flow is conservative except at the reaction.** Falling is a plain transfer (nothing created or destroyed). Spreading splits a tile's own level in half (floor/ceil) with the neighbor it spreads into - also exactly conservative. The **only** thing that ever reduces a pool's total fluid is the Obsidian reaction (Water loses one level per reacting Lava neighbor). Do not implement a spread rule that duplicates volume (i.e. never leave the source tile's level unchanged after it spreads into a neighbor).
- **Fluids are not minable, not solid, and not collectible.** Every fluid `BlockInfo` row has `requiredTool = ToolType::None` (mining does nothing), `solid = false` (player passes through), `drop = BlockType::Air` (nothing to pick up). No bucket item, no inventory interaction.
- **No damage/health system exists and this plan does not add one.** Lava does not hurt the player. The only player-facing effect of fluids is gravity halving while overlapping any fluid tile.
- **Simulation/rendering split:** `Blocks.cpp`, `Physics.cpp`, `Player.cpp`, `TerrainGenerator.cpp`, and the new `FluidSim.cpp` compile into `Litharia_core`, reachable from `Litharia_tests`. `Game.cpp`/`Game.h` compile only into the `Litharia` executable and are **not** linked into the test binary - those changes are build-verified, not doctest-verified, matching every prior plan in this repo.
- **`CMakeLists.txt` has no glob-based test/source discovery.** Every new `.cpp` file (both `src/World/FluidSim.cpp` and `tests/test_fluids.cpp`) must be added to its respective `add_library`/`add_executable` list by hand or it will not compile in at all.
- **Commit after every task** with a `feat:` prefixed message.
- **Spec:** `docs/superpowers/specs/2026-07-19-fluids-and-obsidian-design.md`.

---

## File Structure

- `src/Blocks/Blocks.h` / `src/Blocks/Blocks.cpp` - 16 new `BlockType` entries and their registry rows; new free functions `isWater`/`isLava`/`isFluid`/`fluidLevel`/`waterAtLevel`/`lavaAtLevel`/`fluidAtLevel` (Task 1).
- `src/World/FluidSim.h` / `src/World/FluidSim.cpp` (**new**) - the fall/spread/react simulation and its active-tile queue (Task 2).
- `src/Physics/Physics.h` / `src/Physics/Physics.cpp` - new free function `overlapsFluid` (Task 3).
- `src/Player/Player.cpp` - `move()`'s gravity line becomes fluid-aware (Task 3).
- `src/World/TerrainGenerator.h` / `src/World/TerrainGenerator.cpp` - new `PoolKind`/`FluidPoolSpawn` types, new `scatterFluids` pass + `growPool` helper, new depth-band/count constants, `generate()`'s pass list gains one entry (Task 4).
- `src/Game/Game.h` / `src/Game/Game.cpp` - new `FluidSim fluids;` member, world-load bootstrap, per-tick fluid simulation call + dirty-marking, mine/place reactivation (Task 5).
- `CMakeLists.txt` - `src/World/FluidSim.cpp` added to `Litharia_core`'s sources (Task 2); `tests/test_fluids.cpp` added to `Litharia_tests`'s sources (Task 1).
- `tests/test_fluids.cpp` (**new**, registered in Task 1) - Tasks 1 and 2's tests.
- `tests/test_player.cpp` - Task 3's gravity test.
- `tests/test_terrain.cpp` - Task 4's world-gen tests.

---

## Canonical Interfaces (defined once, referenced by later tasks)

```cpp
// src/Blocks/Blocks.h (Task 1)
bool isWater(BlockType type);                    // true for Water1..Water8
bool isLava(BlockType type);                     // true for Lava1..Lava8
bool isFluid(BlockType type);                    // isWater || isLava
int fluidLevel(BlockType type);                  // 1-8 for a fluid tile, 0 otherwise
BlockType waterAtLevel(int level);               // level 1-8 -> Water1..Water8
BlockType lavaAtLevel(int level);                // level 1-8 -> Lava1..Lava8
BlockType fluidAtLevel(BlockType sameTypeAs, int level); // Air if level <= 0, else same family as sameTypeAs at `level`
```

```cpp
// src/World/FluidSim.h (Task 2)
class FluidSim
{
public:
    static constexpr float TICK_INTERVAL = 0.1f;

    void activate(int x, int y);
    void activateAround(int x, int y); // activates (x,y) and its 4 neighbors
    void activateAll(const World& world); // one-time full-world scan, used at world load

    void tick(World& world, float dt, std::vector<sf::Vector2i>& changedTiles);

private:
    void step(World& world, std::vector<sf::Vector2i>& changedTiles);
    bool reactAt(World& world, int x, int y, std::vector<sf::Vector2i>& changed);
    bool fallAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);
    bool spreadAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);

    std::vector<sf::Vector2i> active;
    float timer = 0.0f;
};
```

```cpp
// src/Physics/Physics.h (Task 3)
bool overlapsFluid(const AABB& box, const World& world);
```

```cpp
// src/World/TerrainGenerator.h (Task 4)
enum class PoolKind { Lake, WaterPool, LavaPool };

struct FluidPoolSpawn
{
    int x;
    int y;
    PoolKind kind;
};

// TerrainGenerator gains:
static constexpr int WATER_POOL_MIN_Y = 280;
static constexpr int WATER_POOL_MAX_Y = 320;
static constexpr int LAVA_MIN_Y = 350;
static constexpr int LAVA_MAX_Y = 495;
static constexpr int SURFACE_LAKE_COUNT = 5;
static constexpr int WATER_POOL_COUNT = 10;
static constexpr int LAVA_POOL_COUNT = 15;

std::vector<FluidPoolSpawn> scatterFluids(World& world) const; // public, like scatterSharpRocks
```

---

## Task 1: Fluid `BlockType` levels and helper functions

**Files:**
- Modify: `src/Blocks/Blocks.h:19-32` (the `BlockType` enum), append helpers near `isSolidBlock`
- Modify: `src/Blocks/Blocks.cpp:9-20` (the registry array)
- Modify: `CMakeLists.txt` (add `tests/test_fluids.cpp` to `Litharia_tests`)
- Test (new file): `tests/test_fluids.cpp`

**Interfaces:**
- Consumes: nothing new from other tasks (this is the plan's first task).
- Produces: 16 new `BlockType` values, `isWater`/`isLava`/`isFluid`/`fluidLevel`/`waterAtLevel`/`lavaAtLevel`/`fluidAtLevel`. Every later task depends on these directly.

- [ ] **Step 1: Create the test file and write the failing tests**

Create `tests/test_fluids.cpp`:

```cpp
#include "doctest.h"

#include "Blocks/Blocks.h"

TEST_CASE("isWater/isLava/isFluid correctly classify every fluid level, and nothing else")
{
    for (int level = 1; level <= 8; ++level)
    {
        CHECK(isWater(waterAtLevel(level)));
        CHECK_FALSE(isLava(waterAtLevel(level)));
        CHECK(isFluid(waterAtLevel(level)));

        CHECK(isLava(lavaAtLevel(level)));
        CHECK_FALSE(isWater(lavaAtLevel(level)));
        CHECK(isFluid(lavaAtLevel(level)));
    }

    CHECK_FALSE(isFluid(BlockType::Air));
    CHECK_FALSE(isFluid(BlockType::Stone));
    CHECK_FALSE(isFluid(BlockType::Obsidian));
    CHECK_FALSE(isWater(BlockType::Lava8));
    CHECK_FALSE(isLava(BlockType::Water8));
}

TEST_CASE("fluidLevel round-trips with waterAtLevel/lavaAtLevel, and is 0 for non-fluid blocks")
{
    for (int level = 1; level <= 8; ++level)
    {
        CHECK(fluidLevel(waterAtLevel(level)) == level);
        CHECK(fluidLevel(lavaAtLevel(level)) == level);
    }

    CHECK(fluidLevel(BlockType::Air) == 0);
    CHECK(fluidLevel(BlockType::Stone) == 0);
    CHECK(fluidLevel(BlockType::Obsidian) == 0);
}

TEST_CASE("fluidAtLevel clamps to Air at or below zero, and matches the source's fluid family")
{
    CHECK(fluidAtLevel(BlockType::Water8, 0) == BlockType::Air);
    CHECK(fluidAtLevel(BlockType::Water8, -1) == BlockType::Air);
    CHECK(fluidAtLevel(BlockType::Water8, 5) == BlockType::Water5);
    CHECK(fluidAtLevel(BlockType::Water1, 5) == BlockType::Water5);
    CHECK(fluidAtLevel(BlockType::Lava3, 5) == BlockType::Lava5);
    CHECK(fluidAtLevel(BlockType::Lava8, 1) == BlockType::Lava1);
}

TEST_CASE("every fluid block is non-solid, unmineable, and drops nothing")
{
    for (int level = 1; level <= 8; ++level)
    {
        const BlockInfo& water = blockInfo(waterAtLevel(level));
        CHECK_FALSE(water.solid);
        CHECK(water.requiredTool == ToolType::None);
        CHECK(water.drop == BlockType::Air);
        CHECK_FALSE(water.name.empty());

        const BlockInfo& lava = blockInfo(lavaAtLevel(level));
        CHECK_FALSE(lava.solid);
        CHECK(lava.requiredTool == ToolType::None);
        CHECK(lava.drop == BlockType::Air);
        CHECK_FALSE(lava.name.empty());
    }
}
```

Add `tests/test_fluids.cpp` to the `add_executable(Litharia_tests ...)` list in `CMakeLists.txt`, anywhere in the list (e.g. right after `tests/test_terrain.cpp`).

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command from Global Constraints.
Expected: **compile error** - `'Water1': is not a member of 'BlockType'` (and similarly for every other new symbol - `waterAtLevel`, `lavaAtLevel`, `isWater`, `isLava`, `isFluid`, `fluidLevel`, `fluidAtLevel`).

- [ ] **Step 3: Extend `BlockType` and add the helper functions**

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
    Obsidian,

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

    Water1,
    Water2,
    Water3,
    Water4,
    Water5,
    Water6,
    Water7,
    Water8,

    Lava1,
    Lava2,
    Lava3,
    Lava4,
    Lava5,
    Lava6,
    Lava7,
    Lava8,

    Count
};
```

Append these free functions right after `isSolidBlock` (end of the file):

```cpp
// True for Water1..Water8.
inline bool isWater(BlockType type)
{
    return type >= BlockType::Water1 && type <= BlockType::Water8;
}

// True for Lava1..Lava8.
inline bool isLava(BlockType type)
{
    return type >= BlockType::Lava1 && type <= BlockType::Lava8;
}

inline bool isFluid(BlockType type)
{
    return isWater(type) || isLava(type);
}

// 1-8 for a fluid tile (1 = almost empty, 8 = full/source-like), 0 otherwise.
inline int fluidLevel(BlockType type)
{
    if (isWater(type))
        return static_cast<int>(type) - static_cast<int>(BlockType::Water1) + 1;

    if (isLava(type))
        return static_cast<int>(type) - static_cast<int>(BlockType::Lava1) + 1;

    return 0;
}

// level must be 1-8.
inline BlockType waterAtLevel(int level)
{
    return static_cast<BlockType>(static_cast<int>(BlockType::Water1) + level - 1);
}

// level must be 1-8.
inline BlockType lavaAtLevel(int level)
{
    return static_cast<BlockType>(static_cast<int>(BlockType::Lava1) + level - 1);
}

// Air if level <= 0; otherwise the same fluid family as sameTypeAs (water stays
// water, lava stays lava) at the given level.
inline BlockType fluidAtLevel(BlockType sameTypeAs, int level)
{
    if (level <= 0)
        return BlockType::Air;

    return isWater(sameTypeAs) ? waterAtLevel(level) : lavaAtLevel(level);
}
```

- [ ] **Step 4: Add the 16 registry rows**

In `src/Blocks/Blocks.cpp`, replace the closing `}};` of the registry array so the file reads:

```cpp
constexpr std::array<BlockInfo, static_cast<std::size_t>(BlockType::Count)> registry = {{
    //  name          color              solid  hardness  drop                  requiredTool
    {"Air",         {  0,   0,   0}, false, 0.00f, BlockType::Air,       ToolType::None},
    {"Grass",       { 86, 176,  74}, true,  0.35f, BlockType::Dirt,      ToolType::Pickaxe},
    {"Dirt",        {134,  89,  52}, true,  0.35f, BlockType::Dirt,      ToolType::Pickaxe},
    {"Stone",       {112, 112, 118}, true,  0.90f, BlockType::Stone,     ToolType::Pickaxe},
    {"Copper Ore",  {201, 116,  56}, true,  1.40f, BlockType::CopperOre, ToolType::Pickaxe, ToolTier::Stone},
    {"Iron Ore",    {166, 174, 190}, true,  2.00f, BlockType::IronOre,   ToolType::Pickaxe, ToolTier::Copper},
    {"Coal",        { 44,  44,  50}, true,  1.10f, BlockType::Coal,      ToolType::Pickaxe},
    {"Oak Log",     {101,  67,  33}, false, 0.60f, BlockType::OakLog,    ToolType::Axe},
    {"Oak Leaves",  { 60, 140,  50}, false, 0.15f, BlockType::Air,       ToolType::Axe},
    {"Obsidian",    { 40,  20,  55}, true,  3.00f, BlockType::Obsidian,  ToolType::Pickaxe, ToolTier::Iron},

    {"Water (Level 1)", {170, 200, 230}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 2)", {151, 187, 229}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 3)", {133, 174, 227}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 4)", {114, 161, 226}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 5)", { 96, 149, 224}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 6)", { 77, 136, 223}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 7)", { 59, 123, 221}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 8)", { 40, 110, 220}, false, 0.00f, BlockType::Air, ToolType::None},

    {"Lava (Level 1)",  { 90,  40,  15}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 2)",  {110,  47,  16}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 3)",  {130,  54,  16}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 4)",  {150,  61,  17}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 5)",  {170,  69,  18}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 6)",  {190,  76,  19}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 7)",  {210,  83,  19}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 8)",  {230,  90,  20}, false, 0.00f, BlockType::Air, ToolType::None},
}};
```

(Only the 10 original rows plus the new 16 - the array's size already tracks `BlockType::Count` automatically via the `static_cast<std::size_t>(BlockType::Count)` template argument, so no other line needs to change.)

- [ ] **Step 5: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 275 + 4 new = 279 (all 4 in the new `test_fluids.cpp`). The pre-existing `"the block registry is fully populated"` test in `tests/test_world.cpp` (which loops over every `BlockType` checking `name` is non-empty and `hardness >= 0.0f`) passes automatically against the 16 new rows with no changes needed there.

- [ ] **Step 6: Build the game executable too**

Run: the Build (game) command from Global Constraints.
Expected: builds clean.

- [ ] **Step 7: Commit**

```bash
git add src/Blocks/Blocks.h src/Blocks/Blocks.cpp CMakeLists.txt tests/test_fluids.cpp
git commit -m "feat: add fluid BlockType levels and helper functions"
```

---

## Task 2: The `FluidSim` engine (fall, spread, react)

**Files:**
- Create: `src/World/FluidSim.h`, `src/World/FluidSim.cpp`
- Modify: `CMakeLists.txt` (add `src/World/FluidSim.cpp` to `Litharia_core`)
- Test: `tests/test_fluids.cpp`

**Interfaces:**
- Consumes: `isWater`/`isLava`/`isFluid`/`fluidLevel`/`fluidAtLevel` (Task 1).
- Produces: `FluidSim` (full public interface listed in Canonical Interfaces above). Task 3 doesn't use `FluidSim` at all (it only touches `Physics`/`Player`); Task 5 is the only later task that constructs and drives a `FluidSim`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_fluids.cpp`:

```cpp
#include <vector>

#include "World/FluidSim.h"
#include "World/World.h"

namespace
{
constexpr float FLUID_STEP = FluidSim::TICK_INTERVAL;
}

TEST_CASE("a fluid tile falls straight down into open air")
{
    World world;
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 10) == BlockType::Air);
    CHECK(world.get(10, 11) == BlockType::Water8);
}

TEST_CASE("a fluid tile does not fall through solid ground")
{
    World world;
    world.set(10, 11, BlockType::Stone);
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 10) == BlockType::Water8);
    CHECK(world.get(10, 11) == BlockType::Stone);
}

TEST_CASE("a blocked fluid tile spreads sideways, splitting its level with the neighbor")
{
    World world;
    world.set(10, 11, BlockType::Stone); // floor - nothing to fall onto
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // Level 8 splits evenly: source keeps ceil(8/2)=4, left neighbor gets floor(8/2)=4.
    CHECK(world.get(10, 10) == BlockType::Water4);
    CHECK(world.get(9, 10) == BlockType::Water4);
}

TEST_CASE("a level-1 fluid tile cannot spread any further")
{
    World world;
    world.set(10, 11, BlockType::Stone);
    world.set(10, 10, BlockType::Water1);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 10) == BlockType::Water1);
    CHECK(world.get(9, 10) == BlockType::Air);
    CHECK(world.get(11, 10) == BlockType::Air);
}

TEST_CASE("a fluid tile falling onto a lower-level match of itself tops it up by exactly one level")
{
    World world;
    world.set(10, 11, BlockType::Water3);
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 11) == BlockType::Water4);
    CHECK(world.get(10, 10) == BlockType::Water7);
}

TEST_CASE("lava adjacent to water solidifies into obsidian and the water loses one level")
{
    World world;
    world.set(10, 10, BlockType::Lava8);
    world.set(11, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 10) == BlockType::Obsidian);
    CHECK(world.get(11, 10) == BlockType::Water7);
}

TEST_CASE("obsidian seals the reaction site - it never reverts or reacts again")
{
    World world;
    world.set(10, 10, BlockType::Lava8);
    world.set(11, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);
    sim.activate(11, 10);

    std::vector<sf::Vector2i> changed;

    for (int i = 0; i < 5; ++i)
        sim.tick(world, FLUID_STEP, changed);

    CHECK(world.get(10, 10) == BlockType::Obsidian);
}

TEST_CASE("a full lava wall meeting a full water wall along a 10-tile contact yields exactly 10 obsidian")
{
    World world;

    for (int y = 0; y < 10; ++y)
    {
        world.set(10, y, BlockType::Lava8);
        world.set(11, y, BlockType::Water8);
    }

    FluidSim sim;
    for (int y = 0; y < 10; ++y)
    {
        sim.activate(10, y);
        sim.activate(11, y);
    }

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    int obsidianCount = 0;
    for (int y = 0; y < 10; ++y)
        if (world.get(10, y) == BlockType::Obsidian)
            ++obsidianCount;

    CHECK(obsidianCount == 10);

    for (int y = 0; y < 10; ++y)
        CHECK(world.get(11, y) == BlockType::Water7);
}

TEST_CASE("tick does nothing until a full TICK_INTERVAL has accumulated")
{
    World world;
    world.set(10, 10, BlockType::Water8);

    FluidSim sim;
    sim.activate(10, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FluidSim::TICK_INTERVAL * 0.4f, changed);

    // Well under one interval: nothing has happened yet.
    CHECK(world.get(10, 10) == BlockType::Water8);
    CHECK(world.get(10, 11) == BlockType::Air);

    sim.tick(world, FluidSim::TICK_INTERVAL * 0.7f, changed);

    // 0.4 + 0.7 = 1.1 intervals - comfortably past the threshold (not a
    // razor's-edge 0.5 + 0.5, which float rounding could land on either side
    // of) - so the tile falls.
    CHECK(world.get(10, 10) == BlockType::Air);
    CHECK(world.get(10, 11) == BlockType::Water8);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** - `'FluidSim': undeclared identifier` (the header doesn't exist yet).

- [ ] **Step 3: Create `FluidSim.h`**

Create `src/World/FluidSim.h`:

```cpp
#pragma once

#include <SFML/System/Vector2.hpp>

#include <vector>

#include "../Blocks/Blocks.h"

class World;

// Ticks Water/Lava tiles: falling, spreading, and reacting into Obsidian where
// they touch. Owns its own active-tile queue so a 1000x500 world never needs a
// full-grid scan - only tiles that changed (or are adjacent to a change) tick.
//
// Falling and spreading are exactly conservative (nothing is created or
// destroyed by flow alone) - the only thing that ever reduces a pool's total
// fluid is the Obsidian reaction.
class FluidSim
{
public:
    // Seconds between simulation steps - independent of the 60Hz physics step,
    // so flow reads as a visible process rather than an instant teleport.
    static constexpr float TICK_INTERVAL = 0.1f;

    // Marks a tile as needing to be checked on the next step. Call this
    // whenever a fluid tile is placed (world generation, world load).
    void activate(int x, int y);

    // Activates (x, y) and its 4 neighbors. Call this whenever a tile change
    // (mining, placing) might newly expose or block a nearby fluid tile.
    void activateAround(int x, int y);

    // A one-time full-world scan that activates every existing fluid tile.
    // Used once, right after world generation, since generation writes fluid
    // tiles directly into World without going through activate().
    void activateAll(const World& world);

    // Advances the internal timer by dt; if a full TICK_INTERVAL has
    // accumulated, runs exactly one simulation step and appends every tile
    // position that changed to `changedTiles` (so the caller can mark chunks
    // dirty), the same convention as Machines::tick's `mined` out-param.
    void tick(World& world, float dt, std::vector<sf::Vector2i>& changedTiles);

private:
    void step(World& world, std::vector<sf::Vector2i>& changedTiles);

    // Each returns true if it made a change (and the caller should not also
    // try the next rule on the same tile this tick).
    bool reactAt(World& world, int x, int y, std::vector<sf::Vector2i>& changed);
    bool fallAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);
    bool spreadAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);

    std::vector<sf::Vector2i> active;
    float timer = 0.0f;
};
```

- [ ] **Step 4: Create `FluidSim.cpp`**

Create `src/World/FluidSim.cpp`:

```cpp
#include "FluidSim.h"

#include "../Core/Constants.h"
#include "World.h"

void FluidSim::activate(int x, int y)
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return;

    active.push_back({x, y});
}

void FluidSim::activateAround(int x, int y)
{
    activate(x, y);
    activate(x - 1, y);
    activate(x + 1, y);
    activate(x, y - 1);
    activate(x, y + 1);
}

void FluidSim::activateAll(const World& world)
{
    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            if (isFluid(world.get(x, y)))
                activate(x, y);
}

void FluidSim::tick(World& world, float dt, std::vector<sf::Vector2i>& changedTiles)
{
    timer += dt;

    if (timer < TICK_INTERVAL)
        return;

    timer -= TICK_INTERVAL;
    step(world, changedTiles);
}

void FluidSim::step(World& world, std::vector<sf::Vector2i>& changedTiles)
{
    std::vector<sf::Vector2i> toProcess;
    toProcess.swap(active);

    for (const sf::Vector2i& pos : toProcess)
    {
        const BlockType type = world.get(pos.x, pos.y);

        if (!isFluid(type))
            continue;

        if (isLava(type) && reactAt(world, pos.x, pos.y, changedTiles))
            continue;

        if (fallAt(world, pos.x, pos.y, type, changedTiles))
            continue;

        spreadAt(world, pos.x, pos.y, type, changedTiles);
    }
}

bool FluidSim::reactAt(World& world, int x, int y, std::vector<sf::Vector2i>& changed)
{
    const int dx[4] = {-1, 1, 0, 0};
    const int dy[4] = {0, 0, -1, 1};

    for (int i = 0; i < 4; ++i)
    {
        const int nx = x + dx[i];
        const int ny = y + dy[i];
        const BlockType neighbor = world.get(nx, ny);

        if (!isWater(neighbor))
            continue;

        world.set(x, y, BlockType::Obsidian);
        world.set(nx, ny, fluidAtLevel(neighbor, fluidLevel(neighbor) - 1));

        activateAround(x, y);
        activateAround(nx, ny);
        changed.push_back({x, y});
        changed.push_back({nx, ny});
        return true;
    }

    return false;
}

bool FluidSim::fallAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed)
{
    const BlockType below = world.get(x, y + 1);

    if (below == BlockType::Air)
    {
        world.set(x, y + 1, type);
        world.set(x, y, BlockType::Air);
        activateAround(x, y);
        activateAround(x, y + 1);
        changed.push_back({x, y});
        changed.push_back({x, y + 1});
        return true;
    }

    const bool sameFluidBelow = (isWater(type) && isWater(below)) || (isLava(type) && isLava(below));

    if (sameFluidBelow && fluidLevel(below) < 8)
    {
        world.set(x, y + 1, fluidAtLevel(type, fluidLevel(below) + 1));
        world.set(x, y, fluidAtLevel(type, fluidLevel(type) - 1));
        activateAround(x, y);
        activateAround(x, y + 1);
        changed.push_back({x, y});
        changed.push_back({x, y + 1});
        return true;
    }

    return false;
}

bool FluidSim::spreadAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed)
{
    const int level = fluidLevel(type);

    if (level < 2)
        return false;

    const int half = level / 2;      // floor - goes to the neighbor
    const int remainder = level - half; // ceil - stays at the source

    if (world.get(x - 1, y) == BlockType::Air)
    {
        world.set(x - 1, y, fluidAtLevel(type, half));
        world.set(x, y, fluidAtLevel(type, remainder));
        activateAround(x, y);
        activateAround(x - 1, y);
        changed.push_back({x, y});
        changed.push_back({x - 1, y});
        return true;
    }

    if (world.get(x + 1, y) == BlockType::Air)
    {
        world.set(x + 1, y, fluidAtLevel(type, half));
        world.set(x, y, fluidAtLevel(type, remainder));
        activateAround(x, y);
        activateAround(x + 1, y);
        changed.push_back({x, y});
        changed.push_back({x + 1, y});
        return true;
    }

    return false;
}
```

- [ ] **Step 5: Register the new source file in CMakeLists.txt**

In `CMakeLists.txt`, add `src/World/FluidSim.cpp` to the `add_library(Litharia_core STATIC ...)` list, next to `src/World/TerrainGenerator.cpp`.

- [ ] **Step 6: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 279 + 9 new = 288 (all 9 in `test_fluids.cpp`).

- [ ] **Step 7: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 8: Commit**

```bash
git add src/World/FluidSim.h src/World/FluidSim.cpp CMakeLists.txt tests/test_fluids.cpp
git commit -m "feat: add the FluidSim fall/spread/react engine"
```

---

## Task 3: Halve player gravity while overlapping any fluid tile

**Files:**
- Modify: `src/Physics/Physics.h`, `src/Physics/Physics.cpp`
- Modify: `src/Player/Player.cpp`
- Test: `tests/test_player.cpp`

**Interfaces:**
- Consumes: `isFluid` (Task 1). Does not depend on `FluidSim` (Task 2) at all - only needs to read `world.get(...)`, which already exists.
- Produces: `bool overlapsFluid(const AABB& box, const World& world)`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_player.cpp` (the file already has a `buildFloor(World&, int)` helper and `constexpr float STEP = 1.0f / 60.0f;` at the top - reuse both, no new helper needed):

```cpp
TEST_CASE("gravity is halved while the player overlaps a fluid tile")
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: the Build (tests) command, then run `Litharia_tests.exe --test-case="gravity is halved*"`.
Expected: **FAIL** - `inWater.velocity().y < inAir.velocity().y` is false (both fall at the same rate today).

- [ ] **Step 3: Add `overlapsFluid` to `Physics`**

In `src/Physics/Physics.h`, add the declaration right next to `overlapsSolid`:

```cpp
bool overlapsSolid(const AABB& box, const World& world);
bool overlapsFluid(const AABB& box, const World& world);
```

In `src/Physics/Physics.cpp`, add the definition right after `overlapsSolid`'s:

```cpp
bool overlapsFluid(const AABB& box, const World& world)
{
    int x0, x1, y0, y1;
    tileRange(box, x0, x1, y0, y1);

    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (isFluid(world.get(x, y)))
                return true;

    return false;
}
```

(`tileRange` is the private anonymous-namespace helper `overlapsSolid` already uses - no new helper needed. If `isFluid` isn't visible, add `#include "../Blocks/Blocks.h"` to `Physics.cpp`'s includes; it's likely already available transitively via `World.h`.)

- [ ] **Step 4: Halve gravity in `Player::move` while overlapping a fluid**

In `src/Player/Player.cpp`, replace:

```cpp
    speed.y += GRAVITY * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);
```

with:

```cpp
    const float gravity = physics::overlapsFluid(body, world) ? GRAVITY * 0.5f : GRAVITY;
    speed.y += gravity * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);
```

- [ ] **Step 5: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 288 + 1 new = 289.

- [ ] **Step 6: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 7: Commit**

```bash
git add src/Physics/Physics.h src/Physics/Physics.cpp src/Player/Player.cpp tests/test_player.cpp
git commit -m "feat: halve player gravity while overlapping a fluid tile"
```

---

## Task 4: World generation - lakes, water pools, lava pools

**Files:**
- Modify: `src/World/TerrainGenerator.h`
- Modify: `src/World/TerrainGenerator.cpp`
- Test: `tests/test_terrain.cpp`

**Interfaces:**
- Consumes: `BlockType::Water8`/`BlockType::Lava8`, `isLava` (Task 1). Does not depend on `FluidSim` (Task 2) - pools are written directly into `World` as fully-filled source-level blobs; the simulation only takes over once the world is loaded (Task 5).
- Produces: `PoolKind`, `FluidPoolSpawn`, `TerrainGenerator::scatterFluids`, and the new depth-band/count constants listed in Canonical Interfaces.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_terrain.cpp`:

```cpp
TEST_CASE("world generation places exactly the expected count of each fluid pool kind")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generate(world);

    const std::vector<FluidPoolSpawn> pools = generator.scatterFluids(world);

    int lakes = 0, waterPools = 0, lavaPools = 0;

    for (const FluidPoolSpawn& p : pools)
    {
        if (p.kind == PoolKind::Lake)
            ++lakes;
        else if (p.kind == PoolKind::WaterPool)
            ++waterPools;
        else
            ++lavaPools;
    }

    CHECK(lakes == TerrainGenerator::SURFACE_LAKE_COUNT);
    CHECK(waterPools == TerrainGenerator::WATER_POOL_COUNT);
    CHECK(lavaPools == TerrainGenerator::LAVA_POOL_COUNT);
}

TEST_CASE("every lava pool spawns within its own depth band")
{
    TerrainGenerator generator(4242u);
    World world;

    const std::vector<FluidPoolSpawn> pools = generator.scatterFluids(world);

    for (const FluidPoolSpawn& p : pools)
    {
        if (p.kind != PoolKind::LavaPool)
            continue;

        REQUIRE(p.y >= TerrainGenerator::LAVA_MIN_Y);
        REQUIRE(p.y <= TerrainGenerator::LAVA_MAX_Y);
    }
}

TEST_CASE("every underground water pool spawns within its own depth band, safely above the lava band")
{
    TerrainGenerator generator(1337u);
    World world;

    const std::vector<FluidPoolSpawn> pools = generator.scatterFluids(world);

    for (const FluidPoolSpawn& p : pools)
    {
        if (p.kind != PoolKind::WaterPool)
            continue;

        REQUIRE(p.y >= TerrainGenerator::WATER_POOL_MIN_Y);
        REQUIRE(p.y <= TerrainGenerator::WATER_POOL_MAX_Y);
        REQUIRE(p.y < TerrainGenerator::LAVA_MIN_Y);
    }
}

TEST_CASE("lava pools skew toward the deeper part of their band")
{
    TerrainGenerator generator(2026u);
    World world;

    const std::vector<FluidPoolSpawn> pools = generator.scatterFluids(world);

    long long totalY = 0;
    int lavaCount = 0;

    for (const FluidPoolSpawn& p : pools)
    {
        if (p.kind != PoolKind::LavaPool)
            continue;

        totalY += p.y;
        ++lavaCount;
    }

    REQUIRE(lavaCount == TerrainGenerator::LAVA_POOL_COUNT);

    const double averageY = static_cast<double>(totalY) / lavaCount;
    const double midpoint =
        (TerrainGenerator::LAVA_MIN_Y + TerrainGenerator::LAVA_MAX_Y) / 2.0;

    CHECK(averageY > midpoint);
}

TEST_CASE("scatterFluids actually carves fluid into the world at every returned position")
{
    World world;
    TerrainGenerator generator(2026u);
    generator.generateBase(world); // real stone underfoot for the pools to carve into

    const std::vector<FluidPoolSpawn> pools = generator.scatterFluids(world);

    for (const FluidPoolSpawn& p : pools)
    {
        if (p.kind == PoolKind::LavaPool)
            CHECK(isLava(world.get(p.x, p.y)));
        else
            CHECK(isWater(world.get(p.x, p.y)));
    }
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: the Build (tests) command.
Expected: **compile error** - `'scatterFluids': is not a member of 'TerrainGenerator'` (and similarly for `PoolKind`, `FluidPoolSpawn`, `SURFACE_LAKE_COUNT`, `WATER_POOL_COUNT`, `LAVA_POOL_COUNT`, `WATER_POOL_MIN_Y`, `WATER_POOL_MAX_Y`, `LAVA_MIN_Y`, `LAVA_MAX_Y`).

- [ ] **Step 3: Declare the new types, constants, and method**

In `src/World/TerrainGenerator.h`, add right before the `class TerrainGenerator` declaration:

```cpp
enum class PoolKind
{
    Lake,
    WaterPool,
    LavaPool,
};

// Where a single fluid pool was placed, and which kind it is - returned by
// scatterFluids so tests (and, later, nothing else - Game reads the world
// directly) can verify counts and placement without re-deriving them.
struct FluidPoolSpawn
{
    int x;
    int y;
    PoolKind kind;
};
```

Add these constants next to `TREE_MIN_SPACING`:

```cpp
    // Underground water pools stay above this - safely below the highest a
    // surface lake's basin could possibly reach (SURFACE_MAX + the largest
    // pool radius, with margin).
    static constexpr int WATER_POOL_MIN_Y = 280;
    static constexpr int WATER_POOL_MAX_Y = 320;

    // Lava pools live below the iron layer, down near the world's floor.
    static constexpr int LAVA_MIN_Y = 350;
    static constexpr int LAVA_MAX_Y = 495;

    static constexpr int SURFACE_LAKE_COUNT = 5;
    static constexpr int WATER_POOL_COUNT = 10;
    static constexpr int LAVA_POOL_COUNT = 15;
```

Add the public method next to `scatterSharpRocks`:

```cpp
    // Pass 6 (called from generate()): carves and fills SURFACE_LAKE_COUNT
    // surface lakes, WATER_POOL_COUNT underground water pools, and
    // LAVA_POOL_COUNT lava pools (biased toward the bottom of its band).
    // Returns what it placed, in placement order - public, like
    // scatterSharpRocks, so tests can verify counts/bands directly.
    std::vector<FluidPoolSpawn> scatterFluids(World& world) const;
```

Add the private helper next to `growVein`:

```cpp
    // Carves a circular blob and fills it entirely with `fluid`, clipped to
    // [minY, maxY]. Unlike growVein (which only ever replaces Stone, to keep
    // ore veins from spilling into caves or dirt), a pool is a basin that
    // displaces whatever terrain is there.
    void growPool(World& world,
                  int centerX,
                  int centerY,
                  float radius,
                  BlockType fluid,
                  int minY,
                  int maxY) const;
```

- [ ] **Step 4: Implement `growPool` and `scatterFluids`**

In `src/World/TerrainGenerator.cpp`, add these constants in the anonymous namespace, next to the Pass 5 (trees) constants:

```cpp
// --- Pass 6: fluids ------------------------------------------------------
constexpr float POOL_MIN_RADIUS = 2.5f;
constexpr float POOL_MAX_RADIUS = 4.5f;

constexpr std::uint32_t SALT_LAKE = 0xA000u;
constexpr std::uint32_t SALT_WATER_POOL = 0xB000u;
constexpr std::uint32_t SALT_LAVA_POOL = 0xC000u;
```

Add `growPool`'s definition right after `growVein`'s:

```cpp
void TerrainGenerator::growPool(World& world,
                                 int centerX,
                                 int centerY,
                                 float radius,
                                 BlockType fluid,
                                 int minY,
                                 int maxY) const
{
    const int reach = static_cast<int>(std::ceil(radius));
    const float radiusSquared = radius * radius;

    for (int dy = -reach; dy <= reach; ++dy)
    {
        for (int dx = -reach; dx <= reach; ++dx)
        {
            if (static_cast<float>(dx * dx + dy * dy) > radiusSquared)
                continue;

            const int x = centerX + dx;
            const int y = centerY + dy;

            if (y < minY || y > maxY)
                continue;

            // A pool carves through whatever is there - unlike growVein, which
            // only ever replaces Stone, a pool is a basin that displaces the
            // terrain, not a mineral that only forms inside it.
            world.set(x, y, fluid);
        }
    }
}
```

Add `scatterFluids`'s definition right after `scatterTrees`'s:

```cpp
std::vector<FluidPoolSpawn> TerrainGenerator::scatterFluids(World& world) const
{
    std::vector<FluidPoolSpawn> spawns;
    spawns.reserve(SURFACE_LAKE_COUNT + WATER_POOL_COUNT + LAVA_POOL_COUNT);

    // Surface lakes: anchored to each chosen column's own surface height, one
    // roughly every WORLD_WIDTH / SURFACE_LAKE_COUNT tiles.
    const int lakeBinWidth = (WORLD_WIDTH - 2) / SURFACE_LAKE_COUNT;

    for (int i = 0; i < SURFACE_LAKE_COUNT; ++i)
    {
        const int binStart = 1 + i * lakeBinWidth;
        const float xRoll = noise::hashFloat(i, 0, worldSeed + SALT_LAKE);
        const int x = binStart + static_cast<int>(xRoll * lakeBinWidth);

        const float radiusRoll = noise::hashFloat(i, 1, worldSeed + SALT_LAKE);
        const float radius = POOL_MIN_RADIUS + radiusRoll * (POOL_MAX_RADIUS - POOL_MIN_RADIUS);

        // Centered radius-below the surface, so the blob's top edge just
        // reaches the surface contour rather than poking a dome above ground.
        const int surface = surfaceHeight(x);
        const int centerY = surface + static_cast<int>(radius);

        growPool(world, x, centerY, radius, BlockType::Water8, 0, WORLD_HEIGHT - 1);
        spawns.push_back({x, centerY, PoolKind::Lake});
    }

    // Underground water pools: within the existing cave-depth range, above the
    // lava band, no depth bias.
    const int waterBinWidth = (WORLD_WIDTH - 2) / WATER_POOL_COUNT;

    for (int i = 0; i < WATER_POOL_COUNT; ++i)
    {
        const int binStart = 1 + i * waterBinWidth;
        const float xRoll = noise::hashFloat(i, 0, worldSeed + SALT_WATER_POOL);
        const int x = binStart + static_cast<int>(xRoll * waterBinWidth);

        const float yRoll = noise::hashFloat(i, 1, worldSeed + SALT_WATER_POOL);
        const int y = WATER_POOL_MIN_Y +
                      static_cast<int>(yRoll * (WATER_POOL_MAX_Y - WATER_POOL_MIN_Y));

        const float radiusRoll = noise::hashFloat(i, 2, worldSeed + SALT_WATER_POOL);
        const float radius = POOL_MIN_RADIUS + radiusRoll * (POOL_MAX_RADIUS - POOL_MIN_RADIUS);

        growPool(world, x, y, radius, BlockType::Water8, WATER_POOL_MIN_Y, WATER_POOL_MAX_Y);
        spawns.push_back({x, y, PoolKind::WaterPool});
    }

    // Lava pools: below the iron layer, biased toward the deeper end of the
    // band (squaring a uniform roll concentrates it near 0, so subtracting
    // that from LAVA_MAX_Y keeps most rolls close to LAVA_MAX_Y) so lava gets
    // progressively more common - and dangerous - the deeper the player digs.
    const int lavaBinWidth = (WORLD_WIDTH - 2) / LAVA_POOL_COUNT;

    for (int i = 0; i < LAVA_POOL_COUNT; ++i)
    {
        const int binStart = 1 + i * lavaBinWidth;
        const float xRoll = noise::hashFloat(i, 0, worldSeed + SALT_LAVA_POOL);
        const int x = binStart + static_cast<int>(xRoll * lavaBinWidth);

        const float yRoll = noise::hashFloat(i, 1, worldSeed + SALT_LAVA_POOL);
        const int y = LAVA_MAX_Y - static_cast<int>(yRoll * yRoll * (LAVA_MAX_Y - LAVA_MIN_Y));

        const float radiusRoll = noise::hashFloat(i, 2, worldSeed + SALT_LAVA_POOL);
        const float radius = POOL_MIN_RADIUS + radiusRoll * (POOL_MAX_RADIUS - POOL_MIN_RADIUS);

        growPool(world, x, y, radius, BlockType::Lava8, LAVA_MIN_Y, LAVA_MAX_Y);
        spawns.push_back({x, y, PoolKind::LavaPool});
    }

    return spawns;
}
```

- [ ] **Step 5: Wire the new pass into `generate()`**

In `src/World/TerrainGenerator.cpp`, replace:

```cpp
void TerrainGenerator::generate(World& world) const
{
    generateBase(world);
    carveSpecialCaves(world);
    scatterOre(world);
    scatterTrees(world);
}
```

with:

```cpp
void TerrainGenerator::generate(World& world) const
{
    generateBase(world);
    carveSpecialCaves(world);
    scatterOre(world);
    scatterFluids(world);
    scatterTrees(world);
}
```

(`scatterFluids`'s returned list is discarded here - `generate()` doesn't need it, only tests and, in Task 5, nobody else does either, since world-gen writes fluid tiles directly and `Game` discovers them via `FluidSim::activateAll` reading `World`, not via this return value.)

- [ ] **Step 6: Run the full test suite and verify it passes**

Run: the Build (tests) command, then run `Litharia_tests.exe`.
Expected: all pass, count now 289 + 5 new = 294 (all 5 in `test_terrain.cpp`).

- [ ] **Step 7: Build the game executable too**

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 8: Commit**

```bash
git add src/World/TerrainGenerator.h src/World/TerrainGenerator.cpp tests/test_terrain.cpp
git commit -m "feat: generate surface lakes, underground water pools, and lava pools"
```

---

## Task 5: Wire `FluidSim` into `Game`

**Files:**
- Modify: `src/Game/Game.h`
- Modify: `src/Game/Game.cpp`

**Interfaces:**
- Consumes: `FluidSim` (Task 2, full public interface). `TerrainGenerator::generate` already calls `scatterFluids` internally (Task 4) - no separate call is needed here for world-gen, only for making the simulation aware of what generation already wrote.
- Produces: nothing new for later tasks - this is the plan's last task.

This task touches only `Game.h`/`Game.cpp`, which are **not** linked into `Litharia_tests` (per Global Constraints) - verification here is "build both targets clean," matching every prior `Game.cpp`-only task in this repo's plans (e.g. the tool-tiers plan's Sharp Rock task).

- [ ] **Step 1: Add the `FluidSim` member**

In `src/Game/Game.h`, add the include:

```cpp
#include "../World/FluidSim.h"
```

Add the member variable next to `Machines machines;`:

```cpp
    FluidSim fluids;
```

Declare `respawnSharpRocksIfNeeded`'s neighbor - add nothing new to the method list; `fluids.tick(...)`/`fluids.activateAll(...)`/`fluids.activateAround(...)` are called directly from `fixedUpdate`/the constructor in `Game.cpp`, no new `Game` method needed.

- [ ] **Step 2: Bootstrap the simulation after world generation**

In `src/Game/Game.cpp`, replace:

```cpp
    generator.generate(world);
    chunks.markAllDirty();

    spawnSharpRocks();
```

with:

```cpp
    generator.generate(world);
    chunks.markAllDirty();

    spawnSharpRocks();
    fluids.activateAll(world);
```

- [ ] **Step 3: Tick the simulation and mark changed tiles dirty**

In `src/Game/Game.cpp`'s `fixedUpdate`, replace:

```cpp
    updateDrops(dt);
    respawnSharpRocksIfNeeded(dt);
    tickMachines(dt);
    updateCrafting(dt);
    updateSmelting(dt);
```

with:

```cpp
    updateDrops(dt);
    respawnSharpRocksIfNeeded(dt);
    tickMachines(dt);
    updateCrafting(dt);
    updateSmelting(dt);

    std::vector<sf::Vector2i> fluidChanges;
    fluids.tick(world, dt, fluidChanges);
    for (const sf::Vector2i& t : fluidChanges)
        chunks.markDirty(t.x, t.y);
```

- [ ] **Step 4: Reactivate fluid neighbors when the player mines or places a tile**

Still in `fixedUpdate`, replace:

```cpp
    if (result.broke)
    {
        // The player mutated one or more tiles; the renderer has to be told.
        for (const BrokenTile& tile : result.broken)
            chunks.markDirty(tile.x, tile.y);

        spawnDrop(result);
    }

    if (result.placed)
        chunks.markDirty(result.placedX, result.placedY);
```

with:

```cpp
    if (result.broke)
    {
        // The player mutated one or more tiles; the renderer has to be told.
        // A newly-opened tile might let a neighboring fluid tile fall or
        // spread into it, so reactivate around it too.
        for (const BrokenTile& tile : result.broken)
        {
            chunks.markDirty(tile.x, tile.y);
            fluids.activateAround(tile.x, tile.y);
        }

        spawnDrop(result);
    }

    if (result.placed)
    {
        chunks.markDirty(result.placedX, result.placedY);
        fluids.activateAround(result.placedX, result.placedY);
    }
```

- [ ] **Step 5: Build both targets and verify they compile clean**

Run: the Build (tests) command from Global Constraints.
Expected: builds clean, all 294 tests still pass (this task touches no code reachable from the test binary, so the count is unchanged from Task 4).

Run: the Build (game) command.
Expected: builds clean.

- [ ] **Step 6: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: wire FluidSim into Game's world load and tick loop"
```

---

## Final check

After Task 5, run the full suite once more (`Litharia_tests.exe`) and confirm **294 test cases, 294 passed, 0 failed** - the plan's cumulative total (275 baseline + 4 + 9 + 1 + 5 + 0 = 294). Then launch `Litharia.exe` and visually confirm: lakes are visible at the surface, mining into a wall reveals water/lava behind it and it visibly falls/spreads, and digging a channel between a lava pool and a water pool produces Obsidian at the contact line.
