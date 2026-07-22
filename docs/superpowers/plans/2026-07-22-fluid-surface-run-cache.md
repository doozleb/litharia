# FluidSurface Run-Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop `ChunkRenderer::rebuild` from re-scanning the same fluid surface run once per member tile — turn an `O(run tiles × run length)` cost per chunk rebuild into `O(run tiles)`, without changing any rendered output.

**Architecture:** Extract `fluidSurfaceHeight`'s existing run-scan into a new `fluidSurfaceRunAt` that returns the run's `[left, right]` bounds alongside its height. `fluidSurfaceHeight` becomes a one-line wrapper (unchanged signature/behavior). `ChunkRenderer::rebuild` keeps one cached `FluidSurfaceRun` per row and only calls `fluidSurfaceRunAt` again when the current tile falls outside the cached run's bounds.

**Tech Stack:** C++20, CMake (Visual Studio generator on this machine), doctest for tests, SFML 3 (Graphics/Window/System).

## Global Constraints

- No change to `fluidSurfaceHeight`'s public signature or behavior — every existing test in `tests/test_fluids.cpp` that calls it must keep passing unmodified.
- No change to rendered output: a fluid surface tile's fill height must be numerically identical before and after this change.
- `src/World/Chunks.cpp` is compiled only into the `Litharia` target, not `Litharia_core` or `Litharia_tests` (`CMakeLists.txt:60-69`) — any task touching it must build the `Litharia` target explicitly to catch compile errors; `Litharia_tests` alone will not.
- `src/World/FluidSurface.cpp` **is** in `Litharia_core` already (`CMakeLists.txt:32`) and already linked into `Litharia_tests` — no new CMake registration is needed for Task 1.

---

### Task 1: `fluidSurfaceRunAt` — expose run bounds alongside the height

**Files:**
- Modify: `src/World/FluidSurface.h` (full file, 12 lines)
- Modify: `src/World/FluidSurface.cpp` (full file, 65 lines)
- Test: `tests/test_fluids.cpp` (append after line 800)

**Interfaces:**
- Consumes: nothing new.
- Produces (used by Task 2): `struct FluidSurfaceRun { int left; int right; float height; };` and `FluidSurfaceRun fluidSurfaceRunAt(const World& world, int x, int y);`, both declared in `src/World/FluidSurface.h`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_fluids.cpp` (after the final existing test case, which ends at line 800):

```cpp

TEST_CASE("fluidSurfaceRunAt reports the run's exact bounds, not just its height")
{
    World world;
    for (int x = 8; x <= 10; ++x)
        world.set(x, 11, BlockType::Stone);

    // Same 3-tile run as the "one flat height" fluidSurfaceHeight test.
    world.set(8, 10, BlockType::Water5);
    world.set(9, 10, BlockType::Water4);
    world.set(10, 10, BlockType::Water5);

    const FluidSurfaceRun run = fluidSurfaceRunAt(world, 9, 10);

    CHECK(run.left == 8);
    CHECK(run.right == 10);
    CHECK(run.height == doctest::Approx((14.0f / 3.0f) / 8.0f));

    // Querying from either end of the same run reports identical bounds.
    const FluidSurfaceRun runFromLeftEnd = fluidSurfaceRunAt(world, 8, 10);
    CHECK(runFromLeftEnd.left == 8);
    CHECK(runFromLeftEnd.right == 10);
}

TEST_CASE("fluidSurfaceRunAt reports a zero-width run for a submerged tile")
{
    World world;
    world.set(8, 11, BlockType::Stone);
    // A two-tall column: surface Water4 on top of a full Water8 (submerged).
    world.set(8, 9, BlockType::Water4);
    world.set(8, 10, BlockType::Water8);

    const FluidSurfaceRun submerged = fluidSurfaceRunAt(world, 8, 10);
    CHECK(submerged.left == 8);
    CHECK(submerged.right == 8);
    CHECK(submerged.height == doctest::Approx(1.0f));
}

TEST_CASE("fluidSurfaceRunAt reports a zero-width run for a non-fluid tile")
{
    World world;
    world.set(8, 10, BlockType::Stone);

    const FluidSurfaceRun run = fluidSurfaceRunAt(world, 8, 10);
    CHECK(run.left == 8);
    CHECK(run.right == 8);
    CHECK(run.height == doctest::Approx(0.0f));
}

TEST_CASE("fluidSurfaceRunAt stops a run at a solid gap and at a different fluid, same as fluidSurfaceHeight")
{
    World world;
    world.set(8, 10, BlockType::Water8);
    world.set(9, 10, BlockType::Stone);  // gap breaks the run
    world.set(10, 10, BlockType::Water2);
    world.set(11, 10, BlockType::Lava8); // different fluid breaks the run

    const FluidSurfaceRun runAt8 = fluidSurfaceRunAt(world, 8, 10);
    CHECK(runAt8.left == 8);
    CHECK(runAt8.right == 8);
    CHECK(runAt8.height == doctest::Approx(1.0f));

    const FluidSurfaceRun runAt10 = fluidSurfaceRunAt(world, 10, 10);
    CHECK(runAt10.left == 10);
    CHECK(runAt10.right == 10);
    CHECK(runAt10.height == doctest::Approx(2.0f / 8.0f));
}
```

- [ ] **Step 2: Confirm the build fails (the type/function don't exist yet)**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
```
Expected: FAIL — compile error, `FluidSurfaceRun`/`fluidSurfaceRunAt` not found.

- [ ] **Step 3: Replace `src/World/FluidSurface.h`**

```cpp
#pragma once

class World;

// A fluid surface run's horizontal bounds and shared fill height - see
// fluidSurfaceRunAt.
struct FluidSurfaceRun
{
    int left;     // inclusive world x of the run's left end
    int right;    // inclusive world x of the run's right end
    float height; // fill fraction (0..1) - same value fluidSurfaceHeight
                  // would return for any x in [left, right]
};

// Computes the surface run containing (x, y): the maximal contiguous run of
// same-family surface tiles through (x, y), and the volume-average level of
// that run as a fill fraction (0..1) - see fluidSurfaceHeight's own comment
// below for the full rules (submerged tiles, the solid/different-fluid stop
// condition, the scan cap). Returns bounds alongside the value so a caller
// iterating a row left-to-right (see ChunkRenderer::rebuild) can detect
// "still inside the run I already scanned" and skip recomputing it for
// every member tile, instead of paying the scan once per tile the way
// calling fluidSurfaceHeight independently per tile would. A tile that
// isn't itself a run member (non-fluid, submerged, or a run wider than the
// scan cap) returns a zero-width run - left == right == x - so a caller
// never mistakenly treats a non-cacheable result as reusable for a
// neighboring tile.
FluidSurfaceRun fluidSurfaceRunAt(const World& world, int x, int y);

// The fill fraction (0..1) at which to draw the fluid surface tile at (x, y).
// A "surface" tile is a fluid tile with no same-family fluid directly above it.
// The value is the volume-average level of the maximal contiguous run of
// same-family surface tiles through (x, y), so an entire run draws at one flat
// height even though the settled tile levels differ by up to one. The scan is
// bounded; a run longer than the cap falls back to the tile's own level (still
// within one level of flat). Returns 0 for a non-fluid tile.
float fluidSurfaceHeight(const World& world, int x, int y);
```

- [ ] **Step 4: Replace `src/World/FluidSurface.cpp`**

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

FluidSurfaceRun fluidSurfaceRunAt(const World& world, int x, int y)
{
    const BlockType here = world.get(x, y);

    if (!isFluid(here))
        return {x, x, 0.0f};

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
        return {x, x, fluidLevel(here) / 8.0f};

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
        return {x, x, fluidLevel(here) / 8.0f};

    int total = 0;
    for (int cx = xL; cx <= xR; ++cx)
        total += fluidLevel(world.get(cx, y));

    const int n = xR - xL + 1;
    return {xL, xR, (static_cast<float>(total) / n) / 8.0f};
}

float fluidSurfaceHeight(const World& world, int x, int y)
{
    return fluidSurfaceRunAt(world, x, y).height;
}
```

Note what changed versus the old file: the old `fluidSurfaceHeight` body is now `fluidSurfaceRunAt`'s body, with every `return <value>;` widened to `return {bounds..., <value>};`; `fluidSurfaceHeight` itself shrinks to a one-line wrapper. No algorithmic change anywhere.

- [ ] **Step 5: Build and run the full test suite**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe
```
Expected: every test case PASSES — the 4 new `fluidSurfaceRunAt` cases plus all pre-existing cases, including the 5 existing `fluidSurfaceHeight` cases in `tests/test_fluids.cpp` (now exercised through the new wrapper, unmodified expectations).

- [ ] **Step 6: Commit**

```bash
git add src/World/FluidSurface.h src/World/FluidSurface.cpp tests/test_fluids.cpp
git commit -m "perf: expose fluidSurfaceRunAt's bounds for per-run caching"
```

---

### Task 2: Cache the run in `ChunkRenderer::rebuild`

**Files:**
- Modify: `src/World/Chunks.cpp:72-123` (`ChunkRenderer::rebuild`)

**Interfaces:**
- Consumes: `FluidSurfaceRun`, `fluidSurfaceRunAt` from Task 1 (`src/World/FluidSurface.h`, already included by `Chunks.cpp:9`).
- Produces: `ChunkRenderer::rebuild` keeps its exact existing signature and effect on `chunk.vertices`/`chunk.dirty` — every caller (`ChunkRenderer::draw`, `Chunks.cpp:149-150`) is unmodified.

This task has no new automated test of its own: `Chunks.cpp` is compiled only into the `Litharia` target (graphics-dependent), which `Litharia_tests` does not link — see Global Constraints. `fluidSurfaceRunAt`'s own correctness (Task 1) plus this task's manual verification (Task 3) are the coverage for this change, consistent with `docs/superpowers/specs/2026-07-22-fluid-surface-run-cache-design.md`'s Testing section, and the same structural reason the `LightRenderer` perf cycle had no automated test for its own wiring task.

- [ ] **Step 1: Replace `ChunkRenderer::rebuild` in `src/World/Chunks.cpp`**

Replace lines 72-123 (the full body of `rebuild`) with:

```cpp
void ChunkRenderer::rebuild(Chunk& chunk, int chunkX, int chunkY) const
{
    chunk.vertices.clear();

    const int startX = chunkX * CHUNK_SIZE;
    const int startY = chunkY * CHUNK_SIZE;

    const int endX = std::min(startX + CHUNK_SIZE, WORLD_WIDTH);
    const int endY = std::min(startY + CHUNK_SIZE, WORLD_HEIGHT);

    for (int y = startY; y < endY; ++y)
    {
        // One cached fluid surface run per row, reused across every tile
        // that falls within it - see FluidSurfaceRun's own comment for why:
        // fluidSurfaceRunAt's scan is only paid once per run instead of
        // once per member tile. right < left means "nothing cached yet",
        // which is guaranteed to miss on the first candidate tile of the
        // row (x >= startX >= 0 > -1 == cachedRun.right).
        FluidSurfaceRun cachedRun{0, -1, 0.0f};

        for (int x = startX; x < endX; ++x)
        {
            const BlockType type = world.get(x, y);
            const BlockType decoration = world.getDecoration(x, y);

            const float left = static_cast<float>(x * TILE_SIZE);
            const float right = left + TILE_SIZE;
            const float bottom = static_cast<float>((y + 1) * TILE_SIZE);
            const float top = static_cast<float>(y * TILE_SIZE);

            // Decoration draws first (full tile, opaque) so terrain drawn
            // after it - in particular translucent fluid - blends on top.
            if (decoration != BlockType::Air)
                appendQuad(chunk.vertices, left, top, right, bottom, toColor(blockInfo(decoration).color));

            if (type == BlockType::Air)
                continue;

            float fluidTop = top;

            // A fluid surface tile - one with no fluid directly above it - is
            // drawn only as full as its level: liquid fills the tile from the
            // bottom up, so a level-1 tile is a 1/8-height sliver and a level-8
            // tile fills the whole block. Submerged fluid (fluid above it) stays
            // full, so only the very top of a pool shows a partial surface.
            if (isFluid(type) && !isFluid(world.get(x, y - 1)))
            {
                // A run is a maximal contiguous stretch, and this loop visits
                // x in strictly increasing order, so a miss here can only
                // mean "new run" (or a lone/capped tile) - never a stale
                // partial overlap with the previous cached run.
                if (x < cachedRun.left || x > cachedRun.right)
                    cachedRun = fluidSurfaceRunAt(world, x, y);

                const float fillHeight = TILE_SIZE * cachedRun.height;
                fluidTop = bottom - fillHeight;
            }

            const sf::Color color =
                toColor(blockInfo(type).color, isFluid(type) ? FLUID_ALPHA : std::uint8_t{255});

            appendQuad(chunk.vertices, left, fluidTop, right, bottom, color);
        }
    }

    chunk.dirty = false;
}
```

The only change from the old file: a `FluidSurfaceRun cachedRun` declared at the top of the `y` loop, and the fluid-surface branch now checks the cache before calling into `FluidSurface` instead of calling `fluidSurfaceHeight` unconditionally. Everything else — decoration drawing, the air skip, the color/alpha computation, the vertex append — is untouched.

- [ ] **Step 2: Build the `Litharia` target (the only target that compiles this file)**

Run:
```bash
cmake --build build --config Debug --target Litharia
```
Expected: builds successfully with no errors or new warnings. This is the step that actually exercises the edited file — `Litharia_tests` does not compile `Chunks.cpp` at all (see Global Constraints).

- [ ] **Step 3: Build and run the full test suite to confirm no regressions elsewhere**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe
```
Expected: every test case PASSES, including `tests/test_fluids.cpp` and Task 1's new `fluidSurfaceRunAt` cases — this task doesn't touch `FluidSurface`, so this is a pure regression check.

- [ ] **Step 4: Self-review the cache logic by hand-tracing one case**

Before committing, trace through a concrete example to confirm the cache can't leak a stale value across a run boundary: a chunk row containing, left to right, `Water8` (run A, one tile), `Stone` (gap), `Water5, Water3` (run B, two tiles). Walking `x` left to right:
- `x` at the `Water8` tile: `cachedRun` starts at `{0, -1, ...}`, so `x < cachedRun.left` is false but `x > cachedRun.right` is true (any `x >= 0 > -1`) → cache miss → `fluidSurfaceRunAt` returns `{thatX, thatX, 1.0f}` → cached.
- `x` at `Stone`: not fluid, branch skipped entirely, cache untouched.
- `x` at the first `Water5` tile of run B: this `x` is outside `[thatX, thatX]` (run A's single-tile bounds) → cache miss → fresh scan finds run B's true bounds → cached.
- `x` at the second `Water3` tile of run B: falls within run B's now-cached `[left, right]` → cache hit → reuses run B's height, no rescan.
Confirm this matches what calling `fluidSurfaceHeight` independently at each of those four tiles would produce (Task 1's tests already cover the underlying values — this step is about the cache-membership logic specifically, not the run math).

- [ ] **Step 5: Commit**

```bash
git add src/World/Chunks.cpp
git commit -m "perf: cache fluidSurfaceRunAt's result per row in ChunkRenderer::rebuild"
```

---

### Task 3: Manual in-game verification

**Files:** none (no code changes — verification only).

**Interfaces:** none.

- [ ] **Step 1: Build the game in Debug**

Run:
```bash
cmake --build build --config Debug --target Litharia
```
Expected: builds successfully.

- [ ] **Step 2: Run the game and check for rendering regressions**

Run:
```bash
build/Debug/Litharia.exe
```
In-game: find or dig into a lake, pool, or lava pocket (the world generates 5 lakes and ~170 pools per `TerrainGenerator`). Confirm:
- A wide, level fluid surface still renders as one flat height across its width, not a jagged/stepped one.
- Mining into the edge of a pool (changing its run's extent) still updates the surface height correctly on the next frame.
- No fluid tile renders at the wrong fill height (over-full, under-full, or a visible seam partway across a body that should read flat).

- [ ] **Step 3: Check for the improvement**

Stand near a wide lake or lava pool while it's actively settling (freshly mined into one, or right after world generation before pools fully settle) — the worst case: many surface tiles in the same chunk row, each previously re-scanning the same run. Confirm movement/rendering feels smoother than before this change — subjective/manual, since this repo has no frame-time benchmark harness. If still choppy, that points at the deferred half of audit item #2 (chunk rebuild throttling near continuously-active fluid — see the design spec's "Rejected alternative" section) or another backlog item (`docs/superpowers/specs/2026-07-22-optimization-audit-design.md`), most likely item #3 (`FluidSim::scanRun`'s own unbounded scan).

No commit for this task — it's verification only, nothing to stage.
