# Conservative, convergent fluid settling

**Date:** 2026-07-19
**Status:** Approved design, ready for implementation planning

## Problem

The fluid simulation never fully settles for many pool shapes: a small set of
cells changes value every step forever. This was diagnosed (see
`src/World/FluidSim.cpp` history and the investigation in the July 2026 perf
session) to a fundamental conflict between the settling rules:

- `fallAt` drives each column to be bottom-heavy (fill the cell below to full).
- `flattenAt` drives each row toward a uniform level.
- The enclosed branch of `flattenAt` rounds a run to a single level with
  `(total + n/2)/n`, which is **deliberately non-conservative** (e.g. 19 units
  across 4 cells becomes 20). That rounding is what makes a "dead-flat" surface.

The non-conservation is a perpetual source/sink: `fallAt` pours a unit down,
the rounding destroys or creates a fraction, and the cell is refilled from the
connected body. The result is a cycle with no fixed point. Because the fluid
tick marks chunks dirty on every change, a never-settling pool also forces
perpetual chunk-mesh rebuilds.

A separate, already-landed fix deduplicated the active-tile queue (it had grown
to ~40x the live fluid-tile count, and the worst fluid step cost ~13.7 ms). That
fix removed the frame-time spikes but did not change convergence. This spec
covers the convergence/conservation redesign only.

## Goal

Fluid settling that is:

- **Conservative** — the obsidian reaction is the only intentional consumer of
  fluid; every other move (fall, spill, level, edge) conserves total fluid
  exactly.
- **Convergent** — every pool shape reaches a fully quiescent fixed point (a
  step that changes nothing), so there is no perpetual shimmer and no perpetual
  chunk rebuild.
- **Visually flat** — settled pools still *look* dead-flat, achieved in the
  renderer rather than by mutating tile levels non-conservatively.

Preserve all other visible behavior: gradual sideways spread, lava slower than
water, spilling over ledges, lava+water -> obsidian, and halved player gravity
while overlapping fluid.

## Key decisions

1. **Flat look lives in the renderer.** The simulation settles a surface to
   *within one level* (the best a conservative model can do when volume is not a
   multiple of width). The renderer draws each surface run at one uniform height
   so it reads as perfectly flat.
2. **Gradual slosh.** An uneven surface equalizes one unit at a time, settling
   over roughly half a second to a second, rather than snapping flat in a single
   step. This is what makes the model provably convergent, and it reads as more
   natural water.
3. **Approach A: unify `flattenAt` + `spreadAt` into one conservative
   `equalizeAt` rule.** (Global per-body relaxation and a minimal patch to the
   existing rules were both considered and rejected — see below.)

## Design

### Settling rules

`FluidSim::step` keeps its per-active-tile structure. For each active tile of
fluid type `T` at level `L`, rules are tried in order; the first to act wins:

1. **`reactAt`** (lava only) — unchanged. Lava adjacent to water becomes
   obsidian; the only intentional fluid consumer.
2. **Lava throttle** — unchanged. On its off-steps, lava does not move but stays
   pending if it still has a legal move.
3. **`fallAt`** — unchanged, already conservative. Air below: the whole tile
   drops. Same-fluid below with room: pour down `min(L, 8 - belowLevel)`. This
   rule owns all *vertical* filling; a column fills bottom-up until every cell
   rests on a full cell.
4. **`equalizeAt`** (new — replaces `flattenAt` and `spreadAt`). Runs only when
   the cell cannot fall (i.e. `fallAt` did not act, so the cell rests on full
   support: solid ground, the world floor, or same-fluid at level 8). It
   considers the two horizontal neighbors, each **eligible** only if it is air
   or same-fluid **and it rests** (its own cell below is not open in-bounds air
   — an air neighbor over a drop is a ledge, left to `cascadeAt`). Among eligible
   neighbors it selects the strictly-lower one by level (air counts as level 0;
   ties go left) and moves `floor((L - neighborLevel) / 2)` units into it —
   **only if that amount is at least 1**, i.e. the level difference is at least
   2. A difference of exactly 1 moves nothing. Moving into a resting neighbor
   whose support is only partial is safe here (unlike the old `spreadAt`):
   because nothing is non-conservative, the fed column simply fills and the
   process terminates rather than looping.
5. **`cascadeAt`** — unchanged, conservative. Spill over a ledge: an air
   neighbor with open air beneath it; the tile tips over so it falls down the
   far side next step.

Two properties of `equalizeAt` are the whole fix:

- **Conservative** — it moves whole units and never rounds, so there is no
  source/sink.
- **No overshoot, no ±1 flicker** — moving half the gap can never invert the
  pair, and a difference of 1 is left as the stable remainder, so a settled
  surface never churns.

Because `fallAt` owns not-yet-full columns and `equalizeAt` owns only
full-support surfaces, their domains are disjoint. They cannot fight — which is
the origin of every oscillation observed.

### Convergence argument

Every legal move reduces a bounded-below quantity:

- Falls and pours move fluid to a lower row, strictly reducing total potential
  energy (sum of level times height).
- Horizontal equalizes move fluid from a higher cell to a strictly-lower one,
  strictly reducing surface variance (sum of squared levels within a row), and
  do nothing once all differences are at most 1.

Nothing is non-conservative, so there is no injected fluid to sustain a cycle. A
monotonically decreasing, bounded-below measure must reach a fixed point;
therefore the system goes fully quiescent. This is validated empirically with a
seed-sweep benchmark in addition to the unit tests below.

### Renderer flat-look

Add a pure, SFML-free helper in the core library:

`float fluidSurfaceHeight(const World& world, int x, int y)` — returns the fill
fraction (0..1) at which to draw the surface tile at `(x, y)`. It scans the
maximal contiguous run of same-type *surface* tiles (a fluid tile with no fluid
directly above it) through `(x, y)`, left and right, and returns the run's
volume-average level divided by 8. Every tile in a run computes the same run and
therefore the same height, so the run draws flat.

- The scan is bounded by a cap (e.g. 128 tiles). Beyond the cap it falls back to
  the tile's own level. This keeps a chunk rebuild cheap for a pathological wide
  body; such a body is still within one level of flat anyway.
- `ChunkRenderer::rebuild` calls this helper in place of the current inline
  `fluidLevel(type) / 8.0f`. Keeping the logic in core makes it unit-testable;
  the renderer only consumes it.

Accepted cosmetic caveats (not worth extra machinery):

- A surface run crossing a chunk boundary can show a one-frame seam while fluid
  is actively flowing, because a chunk caches its mesh until re-dirtied. It is
  seamless once settled. If it ever looks bad, the remedy is to also mark
  horizontal-neighbor chunks dirty on a boundary change.
- The volume-average is used (accurate to the fluid actually present) rather
  than the run maximum (always flat but visually overfills by up to 1/8 tile).

### Rejected approaches

- **B: global per-body relaxation.** Flood-fill each connected body and assign
  its equilibrium level directly. Settles instantly and exactly, but needs
  connected-component tracking on every disturbance, is much heavier, and gives
  no gradual slosh. Rejected.
- **C: minimal patch to the current rules.** Make `flattenAt` conservative and
  add a "do not move if imbalance <= 1" threshold. Smaller diff, but keeps two
  rules with overlapping domains, so convergence is much harder to guarantee —
  the exact fragility this redesign exists to remove. Rejected.

## Testing

### Existing tests to rewrite (they encoded the removed non-conservation)

- `tests/test_fluids.cpp` *"an uneven run rounds to the nearest level and stores
  it uniformly"* (currently asserts 19 -> 20, all Water5). Rewrite to assert the
  conservative outcome: 19 units settle to flat-within-1 (e.g. 5,5,5,4) with the
  total staying 19.
- `tests/test_fluids.cpp` *"a resting, uneven row of water levels itself flat in
  a single step"* (currently asserts 20 -> all Water5 in one tick). Rewrite to
  tick until quiescent, then assert flat-within-1 and total conserved (exactly
  20).

### Existing tests that must still pass unchanged

Falling/edge conservation, spill-over-ledge, pour-to-fill, all obsidian/reaction
tests, lava-slower-than-water, the "settles to a flat surface filling its basin,
maxSurface - minSurface <= 1" test, and the halved-gravity-in-fluid tests. These
are the guardrails that the redesign did not regress behavior.

### New tests

1. **Convergence** — several pool shapes (including irregular basins like the
   generated worlds that oscillated) reach quiescence: a step eventually appends
   nothing to `changed`.
2. **No flicker** — after settling, one more tick produces zero changes.
3. **Exact conservation** — an isolated pool (no reaction) conserves total fluid
   exactly from start to settled.
4. **Equalize unit rule** — a difference-of-1 neighbor pair does not exchange; a
   difference-of-2-or-more pair moves `floor(diff/2)`.
5. **`fluidSurfaceHeight` helper** — a within-1-level run reports one uniform
   height for every tile in the run; a run stops at a solid gap and at a
   different fluid.

### Out-of-suite validation

Re-run the seed-sweep benchmark (throwaway harness, not committed) across many
generated worlds to confirm every world reaches quiescence, before declaring the
work done. The permanent guarantee lives in the unit tests above.

## Notes

- The diagnostic `activeCount()` accessor used during investigation is not part
  of this design; convergence tests assert on `changed` being empty instead.
- The active-queue dedup fix is already landed and independent of this work.
