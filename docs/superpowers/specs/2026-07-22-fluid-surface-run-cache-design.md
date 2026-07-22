# FluidSurface: Cache a Run's Height Instead of Rescanning It Per Tile

## Purpose

This is (the first half of) item #2 from the whole-game optimization audit
(`docs/superpowers/specs/2026-07-22-optimization-audit-design.md`), flagged
there as one of the two most likely-to-be-*felt* items on the backlog
(alongside the now-fixed `LightRenderer` item), since it bites during
ordinary play near any lake or lava pool.

`ChunkRenderer::rebuild` (`src/World/Chunks.cpp:72-123`) calls
`fluidSurfaceHeight(world, x, y)` (`src/World/FluidSurface.cpp:16-65`) once
per fluid-surface tile while building a chunk's mesh
(`Chunks.cpp:109-113`). Every one of those calls independently re-derives
the same information from scratch: `fluidSurfaceHeight` scans up to
`MAX_RUN_SCAN=128` tiles left and right to find the full extent of the
contiguous same-family surface run the tile belongs to, then re-walks that
whole range a second time to average `fluidLevel` across it
(`FluidSurface.cpp:41-64`). A run of `N` surface tiles in the same chunk
row therefore costs `O(N * runLength)` instead of the `O(N)` it should — for
a 32-wide dirty row through a wide lake, on the order of 10K+ redundant
`world.get()` calls per chunk rebuild, and `Chunks::markDirty`
(`Chunks.cpp:55-64`) plus `Game::fixedUpdate`'s per-tile fluid-change
marking (`src/Game/Game.cpp:1011-1025`) mean a chunk near actively flowing
fluid can rebuild up to ~20 times a second (`FluidSim::TICK_INTERVAL =
0.05s`, `src/World/FluidSim.h:37`) — so this cost repeats continuously for
as long as the fluid keeps moving, not once per real change.

## Design

### The mechanism: one cached run per row, reused across its member tiles

`FluidSurface.h`/`.cpp` gains a new function that returns a run's bounds
alongside its height, so a caller iterating a row left-to-right can tell
whether it's still inside the same run without recomputing:

```cpp
// FluidSurface.h
struct FluidSurfaceRun
{
    int left;     // inclusive world x of the run's left end
    int right;    // inclusive world x of the run's right end
    float height; // fill fraction (0..1) - same value fluidSurfaceHeight
                  // would return for any x in [left, right]
};

// Same computation fluidSurfaceHeight does, but returns the run's bounds
// too - a caller iterating left-to-right across a row (see
// ChunkRenderer::rebuild) can compare its current x against [left, right]
// to tell "still inside the run I already scanned" from "need a fresh
// scan," instead of paying the scan on every member tile independently.
// A tile that isn't itself a run member (non-fluid, submerged, or a run
// wider than MAX_RUN_SCAN) returns a zero-width run - left == right == x -
// so a caller never mistakenly treats a non-cacheable result as reusable
// for a neighboring tile.
FluidSurfaceRun fluidSurfaceRunAt(const World& world, int x, int y);

// Unchanged signature and behavior - now a one-line wrapper around
// fluidSurfaceRunAt, kept for any caller (including the 5 existing tests
// in tests/test_fluids.cpp) that just wants one tile's value.
float fluidSurfaceHeight(const World& world, int x, int y);
```

`fluidSurfaceRunAt` is `fluidSurfaceHeight`'s existing algorithm, byte-for-
byte, with its two early-return branches (`!isFluid(here)`, `!isRunMember(x)`)
and its `scanned >= MAX_RUN_SCAN` fallback each changed only to additionally
report a zero-width run (`{x, x, value}`) alongside the value they already
compute — the value itself, and every branch condition that produces it, is
untouched. `fluidSurfaceHeight(world, x, y)` becomes
`return fluidSurfaceRunAt(world, x, y).height;`.

`ChunkRenderer::rebuild` keeps one cached `FluidSurfaceRun` per row, reset
at the start of each row (a run never spans rows - `fluidSurfaceHeight`
only ever scans horizontally at a fixed `y`):

```cpp
for (int y = startY; y < endY; ++y)
{
    FluidSurfaceRun cachedRun{0, -1, 0.0f}; // right < left: nothing cached yet

    for (int x = startX; x < endX; ++x)
    {
        // ...decoration + air-skip unchanged...

        if (isFluid(type) && !isFluid(world.get(x, y - 1)))
        {
            if (x < cachedRun.left || x > cachedRun.right)
                cachedRun = fluidSurfaceRunAt(world, x, y);

            const float fillHeight = TILE_SIZE * cachedRun.height;
            fluidTop = bottom - fillHeight;
        }

        // ...unchanged...
    }
}
```

Since `rebuild`'s `x` loop already advances strictly left-to-right and a
row's surface runs are, by construction, maximal non-overlapping contiguous
stretches, a single cache slot is sufficient: the first tile of a run
misses the cache and pays the scan once; every other tile in that same run
falls within `[cachedRun.left, cachedRun.right]` and reuses the cached
`height` for a plain bounds check instead of a rescan. A tile outside any
run's bounds (a gap, a different fluid family, or the very next run) misses
the cache and triggers exactly one fresh scan, which then serves that run's
own member tiles the same way. Zero-width runs (submerged tiles, or a run
wider than `MAX_RUN_SCAN`) never produce a false cache hit, since no other
tile's `x` can equal `left == right == x` except that exact tile itself -
matching today's behavior exactly for those cases (each such tile already
computes its own independent value; the cache adds no benefit there but
also doesn't change the result, since it just falls through to a fresh
`fluidSurfaceRunAt` call every time, identical to calling
`fluidSurfaceHeight` directly as today).

### Why this doesn't change any observable behavior

The run-finding scan, the `MAX_RUN_SCAN` cap and its own-level fallback, and
the averaging math are all unmodified - `fluidSurfaceRunAt` is
`fluidSurfaceHeight`'s current body with its return points widened to also
report bounds. `Chunks::rebuild`'s only change is *how many times* that
unmodified computation runs for a given row (once per distinct run instead
of once per tile in it) - every tile's resulting fill height is identical
to what `fluidSurfaceHeight(world, x, y)` would return for that tile today.

## Rejected alternative: also throttle chunk rebuild frequency near active fluid

The audit's original finding bundled two problems: this scan's O(run²)
cost, and the fact that a visible chunk with continuously-flowing fluid can
be marked dirty and fully rebuilt up to ~20 times a second
(`FluidSim`'s tick rate), not just once per real visual change. Fixing that
second half would mean changing *when* a dirty chunk actually rebuilds -
e.g. rate-limiting rebuilds per chunk independent of how often `markDirty`
fires - which is a genuine (if likely small) behavior change: fluid visuals
would lag up to one throttle window behind the simulation's actual state,
versus today's always-immediate-next-frame rebuild. That's a different kind
of risk than a provably-identical-output caching change, and it's a
separate architectural decision (chunk-level rebuild scheduling, not
fluid-surface math) — left for its own future cycle rather than folded into
this one. This fix alone still addresses what audit item #2's own writeup
called the dominant cost of the two (the O(run²) rescan), so it should
deliver most of the practical improvement on its own.

## Out of scope

- **Chunk rebuild throttling near continuously-active fluid** - see
  Rejected Alternative above; a separate future item.
- **Audit item #10** (`Chunks::rebuild` missing a vertex-array `reserve()`)
  - not actionable as originally written: this codebase's `sf::VertexArray`
  (SFML 3, `libs/SFML/include/SFML/Graphics/VertexArray.hpp`) exposes no
  `reserve()` at all, only `resize()`/`append()`/`clear()`. Doing this
  "properly" would mean rewriting the quad-building loop to pre-size via
  `resize()` and index-assign instead of `append()` - a materially bigger,
  riskier change than the one-liner the audit assumed, and out of
  proportion to its (already-labeled-Minor) payoff. Dropped rather than
  forced in.
- Any other audit backlog item (`FluidSim::scanRun`'s own unbounded scan,
  machine draw/tick scaling, etc.) - each gets its own cycle.
- Any change to fluid physics, fill-fraction math, or rendering appearance
  beyond removing redundant recomputation - purely a performance change to
  how many times `ChunkRenderer::rebuild` computes the same run.

## Testing

`fluidSurfaceHeight`'s existing 5 tests in `tests/test_fluids.cpp`
(`fluidSurfaceHeight reports one flat height across a within-one-level
run`, `...stops a run at a solid gap and at a different fluid`,
`...on a submerged tile reports its own level, not the run`, `...returns 0
for a non-fluid tile`, plus the within-one-level averaging case) all call
`fluidSurfaceHeight` directly and must keep passing unmodified, since its
signature and behavior are unchanged - this alone exercises
`fluidSurfaceRunAt`'s value output through its wrapper.

New coverage to add, directly against `fluidSurfaceRunAt` (not just
`fluidSurfaceHeight`), since the bounds it reports are new observable
surface area no existing test touches:

- **Run bounds match the actual run extent**: a 3-tile run (matching the
  existing "one flat height" test's setup) should report `left`/`right`
  spanning exactly those 3 tiles for a query at any tile within it, not
  just a matching `height`.
- **A submerged tile reports a zero-width run**: same setup as the existing
  "submerged tile" test - assert `left == right == x` for the submerged
  query, confirming a caller can't mistakenly treat it as cacheable for a
  neighbor.
- **A non-fluid tile reports a zero-width run**: same as above, for the
  existing "returns 0 for a non-fluid tile" case.

`ChunkRenderer::rebuild`'s own row-cache integration has no automated test
of its own: `src/World/Chunks.cpp` is compiled only into the `Litharia`
target (`CMakeLists.txt:65`), the same graphics-dependent boundary
`LightRenderer.cpp` sits behind - it's never linked into `Litharia_core` or
`Litharia_tests`, so there's no seam to unit-test its vertex output at all,
the same limitation the `LightRenderer` perf cycle already ran into.
Coverage for the cache-correctness question ("did the cache leak a stale
value onto the wrong tile?") comes instead from `fluidSurfaceRunAt`'s own
bounds tests above (correct bounds is what makes the caller's cache-hit
check correct) plus a manual line-by-line self-review during implementation
confirming the cache-check logic can't apply a cached `height` to an `x`
outside `[left, right]`, plus the manual in-game check below - the same
compensating pattern the `LightRenderer` cycle used for the same structural
reason.

Verification: full test suite passing (`ctest` / the project's normal
`Litharia_tests` run), plus a manual in-game check that a wide lake or lava
pool still renders at the same flat surface height as before this change,
and that frame time near actively-flowing water/lava feels smoother than
before - same manual-check caveat as prior perf specs in this repo, since
there's still no frame-time benchmark harness (see the audit doc's own
"out of scope" section, which already flags this as worth addressing
separately).
