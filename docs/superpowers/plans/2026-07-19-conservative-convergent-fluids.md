# Conservative, Convergent Fluid Settling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the non-conservative fluid-settling rules with a conservative, provably-convergent model so every pool settles to a full stop, and move the dead-flat surface look into the renderer.

**Architecture:** Unify `flattenAt` + `spreadAt` into a single conservative `equalizeAt` rule that only moves fluid `floor(diff/2)` toward a strictly-lower resting neighbour and never moves on a difference of 1. Keep `fallAt`/`cascadeAt`/`reactAt` unchanged. Add a pure `fluidSurfaceHeight` helper in core that the chunk renderer uses to draw a surface run at one flat height.

**Tech Stack:** C++20, MSVC (Visual Studio generator), SFML 3, doctest. Build via the existing CMake project in `build/`.

## Global Constraints

- C++20, `CMAKE_CXX_STANDARD 20` (verbatim from `CMakeLists.txt`).
- Core library `Litharia_core` links **SFML::System only** — no Graphics/Window in core. The renderer helper is SFML-free.
- World is `WORLD_WIDTH = 1000` x `WORLD_HEIGHT = 500`; `TILE_SIZE = 16`; `CHUNK_SIZE = 32` (from `src/Core/Constants.h`).
- Fluid tile levels are 1..8; `fluidLevel`, `fluidAtLevel`, `isWater`, `isLava`, `isFluid` live in `src/Blocks/Blocks.h`.
- Fluid sim step cadence: `FluidSim::TICK_INTERVAL = 0.1f`; one `tick(world, TICK_INTERVAL, changed)` call runs exactly one step.
- Obsidian reaction is the ONLY intentional fluid consumer; every other move conserves total fluid exactly.
- Tests use doctest; the test target lists each test file explicitly in `CMakeLists.txt`.
- Build command (Debug): `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug`
- Run tests: `& "build/Debug/Litharia_tests.exe"`

---

### Task 1: Commit the already-applied active-queue dedup fix

The dedup fix (a `pending` flag so the active queue holds each tile at most once) is already applied to `src/World/FluidSim.cpp` / `.h` and verified (all 301 tests pass; worst fluid step dropped from ~13.7 ms to ~0.5 ms). Commit it as the branch's first change so history is clean before the redesign.

**Files:**
- Modify (already changed, uncommitted): `src/World/FluidSim.cpp`, `src/World/FluidSim.h`

**Interfaces:**
- Consumes: nothing.
- Produces: `FluidSim` with a deduplicated active queue; public API unchanged (`activate`, `activateAround`, `activateAll`, `tick`).

- [ ] **Step 1: Confirm the working tree has the dedup change and nothing else stray**

Run: `git status --short && git diff --stat`
Expected: only `src/World/FluidSim.cpp` and `src/World/FluidSim.h` modified.

- [ ] **Step 2: Build and run the full suite to confirm green before committing**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: `[doctest] Status: SUCCESS!`, 301 test cases passed.

- [ ] **Step 3: Commit**

```bash
git add src/World/FluidSim.cpp src/World/FluidSim.h
git commit -m "perf: dedupe FluidSim active queue so it stays proportional to live fluid

The active-tile queue pushed every activateAround target unconditionally, so
the same tile was queued and fully re-simulated dozens of times per step - the
queue grew to ~40x the live fluid-tile count and the worst step cost ~13.7 ms.
A per-tile pending flag keeps the queue a true set; worst step ~0.5 ms.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Replace flatten + spread with the conservative `equalizeAt` rule

Swap the two non-conservative horizontal rules for one conservative rule, rewire `step()`, and update `canMove` so lava still goes quiescent. This is the core of the redesign and carries the fluid-simulation tests.

**Files:**
- Modify: `src/World/FluidSim.h` (replace `flattenAt`/`spreadAt` declarations with `equalizeAt`)
- Modify: `src/World/FluidSim.cpp` (remove `flattenAt`/`spreadAt`, add `equalizeAt`, update `step` and `canMove`)
- Test: `tests/test_fluids.cpp`

**Interfaces:**
- Consumes: `FluidSim` from Task 1; `sameFluid` (file-local helper in `FluidSim.cpp`); `fluidLevel`, `fluidAtLevel` from `Blocks.h`.
- Produces: private method `bool FluidSim::equalizeAt(World&, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed)`; `flattenAt` and `spreadAt` no longer exist.

- [ ] **Step 1: Rewrite the two tests that asserted non-conservation, and add equalize behavior tests**

In `tests/test_fluids.cpp`, **replace** the test case titled `"a resting, uneven row of water levels itself flat in a single step"` (currently around line 219) with:

```cpp
TEST_CASE("a resting, uneven row of water settles flat and conserves its total")
{
    World world;

    // A four-wide basin: stone floor, stone walls at each end.
    for (int x = 8; x <= 11; ++x)
        world.set(x, 11, BlockType::Stone);
    world.set(7, 10, BlockType::Stone);
    world.set(12, 10, BlockType::Stone);

    // Deep on the left, shallow on the right - total 8 + 8 + 2 + 2 = 20.
    world.set(8, 10, BlockType::Water8);
    world.set(9, 10, BlockType::Water8);
    world.set(10, 10, BlockType::Water2);
    world.set(11, 10, BlockType::Water2);

    FluidSim sim;
    for (int x = 8; x <= 11; ++x)
        sim.activate(x, 10);

    // Gradual slosh: run to a full stop rather than a single snap.
    std::vector<sf::Vector2i> changed;
    int emptyRun = 0;
    for (int i = 0; i < 200 && emptyRun < 3; ++i)
    {
        changed.clear();
        sim.tick(world, FLUID_STEP, changed);
        emptyRun = changed.empty() ? emptyRun + 1 : 0;
    }

    // 20 over 4 columns settles to exactly 5 each, conserving the total.
    int total = 0;
    for (int x = 8; x <= 11; ++x)
        total += fluidLevelAt(world, x, 10);
    CHECK(total == 20);
    for (int x = 8; x <= 11; ++x)
        CHECK(world.get(x, 10) == BlockType::Water5);
}
```

**Replace** the test case titled `"an uneven run rounds to the nearest level and stores it uniformly"` (currently around line 248) with:

```cpp
TEST_CASE("an uneven run settles flat within one level and conserves exactly")
{
    World world;

    for (int x = 8; x <= 11; ++x)
        world.set(x, 11, BlockType::Stone);
    world.set(7, 10, BlockType::Stone);
    world.set(12, 10, BlockType::Stone);

    // Total 6 + 6 + 6 + 1 = 19 over 4 columns. Conservative settling keeps the
    // total at 19 (three cells at 5, one at 4) - flat within a single level -
    // rather than rounding to 20.
    world.set(8, 10, BlockType::Water6);
    world.set(9, 10, BlockType::Water6);
    world.set(10, 10, BlockType::Water6);
    world.set(11, 10, BlockType::Water1);

    FluidSim sim;
    for (int x = 8; x <= 11; ++x)
        sim.activate(x, 10);

    std::vector<sf::Vector2i> changed;
    int emptyRun = 0;
    for (int i = 0; i < 200 && emptyRun < 3; ++i)
    {
        changed.clear();
        sim.tick(world, FLUID_STEP, changed);
        emptyRun = changed.empty() ? emptyRun + 1 : 0;
    }

    int total = 0;
    int minLevel = 8;
    int maxLevel = 1;
    for (int x = 8; x <= 11; ++x)
    {
        const int lvl = fluidLevelAt(world, x, 10);
        total += lvl;
        minLevel = std::min(minLevel, lvl);
        maxLevel = std::max(maxLevel, lvl);
    }
    CHECK(total == 19);            // conserved exactly
    CHECK(maxLevel - minLevel <= 1); // flat within one level
}
```

Add `#include <algorithm>` near the top include block of `tests/test_fluids.cpp` (for `std::min`/`std::max`) if not already present.

Then **append** these new test cases at the end of `tests/test_fluids.cpp`:

```cpp
TEST_CASE("equalize leaves a one-level surface difference alone (no flicker)")
{
    World world;
    world.set(8, 11, BlockType::Stone);
    world.set(9, 11, BlockType::Stone);
    world.set(7, 10, BlockType::Stone);
    world.set(10, 10, BlockType::Stone);
    world.set(8, 10, BlockType::Water5);
    world.set(9, 10, BlockType::Water4);

    FluidSim sim;
    sim.activate(8, 10);
    sim.activate(9, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // A difference of one is the stable remainder: nothing moves.
    CHECK(world.get(8, 10) == BlockType::Water5);
    CHECK(world.get(9, 10) == BlockType::Water4);
    CHECK(changed.empty());
}

TEST_CASE("equalize moves floor(diff/2) toward a strictly lower neighbour and conserves")
{
    World world;
    world.set(8, 11, BlockType::Stone);
    world.set(9, 11, BlockType::Stone);
    world.set(7, 10, BlockType::Stone);
    world.set(10, 10, BlockType::Stone);
    world.set(8, 10, BlockType::Water8);
    world.set(9, 10, BlockType::Water2);

    FluidSim sim;
    sim.activate(8, 10);
    sim.activate(9, 10);

    std::vector<sf::Vector2i> changed;
    sim.tick(world, FLUID_STEP, changed);

    // (8 - 2) / 2 = 3 moves right: 8 -> 5, 2 -> 5. Total conserved at 10.
    CHECK(world.get(8, 10) == BlockType::Water5);
    CHECK(world.get(9, 10) == BlockType::Water5);
    CHECK(fluidLevelAt(world, 8, 10) + fluidLevelAt(world, 9, 10) == 10);
}

TEST_CASE("a settled pool produces no further changes (fully quiescent)")
{
    World world;
    for (int x = 8; x <= 11; ++x)
        world.set(x, 11, BlockType::Stone);
    world.set(7, 10, BlockType::Stone);
    world.set(12, 10, BlockType::Stone);
    for (int x = 8; x <= 11; ++x)
        world.set(x, 10, BlockType::Water5);

    FluidSim sim;
    for (int x = 8; x <= 11; ++x)
        sim.activate(x, 10);

    std::vector<sf::Vector2i> changed;
    for (int i = 0; i < 5; ++i)
        sim.tick(world, FLUID_STEP, changed);

    // Already flat and conserved: an equal, resting row never churns.
    CHECK(changed.empty());
    for (int x = 8; x <= 11; ++x)
        CHECK(world.get(x, 10) == BlockType::Water5);
}
```

- [ ] **Step 2: Run the new tests to verify they fail against the current (flatten/spread) code**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: FAIL. The "rounds to nearest level" replacement and the no-flicker test fail because the current `flattenAt` rounds non-conservatively (19 -> 20) and re-levels difference-of-1 rows.

- [ ] **Step 3: In `src/World/FluidSim.h`, replace the `flattenAt` and `spreadAt` declarations with `equalizeAt`**

Find:

```cpp
    bool flattenAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);
    bool spreadAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);
```

Replace with:

```cpp
    // Conservative horizontal levelling. Runs only when the cell cannot fall
    // (rests on full support). Moves floor((self - neighbour)/2) units toward
    // the strictly-lower resting neighbour, and nothing when the difference is
    // one - so a settled surface goes still and no fluid is created or lost.
    bool equalizeAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);
```

- [ ] **Step 4: In `src/World/FluidSim.cpp`, rewire `step()` to call `equalizeAt`**

Find the rule chain in `step()`:

```cpp
        if (fallAt(world, x, y, type, changedTiles))
            continue;

        if (flattenAt(world, x, y, type, changedTiles))
            continue;

        if (spreadAt(world, x, y, type, changedTiles))
            continue;

        cascadeAt(world, x, y, type, changedTiles);
```

Replace with:

```cpp
        if (fallAt(world, x, y, type, changedTiles))
            continue;

        if (equalizeAt(world, x, y, type, changedTiles))
            continue;

        cascadeAt(world, x, y, type, changedTiles);
```

- [ ] **Step 5: In `src/World/FluidSim.cpp`, delete the whole `flattenAt` and `spreadAt` function definitions and add `equalizeAt` in their place**

Remove the entire `bool FluidSim::flattenAt(...) { ... }` and `bool FluidSim::spreadAt(...) { ... }` definitions. Insert this single definition where they were (between `fallAt` and `cascadeAt`):

```cpp
bool FluidSim::equalizeAt(World& world, int x, int y, BlockType type,
                          std::vector<sf::Vector2i>& changed)
{
    const int level = fluidLevel(type);

    // Find the strictly-lower resting neighbour that gives the biggest downhill
    // move. Air counts as level 0. Ties go left (dx = -1 is tried first and we
    // only replace on a strict improvement).
    int bestDx = 0;
    int bestNeighborLevel = level;

    for (const int dx : {-1, 1})
    {
        const int nx = x + dx;

        if (!world.inBounds(nx, y))
            continue;

        const BlockType n = world.get(nx, y);
        const bool neighborIsAir = (n == BlockType::Air);

        if (!neighborIsAir && !sameFluid(type, n))
            continue; // a wall or a different fluid: cannot level into it

        // The neighbour must rest: an air neighbour over open air is a ledge,
        // left to cascadeAt, not filled here.
        const BlockType belowNeighbor = world.get(nx, y + 1);
        const bool neighborRests = !(belowNeighbor == BlockType::Air && world.inBounds(nx, y + 1));
        if (!neighborRests)
            continue;

        const int neighborLevel = neighborIsAir ? 0 : fluidLevel(n);

        if (neighborLevel < bestNeighborLevel)
        {
            bestNeighborLevel = neighborLevel;
            bestDx = dx;
        }
    }

    if (bestDx == 0)
        return false;

    // No overshoot, and no move on a difference of one: that is the stable
    // remainder that lets a within-one-level surface come to rest.
    const int move = (level - bestNeighborLevel) / 2;
    if (move < 1)
        return false;

    const int nx = x + bestDx;
    world.set(nx, y, fluidAtLevel(type, bestNeighborLevel + move));
    world.set(x, y, fluidAtLevel(type, level - move));
    activateAround(x, y);
    activateAround(nx, y);
    changed.push_back({x, y});
    changed.push_back({nx, y});
    return true;
}
```

- [ ] **Step 6: In `src/World/FluidSim.cpp`, update `canMove` so lava's throttle matches the new equalize rule**

Find the current `canMove` body:

```cpp
    for (const int dx : {-1, 1})
    {
        if (!world.inBounds(x + dx, y))
            continue;

        const BlockType n = world.get(x + dx, y);

        if (n == BlockType::Air)
            return true;

        if (sameFluid(type, n) && fluidLevel(n) != fluidLevel(type))
            return true;
    }

    return false;
```

Replace with:

```cpp
    const int level = fluidLevel(type);

    for (const int dx : {-1, 1})
    {
        const int nx = x + dx;

        if (!world.inBounds(nx, y))
            continue;

        const BlockType n = world.get(nx, y);
        const bool neighborIsAir = (n == BlockType::Air);

        if (!neighborIsAir && !sameFluid(type, n))
            continue;

        // Mirror equalizeAt: the neighbour must rest, and there must be a real
        // (>= 2) downhill difference for a move to happen. Anything less is the
        // stable remainder, so the tile is done.
        const BlockType belowNeighbor = world.get(nx, y + 1);
        const bool neighborRests = !(belowNeighbor == BlockType::Air && world.inBounds(nx, y + 1));
        if (!neighborRests)
            continue;

        const int neighborLevel = neighborIsAir ? 0 : fluidLevel(n);
        if (level - neighborLevel >= 2)
            return true;
    }

    return false;
```

Note: the `below == Air` (can fall) and `sameFluid below with space` (can pour) checks at the top of `canMove` stay unchanged; only the horizontal loop above is replaced.

- [ ] **Step 7: Build and run the full suite**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: `[doctest] Status: SUCCESS!` — all tests pass, including the rewritten and new fluid tests, and the unchanged conservation/reaction/lava/spill/basin tests.

- [ ] **Step 8: Commit**

```bash
git add src/World/FluidSim.h src/World/FluidSim.cpp tests/test_fluids.cpp
git commit -m "feat: conservative equalize rule replaces non-conservative flatten/spread

fallAt owns vertical filling; equalizeAt owns horizontal levelling on full
support, moving floor(diff/2) toward a strictly-lower resting neighbour and
never on a difference of one. Their domains are disjoint and every move is
conservative, so pools settle to flat-within-one-level and go fully quiescent.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Convergence test over generated worlds

Lock in the whole point of the redesign: real generated worlds (including seeds that oscillated forever before) reach a full stop.

**Files:**
- Test: `tests/test_fluids.cpp`

**Interfaces:**
- Consumes: `FluidSim`, `TerrainGenerator`, `World`.
- Produces: nothing (test only).

- [ ] **Step 1: Add the convergence test**

Add `#include "World/TerrainGenerator.h"` to the include block near the top of `tests/test_fluids.cpp` (after the other `World/...` includes). Then append:

```cpp
TEST_CASE("every generated world's fluids settle to a full stop")
{
    // Seeds 1, 42 and 555 all oscillated forever under the old flatten/spread
    // rules; a conservative sim must bring each to quiescence.
    for (std::uint32_t seed : {1u, 42u, 555u, 7u, 88u})
    {
        World world;
        TerrainGenerator gen(seed);
        gen.generate(world);

        FluidSim sim;
        sim.activateAll(world);

        bool settled = false;
        int emptyRun = 0;
        for (int i = 0; i < 4000; ++i)
        {
            std::vector<sf::Vector2i> changed;
            sim.tick(world, FLUID_STEP, changed);
            emptyRun = changed.empty() ? emptyRun + 1 : 0;
            if (emptyRun >= 3) { settled = true; break; }
        }

        CHECK(settled);
    }
}
```

Add `#include <cstdint>` to the top include block if `std::uint32_t` is not already available there.

- [ ] **Step 2: Build and run the full suite**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: `[doctest] Status: SUCCESS!` — the convergence test passes (each seed settles within the step budget).

- [ ] **Step 3: Commit**

```bash
git add tests/test_fluids.cpp
git commit -m "test: generated worlds' fluids reach full quiescence

Covers seeds 1, 42 and 555, which never settled under the old rules.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Add the pure `fluidSurfaceHeight` renderer helper (core)

A testable, SFML-free function that returns the flat fill fraction for a surface tile by averaging its contiguous surface run.

**Files:**
- Create: `src/World/FluidSurface.h`
- Create: `src/World/FluidSurface.cpp`
- Modify: `CMakeLists.txt` (add `src/World/FluidSurface.cpp` to `Litharia_core`)
- Test: `tests/test_fluids.cpp`

**Interfaces:**
- Consumes: `World::get`, `WORLD_WIDTH` (`Constants.h`), `isWater`/`isLava`/`isFluid`/`fluidLevel` (`Blocks.h`).
- Produces: `float fluidSurfaceHeight(const World& world, int x, int y)` — fill fraction in `[0, 1]`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_fluids.cpp`:

```cpp
#include "World/FluidSurface.h"

TEST_CASE("fluidSurfaceHeight reports one flat height across a within-one-level run")
{
    World world;
    for (int x = 8; x <= 10; ++x)
        world.set(x, 11, BlockType::Stone);

    // Surface run 5, 4, 5 with open air above each: average (5+4+5)/3 = 4.667.
    world.set(8, 10, BlockType::Water5);
    world.set(9, 10, BlockType::Water4);
    world.set(10, 10, BlockType::Water5);

    const float h8 = fluidSurfaceHeight(world, 8, 10);
    const float h9 = fluidSurfaceHeight(world, 9, 10);
    const float h10 = fluidSurfaceHeight(world, 10, 10);

    CHECK(h8 == doctest::Approx(h9));
    CHECK(h9 == doctest::Approx(h10));
    CHECK(h8 == doctest::Approx((14.0f / 3.0f) / 8.0f));
}

TEST_CASE("fluidSurfaceHeight stops a run at a solid gap and at a different fluid")
{
    World world;
    world.set(8, 10, BlockType::Water8);
    world.set(9, 10, BlockType::Stone);  // gap breaks the run
    world.set(10, 10, BlockType::Water2);
    world.set(11, 10, BlockType::Lava8); // different fluid breaks the run

    // The run through x=8 is just {8}: 8/8 = 1.0.
    CHECK(fluidSurfaceHeight(world, 8, 10) == doctest::Approx(1.0f));
    // The run through x=10 is just {10} (stone left, lava right): 2/8.
    CHECK(fluidSurfaceHeight(world, 10, 10) == doctest::Approx(2.0f / 8.0f));
}
```

- [ ] **Step 2: Run to verify failure (link error / not declared)**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests; & "build/Debug/Litharia_tests.exe"`
Expected: FAIL to compile/link — `fluidSurfaceHeight` and its header do not exist yet.

- [ ] **Step 3: Create `src/World/FluidSurface.h`**

```cpp
#pragma once

class World;

// The fill fraction (0..1) at which to draw the fluid surface tile at (x, y).
// A "surface" tile is a fluid tile with no same-family fluid directly above it.
// The value is the volume-average level of the maximal contiguous run of
// same-family surface tiles through (x, y), so an entire run draws at one flat
// height even though the settled tile levels differ by up to one. The scan is
// bounded; a run longer than the cap falls back to the tile's own level (still
// within one level of flat). Returns 0 for a non-fluid tile.
float fluidSurfaceHeight(const World& world, int x, int y);
```

- [ ] **Step 4: Create `src/World/FluidSurface.cpp`**

```cpp
#include "FluidSurface.h"

#include "../Blocks/Blocks.h"
#include "../Core/Constants.h"
#include "World.h"

namespace
{

// Cap on how far a single surface run is scanned, so one rebuild of a very wide
// body stays cheap. Beyond it, a tile just uses its own level.
constexpr int MAX_RUN_SCAN = 128;

} // namespace

float fluidSurfaceHeight(const World& world, int x, int y)
{
    const BlockType here = world.get(x, y);

    if (!isFluid(here))
        return 0.0f;

    const bool water = isWater(here);

    // A run member is a same-family fluid tile that is itself a surface tile
    // (no same-family fluid directly above it).
    auto isRunMember = [&](int cx) {
        const BlockType t = world.get(cx, y);
        const bool sameFamily = water ? isWater(t) : isLava(t);
        if (!sameFamily)
            return false;

        const BlockType above = world.get(cx, y - 1);
        const bool submerged = water ? isWater(above) : isLava(above);
        return !submerged;
    };

    if (!isRunMember(x))
        return fluidLevel(here) / 8.0f;

    int xL = x;
    int xR = x;
    int scanned = 1;

    while (xL - 1 >= 0 && scanned < MAX_RUN_SCAN && isRunMember(xL - 1))
    {
        --xL;
        ++scanned;
    }
    while (xR + 1 < WORLD_WIDTH && scanned < MAX_RUN_SCAN && isRunMember(xR + 1))
    {
        ++xR;
        ++scanned;
    }

    if (scanned >= MAX_RUN_SCAN)
        return fluidLevel(here) / 8.0f;

    int total = 0;
    for (int cx = xL; cx <= xR; ++cx)
        total += fluidLevel(world.get(cx, y));

    const int n = xR - xL + 1;
    return (static_cast<float>(total) / n) / 8.0f;
}
```

- [ ] **Step 5: Add the new source file to the core library in `CMakeLists.txt`**

Find the `Litharia_core` source list and the line `src/World/FluidSim.cpp`. Add directly after it:

```cmake
    src/World/FluidSurface.cpp
```

- [ ] **Step 6: Build and run the full suite**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug; & "build/Debug/Litharia_tests.exe"`
Expected: `[doctest] Status: SUCCESS!` — the two `fluidSurfaceHeight` tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/World/FluidSurface.h src/World/FluidSurface.cpp CMakeLists.txt tests/test_fluids.cpp
git commit -m "feat: fluidSurfaceHeight helper averages a surface run to one flat height

Pure, SFML-free core helper so the settled within-one-level surface can be
drawn dead flat. Bounded scan; unit-tested.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Draw fluid surfaces flat in the chunk renderer

Wire the helper into `ChunkRenderer::rebuild` so pools render dead-flat.

**Files:**
- Modify: `src/World/Chunks.cpp`

**Interfaces:**
- Consumes: `fluidSurfaceHeight` from Task 4.
- Produces: nothing (rendering only). No unit test — the renderer lives in the graphics target; correctness of the height math is already covered by Task 4, and this step is verified by building the game and a visual check.

- [ ] **Step 1: Include the helper in `src/World/Chunks.cpp`**

Add to the include block (with the other `../World`/local includes):

```cpp
#include "FluidSurface.h"
```

- [ ] **Step 2: Use the helper for surface-tile fill height**

Find in `ChunkRenderer::rebuild`:

```cpp
            if (isFluid(type) && !isFluid(world.get(x, y - 1)))
            {
                const float fillHeight = TILE_SIZE * (fluidLevel(type) / 8.0f);
                top = bottom - fillHeight;
            }
```

Replace with:

```cpp
            if (isFluid(type) && !isFluid(world.get(x, y - 1)))
            {
                const float fillHeight = TILE_SIZE * fluidSurfaceHeight(world, x, y);
                top = bottom - fillHeight;
            }
```

- [ ] **Step 3: Build the whole project (game + tests)**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug`
Expected: `Litharia.vcxproj -> ...Litharia.exe` and `Litharia_tests.vcxproj -> ...Litharia_tests.exe`, no errors.

- [ ] **Step 4: Run the full test suite once more**

Run: `& "build/Debug/Litharia_tests.exe"`
Expected: `[doctest] Status: SUCCESS!`, 300+ test cases passed (the two rewritten fluid tests replaced two originals; net case count is unchanged plus the new cases added).

- [ ] **Step 5: Visual smoke check (manual)**

Run: `& "build/Debug/Litharia.exe"`
Expected: the game launches; surface pools render with a flat top (no per-column 1/8-tile stairstep on settled water), and water visibly settles to rest rather than shimmering. Close the window to end.

- [ ] **Step 6: Commit**

```bash
git add src/World/Chunks.cpp
git commit -m "feat: render fluid surface runs at one flat height

ChunkRenderer draws each surface run at its volume-average level via
fluidSurfaceHeight, so a settled within-one-level pool looks dead flat.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Final verification and cleanup

**Files:** none (verification only).

- [ ] **Step 1: Full clean build + tests**

Run: `& "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug; & "build/Debug/Litharia_tests.exe"`
Expected: `[doctest] Status: SUCCESS!`.

- [ ] **Step 2: Confirm no stray files or leftover diagnostics**

Run: `git status --short`
Expected: clean working tree (all changes committed; no `bench/` directory, no trace code).

- [ ] **Step 3: Confirm the spec's guarantees hold**

Re-read `docs/superpowers/specs/2026-07-19-conservative-convergent-fluids-design.md` and confirm each is met: conservative (only obsidian consumes), convergent (Task 3 test), flat look (Tasks 4-5), all prior behaviors preserved (unchanged tests still green). No code change expected; this is a checklist.

---

## Self-Review

**Spec coverage:**
- Conservative model + equalize rule -> Task 2. ✓
- Convergence guarantee -> Task 2 (no-flicker/quiescent tests) + Task 3 (generated worlds). ✓
- Flat look in renderer -> Task 4 (helper) + Task 5 (wire-in). ✓
- Preserve fall/cascade/react/lava/gravity behavior -> Task 2 keeps those rules; unchanged tests are the guard. ✓
- Rewrite the two non-conservation tests -> Task 2 Step 1. ✓
- New tests: convergence, no-flicker, exact conservation, equalize unit rule, surface-height helper -> Tasks 2-4. ✓
- Dedup accessor not shipped -> confirmed; convergence tests use `changed.empty()`. ✓
- Dedup fix committed -> Task 1. ✓

**Placeholder scan:** No TBD/TODO; every code step contains full code; every command has expected output. ✓

**Type consistency:** `equalizeAt(World&, int, int, BlockType, std::vector<sf::Vector2i>&)` declared (Task 2 Step 3) and defined (Step 5) identically, and called in `step()` (Step 4) with matching args. `fluidSurfaceHeight(const World&, int, int) -> float` declared (Task 4 Step 3), defined (Step 4), consumed in tests (Step 1) and renderer (Task 5 Step 2) identically. `flattenAt`/`spreadAt` removed from both header and source together. ✓
