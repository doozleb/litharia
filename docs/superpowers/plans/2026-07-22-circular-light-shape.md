# Circular Light Shape Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make sky, Torch, and Lava light read as a true circle instead of a diamond, by replacing `Lighting::floodFill`'s step-decay BFS with a per-seed local search whose brightness comes from real (Euclidean) distance.

**Architecture:** For each seed, run a small, local, 8-directional (orthogonal + diagonal) reachability search bounded to a disk of the seed's own radius, blocking diagonal steps that would cut a solid wall's corner. Every tile confirmed reachable this way gets its displayed level from true distance to the seed (`floor(level - sqrt(dx² + dy²))`), then merges into the existing generation-stamped `floodStamp`/`floodBest` scratch exactly as today (brightest-wins across seeds). This is a self-contained rewrite of `floodFill`'s internals only — its signature and every public `Lighting` method that calls it are untouched.

**Tech Stack:** C++20, CMake (Visual Studio generator on this machine), doctest for tests.

## Global Constraints

- `floodFill`'s signature, and every public `Lighting` method's signature/return value (`recomputeAll`, `skyLight`, `torchLight`, `lavaLight`, `heldTorchLight`, `ambientOutline`), stay exactly as they are — internal-only change.
- No change to brightness constants, tint colors, or decay rate (`MAX_LIGHT_LEVEL`, `TORCH_LIGHT_LEVEL`, etc).
- `ambientOutline` and `LightRenderer`'s wall-penetration glow stay diamond-shaped — out of scope, must not be touched.
- The generation-stamp scratch mechanism (`floodStamp`, `floodBest`, `floodGeneration`, and its wraparound guard, all in `src/World/Lighting.h`/`Lighting.cpp`) is reused as-is — only what feeds into it changes.
- A diagonal step from `(cx, cy)` to `(cx + dx, cy + dy)` is only taken if both flanking orthogonal tiles — `(cx + dx, cy)` and `(cx, cy + dy)` — are also non-solid (corner-cutting blocked).
- Reachability within a seed's search is bounded to `dx² + dy² ≤ level²` (its own radius disk); displayed level is `floor(level - sqrt(dx² + dy²))`, discarded if `≤ 0`.
- The per-seed local search buffer must be sized to the seed's own disk (`(2*level + 1)²`), not world-sized — this must not reintroduce the full-world-sized per-frame allocation the 2026-07-22 lighting-perf work just eliminated from `heldTorchLight`'s call path.

---

### Task 1: Circular `floodFill`

**Files:**
- Modify: `src/World/Lighting.cpp:1-68` (includes, `floodFill`)
- Test: `tests/test_lighting.cpp` (append two new test cases)

**Interfaces:**
- Consumes: nothing from other tasks — this is the only task in this plan.
- Produces: `floodFill` keeps its exact existing signature
  (`std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world, const std::vector<LightSeed>& seeds) const`)
  and continues to merge multiple seeds by brightest-wins into
  `floodStamp`/`floodBest`/`floodGeneration` exactly as today — only the
  per-seed shape of what gets merged changes. `recomputeAll` (`Lighting.cpp:70-115`)
  and `heldTorchLight` (`Lighting.cpp:141-144`) call it exactly as they do
  today, unmodified.

- [ ] **Step 1: Add two new test cases**

Append to the end of `tests/test_lighting.cpp` (currently 563 lines — add after the last line):

```cpp

TEST_CASE("floodFill produces circular, not diamond, light: diagonal distance uses true Euclidean falloff")
{
    World world;
    fillSolid(world);
    for (int x = 45; x <= 55; ++x)
        for (int y = 45; y <= 55; ++y)
            world.set(x, y, BlockType::Air);

    Machines machines;
    machines.place(MachineType::Torch, 50, 50, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    // Straight line (dx=3, dy=0) still decays by exactly 1 per step, same
    // as the old diamond shape - this is the "shapes agree" case.
    CHECK(lighting.torchLight(53, 50) == Lighting::TORCH_LIGHT_LEVEL - 3);

    // Diagonal (dx=3, dy=3): true Euclidean distance is sqrt(18) ~= 4.24,
    // not the Manhattan distance of 6 the old diamond shape used -
    // floor(15 - 4.2426...) = 10, not 15 - 6 = 9.
    CHECK(lighting.torchLight(53, 53) == 10);
}

TEST_CASE("floodFill blocks a diagonal step around a solid corner, even though the target tile itself is open")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air); // the Torch's own tile
    world.set(11, 11, BlockType::Air); // diagonally adjacent, but...
    // (11, 10) and (10, 11) - the two tiles flanking that diagonal step -
    // stay Stone, so there is no straight-or-right-angle path from
    // (10, 10) to (11, 11) at all, direct diagonal or otherwise.

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(11, 11) == 0);
}
```

- [ ] **Step 2: Build and run these two tests to confirm the expected pre-implementation results**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe -tc="floodFill produces circular*,floodFill blocks a diagonal step*"
```
Expected: the **first** test (diagonal distance) FAILS — today's diamond-shaped `floodFill` computes `torchLight(53, 53)` from Manhattan step count (`15 - 6 = 9`), not `10`, so `CHECK(lighting.torchLight(53, 53) == 10)` fails. This is the genuine red half of this change.

The **second** test (corner-cutting) already PASSES today — the current implementation only ever expands to the 4 orthogonal neighbors, so it never reaches `(11, 11)` at all regardless of any corner rule, and `torchLight(11, 11)` already reads `0`. This is expected and correct at this checkpoint: the test's job is to guard the *upcoming* implementation against a bug where diagonal movement is added without the corner-cutting check (which would incorrectly light `(11, 11)`), not to be red against today's code. Do not treat this as a problem — proceed to Step 3.

- [ ] **Step 3: Replace `floodFill` with the circular, per-seed implementation**

Add `<cmath>` to the includes at the top of `src/World/Lighting.cpp` (currently just `<algorithm>` alongside the project headers):

```cpp
#include "Lighting.h"

#include <algorithm>
#include <cmath>

#include "../Blocks/Blocks.h"
#include "../Core/Constants.h"
#include "../Machines/Machine.h"
#include "../Machines/MachineType.h"
#include "../Machines/Machines.h"
#include "World.h"
```

Replace the current `floodFill` body (`src/World/Lighting.cpp:21-68`, from `std::vector<std::pair<sf::Vector2i, int>> Lighting::floodFill(...)` through its closing `}`) with:

```cpp
std::vector<std::pair<sf::Vector2i, int>> Lighting::floodFill(const World& world,
                                                                const std::vector<LightSeed>& seeds) const
{
    // See floodStamp/floodBest's declaration in Lighting.h for why this is
    // a counter bump instead of a std::fill over the whole grid.
    ++floodGeneration;
    if (floodGeneration == 0)
    {
        std::fill(floodStamp.begin(), floodStamp.end(), 0);
        floodGeneration = 1;
    }

    std::vector<std::pair<sf::Vector2i, int>> result;

    // Merges one candidate (x, y, level) into this call's generation-stamped
    // best-so-far, across every seed processed below - unchanged from
    // floodFill's pre-circular-shape merge rule.
    auto tryImprove = [&](int x, int y, int level)
    {
        if (level <= 0 || !world.inBounds(x, y) || world.isSolid(x, y))
            return;

        const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
        const int currentBest = (floodStamp[i] == floodGeneration) ? floodBest[i] : -1;
        if (level <= currentBest)
            return;

        floodStamp[i] = floodGeneration;
        floodBest[i] = static_cast<std::int8_t>(level);
        result.push_back({{x, y}, level});
    };

    static constexpr int NEIGHBOR_OFFSETS[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

    for (const LightSeed& seed : seeds)
    {
        if (seed.level <= 0 || !world.inBounds(seed.x, seed.y) || world.isSolid(seed.x, seed.y))
            continue;

        // A local, per-seed reachability search bounded to this seed's own
        // radius disk - sized to the seed, never to the world, so this
        // stays cheap even called every frame (heldTorchLight).
        const int radius = seed.level;
        const int side = 2 * radius + 1;
        std::vector<std::uint8_t> reached(static_cast<std::size_t>(side) * side, 0);
        std::vector<sf::Vector2i> localQueue{{0, 0}}; // coords relative to the seed
        reached[static_cast<std::size_t>(radius) * side + radius] = 1;

        for (std::size_t head = 0; head < localQueue.size(); ++head)
        {
            const sf::Vector2i local = localQueue[head];
            const int cx = seed.x + local.x;
            const int cy = seed.y + local.y;

            for (const auto& offset : NEIGHBOR_OFFSETS)
            {
                const int dx = offset[0];
                const int dy = offset[1];
                const int nlx = local.x + dx;
                const int nly = local.y + dy;

                if (nlx < -radius || nlx > radius || nly < -radius || nly > radius)
                    continue;
                if (nlx * nlx + nly * nly > radius * radius)
                    continue;

                const int nx = cx + dx;
                const int ny = cy + dy;
                if (!world.inBounds(nx, ny) || world.isSolid(nx, ny))
                    continue;

                if (dx != 0 && dy != 0)
                {
                    // Corner-cutting guard: a diagonal step is only taken
                    // if both flanking orthogonal tiles are open too.
                    if (!world.inBounds(cx + dx, cy) || world.isSolid(cx + dx, cy))
                        continue;
                    if (!world.inBounds(cx, cy + dy) || world.isSolid(cx, cy + dy))
                        continue;
                }

                const std::size_t li = static_cast<std::size_t>(nly + radius) * side +
                                        static_cast<std::size_t>(nlx + radius);
                if (reached[li])
                    continue;

                reached[li] = 1;
                localQueue.push_back({nlx, nly});
            }
        }

        for (const sf::Vector2i& local : localQueue)
        {
            const double distance =
                std::sqrt(static_cast<double>(local.x) * local.x + static_cast<double>(local.y) * local.y);
            const int level = static_cast<int>(std::floor(static_cast<double>(seed.level) - distance));
            tryImprove(seed.x + local.x, seed.y + local.y, level);
        }
    }

    return result;
}
```

Everything else in `Lighting.cpp` (`recomputeAll`, `skyLight`, `torchLight`, `lavaLight`, `heldTorchLight`, `ambientOutline`) stays as-is for this task — they already call `floodFill(world, ...)` unqualified from within member functions, so no caller-side changes are needed.

- [ ] **Step 4: Build and run the full test suite**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe
```
Expected: all test cases PASS, including both new ones from Step 1 and every pre-existing case (straight-line decay, occlusion, out-of-bounds, multi-channel independence, cross-call scratch isolation). If the diagonal test still fails, check the `floor(seed.level - distance)` cast order and that `distance` is computed in `double`, not truncated to `int` early. If the corner-cutting test now fails (starts lighting `(11, 11)`), check that the corner guard runs before the `reached`/`isSolid` checks on the diagonal target tile, and that it checks both flanking tiles, not just one.

- [ ] **Step 5: Commit**

```bash
git add src/World/Lighting.cpp tests/test_lighting.cpp
git commit -m "feat: make sky, Torch, and Lava light circular instead of diamond-shaped"
```

---

### Task 2: Manual in-game verification

**Files:** none (no code changes — verification only).

**Interfaces:** none.

- [ ] **Step 1: Build the game in Debug**

Run:
```bash
cmake --build build --config Debug --target Litharia
```
Expected: builds successfully with no new warnings from `Lighting.cpp`.

- [ ] **Step 2: Run the game and check for visual correctness**

Run the built `Litharia.exe`. In-game: mine into an open cavern, place a Torch, and step back far enough to see its full lit radius. Confirm:
- The lit area reads as round (a circle), not diamond-edged — most visible where the light meets darkness at the edge of its radius, and along the diagonals in particular (the tiles that most differ between the old diamond shape and the new circle).
- A sunlit open-air shaft at the surface shows the same round falloff.
- A Torch's light still visibly stops at a solid wall, and does not visibly leak diagonally around a single wall corner into a fully enclosed pocket next to it.

- [ ] **Step 3: Note the outcome**

If an interactive session/screenshot capability is available, capture and note what was observed. If not (as in the session that produced this plan), record that build-and-launch succeeded without a crash, and that the visual shape claim rests on the code-level verification from Task 1's tests (the diagonal-distance and corner-cutting assertions) rather than a direct screenshot — same limitation noted in the 2026-07-22 lighting-perf plan's own manual-verification task.

No commit for this task — it's verification only, nothing to stage.
