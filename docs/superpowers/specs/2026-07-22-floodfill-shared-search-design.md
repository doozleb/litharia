# Lighting: Replace floodFill's Per-Seed Search With One Shared Multi-Source Dijkstra

## Purpose

The lava-seed redundancy fix (`docs/superpowers/specs/2026-07-22-lighting-lava-seed-design.md`)
reduced `Lighting::recomputeAll`'s measured cost from ~6.6s/call to ~4.2s/call
(~36%) — a real, correct, already-shipped improvement, but nowhere near
enough: the game still freezes for multiple seconds every time `recomputeAll`
runs, which is most ticks while lava is actively settling after world
generation. That fix only removed *redundant lava seeds*; it left the
underlying structural problem untouched: `floodFill`
(`src/World/Lighting.cpp:22-124`) still processes every seed with its own
independent local search, and **sky seeding was never addressed at all** —
`recomputeAll` seeds the sky channel from every open-to-surface column (up to
`WORLD_WIDTH` = 1000 seeds), each running its own local BFS-plus-`sqrt` pass,
heavily overlapping with its immediate neighbours exactly the way lava tiles
did. This is very likely a large share of the remaining ~4.2s, since it was
never touched by the prior fix.

This spec replaces `floodFill`'s traversal entirely: one shared multi-source
search per call, instead of one independent search per seed. Total work
becomes proportional to the *union* of every seed's reachable area, not the
*sum* — the redundancy the per-seed design pays for regardless of how many
seeds turn out to overlap.

## Design

### The mechanism: multi-source Dijkstra over an 8-connected weighted grid

`floodFill`'s current per-seed loop (`Lighting.cpp:57-121`) allocates a local
`(2·radius+1)²` grid per seed, runs a bounded BFS out to `radius` steps with
a corner-cutting guard, then computes each reached cell's brightness via
`floor(seedLevel - sqrt(dx² + dy²))` — the *straight-line* offset from that
seed, not the BFS path length. `tryImprove` then merges every seed's
candidates by taking the max per tile.

The replacement is a single, standard multi-source Dijkstra:

- **Edges**: from each non-solid tile to each of its non-solid neighbours —
  4 orthogonal edges of weight `1.0`, and 4 diagonal edges of weight
  `sqrt(2)`, with the *exact same* corner-cutting guard `floodFill` already
  uses (a diagonal edge only exists if both flanking orthogonal tiles are
  open) — ported directly, not reinterpreted.
- **Sources**: every seed in the call's seed list, each pushed onto a shared
  min-priority-queue at distance `0.0`.
- **Traversal**: repeatedly pop the minimum-distance entry. If this tile is
  already finalized this call (generation-stamped, same
  `floodStamp`/`floodGeneration` convention `Lighting` already uses for its
  scratch state — see `Lighting.h`), skip it; a tile can be pushed onto the
  queue multiple times as different paths reach it, and only the first pop
  (guaranteed minimal by Dijkstra's own correctness property) finalizes it.
  Otherwise, finalize it: compute `level = floor(seedLevel - distance)`; if
  `level > 0`, record it in the result (this replaces `tryImprove` entirely —
  Dijkstra's optimality guarantees each tile is finalized exactly once, with
  its true minimum distance, so there's nothing left to "improve" once
  finalized). Then relax each of its up to 8 neighbours: push a candidate
  `(distance + edgeWeight, nx, ny)` only if the resulting level would still
  be `> 0` and the neighbour isn't already finalized.
- **Termination**: the queue empties on its own once every reachable
  positive-level tile has been finalized — no separate radius check needed,
  since candidates that would yield `level ≤ 0` are never pushed.

This assumes every seed passed into one `floodFill` call shares the same
`seedLevel` — true for every current caller (`recomputeAll`'s sky/torch/lava
seed lists are each internally uniform; `heldTorchLight` passes exactly one
seed) — and is recorded here as an explicit precondition, not silently
assumed. A future caller mixing seed levels in one call would need the queue
ordered by remaining budget (`seedLevel - distance`) rather than raw
distance; not needed today, so not built.

### New scratch state (persistent, reused, same convention as existing `Lighting` state)

```cpp
// One candidate in the shared frontier: the accumulated distance to reach
// (x, y) via the path that produced this entry, not necessarily its final
// (true minimum) distance - standard lazy-deletion Dijkstra, where a tile
// can appear more than once and only its first (smallest-distance) pop is
// authoritative.
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

// Replaces floodBest's role (still generation-stamped the same way) and
// adds the shared frontier queue, persistent across calls like every other
// scratch member here.
mutable std::vector<std::uint32_t> floodStamp; // WORLD_WIDTH * WORLD_HEIGHT (kept)
mutable std::vector<std::int8_t>   floodBest;   // WORLD_WIDTH * WORLD_HEIGHT (kept)
mutable std::uint32_t              floodGeneration = 0; // kept

// New: a persistent binary-heap-backed priority queue (std::push_heap/
// pop_heap over this vector, ordered by FloodEntryGreater), reused across
// calls via clear() (keeps its allocated capacity - same "reuse persistent
// scratch buffers" principle as every other perf fix this session).
mutable std::vector<FloodEntry> floodHeap;
```

`floodHeap` is a plain `std::vector` manipulated with `std::push_heap`/
`std::pop_heap` (not a `std::priority_queue`, which has no `clear()` that
preserves capacity — `std::priority_queue::swap`ing in an empty one would
discard the underlying buffer's capacity every call, reintroducing the exact
per-call-allocation churn this whole session has been eliminating elsewhere).
`floodHeap.clear()` at the start of each call keeps its capacity; the
per-seed local `reached`/`localQueue` scratch (`Lighting.cpp:67-68`) is
deleted entirely — there is no longer a per-seed local search to scratch for.

### The per-seed local `sqrt` pass is gone; distance accumulates incrementally

Today's algorithm computes each reached cell's distance in one shot, directly
from the seed's absolute position, after the local BFS finishes. The
replacement accumulates distance incrementally as edge weights during
relaxation (`newDistance = distance + edgeWeight`) — no separate `sqrt` pass
over anything. Floating-point summation of many small `sqrt(2)` increments
along a long path could differ from computing the same total in one
multiplication by a few ULPs; irrelevant here since every distance is
immediately `floor()`-ed into a small integer level, many orders of
magnitude coarser than float rounding error.

### The already-shipped interior-lava-seed skip is kept, not superseded

`recomputeAll`'s lava-seed loop (fixed in the prior cycle) still skips
interior lava tiles before building the seed list `floodFill` receives. This
remains valuable even with a shared traversal: it shrinks the initial
seed-push count (and therefore the number of near-duplicate distance-0
entries competing in the queue at start), and there's no reason to revert a
correct, already-reviewed optimization just because a bigger one is landing
on top of it. The two are complementary.

### What changes visually: circular falloff becomes octagon-ish off-axis

This is the one deliberate, real behavior change, and it is **not** confined
to tiles that bend around obstacles — it is present everywhere, including
fully open rooms with no obstacles at all. Today's `sqrt(dx² + dy²)` computes
the *true continuous* Euclidean distance regardless of grid path length. The
replacement's distance is the *graph-shortest-path* distance over an
8-connected grid with edge weights `{1, sqrt(2)}` — which exactly equals the
true Euclidean distance only along the 8 principal directions (0°, 45°, 90°,
...); for every other direction it is strictly larger. Concretely: an offset
of `(3, 1)` gives a graph distance of `sqrt(2) + 2 ≈ 3.414` (one diagonal
step to `(1,1)`, then two orthogonal steps) versus the true straight-line
distance `sqrt(10) ≈ 3.162` — a real, visible difference, not a rounding
artifact. The practical effect: light that reads as circular along 8 evenly-
spaced directions and very slightly more octagon-shaped in between — a
standard, well-understood characteristic of 8-connected weighted-grid
distance fields, not a bug, but a genuine departure from the prior spec's
"true Euclidean falloff" framing.

**Existing test impact, checked directly rather than assumed:** the one test
asserting the circular shape explicitly
(`"floodFill produces circular, not diamond, light: diagonal distance uses
true Euclidean falloff"`, `tests/test_lighting.cpp`) checks a straight offset
(`dx=3, dy=0`) and a *pure diagonal* offset (`dx=3, dy=3`) — both principal
directions, where graph distance and true Euclidean distance are identical
(`3 × sqrt(2)` both ways). This test is expected to keep passing unmodified.
A full scan of every other `*Light(x, y)`-asserting test in the file (done
during implementation, not assumed here) is required to confirm none of them
happen to check an off-axis, off-diagonal offset that this change would
shift — see Testing below.

## Rejected alternative: spatial-lookup approach preserving exact circular shape

Instead of a graph-shortest-path traversal, do one cheap shared *reachability*
pass (unweighted multi-source BFS, purely to know which tiles are reachable
at all) followed by, for each reachable tile, a lookup against only
*spatially nearby* seeds (via a coarse grid bucketing seeds by position) to
find the true nearest valid one and compute its exact straight-line distance,
same as today. This would preserve the exact current shape/values with no
test changes needed. Rejected for this cycle: it requires a second,
different kind of per-(tile, candidate-seed) reachability check bounded to
just the spatially-near candidates, which is a more intricate design to get
right, and its actual speedup wasn't verified before this decision — a
standard multi-source Dijkstra is the well-understood, proven-correct tool
for "one shared traversal, total cost proportional to reachable area, not
seed count." Worth revisiting later only if the octagon-ish shape turns out
to be more visually objectionable in practice than expected.

## Rejected alternative: parallelize the existing per-seed searches

Distribute the current algorithm's independent per-seed searches across CPU
threads instead of restructuring the algorithm. Zero behavior/value change
(bit-for-bit identical output), but total work is unchanged — only spread
across cores — so the ceiling on improvement is bounded by core count, not
by removing the actual redundancy. Also introduces this codebase's first use
of threading (today's `Lighting` and every other subsystem are single-
threaded by design, per the prior lighting-perf spec's own global
constraint), a meaningful scope increase for a partial win. Rejected in favor
of the algorithmic fix, which removes the redundant work at its root instead
of just parallelizing it.

## Out of scope

- `ambientOutline` (`Lighting.cpp:203-279`) — its own separate, unweighted,
  step-count-based BFS, unrelated to `floodFill`, already efficient (no
  per-seed redundancy — it's a single-source search from the player's own
  position). Untouched.
- `Lighting::recomputeAll`'s interior-lava-seed skip — kept as-is (see
  Design above), not modified by this cycle.
- Any change to `MAX_LIGHT_LEVEL`, `TORCH_LIGHT_LEVEL`, or the wall-
  penetration logic in `LightRenderer` (a separate, already-optimized
  consumer of `Lighting`'s output via `SparseTileGrid` — unaffected by how
  the underlying levels are computed).
- Multithreading (see Rejected Alternative above).

## Testing

**Full re-scan required, not assumed**: read every `TEST_CASE` in
`tests/test_lighting.cpp` that asserts a specific `skyLight`/`torchLight`/
`lavaLight`/`heldTorchLight` value at an offset that is neither purely
orthogonal (`dx=0` or `dy=0`) nor purely diagonal (`|dx|=|dy|`) from its
source. Any such test's expected value needs recomputing for the new graph-
distance formula (straightforward: replace `sqrt(dx²+dy²)` with the true
8-connected shortest-path distance for that specific offset — for small
offsets this is `max(|dx|,|dy|) × 1 + min(|dx|,|dy|) × (sqrt(2) - 1)`, i.e.,
diagonal-then-straight, the standard octile distance formula) — update the
expected value and add a one-line comment noting *why* it changed (this
spec's shape-change decision), not just silently edit the number.

**New coverage to add**:

- **An off-axis offset's new expected value**: pick a clean, easy-to-verify
  case (e.g. `dx=3, dy=1`) in fully open space and assert the *exact* new
  octile-distance-based level, with a comment showing the arithmetic
  (`sqrt(2) + 2 ≈ 3.414`, `floor(15 - 3.414) = 11`) — the direct regression
  test for "the new shape is what we intend, not an accident."
- **Multi-source deduplication correctness**: two seeds close enough that
  their reachable areas overlap (e.g. two lava tiles or two torches a few
  tiles apart) — assert the overlapping region reads the *higher* of the two
  seeds' contributions where they'd differ (e.g. two different-level seeds,
  or confirm symmetric equal-level seeds agree) — this is the direct
  regression test for "finalizing on first pop still picks the true best
  source," replacing `tryImprove`'s old explicit max-taking with Dijkstra's
  implicit guarantee.
- **Corner-cutting still blocks a diagonal edge exactly as before**: the
  existing `"floodFill blocks a diagonal step around a solid corner..."`
  test should keep passing unmodified (this is a reachability assertion —
  `level == 0`, i.e., unreached — not a distance-value assertion, so it's
  unaffected by the shape change regardless of metric).
- **Cross-call isolation for the new `floodHeap` scratch**: mirrors the
  existing `floodStamp`/`floodBest` isolation tests — call `recomputeAll`
  (or `heldTorchLight`) twice with different seeds and confirm the second
  call's result doesn't retain anything from the first, now also covering
  the new heap scratch specifically (a stale heap entry from a prior call
  finalizing after `floodGeneration` has already advanced would be a bug
  distinct from the stamp/best staleness the existing tests already check).

**Mandatory empirical verification, same standard as the lava-seed cycle**:
this fix's entire purpose is a measured wall-clock improvement, and the
lava-seed cycle already demonstrated that passing unit tests is not
sufficient evidence. Re-run the same `sf::Clock`-based timing instrumentation
technique (temporary, reverted before finishing, not committed) against a
freshly generated world, report the actual new `recomputeAll` per-call
average, and compare plainly against the ~4.2s/call baseline this cycle
starts from. State clearly whether this now brings the game to something
that doesn't block noticeably (target: comfortably under 100ms per call) —
or, if it's still short of that, say so rather than rounding up, the same
way the honest Task 2 report for the prior cycle did.
