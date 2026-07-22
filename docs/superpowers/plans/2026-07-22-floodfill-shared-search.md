# floodFill Shared Multi-Source Search Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `Lighting::floodFill`'s per-seed independent local search with one shared multi-source Dijkstra per call, to actually resolve the game-hangs-on-launch bug that the prior lava-seed fix only partially addressed (~6.6s/call → ~4.2s/call, still far too slow).

**Architecture:** A single priority-queue-driven traversal processes every seed in a `floodFill` call together: all seeds start in the queue at distance 0; each pop finalizes a tile (generation-stamped, same convention `Lighting` already uses) at its true minimum distance and relaxes its up to 8 neighbours (orthogonal weight 1, diagonal weight `sqrt(2)`, same corner-cutting guard as today). This replaces the per-seed local BFS-plus-`sqrt` search and `tryImprove`'s explicit max-merge entirely — Dijkstra's own optimality guarantees each tile is finalized once, correctly, without a separate merge step.

**Tech Stack:** C++20, CMake (Visual Studio generator on this machine), doctest for tests, SFML 3 (Graphics/Window/System).

## Global Constraints

- No change to `floodFill`'s signature or return type (`std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world, const std::vector<LightSeed>& seeds) const`).
- The corner-cutting guard's condition is ported exactly, not reinterpreted: a diagonal edge only exists if both flanking orthogonal tiles are open.
- Precondition (documented, not enforced): every seed passed to one `floodFill` call shares the same `seed.level` — true of every current caller (`recomputeAll`'s sky/torch/lava seed lists are each internally uniform; `heldTorchLight` passes exactly one seed).
- **Every one of the 393 existing test cases in `tests/test_lighting.cpp` must keep passing with unmodified expected values.** This was verified by hand during planning, not assumed: every existing assertion that checks a specific light value is either axis-aligned (`dx=0` or `dy=0`) or pure-diagonal (`|dx|=|dy|`) relative to its source — both cases where the new graph-based distance is provably identical to the old straight-line distance — or is a pure reachability check (`== 0` for blocked/unreachable), unaffected by the distance metric at all. The one existing shape-specific test (`"floodFill produces circular, not diamond, light..."`) checks `dx=3,dy=0` (axis) and `dx=3,dy=3` (pure diagonal) — both safe. The corner-cutting test (`"floodFill blocks a diagonal step around a solid corner..."`) is a reachability check, also safe. `ambientOutline`'s 8 tests are a separate, untouched algorithm entirely.
- `ambientOutline`, `recomputeAll`'s interior-lava-seed skip, and `LightRenderer`/`SparseTileGrid` (downstream consumers of `Lighting`'s output) are untouched.
- No multithreading.

---

### Task 1: Rewrite `floodFill` as a shared multi-source Dijkstra

**Files:**
- Modify: `src/World/Lighting.h:125-171` (private section: add `FloodEntry`/`FloodEntryGreater`, add `floodHeap`, update `floodFill`'s doc comment)
- Modify: `src/World/Lighting.cpp:22-124` (`floodFill`'s full body)
- Test: `tests/test_lighting.cpp` (append after line 657)

**Interfaces:**
- Consumes: nothing new from outside `Lighting`.
- Produces: `floodFill` keeps its exact existing signature and (per the Global Constraints' verified-safe cases) produces identical output to today for every axis-aligned or pure-diagonal query — `recomputeAll`, `heldTorchLight`, and every existing caller are unmodified.

- [ ] **Step 1: Write the two new failing tests**

Append to `tests/test_lighting.cpp` (after the final existing `TEST_CASE`, which ends at line 657):

```cpp

TEST_CASE("floodFill's shared search gives an off-axis tile a slightly larger (octile) distance than straight-line")
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

    // Offset (dx=4, dy=3) is neither axis-aligned nor pure-diagonal. The
    // true straight-line distance is exactly 5 (a 3-4-5 right triangle),
    // which the old per-seed algorithm used directly: floor(15 - 5) = 10.
    // The shared search's graph (octile) distance - one diagonal step per
    // unit of the smaller axis, then straight steps for the remainder -
    // is max(4,3) + min(4,3) * (sqrt(2) - 1) = 4 + 3 * 0.41421356... ~=
    // 5.2426, giving floor(15 - 5.2426...) = 9: a full integer dimmer, not
    // just a rounding nuance. This is the one deliberate behavior change
    // this cycle makes - see docs/superpowers/specs/
    // 2026-07-22-floodfill-shared-search-design.md.
    CHECK(lighting.torchLight(54, 53) == 9);
}

TEST_CASE("floodFill's shared multi-source search picks each tile's true nearest seed, not just whichever was pushed first")
{
    World world;
    fillSolid(world);
    for (int x = 10; x <= 20; ++x)
        world.set(x, 10, BlockType::Air);
    world.set(10, 10, BlockType::Lava8);
    world.set(20, 10, BlockType::Lava8);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    // (13, 10) is 3 steps from the left source, 7 from the right - the
    // shared search must resolve to the closer (higher-level) one.
    CHECK(lighting.lavaLight(13, 10) == Lighting::MAX_LIGHT_LEVEL - 3);
    // (17, 10) is the mirror image: 3 from the right source, 7 from the left.
    CHECK(lighting.lavaLight(17, 10) == Lighting::MAX_LIGHT_LEVEL - 3);
    // (15, 10), the midpoint, is equidistant (5 from each) - both sources
    // agree on the same value, directly checking the merge doesn't
    // double-count or otherwise misbehave when two seeds tie.
    CHECK(lighting.lavaLight(15, 10) == Lighting::MAX_LIGHT_LEVEL - 5);
}
```

Note: a dedicated new test for `floodHeap` cross-call isolation is deliberately not added — `floodHeap.clear()` runs unconditionally at the start of every `floodFill` call (Step 3 below), before any seed is pushed, so a stale heap entry surviving between calls is impossible by construction, unlike `floodStamp`/`floodBest`'s generation-comparison mechanism (which the two existing isolation tests, `"floodFill's persistent scratch does not leak stale state between successive recomputeAll calls"` and `"heldTorchLight's persistent scratch does not leak between successive calls at different sources"`, already exercise end-to-end against the new implementation).

- [ ] **Step 2: Run the test suite to confirm the new tests fail against today's (unmodified) implementation**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe -tc="floodFill's shared search gives an off-axis tile a slightly larger (octile) distance than straight-line,floodFill's shared multi-source search picks each tile's true nearest seed, not just whichever was pushed first"
```
Expected: the off-axis test **FAILS** — today's unmodified code gives
`lighting.torchLight(54, 53) == 10` (straight-line distance exactly 5), not
the `9` the test asserts. This is a genuine red/green test: it must fail
now and pass only after Step 4's rewrite. The multi-source dedup test
**PASSES already today** — it only exercises axis-aligned distances and
correct max-merging, which the old `tryImprove`-based code already handles
correctly; it's a characterization test for the rewrite (confirming the new
code doesn't regress this), not a red/green regression test.

- [ ] **Step 3: Replace the private section of `src/World/Lighting.h`**

Replace lines 125-171 (from `private:` to the closing `};`) with:

```cpp
private:
    struct LightSeed
    {
        int x;
        int y;
        int level;
    };

    // One candidate in floodFill's shared frontier: the accumulated
    // distance to reach (x, y) via the path that produced this entry, not
    // necessarily its final (true minimum) distance - standard
    // lazy-deletion Dijkstra, where a tile can appear more than once and
    // only its first (smallest-distance) pop is authoritative.
    struct FloodEntry
    {
        float distance;
        int x;
        int y;
    };

    // Min-heap by distance: smallest distance pops first.
    struct FloodEntryGreater
    {
        bool operator()(const FloodEntry& a, const FloodEntry& b) const
        {
            return a.distance > b.distance;
        }
    };

    // A single shared multi-source Dijkstra across every seed in `seeds`:
    // every seed starts queued at distance 0, and each step relaxes the 4
    // orthogonal (weight 1) and 4 diagonal (weight sqrt(2), corner-cutting
    // guarded - see the .cpp) neighbours of whichever tile the frontier's
    // smallest-distance entry names, stopping once a tile's resulting level
    // (seedLevel - distance, floored) would be 0. Since Dijkstra finalizes
    // each tile exactly once, at its true minimum distance, the first time
    // it's popped, a tile is only ever improved once - the popped visit
    // order is already the full sparse result, same invariant the old
    // per-seed version relied on. Serves the world-wide recompute and a
    // single moving source (Lighting::heldTorchLight) alike - see
    // docs/superpowers/specs/2026-07-22-floodfill-shared-search-design.md
    // for the full design and its one deliberate behavior change: falloff
    // is exactly circular only along the 8 principal directions, very
    // slightly octagon-ish between them - a standard property of
    // 8-connected weighted-grid distance, present even with no obstacles
    // at all.
    //
    // Precondition (unenforced, true of every current caller): every seed
    // in one call shares the same seed.level.
    //
    // Uses floodStamp/floodBest/floodGeneration and floodHeap (below) as
    // scratch rather than allocating fresh buffers per call - see
    // floodStamp's own comment for why.
    std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world,
                                                          const std::vector<LightSeed>& seeds) const;

    std::vector<LightLevel> levels; // WORLD_WIDTH * WORLD_HEIGHT

    // Persistent scratch for floodFill, sized once at construction rather
    // than reallocated per call. floodBest[i] is only meaningful when
    // floodStamp[i] == floodGeneration; any other stamp value means tile i
    // hasn't been touched during the current call, equivalent to the old
    // per-call buffer's -1 sentinel.
    mutable std::vector<std::uint32_t> floodStamp; // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::vector<std::int8_t> floodBest;     // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::uint32_t floodGeneration = 0;

    // Persistent scratch for floodFill's shared frontier - a binary heap
    // (std::push_heap/pop_heap, ordered by FloodEntryGreater) over this
    // vector, reused across calls via clear() so its allocated capacity
    // survives between calls instead of being discarded and regrown.
    mutable std::vector<FloodEntry> floodHeap;

    // Persistent scratch for ambientOutline's own BFS - added in a later
    // step of this same change, same generation-stamp trick, kept separate
    // from floodFill's scratch above since the two searches store different
    // payloads (best light level vs. BFS step count).
    mutable std::vector<std::uint32_t> outlineStamp; // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::vector<std::int16_t> outlineStep;   // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::uint32_t outlineGeneration = 0;
};
```

No change to `Lighting`'s constructor is needed: `floodHeap` is a plain
`std::vector<FloodEntry>` that default-constructs empty and grows (keeping
its capacity across calls via `clear()`, not resizing to a fixed world-size
constant like `floodStamp`/`floodBest`, since its needed size is
data-dependent, not a fixed world-size quantity).

- [ ] **Step 4: Replace `floodFill` in `src/World/Lighting.cpp`**

Replace lines 22-124 (the full body of `floodFill`) with:

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
    floodHeap.clear();

    static constexpr int NEIGHBOR_OFFSETS[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    const float diagonalWeight = std::sqrt(2.0f);

    // A single shared multi-source Dijkstra across every seed, instead of
    // an independent local search per seed - see docs/superpowers/specs/
    // 2026-07-22-floodfill-shared-search-design.md. Precondition (see
    // Lighting.h's comment on this method): every seed shares the same
    // seed.level.
    int seedLevel = 0;

    for (const LightSeed& seed : seeds)
    {
        if (seed.level <= 0 || !world.inBounds(seed.x, seed.y) || world.isSolid(seed.x, seed.y))
            continue;

        seedLevel = seed.level;
        floodHeap.push_back({0.0f, seed.x, seed.y});
        std::push_heap(floodHeap.begin(), floodHeap.end(), FloodEntryGreater{});
    }

    while (!floodHeap.empty())
    {
        std::pop_heap(floodHeap.begin(), floodHeap.end(), FloodEntryGreater{});
        const FloodEntry entry = floodHeap.back();
        floodHeap.pop_back();

        const std::size_t i = static_cast<std::size_t>(entry.y) * WORLD_WIDTH + entry.x;

        // Already finalized via an earlier (necessarily smaller-or-equal
        // distance) pop this call - a tile can be pushed more than once as
        // different paths reach it, but only its first pop is authoritative
        // (standard lazy-deletion Dijkstra).
        if (floodStamp[i] == floodGeneration)
            continue;

        const int level = static_cast<int>(std::floor(static_cast<double>(seedLevel) - entry.distance));
        if (level <= 0)
            continue;

        floodStamp[i] = floodGeneration;
        floodBest[i] = static_cast<std::int8_t>(level);
        result.push_back({{entry.x, entry.y}, level});

        for (const auto& offset : NEIGHBOR_OFFSETS)
        {
            const int dx = offset[0];
            const int dy = offset[1];
            const int nx = entry.x + dx;
            const int ny = entry.y + dy;

            if (!world.inBounds(nx, ny) || world.isSolid(nx, ny))
                continue;

            const std::size_t ni = static_cast<std::size_t>(ny) * WORLD_WIDTH + nx;
            if (floodStamp[ni] == floodGeneration)
                continue;

            if (dx != 0 && dy != 0)
            {
                // Corner-cutting guard: a diagonal step is only taken if
                // both flanking orthogonal tiles are open too - unchanged
                // from the per-seed search this replaces.
                if (!world.inBounds(entry.x + dx, entry.y) || world.isSolid(entry.x + dx, entry.y))
                    continue;
                if (!world.inBounds(entry.x, entry.y + dy) || world.isSolid(entry.x, entry.y + dy))
                    continue;
            }

            const float edgeWeight = (dx != 0 && dy != 0) ? diagonalWeight : 1.0f;
            const float newDistance = entry.distance + edgeWeight;

            // No point pushing a candidate that would decay to 0 or below.
            if (static_cast<int>(std::floor(static_cast<double>(seedLevel) - newDistance)) <= 0)
                continue;

            floodHeap.push_back({newDistance, nx, ny});
            std::push_heap(floodHeap.begin(), floodHeap.end(), FloodEntryGreater{});
        }
    }

    return result;
}
```

Note what changed versus the old file: the entire per-seed local-BFS-plus-
`sqrt` loop (old lines 57-121) and the `tryImprove` lambda (old lines 36-52)
are gone, replaced by one shared Dijkstra loop. `recomputeAll`,
`heldTorchLight`, and `ambientOutline` (which doesn't call `floodFill` at
all) are completely untouched — they still call `floodFill(world, seeds)`
exactly as before and get back the same `std::vector<std::pair<sf::Vector2i,
int>>` shape.

- [ ] **Step 5: Build and run the full test suite**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe
```
Expected: **all 393 test cases pass** (391 existing + 2 new) — per the
Global Constraints, every existing test was verified by hand during
planning to use only axis-aligned, pure-diagonal, or reachability-only
assertions, so none of their expected values should need to change. If any
existing test fails, do not "fix" it by changing its expected value without
first re-deriving by hand whether the new value is actually correct for
that specific offset under the octile-distance formula — a failing
axis-aligned or pure-diagonal test indicates a bug in the new `floodFill`
(most likely: the corner-cutting guard condition, the `floodStamp`-checked-
before-push optimization introducing a subtle skip, or an off-by-one in
which tile gets finalized vs. relaxed), not an expected-value problem.

- [ ] **Step 6: Commit**

```bash
git add src/World/Lighting.h src/World/Lighting.cpp tests/test_lighting.cpp
git commit -m "perf: replace floodFill's per-seed search with one shared multi-source Dijkstra"
```

---

### Task 2: Empirical before/after measurement (mandatory verification)

**Files:** none committed — temporary instrumentation only, reverted at the end of this task.

**Interfaces:** none.

Same standard as the prior (lava-seed) cycle: this fix's entire purpose is a
measured wall-clock improvement, and unit tests passing is necessary but not
sufficient evidence. Do not skip this task or round up to "fixed" without
the numbers to support it.

- [ ] **Step 1: Add temporary timing instrumentation to `src/Game/Game.cpp`**

In `fixedUpdate` (`src/Game/Game.cpp`), find this block:

```cpp
    std::vector<sf::Vector2i> fluidChanges;
    fluids.tick(world, dt, fluidChanges);
```

Replace it with:

```cpp
    // TEMP DIAGNOSTIC - not part of the design, revert before finishing this task.
    static float fluidTotalMs = 0.0f;
    static float lightingTotalMs = 0.0f;
    static int lightingCallCount = 0;
    static int fluidCallCount = 0;

    sf::Clock fluidClock;
    std::vector<sf::Vector2i> fluidChanges;
    fluids.tick(world, dt, fluidChanges);
    fluidTotalMs += fluidClock.getElapsedTime().asSeconds() * 1000.0f;
    ++fluidCallCount;
```

Then find this block (a few lines further down in the same function):

```cpp
    if (lavaLightingChanged && lavaLightingCooldown <= 0.0f)
    {
        lighting.recomputeAll(world, machines);
        lavaLightingCooldown = LAVA_LIGHTING_COOLDOWN_SECONDS;
    }
```

Replace it with:

```cpp
    if (lavaLightingChanged && lavaLightingCooldown <= 0.0f)
    {
        sf::Clock lightingClock;
        lighting.recomputeAll(world, machines);
        lightingTotalMs += lightingClock.getElapsedTime().asSeconds() * 1000.0f;
        ++lightingCallCount;
        lavaLightingCooldown = LAVA_LIGHTING_COOLDOWN_SECONDS;
    }
```

Then find the `window.setTitle(...)` call near the end of `fixedUpdate`:

```cpp
    window.setTitle("Litharia" + mode + "  -  machines: " + std::to_string(machines.count()) +
                    "  -  drops: " + std::to_string(drops.size()) + "  -  holding: " +
                    std::string(held.empty() ? "nothing"
                                             : std::string(itemInfo(held.type).name) + " x" +
                                                   std::to_string(held.count)));
```

Replace it with:

```cpp
    window.setTitle("Litharia" + mode + "  -  machines: " + std::to_string(machines.count()) +
                    "  -  drops: " + std::to_string(drops.size()) + "  -  holding: " +
                    std::string(held.empty() ? "nothing"
                                             : std::string(itemInfo(held.type).name) + " x" +
                                                   std::to_string(held.count)) +
                    "  -  fluidMs: " + std::to_string(static_cast<int>(fluidTotalMs)) +
                    "/" + std::to_string(fluidCallCount) +
                    "  -  lightMs: " + std::to_string(static_cast<int>(lightingTotalMs)) +
                    "/" + std::to_string(lightingCallCount));
```

(This is the exact same instrumentation used for the prior cycle's Task 2 —
if `Game.cpp` still has it from an incomplete prior run, confirm with `git
status`/`git diff` that the working tree is clean before starting, per Step
5 below's expectation.)

- [ ] **Step 2: Build the Release configuration**

Debug-build overhead (unoptimized code, checked STL iterators) makes this
game unusably slow independent of any fix — always measure in Release.

Run:
```bash
cmake --build build --config Release --target Litharia
```
Expected: builds successfully.

- [ ] **Step 3: Launch and observe `recomputeAll`'s per-call cost for at least 60 real seconds**

On Windows, launch the instrumented build and poll its window title via
PowerShell (validated working in the prior cycle):

```powershell
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Title {
    [DllImport("user32.dll")]
    public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder text, int count);
}
"@

$proc = Start-Process -FilePath "build\Release\Litharia.exe" -PassThru
$start = Get-Date
for ($i = 0; $i -lt 12; $i++) {
    Start-Sleep -Seconds 5
    $proc.Refresh()
    $elapsed = ((Get-Date) - $start).TotalSeconds
    $title = New-Object System.Text.StringBuilder 512
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) {
        [Win32Title]::GetWindowText($proc.MainWindowHandle, $title, 512) | Out-Null
    }
    Write-Output "t=${elapsed}s title=$title"
}
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
Write-Output "done"
```

On a platform without PowerShell available, any equivalent that launches
the built executable and reads its window title every 5-10 seconds for at
least 60 seconds works equally well. Record every `lightMs: <total>/<count>`
value sampled, and also note `fluidMs` (should stay negligible, confirming
this fix didn't regress `FluidSim`).

Do not skip ahead to judging the result — record the actual numbers first.

- [ ] **Step 4: Compare against the pre-fix baseline and report the result**

Baseline this cycle starts from (the prior cycle's own measurement, on a
different freshly-generated world — a fresh in-task measurement is still
required since world generation is randomized every launch): ~4.2s average
per call (50,395ms / 12 calls), itself down from the original ~6.6s/call
before any fix.

Report, in the task's completion notes: the new total call count, new total
milliseconds, and new average per-call cost, alongside both the ~4.2s and
~6.6s baselines. State plainly whether this now brings the game to
something that doesn't block noticeably (target: comfortably under 100ms
per call) — or, if it's still short of that, say so rather than rounding
up, the same way the honest report for the prior cycle did. If the world
generated for this test happens to contain little lava or few open-to-sky
columns (unlikely for the sky channel specifically, but check), note that
and consider a second run.

- [ ] **Step 5: Revert the temporary instrumentation**

```bash
git checkout -- src/Game/Game.cpp
git status --short
```
Expected: clean working tree (the temporary instrumentation was never
committed — Task 1's commit already captured the real fix).

No commit for this task — it's measurement only.
