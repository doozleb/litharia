# Lighting Performance: Stop Allocating Full-World Scratch Buffers Every Frame

## Purpose

The lighting *data* (`Lighting::levels`, 2 bytes/tile) is already cheap and
only rebuilt on rare events (`recomputeAll`, gated to block/Torch edits and
rate-limited lava movement - `src/Game/Game.cpp:1001-1041`). The actual
source of the reported lag is two functions that run **every single rendered
frame**, unconditionally or near-unconditionally, from `Game::render`
(`src/Game/Game.cpp:1129`, `:1149-1150`):

- `Lighting::heldTorchLight` (when a Torch is equipped)
- `Lighting::ambientOutline` (always)

Both do a small, local, radius-bounded search (a few hundred to a few
thousand tiles), but both currently allocate and zero-fill **full-world-sized**
scratch vectors to do it:

- `floodFill`'s `best` buffer (`src/World/Lighting.cpp:20`) -
  `WORLD_WIDTH * WORLD_HEIGHT` (500,000) `int8_t` entries, backing both
  `recomputeAll` and `heldTorchLight`.
- `ambientOutline`'s `visited` and `stepOf` buffers
  (`src/World/Lighting.cpp:146-147`) - 500,000 entries each, one of them a
  full `int`.

That's ~3MB of fresh heap allocation plus a full sweep over 500,000 cells,
repeated up to 60 times a second, to answer a query that only ever touches a
few hundred to a few thousand of them. This spec removes that waste without
changing any observable lighting behavior.

## Design

### The mechanism: generation-stamped persistent scratch buffers

`Lighting` gains a small set of scratch members, sized once (in the
constructor, alongside `levels`) rather than allocated per call:

```cpp
// For floodFill (backs recomputeAll and heldTorchLight):
mutable std::vector<std::uint32_t> floodStamp;  // WORLD_WIDTH * WORLD_HEIGHT
mutable std::vector<std::int8_t>   floodBest;   // WORLD_WIDTH * WORLD_HEIGHT
mutable std::uint32_t              floodGeneration = 0;

// For ambientOutline's own BFS:
mutable std::vector<std::uint32_t> outlineStamp; // WORLD_WIDTH * WORLD_HEIGHT
mutable std::vector<std::int16_t>  outlineStep;  // WORLD_WIDTH * WORLD_HEIGHT
mutable std::uint32_t              outlineGeneration = 0;
```

Each call increments its generation counter instead of clearing its buffer.
A tile counts as "not yet touched this call" whenever its stamp doesn't
equal the current generation - so a lookup becomes:

```cpp
const int currentBest = (floodStamp[i] == floodGeneration) ? floodBest[i] : -1;
```

and a write becomes `floodStamp[i] = floodGeneration; floodBest[i] = level;`.
Reset goes from O(world size) to O(1); the only cells ever touched are the
ones the BFS actually visits - the same set it touches today, just without
the wasted full-grid clear first. `floodFill` and `ambientOutline`'s BFS move
from static functions / local buffers to private instance methods, since
they now read and write `this`'s scratch state. Both stay callable from
`Lighting`'s existing `const` public methods (`heldTorchLight`,
`ambientOutline`) because the scratch buffers are `mutable` - standard for
implementation-detail caches that don't affect an object's logical state
(`levels`, the only thing `Lighting`'s public API actually exposes, is
untouched by this).

`recomputeAll` calls `floodFill` three times per invocation (sky, torch,
lava seeds); each call bumps `floodGeneration` and reuses the same buffers,
so results from one channel's flood can never leak into another's.

### Generation wraparound

`uint32_t` wraps after ~4.29 billion calls. At the call rates here (at most
a few `floodFill`/`ambientOutline` calls per frame, well under 300/sec even
in the torch-equipped worst case), that's on the order of months of
continuous, uninterrupted play before a wraparound could occur - implausible
in practice, but cheap to close off anyway: if incrementing a generation
counter ever produces `0` (the "never touched" sentinel value stamps start
at), that buffer's `stamp` vector is cleared once (an O(world size) reset,
but one that happens at most once per ~4.29 billion calls) and the counter
restarts at `1`.

### Why this doesn't change any observable behavior

The BFS traversal logic itself - which neighbours get visited, in what
order, with what decay/occlusion rule - is untouched. Only the bookkeeping
used to answer "has tile *i* been improved on already during this call, and
if so to what value" changes, from "look up a value that was preset to `-1`
across the whole grid this call" to "look up a value that's only meaningful
if the stamp matches this call's generation." Both are exactly equivalent
views of the same information. `recomputeAll`, `heldTorchLight`, and
`ambientOutline` all keep their exact signatures and return values.

### Rejected alternative: bounding-box-local scratch buffers

Allocate a buffer sized to `(2*radius+1)²` around the source instead of the
full world, with local coordinate translation clamped to world bounds.
Shrinks the per-call allocation substantially (e.g. ~10K cells instead of
500K for a held Torch at `TORCH_LIGHT_LEVEL` = 15) but still allocates and
frees fresh heap memory every single frame - smaller garbage instead of no
garbage. Rejected in favor of the generation-stamp approach, which pays a
fixed one-time memory cost (~5.5MB, allocated at startup) instead of
continuous per-frame allocator churn, for comparable implementation
complexity.

## Out of scope

- **`LightRenderer::draw`'s per-visible-tile work**
  (`src/World/LightRenderer.cpp:97-242`) - the `unordered_map` lookups per
  visible tile and the 24-offset wall-penetration search per solid tile are
  bounded by *screen* size (thousands of tiles), not world size, and are a
  much smaller cost by comparison. Left untouched; a reasonable follow-up if
  profiling still shows lighting as a hotspot after this change, but not
  part of it.
- **The pending "circular light shape" spec**
  (`docs/superpowers/specs/2026-07-22-circular-light-shape-design.md`, not
  yet implemented) - changes `floodFill`'s neighbour-expansion and distance
  math. This spec only changes the visited/best bookkeeping underneath that
  traversal, so it's independent of and compatible with that shape change
  landing before or after this one.
- Any change to brightness constants, tint colors, decay rate, or radii
  (`MAX_LIGHT_LEVEL`, `TORCH_LIGHT_LEVEL`, `AMBIENT_OUTLINE_RADIUS`, etc.) -
  purely a performance change to how scratch state is managed internally.
- `recomputeAll`'s call frequency/gating in `Game.cpp` - already
  event-gated and rate-limited; not a per-frame cost today.

## Testing

Existing `tests/test_lighting.cpp` coverage (straight-line decay, occlusion,
out-of-bounds, multi-channel independence, lava/torch seeding) exercises
`recomputeAll`, which calls the refactored `floodFill` three times per test -
this alone should catch any bookkeeping bug that lets stale data leak
between calls or between channels. No test changes are needed since no
observable behavior changes.

New coverage to add:

- A **cross-call isolation test**: call `recomputeAll` (or `heldTorchLight`)
  twice in a row with different seed positions/results, and assert the
  second call's results don't retain anything from the first - the direct
  regression test for "did the generation stamp actually invalidate stale
  entries."
- An **`ambientOutline` repeat-call test**: call it twice with different
  `playerTile`/`radius` values and assert the second call's result reflects
  only the second query, not a union with the first.

Verification: `tests/test_lighting.cpp` passing under the project's normal
test run (`ctest` / whatever this repo's build wires up -
`build/Litharia_tests.dir`), plus a manual in-game check that frame time
with a Torch equipped and moving through open caverns is visibly smoother
than before, since there's no existing perf-benchmark harness in this repo
to assert a number against.
