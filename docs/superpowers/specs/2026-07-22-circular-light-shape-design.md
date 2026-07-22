# Circular Light Shape: Sky, Torch, and Lava Light a Circle, Not a Diamond

## Purpose

`Lighting::floodFill` (`src/World/Lighting.cpp:17-54`) is the single flood-fill
engine shared by sky light, Torch light (both a placed Torch and the
player's held Torch), and Lava light. It expands from each seed through the
4 orthogonal neighbours only, decaying by exactly 1 per step. In open,
unobstructed space that step-count decay is Manhattan distance, so every
light source currently reads as a diamond (a rotated square) rather than a
circle. This spec changes the shape of light produced by all three channels
to a true circle, while keeping every other invariant of the system
(brightness levels, out-of-bounds behaviour, "blocked entirely by a solid
tile") exactly as it is today.

## Design

### The mechanism

Replace `floodFill`'s per-seed step decay with a per-seed **bounded 8-directional
flood fill whose displayed brightness is computed from real (Euclidean)
distance**, not from BFS step count:

- For each seed (`x`, `y`, `level`), only tiles within `level` Euclidean tiles
  of the seed are ever considered - candidates are filtered by
  `dx*dx + dy*dy <= level*level` before anything else, using plain integer
  arithmetic (no `sqrt` needed for the cutoff test itself).
- Expansion walks all 8 neighbours (orthogonal + diagonal), not just 4,
  continuing to any not-yet-visited, in-bounds, non-solid, in-radius tile.
- **Corner-cutting is blocked**: a diagonal step from `(x, y)` to
  `(x + dx, y + dy)` is only taken if both flanking orthogonal tiles -
  `(x + dx, y)` and `(x, y + dy)` - are also non-solid. This keeps "blocked
  entirely by a solid tile" true in spirit: light can no longer slip
  diagonally past a single wall corner it couldn't have reached by any
  straight or right-angle path.
- Once a tile is confirmed reachable (connected to the seed via such a path,
  entirely within the radius disk), its displayed level is computed directly
  from true distance: `floor(level - sqrt(dx*dx + dy*dy))`, discarded if
  that comes out `<= 0`.
- Multiple seeds in one `floodFill` call (many placed Torches, many Lava
  tiles, every open sky column) are processed independently and merged by
  taking the brighter (`max`) result per tile - identical combination
  semantics to today, just fed by the new per-seed shape.

This is a self-contained change to `floodFill`'s internals only. Its
signature, and every public `Lighting` method that calls it
(`recomputeAll`, `heldTorchLight`), stay exactly as they are.

### Why this doesn't break existing tests

Every existing shape assertion in `tests/test_lighting.cpp` (e.g. "decays by
1 per step") measures brightness along a straight horizontal or vertical
line from a source. On such a line, Euclidean distance and step count are
identical (`dx=0` or `dy=0`), so `floor(level - sqrt(dx²+dy²))` produces the
exact same numbers the old step-decay did. No existing test needs to change.

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
having. `heldTorchLight` (called once per frame, per
`src/Game/Game.cpp:1129`) sees a modest increase from a ~481-tile diamond to
a ~707-tile disk at `TORCH_LIGHT_LEVEL` = 15; trivial at 60 fps.
`recomputeAll` (block/Torch edits only, never per-frame) is unaffected in
cost class even for a fully open sky column seeding hundreds of independent
seeds.

## Out of scope

- **`ambientOutline`** (`Lighting.cpp:133-199`) - the flat, non-decaying
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

## Testing

Existing `tests/test_lighting.cpp` coverage (straight-line decay, occlusion,
out-of-bounds, multi-channel independence) continues to hold unmodified, per
the reasoning above, and should still pass. New coverage to add:

- A diagonal-distance case: a tile at `(seed.x + 3, seed.y + 3)` in open
  space should read `floor(level - sqrt(18))` ≈ `level - 4`, not
  `level - 6` (what the old Manhattan/diamond decay would have produced) -
  this is the one assertion that actually distinguishes circular from
  diamond.
- A corner-cutting case: a seed with two solid tiles forming an orthogonal
  corner should *not* light the diagonal tile tucked behind that corner,
  even though it's within Euclidean range.
- The existing straight-line, occlusion, and multi-seed-merge tests continue
  to serve as regression coverage that the new mechanism reproduces the old
  behaviour exactly where the two shapes agree.

Verification is `tests/test_lighting.cpp` passing under the project's normal
test run, plus a manual/visual check in-game: a placed Torch and a sunlit
open-air shaft should both read as round rather than diamond-edged when
viewed from a distance.
