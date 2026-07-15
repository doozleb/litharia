# Machine Feedback (Fuel/Progress Bars + Hover Tooltip) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the factory's internal state legible without changing its simulation: every generator/drill/smelter shows a small vertical fill bar (fuel for generators, cycle progress for drill/smelter, in two visually distinct colors), and hovering any placed machine — build mode or not — shows a tooltip with its input/output, power state, and a plain-English reason when it is idle or unpowered (e.g. "No ore within 4 tiles below.", "No power: network demand exceeds supply.").

**Architecture:** A new pure-logic query, `Machines::inspect(x, y, world)`, re-derives a machine's display status (`MachineStatus`) read-only, without touching `tick()` or any field `tick()` relies on — so the already-passing 104 tests in `Litharia_tests` are unaffected. The bar's fill fraction is split out as a standalone pure function, `barStatus(const Machine&)`, that needs no `World` at all, so `MachineRenderer` can call it for every visible machine every frame with no extra dependency. Both live in `Litharia_core` and are covered by doctest, exactly like the existing power/transport/processing logic. Only `MachineRenderer` (world-space bars) and a new `Hud::drawMachineTooltip` (screen-space text panel, reusing `Hud`'s existing font) touch SFML Graphics, and `Game` wires the tooltip to whatever tile the mouse is over.

**Tech Stack:** C++20, SFML 3 (System only for core; Graphics/Window for renderer/HUD), CMake, doctest (vendored single header).

## Global Constraints

- **C++ standard:** C++20, already configured — do not change.
- **Simulation/rendering split:** Files compiled into `Litharia_core` MUST NOT include `<SFML/Graphics.hpp>` or `<SFML/Window.hpp>`. `MachineStatus.h/.cpp` and the new `Machines::inspect`/`idleReason` code are pure logic and belong in `Litharia_core`. Only `MachineRenderer.cpp`, `Hud.cpp`, and `Game.cpp` may touch SFML Graphics/Window.
- **Read-only queries:** `barStatus()` and `Machines::inspect()` must never mutate a `Machine` or `Machines` — no side effects, no writes to `progress`/`fuel`/`input`/`output`/`powered`. This is what makes it safe to call them every frame purely for display.
- **Determinism:** Like the rest of the simulation, these queries are a pure function of current state — no wall-clock reads, no RNG.
- **World edge safety:** `World::get` returns `BlockType::Air` out of bounds — never add a redundant bounds check around it.
- **Reason strings are the single source of truth for their own tests:** build any reason that embeds a tunable constant (e.g. `DRILL_REACH`) with that constant, never a hardcoded number, so the test and the behavior can't drift independently.
- **Fixed timestep unaffected:** None of this work ticks the factory; `Game::tickMachines` / `Machines::tick` are untouched. The tooltip and bars are read once per rendered frame, inside `Game::render()`, never inside `fixedUpdate`.
- **Every new `.cpp` is added to the correct CMake target** (`Litharia_core` for logic, `Litharia` for rendering, `Litharia_tests` for tests) in `CMakeLists.txt`. A file that compiles but is not listed will link-fail.
- **Commit after every task** with a `feat:` prefixed message.
- **Build/test commands:** the primary command is `cmake --build build --target <target> --config Debug`. If `cmake` is not on your shell's PATH, fall back to MSBuild directly (verified working):
  `& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" build\<Target>.vcxproj /p:Configuration=Debug /nologo /v:minimal`
  where `<Target>` is `Litharia_tests` or `Litharia`. Run the test binary directly at `build\Debug\Litharia_tests.exe` (or `./build/Debug/Litharia_tests.exe` from bash).

## Design Summary (from brainstorming, 2026-07-15)

Decisions this slice implements, for reviewer context:

- **Scope:** feedback/legibility only. This does **not** change `DRILL_REACH`, ore depth, or any tuning — a drill with no ore in reach will still never mine; it will just now say so.
- **Audience:** player-facing, permanent UI (not a stripped-later debug overlay).
- **Presentation:** always-on vertical fill bars drawn inside each machine's tile, plus a hover tooltip (shown whenever the cursor is over a placed machine, in or out of build mode).
- **Bar scope:** both fuel (generator) and progress (drill, smelter), in two distinct colors so "waiting on fuel" is never visually confused with "mid-cycle": amber/orange for fuel, cyan/blue for progress.
- **Tooltip depth:** raw stats (name, powered/fuel state, input, output, bar percentage) *plus* a plain-English reason when idle/unpowered (not just stats).

---

## File Structure

**New — core logic (into `Litharia_core`):**
- `src/Machines/MachineStatus.h` / `src/Machines/MachineStatus.cpp` — the `MachineBar` enum, `MachineStatus` struct, and the pure `barStatus()` query.

**New — tests (into `Litharia_tests`):**
- `tests/test_machine_status.cpp` — `barStatus()` cases (no `Machines`/`World` needed).
- `tests/test_machine_inspect.cpp` — `Machines::inspect()` cases (needs `Machines` + `World`).

**Modified:**
- `src/Machines/Machines.h` / `src/Machines/Machines.cpp` — add `inspect()`, private `idleReason()`, and a persisted `networkSupply` member.
- `src/Machines/MachineRenderer.cpp` — draw the fuel/progress bar inside each machine's tile.
- `src/Hud/Hud.h` / `src/Hud/Hud.cpp` — add `drawMachineTooltip(...)`.
- `src/Game/Game.h` / `src/Game/Game.cpp` — look up the hovered machine each frame and draw its tooltip.
- `CMakeLists.txt` — register the new `.cpp`/test files.

---

## Canonical Interfaces (defined once, referenced by all tasks)

```cpp
// src/Machines/MachineStatus.h  (Task 1)
enum class MachineBar : std::uint8_t { None, Fuel, Progress };

struct MachineStatus
{
    MachineBar bar = MachineBar::None;
    float fraction = 0.0f;  // 0..1, meaningful only when bar != MachineBar::None
    std::string reason;     // empty when running normally; explains why otherwise
};

// Pure, no World needed: which bar (if any) a machine shows and how full it is.
MachineStatus barStatus(const Machine& m);

// src/Machines/Machines.h  (Task 2, added to the existing class)
class Machines
{
public:
    // ...existing members unchanged...

    // Read-only: bar + fraction from barStatus(), plus a reason string that
    // explains why the machine is idle/unpowered (empty when it is running fine
    // or the tile is empty/not a processing machine).
    MachineStatus inspect(int x, int y, const World& world) const;

private:
    // ...existing members unchanged...
    std::string idleReason(const Machine& m, const World& world) const;
    std::vector<float> networkSupply; // per-network supply, alongside networkDemand
};

// src/Hud/Hud.h  (Task 4, added to the existing class)
class Hud
{
public:
    // ...existing members unchanged...
    void drawMachineTooltip(sf::RenderWindow& window,
                            const Machine& machine,
                            const MachineStatus& status,
                            sf::Vector2f screenPos);
};
```

---

### Task 1: `MachineStatus` + pure `barStatus()`

**Files:**
- Create: `src/Machines/MachineStatus.h`, `src/Machines/MachineStatus.cpp`
- Test: `tests/test_machine_status.cpp` (create)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `MachineBar`, `MachineStatus`, `barStatus(const Machine&)` (see Canonical Interfaces). No dependency on `Machines` or `World`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_machine_status.cpp`:

```cpp
#include "doctest.h"

#include "Machines/Machine.h"
#include "Machines/MachineStatus.h"
#include "Machines/MachineType.h"
#include "Machines/Recipes.h"

TEST_CASE("an unfuelled generator shows an empty fuel bar")
{
    Machine m;
    m.type = MachineType::BurnerGenerator;
    m.fuel = 0.0f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Fuel);
    CHECK(status.fraction == doctest::Approx(0.0f));
}

TEST_CASE("a generator mid-burn shows a proportional fuel bar")
{
    Machine m;
    m.type = MachineType::BurnerGenerator;
    m.fuel = COAL_BURN_SECONDS * 0.5f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Fuel);
    CHECK(status.fraction == doctest::Approx(0.5f));
}

TEST_CASE("fuel fraction never exceeds 1 even if fuel overshoots capacity")
{
    Machine m;
    m.type = MachineType::BurnerGenerator;
    m.fuel = COAL_BURN_SECONDS * 2.0f; // should not happen in practice

    CHECK(barStatus(m).fraction == doctest::Approx(1.0f));
}

TEST_CASE("a drill mid-mining shows proportional progress")
{
    Machine m;
    m.type = MachineType::Drill;
    m.progress = machineInfo(MachineType::Drill).actionTime * 0.25f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Progress);
    CHECK(status.fraction == doctest::Approx(0.25f));
}

TEST_CASE("a smelter with no input shows no bar at all")
{
    Machine m;
    m.type = MachineType::Smelter; // input left empty

    CHECK(barStatus(m).bar == MachineBar::None);
}

TEST_CASE("a smelter mid-smelt shows progress against its recipe's time")
{
    Machine m;
    m.type = MachineType::Smelter;
    m.input = {ItemType::CopperOre, 1};

    const SmeltRecipe* recipe = smeltRecipeFor(ItemType::CopperOre);
    REQUIRE(recipe != nullptr);
    m.progress = recipe->seconds * 0.5f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Progress);
    CHECK(status.fraction == doctest::Approx(0.5f));
}

TEST_CASE("a belt never shows a bar")
{
    Machine m;
    m.type = MachineType::Belt;

    CHECK(barStatus(m).bar == MachineBar::None);
}
```

- [ ] **Step 2: Register the new files in CMake**

In `CMakeLists.txt`, add to the `Litharia_core` source list (after `src/Machines/Machines.cpp`):

```cmake
    src/Machines/MachineStatus.cpp
```

And add to the `Litharia_tests` source list (after `tests/test_factory.cpp`):

```cmake
    tests/test_machine_status.cpp
```

- [ ] **Step 3: Run tests to verify they fail to compile**

Run: `cmake --build build --target Litharia_tests --config Debug`
Expected: FAIL to compile — `Machines/MachineStatus.h` not found.

- [ ] **Step 4: Write minimal implementation**

Create `src/Machines/MachineStatus.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>

#include "Machine.h"

// What a machine shows inside its tile: nothing, a fuel level, or a work-cycle
// progress. Two kinds so the renderer can color them differently and a player
// never mistakes "waiting on fuel" for "mid-cycle".
enum class MachineBar : std::uint8_t
{
    None,
    Fuel,
    Progress
};

struct MachineStatus
{
    MachineBar bar = MachineBar::None;
    float fraction = 0.0f; // 0..1, meaningful only when bar != MachineBar::None
    std::string reason;    // empty when running normally; explains why otherwise
};

// Pure: no World, no Machines container. Just what this one machine's own
// fields say about its bar right now.
MachineStatus barStatus(const Machine& m);
```

Create `src/Machines/MachineStatus.cpp`:

```cpp
#include "MachineStatus.h"

#include <algorithm>

#include "MachineType.h"
#include "Recipes.h"

MachineStatus barStatus(const Machine& m)
{
    MachineStatus status;

    if (m.type == MachineType::BurnerGenerator)
    {
        status.bar = MachineBar::Fuel;
        status.fraction = std::clamp(m.fuel / COAL_BURN_SECONDS, 0.0f, 1.0f);
    }
    else if (m.type == MachineType::Drill)
    {
        status.bar = MachineBar::Progress;
        status.fraction =
            std::clamp(m.progress / machineInfo(MachineType::Drill).actionTime, 0.0f, 1.0f);
    }
    else if (m.type == MachineType::Smelter)
    {
        const SmeltRecipe* recipe = m.input.empty() ? nullptr : smeltRecipeFor(m.input.type);

        if (recipe != nullptr)
        {
            status.bar = MachineBar::Progress;
            status.fraction = std::clamp(m.progress / recipe->seconds, 0.0f, 1.0f);
        }
    }

    return status;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: all test cases pass, including the 7 new ones (111 total).

- [ ] **Step 6: Commit**

```bash
git add src/Machines/MachineStatus.h src/Machines/MachineStatus.cpp tests/test_machine_status.cpp CMakeLists.txt
git commit -m "feat: add machine status bar type and pure barStatus() query"
```

---

### Task 2: `Machines::inspect()` with plain-English idle/unpowered reasons

**Files:**
- Modify: `src/Machines/Machines.h`, `src/Machines/Machines.cpp`
- Test: `tests/test_machine_inspect.cpp` (create)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `MachineStatus`, `barStatus()` (Task 1); `Machine`, `MachineInfo`, `machineInfo()`, `DRILL_REACH`, `COAL_BURN_SECONDS` (existing); `SmeltRecipe`, `smeltRecipeFor()` (existing); `World::get` (existing).
- Produces: `Machines::inspect(int x, int y, const World& world) const -> MachineStatus`, used by Task 3 (bars, via `barStatus` directly — not `inspect`) and Task 5 (tooltip, via `inspect`).

- [ ] **Step 1: Write the failing tests**

Create `tests/test_machine_inspect.cpp`:

```cpp
#include "doctest.h"

#include <string>

#include "Machines/Machines.h"
#include "World/World.h"

namespace
{
constexpr float STEP = 1.0f / 60.0f;
}

TEST_CASE("inspecting an empty tile returns a default status")
{
    Machines m;
    World world;

    const MachineStatus status = m.inspect(3, 3, world);

    CHECK(status.bar == MachineBar::None);
    CHECK(status.reason.empty());
}

TEST_CASE("a drill with no generator anywhere reports no fuel in its network")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);

    m.place(MachineType::Drill, 0, 0, Direction::Right); // no generator

    std::vector<sf::Vector2i> mined;
    m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(0, 0, world);

    CHECK(status.reason == "No power: no fuel in this network.");
}

TEST_CASE("a network whose demand exceeds supply reports that reason")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);

    // Generator supply is 10; three drills demand 15 together.
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Right);
    m.place(MachineType::Drill, 2, 0, Direction::Right);
    m.place(MachineType::Drill, 3, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 10; ++i)
        m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(1, 0, world);

    CHECK(status.reason == "No power: network demand exceeds supply.");
}

TEST_CASE("a powered drill with no ore in reach reports that reason")
{
    Machines m;
    World world;
    world.fill(BlockType::Air); // nothing to mine anywhere

    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 10; ++i)
        m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(1, 0, world);

    CHECK(status.reason == "No ore within " + std::to_string(DRILL_REACH) + " tiles below.");
}

TEST_CASE("a powered drill with ore in reach reports no reason")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);
    world.set(1, 1, BlockType::CopperOre); // directly beneath the drill

    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 10; ++i)
        m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(1, 0, world);

    CHECK(status.reason.empty());
}

TEST_CASE("a drill whose output cannot be pushed anywhere reports that its output is full")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);
    world.set(1, 1, BlockType::CopperOre);

    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Up); // faces open sky: nothing accepts its output
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 120; ++i) // long enough to mine once and fill the output
        m.tick(world, STEP, mined);

    REQUIRE_FALSE(m.at(1, 0)->output.empty());

    const MachineStatus status = m.inspect(1, 0, world);

    CHECK(status.reason == "Output is full.");
}

TEST_CASE("a smelter with no ore yet reports that it is waiting")
{
    Machines m;
    World world;

    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Smelter, 1, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 10; ++i)
        m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(1, 0, world);

    CHECK(status.reason == "Waiting for ore.");
}

TEST_CASE("an out-of-fuel generator with nothing queued reports it needs coal")
{
    Machines m;
    World world;

    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right); // never fuelled

    const MachineStatus status = m.inspect(0, 0, world);

    CHECK(status.reason == "Out of fuel: needs coal.");
}
```

- [ ] **Step 2: Register the new test file in CMake**

In `CMakeLists.txt`, add to the `Litharia_tests` source list (after `tests/test_machine_status.cpp`):

```cmake
    tests/test_machine_inspect.cpp
```

- [ ] **Step 3: Run tests to verify they fail to compile**

Run: `cmake --build build --target Litharia_tests --config Debug`
Expected: FAIL to compile — `Machines::inspect` is not a member of `Machines`.

- [ ] **Step 4: Write minimal implementation**

In `src/Machines/Machines.h`, add the include near the top (after `#include "Machine.h"`):

```cpp
#include "MachineStatus.h"
```

Add the public method, directly below the existing `tryInsert` declaration:

```cpp
    // Read-only: bar + fraction from barStatus(), plus a reason string that
    // explains why the machine is idle/unpowered (empty when it is running fine,
    // or the tile is empty/not a processing machine).
    MachineStatus inspect(int x, int y, const World& world) const;
```

Add the private method and member, directly below the existing `void tickSmelters(float dt);` declaration and the existing `std::vector<float> networkDemand;` member respectively:

```cpp
    std::string idleReason(const Machine& m, const World& world) const;
```

```cpp
    std::vector<float> networkSupply; // per-network supply, alongside networkDemand
```

In `src/Machines/Machines.cpp`, change `updatePower()` to persist supply as well as demand — find:

```cpp
    networkDemand = demand;
}
```

and change it to:

```cpp
    networkDemand = demand;
    networkSupply = supply;
}
```

Add the two new methods directly after the existing `Machines::tryInsert` definition (before `Machines::assignNetworks`):

```cpp
MachineStatus Machines::inspect(int x, int y, const World& world) const
{
    const Machine* m = at(x, y);
    if (m == nullptr)
        return {};

    MachineStatus status = barStatus(*m);
    status.reason = idleReason(*m, world);
    return status;
}

std::string Machines::idleReason(const Machine& m, const World& world) const
{
    const MachineInfo& info = machineInfo(m.type);

    if (info.consumer && !m.powered)
    {
        const bool hasSupply = m.network >= 0
            && m.network < static_cast<int>(networkSupply.size())
            && networkSupply[m.network] > 0.0f;

        return hasSupply ? "No power: network demand exceeds supply."
                          : "No power: no fuel in this network.";
    }

    if (m.type == MachineType::Drill)
    {
        if (!m.output.empty())
            return "Output is full.";

        for (int dy = 1; dy <= DRILL_REACH; ++dy)
            if (isOre(world.get(m.x, m.y + dy)))
                return "";

        return "No ore within " + std::to_string(DRILL_REACH) + " tiles below.";
    }

    if (m.type == MachineType::Smelter)
    {
        if (m.input.empty())
            return "Waiting for ore.";

        // tryInsert only ever accepts items with a recipe, so a non-empty input
        // is always smeltable: no null check needed here.
        const SmeltRecipe* recipe = smeltRecipeFor(m.input.type);

        const bool outputReady = m.output.empty()
            || (m.output.type == recipe->out && m.output.count < itemInfo(recipe->out).maxStack);

        return outputReady ? "" : "Output is full.";
    }

    if (m.type == MachineType::BurnerGenerator)
        return (m.fuel <= 0.0f && m.input.empty()) ? "Out of fuel: needs coal." : "";

    return "";
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: all test cases pass (119 total: 111 from Task 1 + 8 new).

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Machines.h src/Machines/Machines.cpp tests/test_machine_inspect.cpp CMakeLists.txt
git commit -m "feat: add Machines::inspect() with plain-English idle/unpowered reasons"
```

---

### Task 3: Render fuel/progress bars on machines

**Files:**
- Modify: `src/Machines/MachineRenderer.cpp`

**Interfaces:**
- Consumes: `barStatus()`, `MachineBar`, `MachineStatus` (Task 1).
- Produces: no new interface — purely visual. Nothing later depends on this task's internals.

There is no automated test for this task: `MachineRenderer.cpp` links SFML Graphics and is part of the `Litharia` executable target only, not `Litharia_core`/`Litharia_tests` (see the Simulation/rendering split constraint), so it cannot be exercised by the doctest binary. Verification is a manual run, matching how the rest of `MachineRenderer.cpp` was originally built with no test file.

- [ ] **Step 1: Add the include**

In `src/Machines/MachineRenderer.cpp`, add to the includes (after `#include "Machines.h"`):

```cpp
#include "MachineStatus.h"
```

- [ ] **Step 2: Draw the bar inside the existing per-machine loop**

Find this block in `MachineRenderer::draw`:

```cpp
        body.setPosition({px, py});
        body.setFillColor(toColor(info.color, alpha));
        target.draw(body);
```

Add directly after it:

```cpp

        const MachineStatus status = barStatus(m);

        if (status.bar != MachineBar::None)
        {
            constexpr float BAR_WIDTH = 4.0f;
            constexpr float BAR_MARGIN = 2.0f;

            const float barAreaHeight = static_cast<float>(TILE_SIZE) - BAR_MARGIN * 2.0f;
            const float barFillHeight = barAreaHeight * status.fraction;

            sf::RectangleShape barBackground({BAR_WIDTH, barAreaHeight});
            barBackground.setPosition({px + BAR_MARGIN, py + BAR_MARGIN});
            barBackground.setFillColor(sf::Color(20, 20, 24, 200));
            target.draw(barBackground);

            // Fills from the bottom up, like a fuel gauge. Fuel is amber, progress
            // is cyan, so the two are never visually confused.
            sf::RectangleShape barFill({BAR_WIDTH, barFillHeight});
            barFill.setPosition({px + BAR_MARGIN, py + BAR_MARGIN + (barAreaHeight - barFillHeight)});
            barFill.setFillColor(status.bar == MachineBar::Fuel ? sf::Color(230, 140, 40)
                                                                 : sf::Color(90, 200, 230));
            target.draw(barFill);
        }
```

- [ ] **Step 3: Build the game executable**

Run: `cmake --build build --target Litharia --config Debug`
Expected: builds with no errors.

- [ ] **Step 4: Manually verify**

Run: `./build/Debug/Litharia.exe`

- Press `B` to enter build mode, `F1` then left-click to place a Burner Generator, `F2` then left-click to place a Drill next to it, `R` to rotate facing if needed.
- Point at the generator and press `F` to load a coal. Confirm an amber bar appears inside the generator's tile and grows/holds (it will only drain once the drill is powered and demanding).
- If the drill is near ore and mining, confirm a cyan bar appears inside the drill's tile and fills up over ~1 second, then resets when it completes a cycle.
- Confirm a Belt or Chute shows no bar at all.

- [ ] **Step 5: Commit**

```bash
git add src/Machines/MachineRenderer.cpp
git commit -m "feat: render fuel and progress bars on machines"
```

---

### Task 4: `Hud::drawMachineTooltip`

**Files:**
- Modify: `src/Hud/Hud.h`, `src/Hud/Hud.cpp`

**Interfaces:**
- Consumes: `Machine`, `MachineStatus`, `MachineBar` (Task 1); `MachineInfo`, `machineInfo()` (existing); `itemInfo()` (existing).
- Produces: `Hud::drawMachineTooltip(sf::RenderWindow&, const Machine&, const MachineStatus&, sf::Vector2f)`, used by Task 5.

No automated test: `Hud.cpp` links SFML Graphics, same reasoning as Task 3. Verified visually once wired up in Task 5 (this task alone only needs to compile).

- [ ] **Step 1: Add the declaration**

In `src/Hud/Hud.h`, add the includes (after `#include <optional>`):

```cpp
#include "../Machines/Machine.h"
#include "../Machines/MachineStatus.h"
```

Add the public method, directly below the existing `draw(...)` declaration:

```cpp
    // A small info panel anchored near the cursor, describing one machine's
    // current state: name, input/output, power/fuel state, bar percentage, and
    // (when idle/unpowered) a plain-English reason. Degrades like draw() does:
    // with no font the panel background still shows, just without text.
    void drawMachineTooltip(sf::RenderWindow& window,
                            const Machine& machine,
                            const MachineStatus& status,
                            sf::Vector2f screenPos);
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build build --target Litharia --config Debug`
Expected: FAIL to compile — `Hud::drawMachineTooltip` is declared but not defined (linker error), since `Hud.cpp` has no implementation yet.

- [ ] **Step 3: Write the implementation**

In `src/Hud/Hud.cpp`, add to the includes (after `#include "../Items/Inventory.h"`):

```cpp
#include <algorithm>
#include <vector>

#include "../Machines/MachineType.h"
```

Add, inside the existing anonymous namespace at the top of the file (after `itemColor`):

```cpp
struct TooltipLine
{
    std::string text;
    sf::Color color;
};
```

Add the method at the end of the file:

```cpp
void Hud::drawMachineTooltip(sf::RenderWindow& window,
                              const Machine& machine,
                              const MachineStatus& status,
                              sf::Vector2f screenPos)
{
    const sf::View previous = window.getView();
    window.setView(window.getDefaultView());

    const MachineInfo& info = machineInfo(machine.type);

    std::vector<TooltipLine> lines;
    lines.push_back({std::string(info.name), sf::Color::White});

    if (info.generator)
        lines.push_back({"Fuel: " + std::to_string(static_cast<int>(machine.fuel)) + "s remaining",
                          sf::Color::White});
    else if (info.consumer)
        lines.push_back({machine.powered ? "Powered: yes" : "Powered: no", sf::Color::White});

    if (!machine.input.empty())
        lines.push_back({"Input: " + std::to_string(machine.input.count) + "x " +
                              std::string(itemInfo(machine.input.type).name),
                          sf::Color::White});

    if (!machine.output.empty())
        lines.push_back({"Output: " + std::to_string(machine.output.count) + "x " +
                              std::string(itemInfo(machine.output.type).name),
                          sf::Color::White});

    if (status.bar != MachineBar::None)
        lines.push_back({(status.bar == MachineBar::Fuel ? std::string("Fuel: ")
                                                           : std::string("Progress: ")) +
                              std::to_string(static_cast<int>(status.fraction * 100.0f)) + "%",
                          sf::Color::White});

    if (!status.reason.empty())
        lines.push_back({status.reason, sf::Color(255, 200, 120)});

    constexpr float PADDING = 8.0f;
    constexpr float LINE_HEIGHT = 18.0f;
    constexpr float CHAR_WIDTH = 7.0f; // rough estimate; only sizes the background panel

    std::size_t longest = 0;
    for (const TooltipLine& line : lines)
        longest = std::max(longest, line.text.size());

    const float width = static_cast<float>(longest) * CHAR_WIDTH + PADDING * 2.0f;
    const float height = static_cast<float>(lines.size()) * LINE_HEIGHT + PADDING * 2.0f;

    const sf::Vector2f pos = screenPos + sf::Vector2f(16.0f, 16.0f);

    sf::RectangleShape panel({width, height});
    panel.setPosition(pos);
    panel.setFillColor(sf::Color(20, 20, 28, 220));
    panel.setOutlineThickness(-1.0f);
    panel.setOutlineColor(sf::Color(90, 90, 105));
    window.draw(panel);

    if (font)
    {
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            sf::Text text(*font, lines[i].text, 14);
            text.setFillColor(lines[i].color);
            text.setPosition({pos.x + PADDING, pos.y + PADDING + static_cast<float>(i) * LINE_HEIGHT});
            window.draw(text);
        }
    }

    window.setView(previous);
}
```

- [ ] **Step 4: Build to verify it compiles and links**

Run: `cmake --build build --target Litharia --config Debug`
Expected: builds with no errors (the method is not called by anything yet, so there is nothing to run and see).

- [ ] **Step 5: Commit**

```bash
git add src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "feat: add Hud::drawMachineTooltip for machine info panels"
```

---

### Task 5: Wire the hover tooltip into `Game`

**Files:**
- Modify: `src/Game/Game.h`, `src/Game/Game.cpp`

**Interfaces:**
- Consumes: `Machines::inspect()` (Task 2), `Hud::drawMachineTooltip()` (Task 4).
- Produces: nothing further — this is the end-to-end integration point.

No automated test: this is the final wiring of already-tested logic into the SFML render loop. Verified manually end-to-end.

- [ ] **Step 1: Declare the method**

In `src/Game/Game.h`, add the private method declaration directly below `sf::Vector2i cursorTile() const;`:

```cpp
    void drawMachineTooltip();
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build build --target Litharia --config Debug`
Expected: builds fine (a declared-but-unused private method is not an error) — this step just confirms the header change alone doesn't break anything before the next step adds the call site.

- [ ] **Step 3: Implement and call it**

In `src/Game/Game.cpp`, add the method directly after `Game::tickMachines`:

```cpp
void Game::drawMachineTooltip()
{
    const sf::Vector2i tile = cursorTile();
    const Machine* machine = machines.at(tile.x, tile.y);

    if (machine == nullptr)
        return;

    const MachineStatus status = machines.inspect(tile.x, tile.y, world);
    const sf::Vector2f screenPos(sf::Mouse::getPosition(window));

    hud.drawMachineTooltip(window, *machine, status, screenPos);
}
```

In `Game::render`, find:

```cpp
    hud.draw(window, player.inventory(), player.selectedSlot());

    window.display();
```

and change it to:

```cpp
    hud.draw(window, player.inventory(), player.selectedSlot());
    drawMachineTooltip();

    window.display();
```

- [ ] **Step 4: Build**

Run: `cmake --build build --target Litharia --config Debug`
Expected: builds with no errors.

- [ ] **Step 5: Manually verify end-to-end**

Run: `./build/Debug/Litharia.exe`

- Walk near any placed machine (build mode does not need to be on) and hover the mouse over it: a tooltip panel should appear near the cursor showing its name, input/output, and power/fuel line.
- Hover a Drill placed with no ore in reach: confirm the tooltip's last line reads `No ore within 4 tiles below.` in the amber reason color.
- Hover a Drill/Smelter on a network with no fuelled generator: confirm it reads `No power: no fuel in this network.`
- Fuel the generator and confirm the reason line disappears (or changes) once the machine starts running, and the bar from Task 3 fills accordingly.
- Move the mouse off any machine: confirm the tooltip disappears entirely.

- [ ] **Step 6: Run the full test suite one more time**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: all 119 test cases still pass — this task touched no logic, only wiring, so nothing here should have changed.

- [ ] **Step 7: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: wire machine hover tooltip into Game"
```
