# Lighting Lava-Seed Redundancy Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce the cost of one confirmed contributor to the game appearing to hang on launch — `Lighting::recomputeAll` costing ~6.6 seconds per call because it seeds its lava flood fill from every individual lava tile instead of once per body's boundary. (Post-implementation update: Task 2's empirical measurement found this cuts the per-call cost by ~36% but does not resolve the hang — see Task 2's report and the ledger. A larger follow-up, the deferred shared multi-source `floodFill` rewrite, is still needed.)

**Architecture:** `recomputeAll`'s lava-seed loop skips tiles whose 4 orthogonal neighbours are all also lava (provably redundant for lighting anything outside the lava body). A second, cheap linear pass afterward force-sets every actual lava tile's own brightness to `MAX_LIGHT_LEVEL` directly, so skipping seeds can never make a lava tile read dimmer than it does today. `floodFill` itself is untouched.

**Tech Stack:** C++20, CMake (Visual Studio generator on this machine), doctest for tests, SFML 3 (Graphics/Window/System).

## Global Constraints

- No change to `floodFill`'s signature, traversal logic, or the circular falloff/corner-cutting behavior it implements — only which tiles get added to `recomputeAll`'s `lavaSeeds` list, plus one new post-pass in `recomputeAll`, change.
- Every lava tile must read `lavaLight(x, y) == Lighting::MAX_LIGHT_LEVEL` at its own position after `recomputeAll`, unconditionally — this must hold for interior tiles exactly as it does today, not just approximately.
- No change to sky or torch seeding.
- `recomputeAll`'s existing signature (`void recomputeAll(const World& world, const Machines& machines)`) and its callers (`Game.cpp:106`, `Game.cpp:1002`, `Game.cpp:1039`) are unchanged.
- This fix's entire purpose is a measured wall-clock improvement — passing unit tests is necessary but not sufficient. The plan's final task is a mandatory empirical before/after measurement using real timing, not just "tests pass."

---

### Task 1: Skip interior lava seeds, force-set lava tiles' own brightness

**Files:**
- Modify: `src/World/Lighting.cpp:126-171` (`recomputeAll`)
- Test: `tests/test_lighting.cpp` (append after line 606)

**Interfaces:**
- Consumes: nothing new from outside `Lighting`.
- Produces: `recomputeAll` keeps its exact existing signature and effect on `levels` for every channel except lava's *seeding mechanism* (not its resulting values) — every existing caller is unmodified.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_lighting.cpp` (after the final existing `TEST_CASE`, which ends at line 606):

```cpp

TEST_CASE("every lava tile lights itself at MAX_LIGHT_LEVEL, including an interior tile no longer seeded directly")
{
    World world;
    for (int x = 10; x <= 12; ++x)
        for (int y = 10; y <= 12; ++y)
            world.set(x, y, BlockType::Lava8);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    // Every tile in the 3x3 block, including the single interior tile
    // (11, 11) - the only one whose 4 orthogonal neighbours are all lava -
    // reads exactly MAX_LIGHT_LEVEL at its own position.
    for (int x = 10; x <= 12; ++x)
        for (int y = 10; y <= 12; ++y)
            CHECK(lighting.lavaLight(x, y) == Lighting::MAX_LIGHT_LEVEL);

    // Immediately outside the block, light still decays normally from
    // whichever boundary tile is nearest.
    CHECK(lighting.lavaLight(9, 11) == Lighting::MAX_LIGHT_LEVEL - 1);
    CHECK(lighting.lavaLight(13, 11) == Lighting::MAX_LIGHT_LEVEL - 1);
    CHECK(lighting.lavaLight(11, 9) == Lighting::MAX_LIGHT_LEVEL - 1);
    CHECK(lighting.lavaLight(11, 13) == Lighting::MAX_LIGHT_LEVEL - 1);
}

TEST_CASE("a single-tile-thick lava wall has no interior tiles and lights exactly as before")
{
    World world;
    for (int x = 8; x <= 12; ++x)
        world.set(x, 10, BlockType::Lava8);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    for (int x = 8; x <= 12; ++x)
        CHECK(lighting.lavaLight(x, 10) == Lighting::MAX_LIGHT_LEVEL);

    CHECK(lighting.lavaLight(7, 10) == Lighting::MAX_LIGHT_LEVEL - 1);
    CHECK(lighting.lavaLight(13, 10) == Lighting::MAX_LIGHT_LEVEL - 1);
}
```

- [ ] **Step 2: Run the test suite to confirm these pass against today's (unmodified) implementation**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe -tc="every lava tile lights itself at MAX_LIGHT_LEVEL, including an interior tile no longer seeded directly,a single-tile-thick lava wall has no interior tiles and lights exactly as before"
```
Expected: both new test cases PASS. Today's code already seeds every lava tile directly, so every tile (interior or not) already reads `MAX_LIGHT_LEVEL` — these are characterization tests for the refactor in the next step, not a red/green bug fix. If either fails here, stop and re-read the test before touching `Lighting.cpp` — the bug is in the test, not the (unmodified) code. If this filter syntax doesn't behave as expected, running the full suite (`build/Debug/Litharia_tests.exe` with no arguments) and confirming both new cases appear and pass is an equally valid check.

- [ ] **Step 3: Replace `recomputeAll` in `src/World/Lighting.cpp`**

Replace lines 126-171 (the full body of `recomputeAll`) with:

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

            skySeeds.push_back({x, y, MAX_LIGHT_LEVEL});
        }
    }

    std::vector<LightSeed> torchSeeds;

    for (const Machine& m : machines.all())
        if (m.type == MachineType::Torch)
            torchSeeds.push_back({m.x, m.y, TORCH_LIGHT_LEVEL});

    std::vector<LightSeed> lavaSeeds;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
    {
        for (int x = 0; x < WORLD_WIDTH; ++x)
        {
            if (!isLava(world.get(x, y)))
                continue;

            // An interior lava tile - every orthogonal neighbour also lava -
            // is a redundant flood-fill seed: it can never light anything
            // outside the lava body that a strictly closer boundary tile of
            // the same body doesn't already light at least as well, since
            // lava is non-solid and never blocks the flood fill from passing
            // through it. Skipping these seeds is what keeps recomputeAll's
            // lava-channel cost proportional to a body's boundary instead of
            // its fill - see docs/superpowers/specs/
            // 2026-07-22-lighting-lava-seed-design.md. The lava tiles
            // themselves still always read MAX_LIGHT_LEVEL regardless - see
            // the force-set pass below, not this seed list.
            const bool interior = isLava(world.get(x - 1, y)) && isLava(world.get(x + 1, y)) &&
                                   isLava(world.get(x, y - 1)) && isLava(world.get(x, y + 1));

            if (interior)
                continue;

            lavaSeeds.push_back({x, y, MAX_LIGHT_LEVEL});
        }
    }

    const auto skyResult = floodFill(world, skySeeds);
    const auto torchResult = floodFill(world, torchSeeds);
    const auto lavaResult = floodFill(world, lavaSeeds);

    std::fill(levels.begin(), levels.end(), LightLevel{0, 0, 0});

    for (const auto& [tile, level] : skyResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].sky =
            static_cast<std::uint16_t>(level);

    for (const auto& [tile, level] : torchResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].torch =
            static_cast<std::uint16_t>(level);

    for (const auto& [tile, level] : lavaResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].lava =
            static_cast<std::uint16_t>(level);

    // Every lava tile is always fully lit at its own position, independent
    // of how far it sits from a boundary seed - see docs/superpowers/specs/
    // 2026-07-22-lighting-lava-seed-design.md for why this can't be left to
    // the (boundary-only) flood fill above: a deep interior tile could
    // otherwise read dimmer than MAX_LIGHT_LEVEL. Cheap by construction - no
    // BFS, no sqrt, just a linear scan - so this costs a small, fixed amount
    // regardless of how the lava is shaped.
    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            if (isLava(world.get(x, y)))
                levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].lava =
                    static_cast<std::uint16_t>(MAX_LIGHT_LEVEL);
}
```

Note what changed versus the old file: the lava-seed loop (previously an
unconditional `if (isLava(...)) lavaSeeds.push_back(...)`) now skips
interior tiles; a new force-set loop runs after the existing three
`levels[...] = level` write-back loops, unconditionally overwriting every
lava tile's own `.lava` field to `MAX_LIGHT_LEVEL`. Sky and torch seeding,
`floodFill` itself, and every other line are untouched.

- [ ] **Step 4: Build and run the full test suite**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe
```
Expected: all test cases PASS, including every existing `test_lighting.cpp`
case (in particular "a lone Lava tile lights itself on the lava channel
only, decaying by 1 per step" and "lava light is blocked entirely by a
solid tile", which directly exercise the still-untouched single-lava-tile
path) and the two added in Step 1. If either new test fails, the bug is
almost certainly in the `interior` condition (check it uses all 4
orthogonal neighbours, not a subset) or in the force-set pass running
*before* the `lavaResult` write-back loop instead of after (it must run
last, so it overrides rather than gets overridden).

- [ ] **Step 5: Commit**

```bash
git add src/World/Lighting.cpp tests/test_lighting.cpp
git commit -m "perf: skip redundant interior lava seeds in recomputeAll"
```

---

### Task 2: Empirical before/after measurement (mandatory verification)

**Files:** none committed — temporary instrumentation only, reverted at the end of this task.

**Interfaces:** none.

This task exists because Task 1's unit tests prove *correctness* (every
lava tile still reads `MAX_LIGHT_LEVEL`), not *performance* — and this
fix's entire purpose is a measured wall-clock improvement. Per the design
spec's own "partial mitigation" section, whether skipping interior seeds
alone is enough to fix the reported hang is an empirical question, not
something provable from reading the code. Do not skip this task or treat
Task 1's passing tests as sufficient evidence the fix worked.

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

- [ ] **Step 2: Build the Release configuration**

Debug-build overhead (unoptimized code, checked STL iterators) makes this
game unusably slow independent of this fix — see the memory entry
`litharia_debug_build_unplayable.md` from earlier in this investigation, if
available in this environment. Always measure in Release.

Run:
```bash
cmake --build build --config Release --target Litharia
```
Expected: builds successfully.

- [ ] **Step 3: Launch and observe `recomputeAll`'s per-call cost for at least 60 real seconds**

On Windows, launch the instrumented build and poll its window title via
PowerShell (this exact script was validated working during the original
investigation):

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
least 60 seconds works equally well — the requirement is the sample data,
not this specific script. Record every `lightMs: <total>/<count>` value
sampled. Compute the average per-call cost (`total ÷ count`) at the final
sample.

Do not skip ahead to judging the result — record the actual numbers first.

- [ ] **Step 4: Compare against the pre-fix baseline and report the result**

Pre-fix baseline (already measured during the original investigation, on a
different freshly-generated world, for reference — a fresh in-task
measurement is still required since world generation is randomized every
launch): 7 calls, 46.5 total seconds, ~6.6 seconds average per call, with
the very first (startup) call alone taking 6.5+ seconds.

Report, in the task's completion notes: the new total call count, new total
milliseconds, and new average per-call cost, alongside the baseline above.
State plainly whether the fix brought `recomputeAll`'s average per-call
cost down to something that no longer blocks the game noticeably (a
reasonable bar: comfortably under 100ms per call, ideally much less, given
`FluidSim`'s own steps average well under 1ms and this call competes with a
60Hz frame budget) — or whether it's improved but still a problem, in which
case say so explicitly rather than rounding up to "fixed." If the world
generated for this test happens to contain little or no lava, note that
too and consider a second run (a fresh launch generates a new random
seed) — a fix that's never exercised by the test world proves nothing.

- [ ] **Step 5: Revert the temporary instrumentation**

```bash
git checkout -- src/Game/Game.cpp
git status --short
```
Expected: clean working tree (the temporary instrumentation was never
committed — Task 1's commit already captured the real fix).

No commit for this task — it's measurement only. If Step 4's result shows
the fix is insufficient, stop here and report the numbers rather than
attempting further changes without a new design pass (per
`docs/superpowers/specs/2026-07-22-lighting-lava-seed-design.md`'s
"Rejected alternative" section, the next step would be a shared
multi-source `floodFill` rewrite — a materially bigger change that needs
its own design cycle, not a same-task improvisation).
