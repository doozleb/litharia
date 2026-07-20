# Day/Night Cycle and Cave Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a real day/night clock and a per-tile lighting engine (sky light from open shafts, block light from Torches and Lava, both BFS-propagated and blocked by solid tiles) so caves are genuinely dark, a disconnected cave stays hidden until dug into, and a new craftable Torch pushes back the darkness.

**Architecture:** Two new window-free core classes (`DayNightClock`, `Lighting`) hold all the actual state and logic and are fully unit-tested without SFML Graphics, matching `FluidSim`'s existing split. A new `LightRenderer` (SFML Graphics, executable-only, like `ChunkRenderer`/`MachineRenderer`) turns that state into a screen-darkening overlay drawn with `sf::BlendMultiply`. A new `MachineType::Torch`/`ItemType::Torch` slots into the existing furniture/recipe registries exactly like Chest/Furnace. `Game` wires the clock, the lighting recompute triggers, and the renderer together.

**Tech Stack:** C++20, SFML 3 (Graphics/Window/System), doctest, CMake + Visual Studio 18 2026 generator (existing project setup - no new dependencies).

## Global Constraints

- `Tile` stays exactly 2 bytes (`type` + `decoration`) - lighting data lives in a separate grid owned by a new `Lighting` class, never added to `Tile`.
- `Lighting`, `DayNightClock`, and every core-lib file may only depend on SFML System (or nothing) - no SFML Graphics/Window include, since they compile into `Litharia_core` alongside `FluidSim`/`Machines`/`Player`.
- `LightRenderer` (SFML Graphics) compiles only into the `Litharia` executable target, matching `ChunkRenderer`/`MachineRenderer` - it has no unit tests, same as those two.
- Day/night cycle: 15 real minutes per lap (10 minutes day, 5 minutes night), smoothly eased across dusk/dawn, not a hard switch.
- Light levels are 0-8 on both channels (sky, block), decaying by exactly 1 per orthogonal step, blocked entirely by a solid tile (`World::isSolid`, which only looks at `type` - the decoration layer never blocks light, same as it never blocks fluid).
- The stored lighting grid (`Lighting::recomputeAll`) is only ever recomputed on a block mined/placed or a Torch placed/removed - never once a tick. The day/night brightness and the player's held-Torch light are purely a per-frame rendering concern and never touch the stored grid.
- Torch recipe: 2 Stone + 1 Stick -> 2 Torch, craftable at the Crafting Table like the game's other early recipes.
- Every new/changed source file must build under the existing `cmake --build build --config Debug` invocation, and `Litharia_tests.exe` must pass in full after every task.

---

### Task 1: DayNightClock

**Files:**
- Create: `src/World/DayNightClock.h`
- Create: `src/World/DayNightClock.cpp`
- Test: `tests/test_day_night_clock.cpp`
- Modify: `CMakeLists.txt` (add `src/World/DayNightClock.cpp` to `Litharia_core`'s sources; add `tests/test_day_night_clock.cpp` to `Litharia_tests`'s sources)

**Interfaces:**
- Produces: `class DayNightClock { void tick(float dt); float daylightFactor() const; }` - `Game` (Task 7) calls `tick(dt)` once a fixedUpdate and reads `daylightFactor()` every frame.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_day_night_clock.cpp`:

```cpp
#include "doctest.h"

#include "World/DayNightClock.h"

// The clock starts at time = DAY_SECONDS * 0.5 = 300s (midday), not at 0 -
// every tick amount below is chosen relative to that known start so the
// resulting absolute `time` values line up with the segment boundaries
// (540 = dusk start, 600 = full night, 840 = dawn start, 900 = wraps to 0).

TEST_CASE("a fresh clock starts at full daylight (midday)")
{
    DayNightClock clock;
    CHECK(clock.daylightFactor() == doctest::Approx(1.0f));
}

TEST_CASE("daylightFactor stays flat at 1.0 through the bulk of the day")
{
    DayNightClock clock;
    clock.tick(100.0f); // time = 400, still well inside the flat-day segment
    CHECK(clock.daylightFactor() == doctest::Approx(1.0f));
}

TEST_CASE("daylightFactor eases from 1 to 0 across the 60s dusk transition")
{
    DayNightClock clock;

    clock.tick(240.0f); // time = 540: dusk starts, still 1.0 at the boundary
    CHECK(clock.daylightFactor() == doctest::Approx(1.0f));

    clock.tick(30.0f); // time = 570: halfway through dusk
    CHECK(clock.daylightFactor() == doctest::Approx(0.5f));

    clock.tick(30.0f); // time = 600: fully night
    CHECK(clock.daylightFactor() == doctest::Approx(0.0f));
}

TEST_CASE("daylightFactor stays flat at 0.0 through the bulk of the night")
{
    DayNightClock clock;
    clock.tick(400.0f); // time = 700, inside the flat-night segment [600, 840)
    CHECK(clock.daylightFactor() == doctest::Approx(0.0f));
}

TEST_CASE("daylightFactor eases from 0 to 1 across the 60s dawn transition")
{
    DayNightClock clock;

    clock.tick(540.0f); // time = 840: dawn starts, still 0.0 at the boundary
    CHECK(clock.daylightFactor() == doctest::Approx(0.0f));

    clock.tick(30.0f); // time = 870: halfway through dawn
    CHECK(clock.daylightFactor() == doctest::Approx(0.5f));

    clock.tick(30.0f); // time wraps to 0: back to full daylight
    CHECK(clock.daylightFactor() == doctest::Approx(1.0f));
}

TEST_CASE("tick wraps around at the end of a 900-second lap")
{
    DayNightClock clock;
    clock.tick(900.0f); // exactly one full lap back to the midday start
    CHECK(clock.daylightFactor() == doctest::Approx(1.0f));
}
```

Add the new file to `CMakeLists.txt`'s `Litharia_tests` executable, right after `tests/test_hud_layout.cpp`:

```cmake
    tests/test_hud_layout.cpp
    tests/test_day_night_clock.cpp
)
```

- [ ] **Step 2: Run the tests to verify they fail to build**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `World/DayNightClock.h` does not exist yet.

- [ ] **Step 3: Create `src/World/DayNightClock.h`**

```cpp
#pragma once

// A day/night clock: a single float that loops over one full day, and a
// smoothly-eased 0 (deep night) to 1 (noon) factor derived from it. Owns no
// SFML dependency at all - Game turns daylightFactor() into actual colors.
class DayNightClock
{
public:
    // 10 minutes of day, 5 of night - a full lap is 15 minutes real time.
    static constexpr float CYCLE_SECONDS = 900.0f;
    static constexpr float DAY_FRACTION = 10.0f / 15.0f;
    static constexpr float DAY_SECONDS = CYCLE_SECONDS * DAY_FRACTION; // 600

    // Dawn and dusk each ramp smoothly over this many seconds, rather than
    // snapping straight from full day to full night.
    static constexpr float TRANSITION_SECONDS = 60.0f;

    // Advances the clock by dt, wrapping back to 0 at the end of a lap.
    void tick(float dt);

    // 0 (deep night) to 1 (noon): flat 1 through the bulk of the day, flat 0
    // through the bulk of the night, smoothly eased across dusk and dawn.
    float daylightFactor() const;

private:
    // Starts at midday - a fresh world spawns in full daylight, not
    // mid-transition or at night.
    float time = DAY_SECONDS * 0.5f;
};
```

- [ ] **Step 4: Create `src/World/DayNightClock.cpp`**

```cpp
#include "DayNightClock.h"

#include <algorithm>

namespace
{

float smoothstep(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void DayNightClock::tick(float dt)
{
    time += dt;

    if (time >= CYCLE_SECONDS)
        time -= CYCLE_SECONDS;
}

float DayNightClock::daylightFactor() const
{
    constexpr float DUSK_START = DAY_SECONDS - TRANSITION_SECONDS;   // 540
    constexpr float DAWN_START = CYCLE_SECONDS - TRANSITION_SECONDS; // 840

    if (time < DUSK_START)
        return 1.0f;

    if (time < DAY_SECONDS)
        return 1.0f - smoothstep((time - DUSK_START) / TRANSITION_SECONDS);

    if (time < DAWN_START)
        return 0.0f;

    return smoothstep((time - DAWN_START) / TRANSITION_SECONDS);
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - every `DayNightClock` test case passes, and the full existing suite still passes.

- [ ] **Step 6: Commit**

```bash
git add src/World/DayNightClock.h src/World/DayNightClock.cpp tests/test_day_night_clock.cpp CMakeLists.txt
git commit -m "feat: add DayNightClock with a smoothly-eased 15-minute day/night cycle"
```

---

### Task 2: Torch item, machine, and recipe

**Files:**
- Modify: `src/Items/Items.h`
- Modify: `src/Items/Items.cpp`
- Modify: `src/Machines/MachineType.h`
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Recipes.cpp`
- Modify: `tests/test_recipes.cpp`
- Modify: `tests/test_machines.cpp`

**Interfaces:**
- Produces: `ItemType::Torch`, `MachineType::Torch` (furniture, 1x1, no power role), a `CraftRecipe` for it (2 Stone + 1 Stick -> 2 Torch). `Lighting` (Task 3) and `Game` (Task 7) both depend on `MachineType::Torch` existing.

- [ ] **Step 1: Write the failing tests**

In `tests/test_recipes.cpp`, change the recipe-count assertion:

```cpp
TEST_CASE("allCraftRecipes exposes every craftable item exactly once")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    CHECK(all.size() == 27);
}
```

Add a new test case at the end of `tests/test_recipes.cpp`:

```cpp
TEST_CASE("the Torch recipe costs 2 stone and 1 stick, yields 2 torches")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::Torch; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stone);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Stick);
    CHECK(it->ingredients[1].count == 1);
    CHECK(it->outputCount == 2);
}
```

In `tests/test_machines.cpp`, change the `isFurniture` test's title and add a line (around line 370-389):

```cpp
TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, Furnace, and Torch")
{
    CHECK_FALSE(isFurniture(MachineType::None));
    CHECK_FALSE(isFurniture(MachineType::BurnerGenerator));
    CHECK_FALSE(isFurniture(MachineType::CopperDrill));
    CHECK_FALSE(isFurniture(MachineType::IronDrill));
    CHECK_FALSE(isFurniture(MachineType::ObsidianDrill));
    CHECK_FALSE(isFurniture(MachineType::CopperBelt));
    CHECK_FALSE(isFurniture(MachineType::IronBelt));
    CHECK_FALSE(isFurniture(MachineType::ObsidianBelt));
    CHECK_FALSE(isFurniture(MachineType::CopperChute));
    CHECK_FALSE(isFurniture(MachineType::IronChute));
    CHECK_FALSE(isFurniture(MachineType::ObsidianChute));
    CHECK_FALSE(isFurniture(MachineType::CopperSmelter));
    CHECK_FALSE(isFurniture(MachineType::IronSmelter));
    CHECK_FALSE(isFurniture(MachineType::ObsidianSmelter));

    CHECK(isFurniture(MachineType::Chest));
    CHECK(isFurniture(MachineType::CraftingTable));
    CHECK(isFurniture(MachineType::Furnace));
    CHECK(isFurniture(MachineType::Torch));
}
```

In `tests/test_machines.cpp`, add a line to `"itemForMachine maps every placeable machine to its own item"` (right before its closing brace, around line 233):

```cpp
    CHECK(itemForMachine(MachineType::Chest) == ItemType::Chest);
    CHECK(itemForMachine(MachineType::CraftingTable) == ItemType::CraftingTable);
    CHECK(itemForMachine(MachineType::Torch) == ItemType::Torch);
}
```

Add a new test case at the end of `tests/test_machines.cpp`:

```cpp
TEST_CASE("a Torch is placed as ordinary 1x1 furniture with no power role and no storage")
{
    Machines machines;
    Machine* torch = machines.place(MachineType::Torch, 5, 5, Direction::Right);

    REQUIRE(torch != nullptr);
    CHECK(torch->storage.slotCount() == 0);

    const MachineInfo& info = machineInfo(MachineType::Torch);
    CHECK_FALSE(info.generator);
    CHECK_FALSE(info.consumer);
    CHECK_FALSE(info.transport);
    CHECK(info.width == 1);
    CHECK(info.height == 1);
}
```

- [ ] **Step 2: Run the tests to verify they fail to build**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `ItemType::Torch` and `MachineType::Torch` are not declared.

- [ ] **Step 3: Add `ItemType::Torch`**

In `src/Items/Items.h`, add `Torch` to the enum, right before `Count`:

```cpp
    Chest,
    Furnace,
    ItemAcceptor,
    Torch,

    Count
```

In `src/Items/Items.cpp`, add a row to the `registry` array, right after the Item Acceptor row:

```cpp
    {"Item Acceptor",    10,  BlockType::Air,       ToolType::None,    { 80, 140, 190}},
    {"Torch",            50,  BlockType::Air,       ToolType::None,    {230, 170,  60}},
}};
```

- [ ] **Step 4: Add `MachineType::Torch`**

In `src/Machines/MachineType.h`, add `Torch` to the enum, right before `Count`:

```cpp
    Chest,
    CraftingTable,
    Furnace,
    ItemAcceptor,
    Torch,

    Count
```

In `src/Machines/MachineRegistry.cpp`, add a row to `registry`, right after the Item Acceptor row:

```cpp
    {"Item Acceptor",     { 80, 140, 190}, false, false, false, 0.0f,  0.0f,  1, 1},
    {"Torch",             {230, 170,  60}, false, false, false, 0.0f,  0.0f,  1, 1},
}};
```

In the same file, add a case to `itemForMachine`, right before `default`:

```cpp
        case MachineType::ItemAcceptor:    return ItemType::ItemAcceptor;
        case MachineType::Torch:           return ItemType::Torch;
        default:                           return ItemType::None;
```

In the same file, extend `isFurniture`:

```cpp
bool isFurniture(MachineType type)
{
    return type == MachineType::Chest || type == MachineType::CraftingTable
        || type == MachineType::Furnace || type == MachineType::Torch;
}
```

- [ ] **Step 5: Add the Torch craft recipe**

In `src/Machines/Recipes.cpp`, change the array size and add a row at the end of `craftRecipes`:

```cpp
constexpr std::array<CraftRecipe, 27> craftRecipes = {{
```

```cpp
    {ItemType::ItemAcceptor,    {{{ItemType::Stone, 10}, {ItemType::CopperPlate, 3}}},    3.0f, true},
    {ItemType::Torch,           {{{ItemType::Stone, 2}, {ItemType::Stick, 1}}},            1.0f, true, 2},
}};
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - every new Torch-related check passes, and the full existing suite (including the generic "every item has a real icon color" and "the machine registry has a valid row per type" loops, which now include Torch automatically) still passes.

- [ ] **Step 7: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h src/Machines/MachineRegistry.cpp src/Machines/Recipes.cpp tests/test_recipes.cpp tests/test_machines.cpp
git commit -m "feat: add a craftable Torch (furniture, 2 Stone + 1 Stick -> 2 Torch)"
```

---

### Task 3: Lighting - data model, floodFill, and block light

**Files:**
- Create: `src/World/Lighting.h`
- Create: `src/World/Lighting.cpp`
- Test: `tests/test_lighting.cpp`
- Modify: `CMakeLists.txt` (add `src/World/Lighting.cpp` to `Litharia_core`'s sources; add `tests/test_lighting.cpp` to `Litharia_tests`'s sources)

**Interfaces:**
- Consumes: `World::inBounds`, `World::isSolid`, `World::get` (`World.h`); `isLava` (`Blocks.h`); `Machines::all()` returning `const std::vector<Machine>&`, `Machine::type`/`x`/`y` (`Machines.h`/`Machine.h`); `MachineType::Torch` (Task 2).
- Produces: `struct LightLevel { sky:4, block:4 }`; `class Lighting { Lighting(); void recomputeAll(const World&, const Machines&); int skyLight(int,int) const; int blockLight(int,int) const; }` - Task 4 extends `recomputeAll`'s body (same signature) to add sky light; Task 5 adds `heldTorchLight`; Task 6/7 consume `skyLight`/`blockLight`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_lighting.cpp`:

```cpp
#include "doctest.h"

#include "Core/Constants.h"
#include "Core/Direction.h"
#include "Machines/Machines.h"
#include "World/Lighting.h"
#include "World/World.h"

namespace
{

void fillSolid(World& world)
{
    world.fill(BlockType::Stone);
}

} // namespace

TEST_CASE("a lone Lava tile lights itself and decays by 1 per orthogonal step")
{
    World world;
    fillSolid(world);
    world.set(9, 10, BlockType::Air);
    world.set(11, 10, BlockType::Air);
    world.set(10, 10, BlockType::Lava8);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.blockLight(10, 10) == 8);
    CHECK(lighting.blockLight(9, 10) == 7);
    CHECK(lighting.blockLight(11, 10) == 7);
}

TEST_CASE("block light is blocked entirely by a solid tile")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Lava8);
    // (11, 10) stays Stone: solid, so light cannot pass through or into it.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.blockLight(11, 10) == 0);
}

TEST_CASE("a placed Torch is a level-8 block light source")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.blockLight(10, 10) == 8);
    CHECK(lighting.blockLight(9, 10) == 7);
}

TEST_CASE("skyLight is 0 everywhere before the sky pass exists")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 10) == 0);
}

TEST_CASE("blockLight and skyLight are 0 out of bounds")
{
    World world;
    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.blockLight(-1, 0) == 0);
    CHECK(lighting.blockLight(WORLD_WIDTH, 0) == 0);
    CHECK(lighting.skyLight(0, -1) == 0);
    CHECK(lighting.skyLight(0, WORLD_HEIGHT) == 0);
}
```

Add the new files to `CMakeLists.txt`: `src/World/Lighting.cpp` to `Litharia_core`'s sources (right after `src/World/FluidSurface.cpp`), and `tests/test_lighting.cpp` to `Litharia_tests`'s sources (right after `tests/test_day_night_clock.cpp`):

```cmake
    src/World/FluidSim.cpp
    src/World/FluidSurface.cpp
    src/World/Lighting.cpp
```

```cmake
    tests/test_day_night_clock.cpp
    tests/test_lighting.cpp
)
```

- [ ] **Step 2: Run the tests to verify they fail to build**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `World/Lighting.h` does not exist yet.

- [ ] **Step 3: Create `src/World/Lighting.h`**

```cpp
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <SFML/System/Vector2.hpp>

class World;
class Machines;

// One tile's current light, split into two independent channels - see
// Lighting's own comment for why two, and why this lives in its own grid
// rather than on Tile.
struct LightLevel
{
    std::uint8_t sky : 4;   // 0-8, this tile's sunlight exposure
    std::uint8_t block : 4; // 0-8, this tile's torch/lava exposure
};
static_assert(sizeof(LightLevel) == 1, "One byte per tile: a 1000x500 world stays 500 KB.");

// Computes and stores per-tile lighting: how exposed to the sky a tile is
// (skyLight) and how close it is to a Torch or Lava tile (blockLight), each
// 0-8 and decaying by 1 per orthogonal step, blocked entirely by solid
// tiles. A separate grid from World/Tile - light values change on every
// block/Torch edit (and skyLight's *effective* brightness changes every
// tick, scaled by the day/night clock - see DayNightClock), which doesn't
// belong on Tile any more than a fluid's flow state would.
//
// recomputeAll() rebuilds the whole grid from scratch rather than patching
// just the changed region: correctly patching only a local region after a
// light SOURCE disappears (a Torch mined, a wall sealing off a lit shaft)
// needs a "clear then re-flood from any surviving neighbour" pass, not a
// simple re-seed - full recompute sidesteps that complexity entirely and is
// still cheap, since it only runs on a block/Torch edit (a rare, player-
// paced event), never once a tick.
class Lighting
{
public:
    Lighting();

    // Clears every tile's sky/block level to 0, then floods block light
    // from every Lava tile and every placed Torch, and sky light from every
    // column open to the world's top edge. Call whenever a block is mined
    // or placed, or a Torch is placed or removed.
    void recomputeAll(const World& world, const Machines& machines);

    int skyLight(int x, int y) const;   // 0-8; 0 out of bounds
    int blockLight(int x, int y) const; // 0-8; 0 out of bounds

private:
    struct LightSeed
    {
        int x;
        int y;
        int level;
    };

    // Multi-source BFS: every seed starts queued at its own level; each step,
    // the 4 orthogonal neighbours get level-1 wherever that beats what they
    // already have, stopping at level 0 or a solid tile (World::isSolid).
    // Since decay is always exactly 1 per step, a tile is only ever improved
    // once, so the BFS visit order alone is already the full sparse result -
    // no second full-grid scan needed to extract it. Static (no `this`) so it
    // can serve both the world-wide recompute and a single moving source
    // (Lighting::heldTorchLight, added later) alike.
    static std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world,
                                                                const std::vector<LightSeed>& seeds);

    std::vector<LightLevel> levels; // WORLD_WIDTH * WORLD_HEIGHT
};
```

- [ ] **Step 4: Create `src/World/Lighting.cpp`**

```cpp
#include "Lighting.h"

#include <algorithm>

#include "../Blocks/Blocks.h"
#include "../Core/Constants.h"
#include "../Machines/Machine.h"
#include "../Machines/MachineType.h"
#include "../Machines/Machines.h"
#include "World.h"

Lighting::Lighting()
    : levels(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, LightLevel{0, 0})
{
}

std::vector<std::pair<sf::Vector2i, int>> Lighting::floodFill(const World& world,
                                                                const std::vector<LightSeed>& seeds)
{
    std::vector<std::int8_t> best(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, -1);
    std::vector<LightSeed> queue;

    auto tryVisit = [&](int x, int y, int level)
    {
        if (level <= 0 || !world.inBounds(x, y) || world.isSolid(x, y))
            return;

        const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
        if (level <= best[i])
            return;

        best[i] = static_cast<std::int8_t>(level);
        queue.push_back({x, y, level});
    };

    for (const LightSeed& seed : seeds)
        tryVisit(seed.x, seed.y, seed.level);

    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const LightSeed e = queue[head];
        tryVisit(e.x - 1, e.y, e.level - 1);
        tryVisit(e.x + 1, e.y, e.level - 1);
        tryVisit(e.x, e.y - 1, e.level - 1);
        tryVisit(e.x, e.y + 1, e.level - 1);
    }

    std::vector<std::pair<sf::Vector2i, int>> result;
    result.reserve(queue.size());
    for (const LightSeed& e : queue)
        result.push_back({{e.x, e.y}, e.level});

    return result;
}

void Lighting::recomputeAll(const World& world, const Machines& machines)
{
    std::vector<LightSeed> blockSeeds;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            if (isLava(world.get(x, y)))
                blockSeeds.push_back({x, y, 8});

    for (const Machine& m : machines.all())
        if (m.type == MachineType::Torch)
            blockSeeds.push_back({m.x, m.y, 8});

    const auto blockResult = floodFill(world, blockSeeds);

    std::fill(levels.begin(), levels.end(), LightLevel{0, 0});

    for (const auto& [tile, level] : blockResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].block =
            static_cast<std::uint8_t>(level);
}

int Lighting::skyLight(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    return levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].sky;
}

int Lighting::blockLight(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    return levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].block;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - every `Lighting` block-light test passes, and the full existing suite still passes.

- [ ] **Step 6: Commit**

```bash
git add src/World/Lighting.h src/World/Lighting.cpp tests/test_lighting.cpp CMakeLists.txt
git commit -m "feat: add Lighting with a shared BFS floodFill and Lava/Torch block light"
```

---

### Task 4: Lighting - sky light pass

**Files:**
- Modify: `src/World/Lighting.cpp` (extend `recomputeAll`'s body only - the header's declared contract, and every other public method, are unchanged)
- Modify: `tests/test_lighting.cpp`

**Interfaces:**
- Consumes: same as Task 3, no new dependencies.
- Produces: `skyLight(x, y)` now returns real values instead of always 0 - Task 6/7 depend on this.

- [ ] **Step 1: Write the failing tests**

In `tests/test_lighting.cpp`, remove the now-outdated test (it asserted the pre-sky-pass state):

```cpp
TEST_CASE("skyLight is 0 everywhere before the sky pass exists")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 10) == 0);
}
```

Replace it with these four test cases:

```cpp
TEST_CASE("an open vertical shaft stays sky-lit at level 8 at every depth")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 50; ++y)
        world.set(10, y, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 0) == 8);
    CHECK(lighting.skyLight(10, 25) == 8);
    CHECK(lighting.skyLight(10, 50) == 8);
}

TEST_CASE("sky light decays by 1 per step spreading sideways from an open shaft")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 20; ++y)
        world.set(10, y, BlockType::Air);
    world.set(11, 20, BlockType::Air); // one step sideways off the shaft, same depth

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 20) == 8);
    CHECK(lighting.skyLight(11, 20) == 7);
}

TEST_CASE("a cave with no path to an open shaft reads sky 0, even directly beside a lit one")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 20; ++y)
        world.set(10, y, BlockType::Air);
    // A sealed pocket, walled off on every side by Stone - no orthogonal
    // path back to the shaft exists (the tile between them stays solid).
    world.set(12, 20, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(12, 20) == 0);
}

TEST_CASE("a column blocked from the surface gets no direct sky seed of its own")
{
    World world;
    fillSolid(world);
    // Open only near the very top; solid resumes at y=5 and stays solid the
    // rest of the way down, with no connection to any other open tile.
    world.set(10, 0, BlockType::Air);
    world.set(10, 1, BlockType::Air);
    world.set(10, 2, BlockType::Air);
    world.set(10, 3, BlockType::Air);
    world.set(10, 4, BlockType::Air);
    // (10, 5) onward stays Stone.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 4) == 8);
    CHECK(lighting.skyLight(10, 5) == 0); // solid: never lit
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe --test-case="an open vertical shaft stays sky-lit at level 8 at every depth"`
Expected: FAIL - `skyLight` still returns 0 everywhere (the sky pass doesn't exist yet).

- [ ] **Step 3: Extend `recomputeAll` with the sky-light pass**

In `src/World/Lighting.cpp`, replace `recomputeAll`'s body:

```cpp
void Lighting::recomputeAll(const World& world, const Machines& machines)
{
    std::vector<LightSeed> skySeeds;

    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        for (int y = 0; y < WORLD_HEIGHT; ++y)
        {
            if (world.isSolid(x, y))
                break;

            skySeeds.push_back({x, y, 8});
        }
    }

    std::vector<LightSeed> blockSeeds;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            if (isLava(world.get(x, y)))
                blockSeeds.push_back({x, y, 8});

    for (const Machine& m : machines.all())
        if (m.type == MachineType::Torch)
            blockSeeds.push_back({m.x, m.y, 8});

    const auto skyResult = floodFill(world, skySeeds);
    const auto blockResult = floodFill(world, blockSeeds);

    std::fill(levels.begin(), levels.end(), LightLevel{0, 0});

    for (const auto& [tile, level] : skyResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].sky =
            static_cast<std::uint8_t>(level);

    for (const auto& [tile, level] : blockResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].block =
            static_cast<std::uint8_t>(level);
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - every sky-light test passes, and the full existing suite (including Task 3's block-light tests, unaffected by this change) still passes.

- [ ] **Step 5: Commit**

```bash
git add src/World/Lighting.cpp tests/test_lighting.cpp
git commit -m "feat: add the sky light pass to Lighting::recomputeAll"
```

---

### Task 5: Lighting::heldTorchLight

**Files:**
- Modify: `src/World/Lighting.h` (add one public method declaration)
- Modify: `src/World/Lighting.cpp` (add its implementation)
- Modify: `tests/test_lighting.cpp`

**Interfaces:**
- Produces: `std::vector<std::pair<sf::Vector2i,int>> Lighting::heldTorchLight(const World&, sf::Vector2i) const` - `Game`/`LightRenderer` (Tasks 6-7) call this every frame when the player has a Torch selected.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_lighting.cpp`:

```cpp
TEST_CASE("heldTorchLight lights its source at level 8 and decays by 1 per step")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(8, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.heldTorchLight(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(10, 10) == 8);
    CHECK(levelAt(9, 10) == 7);
    CHECK(levelAt(8, 10) == 6);
}

TEST_CASE("heldTorchLight is blocked by solid tiles, same as a placed Torch")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    // (11, 10) stays Stone: solid, unreachable.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.heldTorchLight(world, {10, 10});

    for (const auto& [tile, level] : result)
        CHECK_FALSE(tile.x == 11 && tile.y == 10);
}

TEST_CASE("heldTorchLight never writes to the stored grid")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    lighting.heldTorchLight(world, {10, 10});

    // No Torch or Lava anywhere in this world, so the stored grid must still
    // read 0 - heldTorchLight is a pure query, not a mutation.
    CHECK(lighting.blockLight(10, 10) == 0);
    CHECK(lighting.blockLight(9, 10) == 0);
}
```

- [ ] **Step 2: Run the tests to verify they fail to build**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `Lighting::heldTorchLight` is not declared.

- [ ] **Step 3: Add `heldTorchLight`**

In `src/World/Lighting.h`, add this public method right after `blockLight`'s declaration:

```cpp
    // A single-source flood fill from `source` at level 8 - the same
    // brightness and decay/occlusion rule as a placed Torch's own
    // blockLight, but computed fresh every call rather than stored in
    // `levels`. Used for the player's held Torch, which moves with them
    // every frame: naturally bounded to within 8 steps of `source` (the seed
    // starts at level 8 and floodFill's decay reaches 0 by then), so this
    // stays cheap enough to call once a frame without forcing a full
    // recompute.
    std::vector<std::pair<sf::Vector2i, int>> heldTorchLight(const World& world,
                                                              sf::Vector2i source) const;
```

In `src/World/Lighting.cpp`, add the implementation at the end of the file:

```cpp
std::vector<std::pair<sf::Vector2i, int>> Lighting::heldTorchLight(const World& world,
                                                                    sf::Vector2i source) const
{
    return floodFill(world, {LightSeed{source.x, source.y, 8}});
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - every `heldTorchLight` test passes, and the full existing suite still passes.

- [ ] **Step 5: Commit**

```bash
git add src/World/Lighting.h src/World/Lighting.cpp tests/test_lighting.cpp
git commit -m "feat: add Lighting::heldTorchLight for the player's carried-Torch light"
```

---

### Task 6: LightRenderer

**Files:**
- Create: `src/World/LightRenderer.h`
- Create: `src/World/LightRenderer.cpp`
- Modify: `CMakeLists.txt` (add `src/World/LightRenderer.cpp` to the `Litharia` executable target's sources - SFML Graphics, not core)

**Interfaces:**
- Consumes: `Lighting::skyLight`/`blockLight` (Tasks 3-4); the `heldTorchLight` result shape `std::vector<std::pair<sf::Vector2i,int>>` (Task 5); `TILE_SIZE`/`WORLD_WIDTH`/`WORLD_HEIGHT` (`Core/Constants.h`).
- Produces: `class LightRenderer { void draw(sf::RenderTarget&, const sf::View&, const Lighting&, float daylightFactor, const std::vector<std::pair<sf::Vector2i,int>>& heldTorchLight) const; }` - `Game::render()` (Task 7) calls this once per frame.

This class has no unit test: it draws with `sf::RenderTarget`/`sf::View`/`sf::VertexArray`, the same SFML Graphics types `ChunkRenderer` and `MachineRenderer` use, and like those two classes it is compiled only into the `Litharia` executable target, which the window-free `Litharia_tests` binary never links. Its only available verification is that the executable builds and links - Task 7 wires it into `Game::render()`, where its actual on-screen effect becomes visible.

- [ ] **Step 1: Create `src/World/LightRenderer.h`**

```cpp
#pragma once

#include <SFML/Graphics.hpp>

#include <utility>
#include <vector>

#include "Lighting.h"

// Draws a screen-darkening overlay on top of everything else in the world:
// solid dark where a tile has no sky or block light reaching it, tinted
// warm where a Torch/Lava tile dominates and cool-toward-white where
// daylight dominates. Drawn with sf::BlendMultiply so it darkens whatever
// was already drawn underneath without needing every entity to know about
// lighting itself.
//
// Unlike ChunkRenderer, this rebuilds every visible tile fresh every single
// frame rather than caching per-chunk geometry: the day/night clock and the
// player's held-Torch light both change continuously, so there is no
// "clean, skip it" case to cache against - a persistent chunk grid would
// only add bookkeeping with nothing to skip.
class LightRenderer
{
public:
    void draw(sf::RenderTarget& target, const sf::View& view, const Lighting& lighting,
              float daylightFactor, const std::vector<std::pair<sf::Vector2i, int>>& heldTorchLight) const;
};
```

- [ ] **Step 2: Create `src/World/LightRenderer.cpp`**

```cpp
#include "LightRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "../Core/Constants.h"

namespace
{

constexpr sf::Color NIGHT_TINT(20, 25, 45);
constexpr sf::Color DAY_TINT(225, 235, 250);
constexpr sf::Color BLOCK_TINT(255, 180, 90);

sf::Color lerp(sf::Color a, sf::Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return sf::Color(
        static_cast<std::uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<std::uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<std::uint8_t>(a.b + (b.b - a.b) * t));
}

std::int64_t tileKey(int x, int y)
{
    return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::uint32_t>(y);
}

// Two triangles per tile: SFML 3 has no quad primitive. Matches Chunks.cpp's
// own appendQuad - small enough that each file keeping its own copy reads
// more clearly than sharing a one-off header for it.
void appendQuad(sf::VertexArray& vertices, float left, float top, float right, float bottom,
                sf::Color color)
{
    vertices.append({{left, top}, color});
    vertices.append({{right, top}, color});
    vertices.append({{right, bottom}, color});

    vertices.append({{left, top}, color});
    vertices.append({{right, bottom}, color});
    vertices.append({{left, bottom}, color});
}

} // namespace

void LightRenderer::draw(sf::RenderTarget& target, const sf::View& view, const Lighting& lighting,
                          float daylightFactor,
                          const std::vector<std::pair<sf::Vector2i, int>>& heldTorchLight) const
{
    const sf::Vector2f center = view.getCenter();
    const sf::Vector2f size = view.getSize();

    const float viewLeft = center.x - size.x * 0.5f;
    const float viewTop = center.y - size.y * 0.5f;
    const float viewRight = center.x + size.x * 0.5f;
    const float viewBottom = center.y + size.y * 0.5f;

    const int firstX = std::max(0, static_cast<int>(std::floor(viewLeft / TILE_SIZE)) - 1);
    const int firstY = std::max(0, static_cast<int>(std::floor(viewTop / TILE_SIZE)) - 1);
    const int lastX = std::min(WORLD_WIDTH - 1, static_cast<int>(std::floor(viewRight / TILE_SIZE)) + 1);
    const int lastY = std::min(WORLD_HEIGHT - 1, static_cast<int>(std::floor(viewBottom / TILE_SIZE)) + 1);

    std::unordered_map<std::int64_t, int> heldMap;
    for (const auto& [tile, level] : heldTorchLight)
        heldMap[tileKey(tile.x, tile.y)] = level;

    const sf::Color skyTint = lerp(NIGHT_TINT, DAY_TINT, daylightFactor);

    sf::VertexArray vertices(sf::PrimitiveType::Triangles);

    for (int y = firstY; y <= lastY; ++y)
    {
        for (int x = firstX; x <= lastX; ++x)
        {
            const float skyEffective = lighting.skyLight(x, y) * daylightFactor;

            int blockEffective = lighting.blockLight(x, y);
            const auto it = heldMap.find(tileKey(x, y));
            if (it != heldMap.end())
                blockEffective = std::max(blockEffective, it->second);

            const float brightness = std::clamp(
                std::max(skyEffective, static_cast<float>(blockEffective)) / 8.0f, 0.0f, 1.0f);

            const sf::Color tint = blockEffective > skyEffective ? BLOCK_TINT : skyTint;
            const sf::Color overlay = lerp(sf::Color::Black, tint, brightness);

            const float left = static_cast<float>(x * TILE_SIZE);
            const float top = static_cast<float>(y * TILE_SIZE);
            appendQuad(vertices, left, top, left + TILE_SIZE, top + TILE_SIZE, overlay);
        }
    }

    target.draw(vertices, sf::BlendMultiply);
}
```

- [ ] **Step 3: Add the new file to CMakeLists.txt**

In `CMakeLists.txt`, add `src/World/LightRenderer.cpp` to the `Litharia` executable's sources (not `Litharia_core` - it uses SFML Graphics):

```cmake
add_executable(Litharia
    src/main.cpp

    src/Game/Game.cpp
    src/World/Chunks.cpp
    src/World/LightRenderer.cpp
    src/Camera/Camera.cpp
    src/Hud/Hud.cpp
    src/Machines/MachineRenderer.cpp
)
```

- [ ] **Step 4: Verify it builds**

Run: `cmake --build build --config Debug --target Litharia`
Expected: SUCCESS - `LightRenderer.cpp` compiles and links into the executable. (No behavioral test exists for this file - see this task's Interfaces note. `Litharia_tests` is unaffected and still passes.)

- [ ] **Step 5: Commit**

```bash
git add src/World/LightRenderer.h src/World/LightRenderer.cpp CMakeLists.txt
git commit -m "feat: add LightRenderer, a BlendMultiply darkness overlay over sky/block light"
```

---

### Task 7: Wire the day/night cycle and lighting into Game and the HUD

**Files:**
- Modify: `src/Game/Game.h`
- Modify: `src/Game/Game.cpp`
- Modify: `src/Hud/Hud.h`
- Modify: `src/Hud/Hud.cpp`

**Interfaces:**
- Consumes: `DayNightClock` (Task 1), `MachineType::Torch`/`ItemType::Torch` (Task 2), `Lighting` (Tasks 3-5), `LightRenderer` (Task 6).
- Produces: the fully wired feature - a running `Litharia` shows a shifting sky, dark caves, a placeable/holdable Torch, and a HUD day/night indicator. Nothing later in this spec depends on new interfaces from this task; it is the integration point.

This task has no isolated unit test of its own (it is `Game`/`Hud` wiring, both SFML Graphics/Window code with no window-free test target) - verification is that `Litharia_tests` still passes in full (nothing here should change any core-lib behavior) and that `Litharia` builds and runs. Per project convention, live/manual verification of the in-game visual result (does the cave actually look dark, does the Torch actually light up when held) is left to you to check by running the game - this plan only confirms it compiles, links, and doesn't break the automated suite.

- [ ] **Step 1: Add new members and change two method signatures in `src/Game/Game.h`**

Add three includes near the other `World/` includes:

```cpp
#include "../World/Chunks.h"
#include "../World/DayNightClock.h"
#include "../World/FluidSim.h"
#include "../World/Lighting.h"
#include "../World/LightRenderer.h"
#include "../World/TerrainGenerator.h"
#include "../World/World.h"
```

Change these two method declarations to return `bool`:

```cpp
    bool placeFurnitureAtCursor(const PlayerInput& input);
    bool mineFurnitureAtCursor(const PlayerInput& input, float dt);
```

Add three new members, right after `FluidSim fluids;`:

```cpp
    Machines machines;
    MachineRenderer machineRenderer;
    FluidSim fluids;
    DayNightClock dayNightClock;
    Lighting lighting;
    LightRenderer lightRenderer;
```

- [ ] **Step 2: Wire construction and the Torch furniture mapping in `src/Game/Game.cpp`**

Add a case to `furnitureMachineForItem`:

```cpp
MachineType furnitureMachineForItem(ItemType item)
{
    switch (item)
    {
        case ItemType::Chest:         return MachineType::Chest;
        case ItemType::CraftingTable: return MachineType::CraftingTable;
        case ItemType::Furnace:       return MachineType::Furnace;
        case ItemType::Torch:         return MachineType::Torch;
        default:                     return MachineType::None;
    }
}
```

Add sky-color helpers to the same anonymous namespace, right after `itemColor`:

```cpp
constexpr sf::Color NIGHT_SKY(15, 18, 35);
constexpr sf::Color DAY_SKY(122, 184, 240); // the game's original fixed sky color

sf::Color lerpColor(sf::Color a, sf::Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return sf::Color(
        static_cast<std::uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<std::uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<std::uint8_t>(a.b + (b.b - a.b) * t));
}

sf::Color skyColor(float daylightFactor)
{
    return lerpColor(NIGHT_SKY, DAY_SKY, daylightFactor);
}
```

In the `Game::Game()` constructor, add one line right after `fluids.activateAll(world);`:

```cpp
    spawnSharpRocks();
    fluids.activateAll(world);
    lighting.recomputeAll(world, machines);
```

- [ ] **Step 3: Return `bool` from `placeFurnitureAtCursor` and `mineFurnitureAtCursor`**

Replace `Game::placeFurnitureAtCursor`:

```cpp
bool Game::placeFurnitureAtCursor(const PlayerInput& input)
{
    if (!input.place)
        return false;

    const ItemStack& held = player.inventory().slot(player.selectedSlot());
    const MachineType type = furnitureMachineForItem(held.type);

    if (type == MachineType::None)
        return false;

    const sf::Vector2i tile = cursorTile();

    if (!player.inReach(tile.x, tile.y))
        return false;

    const MachineInfo& info = machineInfo(type);

    for (int dy = 0; dy < info.height; ++dy)
        for (int dx = 0; dx < info.width; ++dx)
        {
            if (world.isSolid(tile.x + dx, tile.y + dy))
                return false;

            const AABB tileBox{{static_cast<float>((tile.x + dx) * TILE_SIZE),
                                static_cast<float>((tile.y + dy) * TILE_SIZE)},
                               {static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)}};

            if (physics::overlaps(tileBox, player.box()))
                return false;
        }

    if (machines.place(type, tile.x, tile.y, Direction::Right) == nullptr)
        return false;

    player.inventory().removeOne(held.type);
    return true;
}
```

Replace `Game::mineFurnitureAtCursor`:

```cpp
bool Game::mineFurnitureAtCursor(const PlayerInput& input, float dt)
{
    const sf::Vector2i tile = cursorTile();
    const Machine* target = machines.at(tile.x, tile.y);
    const bool validTarget = input.mine && target != nullptr && isFurniture(target->type)
        && player.inReach(tile.x, tile.y);

    if (!validTarget)
    {
        miningFurniture = false;
        miningFurnitureProgress = 0.0f;
        return false;
    }

    if (!miningFurniture || miningFurnitureTarget.x != tile.x || miningFurnitureTarget.y != tile.y)
    {
        miningFurniture = true;
        miningFurnitureTarget = tile;
        miningFurnitureProgress = 0.0f;
    }

    miningFurnitureProgress += dt;
    if (miningFurnitureProgress < MINING_FURNITURE_SECONDS)
        return false;

    const MachineType type = target->type;
    const Inventory storage = target->storage;
    if (!machines.remove(tile.x, tile.y))
        return false;

    refundMachineItem(type);
    spillInventoryToGround(storage);

    miningFurniture = false;
    miningFurnitureProgress = 0.0f;
    return true;
}
```

- [ ] **Step 4: Tick the clock and recompute lighting on the rare edits, in `Game::fixedUpdate`**

Replace `Game::fixedUpdate` in full:

```cpp
void Game::fixedUpdate(float dt)
{
    dayNightClock.tick(dt);

    const PlayerInput input = readInput();
    const ActionResult result = player.update(input, world, dt, &machines);

    // Spawn at the position the hit landed, before a fatal hit's respawn (just
    // below) moves the player away from it.
    if (result.damageTaken > 0)
        spawnDamagePopup(result.damageTaken);

    if (player.isDead())
    {
        player.respawn(findSpawn());
        camera.snapTo(player.center());
    }

    bool lightingDirty = result.broke || result.placed;

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

    if (placeFurnitureAtCursor(input))
        lightingDirty = true;
    if (mineFurnitureAtCursor(input, dt))
        lightingDirty = true;

    if (lightingDirty)
        lighting.recomputeAll(world, machines);

    updateDrops(dt);
    updateDamagePopups(dt);
    respawnSharpRocksIfNeeded(dt);
    tickMachines(dt);
    updateCrafting(dt);
    updateSmelting(dt);

    std::vector<sf::Vector2i> fluidChanges;
    fluids.tick(world, dt, fluidChanges);
    for (const sf::Vector2i& t : fluidChanges)
        chunks.markDirty(t.x, t.y);

    camera.follow(player.center(), dt);

    const ItemStack& held = player.inventory().slot(player.selectedSlot());

    const std::string mode = buildMode
        ? "  -  BUILD: " + std::string(machineInfo(buildType).name)
        : "";

    window.setTitle("Litharia" + mode + "  -  machines: " + std::to_string(machines.count()) +
                    "  -  drops: " + std::to_string(drops.size()) + "  -  holding: " +
                    std::string(held.empty() ? "nothing"
                                             : std::string(itemInfo(held.type).name) + " x" +
                                                   std::to_string(held.count)));
}
```

- [ ] **Step 5: Draw the sky, the light overlay, and the HUD indicator in `Game::render`**

Replace `Game::render` in full:

```cpp
void Game::render()
{
    window.clear(skyColor(dayNightClock.daylightFactor()));

    window.setView(camera.view());

    chunks.draw(window, camera.view());

    machineRenderer.draw(window, machines, buildMode);

    drawMiningHighlight();

    // Dropped stacks.
    sf::RectangleShape item({ItemEntity::SIZE, ItemEntity::SIZE});
    item.setOutlineThickness(-1.0f);
    item.setOutlineColor(sf::Color(30, 25, 20));

    for (const ItemEntity& drop : drops)
    {
        item.setPosition(drop.position());
        item.setFillColor(itemColor(drop.stack().type));

        window.draw(item);
    }

    // The player, until there is a sprite for one.
    sf::RectangleShape body({Player::WIDTH, Player::HEIGHT});
    body.setPosition(player.position());
    body.setFillColor(sf::Color(232, 90, 80));
    body.setOutlineThickness(-2.0f);
    body.setOutlineColor(sf::Color(40, 20, 20));

    window.draw(body);

    drawDamagePopups();

    std::vector<std::pair<sf::Vector2i, int>> heldLight;
    const ItemStack& held = player.inventory().slot(player.selectedSlot());
    if (held.type == ItemType::Torch)
    {
        const sf::Vector2i playerTile{
            static_cast<int>(std::floor(player.center().x / TILE_SIZE)),
            static_cast<int>(std::floor(player.center().y / TILE_SIZE))};
        heldLight = lighting.heldTorchLight(world, playerTile);
    }

    lightRenderer.draw(window, camera.view(), lighting, dayNightClock.daylightFactor(), heldLight);

    hud.draw(window, player.inventory(), player.selectedSlot());
    hud.drawHealth(window, player.health(), Player::MAX_HEALTH);
    hud.drawDayNightIndicator(window, dayNightClock.daylightFactor());

    const sf::Vector2f mouseScreenPos(sf::Mouse::getPosition(window));
    if (hud.isHealthBarHovered(mouseScreenPos))
        hud.drawHealthTooltip(window, player.health(), Player::MAX_HEALTH);

    if (buildMode)
        hud.drawBuildPalette(window, buildType, player.inventory());

    if (inventoryOpen)
        drawInventoryPanels();

    if (dragging)
        hud.drawDragGhost(window, dragStack, sf::Vector2f(sf::Mouse::getPosition(window)));

    drawMachineTooltip();

    window.display();
}
```

- [ ] **Step 6: Add `Hud::drawDayNightIndicator`**

In `src/Hud/Hud.h`, add this public method declaration right after `drawHealthTooltip`:

```cpp
    // A small bar in the top-right HUD cluster (just above the hotbar)
    // showing the day/night clock's current daylightFactor as a fill
    // fraction. Shapes only, like drawHealth - renders fine with no font.
    void drawDayNightIndicator(sf::RenderWindow& window, float daylightFactor);
```

In `src/Hud/Hud.cpp`, add the implementation right after `Hud::drawHealth`'s closing brace:

```cpp
void Hud::drawDayNightIndicator(sf::RenderWindow& window, float daylightFactor)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    constexpr float WIDTH = 120.0f;
    constexpr float HEIGHT = 10.0f;

    const sf::Vector2f hotbar = hotbarOrigin(sf::Vector2f(window.getSize()));
    const sf::Vector2f pos{hotbar.x, hotbar.y - HEIGHT - MARGIN};

    sf::RectangleShape back({WIDTH, HEIGHT});
    back.setPosition(pos);
    back.setFillColor(sf::Color(20, 20, 30));
    back.setOutlineThickness(2.0f);
    back.setOutlineColor(sf::Color(10, 10, 15));
    window.draw(back);

    const float frac = std::clamp(daylightFactor, 0.0f, 1.0f);

    if (frac > 0.0f)
    {
        sf::RectangleShape fill({WIDTH * frac, HEIGHT});
        fill.setPosition(pos);
        fill.setFillColor(sf::Color(255, 210, 120));
        window.draw(fill);
    }

    window.setView(previous);
}
```

- [ ] **Step 7: Build both targets and run the full test suite**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - the full suite (every existing test plus every test from Tasks 1-5) passes unchanged; nothing in this task touches core-lib behavior.

Run: `cmake --build build --config Debug --target Litharia`
Expected: SUCCESS - the game builds with the day/night cycle and lighting fully wired in.

Manually running the game and confirming the visual result (sky shifting, caves darkening, the Torch lighting up when held/placed) is left to you - this plan's own verification stops at "it builds and the automated suite passes."

- [ ] **Step 8: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "feat: wire the day/night cycle and cave lighting into Game and the HUD"
```
