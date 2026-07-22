# Lighting Lava-Only Recompute Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the lava-triggered lighting recompute from paying for a full sky+torch re-flood it never needs — live instrumentation confirmed the sky channel alone costs ~89% (~167.8ms of ~189ms) of every such call, on a path where only lava ever changed.

**Architecture:** Factor `recomputeAll`'s lava-channel logic (interior-skip seeding, `floodFill`, force-set pass) into a private `recomputeLavaChannel` helper, add a public `recomputeLava` entry point that calls only that helper, and switch the lava-triggered call site in `Game::fixedUpdate` from `recomputeAll` to `recomputeLava`. The startup and block/Torch-edit call sites keep calling full `recomputeAll`, unchanged.

**Tech Stack:** C++20, CMake (Visual Studio generator on this machine), doctest for tests, SFML 3 (Graphics/Window/System).

## Global Constraints

- `recomputeAll`'s existing signature and behavior are unchanged for every channel — it must still produce byte-for-byte identical output to today, on every call site that still uses it (`Game.cpp:106`, `Game.cpp:1002`).
- `recomputeLava(const World& world)` touches only `levels[...].lava` — `.sky` and `.torch` must be provably untouched (verified by tests, not just by the code's structure).
- Only one call site changes: `Game.cpp:1039` (inside `if (lavaLightingChanged && lavaLightingCooldown <= 0.0f)`), from `lighting.recomputeAll(world, machines)` to `lighting.recomputeLava(world)`.
- `floodFill`, the interior-lava-seed-skip condition, and the force-set pass's own logic are unchanged — only where that logic lives (a shared private helper) and which public entry points call it.
- This fix's entire purpose is a measured wall-clock improvement on the lava-triggered path specifically — the plan's final task is a mandatory empirical measurement, not just passing unit tests.

---

### Task 1: Factor out `recomputeLavaChannel`, add `recomputeLava`, switch the lava-triggered call site

**Files:**
- Modify: `src/World/Lighting.h:90` (add `recomputeLava` declaration right after `recomputeAll`'s, in the public section) and `:178-179` (add `recomputeLavaChannel` right after `floodFill`'s declaration, in the private section)
- Modify: `src/World/Lighting.cpp:121-202` (`recomputeAll`'s full body — split into `recomputeLavaChannel` + a trimmed `recomputeAll` + new `recomputeLava`)
- Modify: `src/Game/Game.cpp:1037-1041` (the lava-triggered recompute call)
- Test: `tests/test_lighting.cpp` (append after line 715)

**Interfaces:**
- Consumes: nothing new from outside `Lighting`.
- Produces: `void Lighting::recomputeLava(const World& world)` — a new public method later tasks (none in this plan) or future call sites can use; `recomputeAll`'s existing signature and callers are unmodified.

- [ ] **Step 1: Write the three new failing tests**

Append to `tests/test_lighting.cpp` (after the final existing `TEST_CASE`, which ends at line 715):

```cpp

TEST_CASE("recomputeLava updates the lava channel and leaves sky/torch alone")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 20; ++y)
        world.set(10, y, BlockType::Air); // open shaft: sky light
    world.set(30, 30, BlockType::Air);
    world.set(29, 30, BlockType::Air); // torch will sit at (30,30)
    world.set(41, 40, BlockType::Air); // where the lava will move to
    world.set(40, 40, BlockType::Lava8); // initial lava position

    Machines machines;
    machines.place(MachineType::Torch, 30, 30, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    // Establish the baseline every channel reads after the full recompute,
    // before recomputeLava ever runs.
    CHECK(lighting.skyLight(10, 20) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.torchLight(30, 30) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(lighting.torchLight(29, 30) == Lighting::TORCH_LIGHT_LEVEL - 1);
    CHECK(lighting.lavaLight(40, 40) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.lavaLight(41, 40) == Lighting::MAX_LIGHT_LEVEL - 1);

    // Move the lava: (40, 40) cools to solid Stone, a new Lava8 tile
    // appears at the already-open (41, 40).
    world.set(40, 40, BlockType::Stone);
    world.set(41, 40, BlockType::Lava8);

    lighting.recomputeLava(world);

    // Lava channel reflects the new position - (40, 40) is now solid, so
    // it reads 0 (a solid tile is never in floodFill's result, same as any
    // other wall).
    CHECK(lighting.lavaLight(41, 40) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.lavaLight(40, 40) == 0);

    // ...but sky and torch are exactly as recomputeAll left them - the
    // direct regression test that recomputeLava never touches those
    // channels.
    CHECK(lighting.skyLight(10, 20) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.torchLight(30, 30) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(lighting.torchLight(29, 30) == Lighting::TORCH_LIGHT_LEVEL - 1);
}

TEST_CASE("recomputeLava works correctly even when called before any recomputeAll")
{
    World world;
    fillSolid(world);
    world.set(9, 10, BlockType::Air);
    world.set(11, 10, BlockType::Air);
    world.set(10, 10, BlockType::Lava8);

    Lighting lighting;
    lighting.recomputeLava(world);

    CHECK(lighting.lavaLight(10, 10) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.lavaLight(9, 10) == Lighting::MAX_LIGHT_LEVEL - 1);
    CHECK(lighting.lavaLight(11, 10) == Lighting::MAX_LIGHT_LEVEL - 1);

    // Sky/torch were never computed at all - they read their
    // zero-initialized default.
    CHECK(lighting.skyLight(10, 10) == 0);
    CHECK(lighting.torchLight(10, 10) == 0);
}

TEST_CASE("recomputeLava and recomputeAll agree on the lava channel for the same world state")
{
    World world;
    fillSolid(world);
    world.set(9, 10, BlockType::Air);
    world.set(11, 10, BlockType::Air);
    world.set(10, 10, BlockType::Lava8);

    Machines machines;
    Lighting lighting;

    lighting.recomputeAll(world, machines);
    CHECK(lighting.lavaLight(9, 10) == Lighting::MAX_LIGHT_LEVEL - 1);

    lighting.recomputeLava(world);
    CHECK(lighting.lavaLight(9, 10) == Lighting::MAX_LIGHT_LEVEL - 1);

    // A second full recompute on the same (unchanged) world must still
    // agree - the direct regression test that the factored-out helper
    // produces the exact same result whichever entry point calls it.
    lighting.recomputeAll(world, machines);
    CHECK(lighting.lavaLight(9, 10) == Lighting::MAX_LIGHT_LEVEL - 1);
}
```

- [ ] **Step 2: Run the test suite to confirm these fail to compile against today's (unmodified) implementation**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
```
Expected: FAIL — compile error, `Lighting::recomputeLava` not found (it doesn't exist yet). This is a genuine red step: the method doesn't exist, so the build itself fails, not just a test assertion.

- [ ] **Step 3: Replace `Lighting.h`'s `recomputeAll` declaration and private section**

In `src/World/Lighting.h`, replace this single line (currently around line 90):

```cpp
    void recomputeAll(const World& world, const Machines& machines);
```

with:

```cpp
    void recomputeAll(const World& world, const Machines& machines);

    // Recomputes only the lava channel, leaving sky and torch untouched -
    // for the lava-triggered call site (Game::fixedUpdate), which never
    // needs to touch sky or torch since only lava moved on that path. See
    // docs/superpowers/specs/2026-07-22-lighting-lava-only-recompute-design.md.
    void recomputeLava(const World& world);
```

Then, in the `private:` section, find the `floodFill` method declaration itself (currently `src/World/Lighting.h:178-179`, reading `std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world, const std::vector<LightSeed>& seeds) const;` — not to be confused with the earlier `FloodEntry`/`FloodEntryGreater` struct definitions or their comments, which precede it), and add a new private method declaration immediately after it:

```cpp
    std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world,
                                                          const std::vector<LightSeed>& seeds) const;

    // Shared by recomputeAll (as its own lava step) and recomputeLava (as
    // its only step): rebuilds the lava channel from scratch (interior-skip
    // seeding, floodFill, force-set pass), touching only levels[...].lava.
    // See docs/superpowers/specs/
    // 2026-07-22-lighting-lava-only-recompute-design.md.
    void recomputeLavaChannel(const World& world);
```

(Find the exact existing `floodFill` declaration line in the current file and add the `recomputeLavaChannel` declaration directly after it — do not duplicate `floodFill`'s own declaration.)

- [ ] **Step 4: Replace `recomputeAll` in `src/World/Lighting.cpp` with the split version**

Replace the full body of `recomputeAll` (currently `src/World/Lighting.cpp:121-202`) with:

```cpp
void Lighting::recomputeLavaChannel(const World& world)
{
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

    const auto lavaResult = floodFill(world, lavaSeeds);

    // Reset only the lava channel - sky/torch are left exactly as the
    // caller already had them, unlike recomputeAll's full-struct
    // std::fill. When called from recomputeAll (below), this means the
    // lava field gets zeroed twice in a row (once by recomputeAll's own
    // std::fill, once here) - a harmless, negligible redundancy confined
    // to the already-existing full-recompute path; recomputeLava's fast
    // path only ever does this single lava-only zero.
    for (LightLevel& level : levels)
        level.lava = 0;

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

    const auto skyResult = floodFill(world, skySeeds);
    const auto torchResult = floodFill(world, torchSeeds);

    std::fill(levels.begin(), levels.end(), LightLevel{0, 0, 0});

    for (const auto& [tile, level] : skyResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].sky =
            static_cast<std::uint16_t>(level);

    for (const auto& [tile, level] : torchResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].torch =
            static_cast<std::uint16_t>(level);

    recomputeLavaChannel(world);
}

void Lighting::recomputeLava(const World& world)
{
    recomputeLavaChannel(world);
}
```

Note what changed versus the old file: the lava-seed-building loop, the
`floodFill(world, lavaSeeds)` call, and the force-set pass all moved into
the new `recomputeLavaChannel`, unchanged in logic — only their write-back
now zeroes and populates just `.lava` instead of relying on `recomputeAll`'s
full-struct `std::fill`. `recomputeAll` keeps its exact sky/torch logic and
now calls `recomputeLavaChannel(world)` as its last step instead of
building/flooding lava inline. `floodFill` itself is untouched.

- [ ] **Step 5: Switch the lava-triggered call site in `src/Game/Game.cpp`**

Find this block (`Game.cpp:1037-1041`):

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
        lighting.recomputeLava(world);
        lavaLightingCooldown = LAVA_LIGHTING_COOLDOWN_SECONDS;
    }
```

The other two `recomputeAll` call sites (`Game.cpp:106`, the constructor;
`Game.cpp:1002`, the block/Torch-edit path) are untouched.

- [ ] **Step 6: Build and run the full test suite**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe
```
Expected: all test cases PASS — **396 total** (393 existing + 3 new), including every pre-existing `recomputeAll`-based test in `tests/test_lighting.cpp` (now indirectly exercising `recomputeLavaChannel` through `recomputeAll`, with no expected values changed). If an existing test fails, the bug is almost certainly an ordering mistake in the split (e.g. `recomputeLavaChannel` running before the sky/torch write-back loops, or the `std::fill` accidentally being removed instead of left in place).

- [ ] **Step 7: Build the `Litharia` target to confirm `Game.cpp`'s call-site change compiles**

Run:
```bash
cmake --build build --config Debug --target Litharia
```
Expected: builds successfully with no errors.

- [ ] **Step 8: Commit**

```bash
git add src/World/Lighting.h src/World/Lighting.cpp src/Game/Game.cpp tests/test_lighting.cpp
git commit -m "perf: split recomputeLava out of recomputeAll for the lava-triggered path"
```

---

### Task 2: Empirical before/after measurement (mandatory verification)

**Files:** none committed — temporary instrumentation only, reverted at the end of this task.

**Interfaces:** none.

Same standard as every prior cycle in this effort: this fix's whole purpose
is eliminating the ~167.8ms of now-unnecessary sky work on the
lava-triggered path specifically. Unit tests passing proves correctness,
not that the recurring stutter is actually gone — do not skip this task or
round up to "fixed" without the numbers.

- [ ] **Step 1: Add temporary timing instrumentation to `src/Game/Game.cpp`**

In `fixedUpdate`, find the (now-switched) lava-triggered block:

```cpp
    if (lavaLightingChanged && lavaLightingCooldown <= 0.0f)
    {
        lighting.recomputeLava(world);
        lavaLightingCooldown = LAVA_LIGHTING_COOLDOWN_SECONDS;
    }
```

Replace it with:

```cpp
    // TEMP DIAGNOSTIC - not part of the design, revert before finishing this task.
    static float lavaOnlyTotalMs = 0.0f;
    static int lavaOnlyCallCount = 0;

    if (lavaLightingChanged && lavaLightingCooldown <= 0.0f)
    {
        sf::Clock lavaOnlyClock;
        lighting.recomputeLava(world);
        lavaOnlyTotalMs += lavaOnlyClock.getElapsedTime().asSeconds() * 1000.0f;
        ++lavaOnlyCallCount;
        lavaLightingCooldown = LAVA_LIGHTING_COOLDOWN_SECONDS;
    }
```

Then find the `window.setTitle(...)` call near the end of `fixedUpdate` and
add the new counters to it:

```cpp
    window.setTitle("Litharia" + mode + "  -  machines: " + std::to_string(machines.count()) +
                    "  -  drops: " + std::to_string(drops.size()) + "  -  holding: " +
                    std::string(held.empty() ? "nothing"
                                             : std::string(itemInfo(held.type).name) + " x" +
                                                   std::to_string(held.count)) +
                    "  -  lavaOnlyMs: " + std::to_string(static_cast<int>(lavaOnlyTotalMs)) +
                    "/" + std::to_string(lavaOnlyCallCount));
```

- [ ] **Step 2: Build the Release configuration**

Debug-build overhead makes this game unusably slow independent of any fix
— always measure in Release.

Run:
```bash
cmake --build build --config Release --target Litharia
```
Expected: builds successfully.

- [ ] **Step 3: Launch and observe the lava-only recompute's per-call cost for at least 60 real seconds**

On Windows, launch the instrumented build and poll its window title via
PowerShell (the same technique validated working across every prior cycle
in this effort):

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

Record every `lavaOnlyMs: <total>/<count>` value sampled. Do not use a git
worktree for this verification — a prior worktree cleanup mistake in this
same overall effort (junctioning into `libs/SFML` then removing the
worktree) deleted the 340MB SFML SDK from the main checkout; this task
needs no worktree at all.

- [ ] **Step 4: Compare against the prior baseline and report the result**

Baseline this cycle starts from (measured live in the prior cycle, on a
different freshly-generated world): ~167.5ms average per call
(16,754ms / 100 calls) for the *full* `recomputeAll` on this same call
path. This cycle's own investigation additionally measured the lava-only
portion of that cost directly: ~14.6ms (lavaSeed ~3.8ms + lavaFlood ~9.4ms
+ forceSet ~1.4ms).

Report, in the task's completion notes: the new total call count, new
total milliseconds, and new average per-call cost for `recomputeLava`
specifically. State plainly whether this average now tracks close to the
~14.6ms figure (confirming the fix works as intended) or is still
noticeably higher (in which case something in the split introduced
unexpected overhead, and that's worth flagging precisely, not glossing
over). Also note whether the user-reported recurring stutter would
plausibly be resolved at this new cost (a ~15ms stutter every ~0.5-0.6s is
far below any perceptible-freeze threshold at 60fps, unlike the prior
~167ms).

- [ ] **Step 5: Revert the temporary instrumentation**

```bash
git checkout -- src/Game/Game.cpp
git status --short
```
Expected: clean working tree (the temporary instrumentation was never
committed — Task 1's commit already captured the real fix).

No commit for this task — it's measurement only.
