# Circular Light Shape: Sky, Torch, and Lava Light a Circle, Not a Diamond

## Purpose

`Lighting::floodFill` (`src/World/Lighting.cpp:21-68`) is the single flood-fill
engine shared by sky light, Torch light (both a placed Torch and the
player's held Torch), and Lava light. It expands from each seed through the
4 orthogonal neighbours only, decaying by exactly 1 per step. In open,
unobstructed space that step-count decay is Manhattan distance, so every
light source currently reads as a diamond (a rotated square) rather than a
circle. This spec changes the shape of light produced by all three channels
to a true circle, while keeping every other invariant of the system
(brightness levels, out-of-bounds behaviour, "blocked entirely by a solid
tile") exactly as it is today.

This revises an earlier draft of the same idea written before
`floodFill` was refactored for performance (see
`docs/superpowers/specs/2026-07-22-lighting-perf-design.md`): `floodFill` is
now a `const` instance method using persistent, generation-stamped scratch
(`floodStamp`/`floodBest`/`floodGeneration` on `Lighting`) instead of a
fresh per-call buffer. The shape mechanism below is written against that
current implementation and is designed to keep that scratch-reuse mechanism
completely untouched.

## Design

### The mechanism

Replace `floodFill`'s single merged-queue, step-decay BFS with a **per-seed
local reachability search**, whose result feeds the same generation-stamped
merge `floodFill` already uses:

- For each seed (`x`, `y`, `level`) in turn, run a bounded 8-directional
  (orthogonal + diagonal) flood fill confined to the square
  `[x - level, x + level] × [y - level, y + level]`, tracked with a small
  local `reached` buffer sized `(2*level + 1)²` (at most 31×31 = 961 cells
  for a Torch's `TORCH_LIGHT_LEVEL` = 15) — not the persistent
  world-sized scratch, since this search never needs to outlive one seed's
  processing.
- A neighbour is only expanded into if it's in-bounds, non-solid, and
  within the seed's radius disk: `dx² + dy² ≤ level²` (plain integer
  arithmetic, no `sqrt` needed for the cutoff test).
- **Corner-cutting is blocked**: a diagonal step from `(cx, cy)` to
  `(cx + dx, cy + dy)` is only taken if both flanking orthogonal tiles -
  `(cx + dx, cy)` and `(cx, cy + dy)` - are also non-solid. This keeps
  "blocked entirely by a solid tile" true in spirit: light can no longer
  slip diagonally past a single wall corner it couldn't have reached by any
  straight or right-angle path. (A tile can still be reached by a *longer*
  orthogonal detour even when the direct diagonal is corner-blocked - reach
  is "any admissible 8-directional path exists," not "the direct diagonal
  specifically.")
- Once a tile is confirmed reachable this way, its displayed level is
  computed directly from true (Euclidean) distance to the seed -
  `floor(level - sqrt(dx² + dy²))` - discarded if that comes out `≤ 0`,
  regardless of which path the local BFS actually used to reach it.
- Each reachable tile is then merged into the result via the existing
  `floodStamp`/`floodBest`/`floodGeneration` scratch, unchanged from
  today: a small helper (replacing today's `tryVisit`) checks whether this
  candidate level beats whatever the tile already holds this call (across
  every seed processed so far, in this or an earlier `floodFill` call's
  generation) and, if so, stamps and records it. Multiple seeds (many
  placed Torches, many Lava tiles, every open sky column) are therefore
  still merged by taking the brighter (`max`) result per tile - identical
  combination semantics to today, just fed by the new per-seed shape.

This is a self-contained change to `floodFill`'s internals only. Its
signature, and every public `Lighting` method that calls it
(`recomputeAll`, `heldTorchLight`), stay exactly as they are. The
generation-stamp scratch mechanism itself - and its wraparound guard - is
untouched; only what feeds into it changes.

### Why this doesn't reintroduce the per-frame allocation problem

`heldTorchLight` runs every rendered frame (`Game.cpp:1129`). The
just-completed performance work
(`docs/superpowers/specs/2026-07-22-lighting-perf-design.md`) specifically
eliminated full-world-sized (500,000-element) scratch allocation from that
per-frame path. This change's local `reached` buffer is bounded to the
seed's own disk - at most ~961 cells (≈1KB) for a Torch - regardless of
world size, so it stays in the same "cheap, bounded per source" cost class
`floodFill` already documents itself as having, and does not reintroduce a
world-sized allocation anywhere on the per-frame path.

### Why this doesn't break existing tests

I checked this against the actual tile layouts in the current
`tests/test_lighting.cpp`, not just in the abstract:

- Every existing *exact-value* assertion (e.g. "decays by 1 per step")
  measures brightness along a straight horizontal or vertical line from a
  source. On such a line, Euclidean distance and step count are identical
  (`dx = 0` or `dy = 0`), so `floor(level - sqrt(dx² + dy²))` produces the
  exact same numbers the old step-decay did.
- The two "blocked / disconnected" tests ("a cave with no path to an open
  shaft reads sky 0...", "a column blocked from the surface gets no direct
  sky seed of its own") each surround their isolated pocket with solid
  tiles on every side, including the diagonals, via `fillSolid` (which sets
  the *entire* world to Stone before the test opens specific tiles) - so
  8-directional expansion doesn't open any new path in either case.
- The multi-channel test ("a tile lit by both a Torch and Lava reads a
  nonzero level on each independent channel") only asserts `> 0`, and the
  Lava seed at `(11, 11)` reaches `(10, 10)` via a pure orthogonal detour
  through `(11, 10)` regardless of whether the direct diagonal is
  corner-blocked - so it still passes.

No existing test needs to change.

### Rejected alternative

**8-directional expansion with a flat cost of 1 per step, regardless of
direction** (no distance math, no `sqrt`) is simpler and marginally cheaper,
but produces an octagon, not a circle - at the radii in play here
(`MAX_LIGHT_LEVEL` = 9, `TORCH_LIGHT_LEVEL` = 15) an octagon is visibly
distinguishable from a circle, not just "diamond with the corners knocked
off." Rejected because it doesn't actually deliver what was asked for.

### Performance

Each seed's search is bounded to the disk of its own radius (at most
~`π × TORCH_LIGHT_LEVEL²` ≈ 707 tiles for a Torch, ~254 for sky/Lava at
`MAX_LIGHT_LEVEL`), independent of how many seeds exist - the same "cheap,
bounded per source" cost class `floodFill` already documents itself as
having. `heldTorchLight` (called once per frame) sees a modest increase
from a ~481-tile diamond to a ~707-tile disk at `TORCH_LIGHT_LEVEL` = 15,
plus one small (≈1KB) local-buffer allocation per frame - trivial at 60fps
and far below the world-sized allocation the perf work already eliminated
from this same call path. `recomputeAll` (block/Torch edits only, never
per-frame) is unaffected in cost class even for a fully open sky column
seeding hundreds of independent seeds.

## Out of scope

- **`ambientOutline`** (`Lighting.cpp:147-223`) - the flat, non-decaying
  "eyes adjusted to the dark" mechanic. It's a separate BFS keyed on step
  count, not a light source, and isn't touched by this spec; its reach
  boundary stays diamond-shaped.
- **`LightRenderer`'s wall-penetration glow** (`LightRenderer.cpp:57-93`) -
  the cosmetic "glimpse light through up to 3 tiles of rock" search is a
  separate, rendering-only Manhattan-diamond lookup, independent of the
  shape of the underlying channel data. Left diamond-shaped; a reasonable
  follow-up if the mismatch (circular light, diamond glow-through-rock)
  reads oddly in practice, but not part of this change.
- Any change to brightness constants (`MAX_LIGHT_LEVEL`, `TORCH_LIGHT_LEVEL`,
  tint colours, decay rate) - purely a shape change.
- The generation-stamp scratch mechanism itself (`floodStamp`, `floodBest`,
  `floodGeneration`, and their wraparound guard) - reused as-is.

## Testing

Existing `tests/test_lighting.cpp` coverage (straight-line decay, occlusion,
out-of-bounds, multi-channel independence, cross-call scratch isolation)
continues to hold unmodified, per the reasoning above, and should still
pass. New coverage to add:

- A diagonal-distance case: a tile at `(seed.x + 3, seed.y + 3)` in open
  space should read `floor(level - sqrt(18))` - for example, `10` for a
  Torch seeded at `TORCH_LIGHT_LEVEL` = 15, since
  `floor(15 - 4.2426...) = 10` - not `level - 6` (what the old
  Manhattan/diamond decay would have produced: `15 - 6 = 9`). This is the
  one assertion that actually distinguishes circular from diamond; compute
  and assert the exact expected value for whichever seed level the test
  uses, don't approximate it.
- A corner-cutting case: a seed with two solid tiles forming an orthogonal
  corner should *not* light the diagonal tile tucked directly behind that
  corner via the direct diagonal step, when no orthogonal detour around it
  exists either (i.e. the tile is genuinely unreachable, not just
  reachable-the-long-way).
- The existing straight-line, occlusion, and multi-seed-merge tests continue
  to serve as regression coverage that the new mechanism reproduces the old
  behaviour exactly where the two shapes agree.

Verification is `tests/test_lighting.cpp` passing under the project's normal
test run, plus a manual/visual check in-game if possible: a placed Torch and
a sunlit open-air shaft should both read as round rather than diamond-edged
when viewed from a distance. (Per the just-completed perf work, this
session has no interactive/screenshot capability for in-game visual
verification - see that spec's Testing section for the same caveat.)
