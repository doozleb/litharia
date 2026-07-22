# Lighting: Stop Seeding recomputeAll's Lava Flood Fill From Every Interior Tile

## Purpose

The game was reported unresponsive on launch — appearing to hang for minutes.
Systematic debugging (this session) ruled out the initially-suspected cause
(`FluidSim::scanRun`'s unbounded width — see the ruled-out section below)
via direct measurement, then found the real one: `Lighting::recomputeAll`
(`src/World/Lighting.cpp:126-171`) costs **~6.6 seconds per call**, measured
live (7 calls, 46.5 total seconds, over a 60-second window on a freshly
generated world). `Game::Game()`'s constructor calls it once, unconditionally,
*before the window shows a single frame* (`src/Game/Game.cpp:106`) — so this
alone accounts for the reported "not loading," independent of anything that
happens afterward. Every subsequent lava-triggered recompute
(`Game.cpp:1037-1041`, rate-limited to at most once per 0.5s but each
individual call still fully blocking the main thread for ~6.6s) repeats the
cost.

### Why this got this expensive: what circular-light-shape actually changed

`Lighting::floodFill` (`Lighting.cpp:22-124`) used to be a single shared
multi-source BFS across every seed together (see
`docs/superpowers/specs/2026-07-22-lighting-perf-design.md`'s description of
the pre-circular-shape algorithm). The circular-light-shape feature
(commit `88675ae`, the day before this session) rewrote it to loop over
**each seed independently** (`Lighting.cpp:57-121`): for every seed, it
allocates a local `(2·radius+1)²` grid, runs its own bounded BFS out to
`radius` steps with a corner-cutting guard, then does a second pass over
every reached cell computing a `double`-precision `std::sqrt` distance. This
is correctly cheap for `heldTorchLight` (exactly one seed, called once per
rendered frame) and `ambientOutline` (its own separate, non-`floodFill` BFS,
untouched by this). It is not cheap for `recomputeAll`'s lava channel
(`Lighting.cpp:147-152`), which seeds from **every individual lava tile in
the world**, not once per lava body — a filled 100-tile lava pool runs this
entire local-BFS-plus-`sqrt`-pass **100 times**, almost entirely redundant
with its own neighbours' identical searches, since lava tiles are non-solid
(`isLava` blocks are not `isSolid`) and so never stop the flood fill from
passing straight through them.

## Design

### The mechanism: skip seeds that are provably redundant

An **interior** lava tile — one whose 4 orthogonal neighbours (`world.get`,
which returns `BlockType::Air` and therefore reads as non-lava at any
out-of-bounds neighbour, so a world-edge lava tile is correctly never
classified interior) are *all* also lava — can never light anything outside
its lava body that a strictly closer **boundary** tile of the same body
(one actually touching rock, air, or a different fluid) doesn't already
light at least as well: since lava doesn't block the flood fill, any tile
reachable from the interior tile's position is reachable via an equal-or-shorter
path from *some* boundary tile of the same contiguous body, and every tile
still inside the lava body already reads `MAX_LIGHT_LEVEL` regardless of
which specific seed reaches it. So `recomputeAll`'s lava-seed loop
(`Lighting.cpp:147-152`) becomes:

```cpp
std::vector<LightSeed> lavaSeeds;

for (int y = 0; y < WORLD_HEIGHT; ++y)
{
    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        if (!isLava(world.get(x, y)))
            continue;

        // An interior lava tile - every orthogonal neighbour also lava - is
        // a redundant flood-fill seed: it can never light anything outside
        // the lava body that a strictly closer boundary tile of the same
        // body doesn't already light at least as well, since lava is
        // non-solid and never blocks the flood fill from passing through
        // it. Skipping these seeds is what keeps recomputeAll's lava-channel
        // cost proportional to a body's boundary instead of its fill - see
        // docs/superpowers/specs/2026-07-22-lighting-lava-seed-design.md.
        const bool interior = isLava(world.get(x - 1, y)) && isLava(world.get(x + 1, y)) &&
                               isLava(world.get(x, y - 1)) && isLava(world.get(x, y + 1));

        if (interior)
            continue;

        lavaSeeds.push_back({x, y, MAX_LIGHT_LEVEL});
    }
}
```

Nothing else in `recomputeAll` or `floodFill` changes. Sky seeding
(`Lighting.cpp:130-139`) is untouched — it already seeds only the single
topmost open tile of each column (`break` at the first solid tile), so it
was never doing per-tile redundant work. Torch seeding
(`Lighting.cpp:141-145`) is untouched — torches are individually placed, not
naturally generated in large filled bodies, so this redundancy doesn't arise
there in practice.

### The one accepted behavioral caveat

The "interior" check only looks at the 4 **orthogonal** neighbours, not the
4 diagonals. A lava tile whose orthogonal neighbours are all lava but which
has a purely-diagonal non-lava neighbour (a one-tile notch cut diagonally
into an otherwise-solid lava blob) is classified interior and skipped, even
though `floodFill`'s corner-cutting guard would, in principle, let a
diagonal step from this exact tile reach that notch. In every case this
matters, some other boundary tile of the same body is within one or two
tiles of that notch and seeds it just as well (the geometry that would make
this actually visible - a deep, purely-diagonal-access pocket with no
orthogonal-adjacent boundary tile anywhere nearby - does not occur in this
game's generated terrain, which builds lava pools as smooth parabolic
bowls, not fractal notches). Accepted as a negligible, purely-cosmetic edge
case in exchange for the seed-count reduction; not something any existing
test exercises or could reasonably distinguish from noise.

### Why this is a partial mitigation, not a guaranteed complete fix

This reduces `recomputeAll`'s lava-channel seed count roughly in proportion
to how "filled" the world's lava bodies are (a shallow 1-2-tile-deep pool
has few or no interior tiles and gets little benefit; a deep, wide, mostly-
solid lava lake could see its seed count fall by many times). It does
**not** change the per-seed cost (still a local BFS plus a `sqrt` pass per
remaining boundary seed) or restructure `floodFill` into a single shared
multi-source search, which is what would be needed to eliminate the
remaining boundary-seed redundancy entirely (neighbouring boundary tiles a
tile or two apart still each pay their own largely-overlapping local
search). Whether this alone brings `recomputeAll` down from ~6.6s to
something acceptable, or only partway there, is an empirical question this
spec cannot answer from reasoning alone — see Testing below for the
mandatory before/after measurement.

## Rejected alternative: rewrite floodFill as a single shared multi-source search

The algorithmically complete fix would replace the current per-seed
independent-local-BFS-plus-`sqrt` design with one traversal processing all
seeds together, the way `floodFill` worked before circular-light-shape.
Getting *true* circular (Euclidean) falloff from a single shared multi-source
traversal is materially harder than the current per-seed approach - the
current code gets circularity for free by measuring each reached cell's
exact Euclidean offset from its own single seed after the fact; a shared
traversal would need something closer to a proper multi-source Euclidean
distance transform, not a simple BFS with integer step decrements (which
would just reproduce the old diamond shape). That's a substantially bigger
redesign, touching the same code path the circular-light-shape feature's own
dedicated test suite (`tests/test_lighting.cpp`) was written against just
yesterday, and carries real risk of subtly breaking that recently-verified
shape/corner-cutting behavior. Rejected for this cycle in favor of the
narrower, provably-safe interior-tile skip, which requires no change to
`floodFill`'s traversal logic at all - if the empirical measurement below
shows the interior-skip alone isn't sufficient, this is the natural next
step, informed by real before/after numbers instead of guesswork.

## Ruled out: FluidSim::scanRun

The initial hypothesis, before measurement, was audit item #3
(`FluidSim::scanRun`'s unbounded per-tile rescan,
`docs/superpowers/specs/2026-07-22-optimization-audit-design.md`). Direct
timing instrumentation (`sf::Clock` around `fluids.tick(...)` in
`Game::fixedUpdate`, temporary, not committed) measured **74ms total across
254 calls** (~0.3ms average) over the same 60-second window where
`recomputeAll` cost 46.5 total seconds across 7 calls - `FluidSim` is
working exactly as designed ("a step averages well under a millisecond",
`FluidSim.h:35-36`). Separately, `growLakeBasin`
(`src/World/TerrainGenerator.cpp:328-356`) caps a generated lake's half-width
at `radius(≤4.5) × LAKE_WIDTH_FACTOR(2.4)` ≈ 10.8 tiles - naturally-generated
bodies never approach `FluidSurface`'s existing `MAX_RUN_SCAN=128`, so
`scanRun`'s theoretical unboundedness, while a real inefficiency worth fixing
on its own merits eventually (it remains on the audit backlog), was never
the cause of this incident. Recorded here so a future investigation doesn't
retread the same reasoning.

## Out of scope

- Rewriting `floodFill` into a shared multi-source search (see Rejected
  Alternative above) - only pursued as a follow-up if the empirical
  measurement below shows the interior-skip alone is insufficient.
- `FluidSim::scanRun`'s own unbounded width (audit item #3) - ruled out as
  the cause of this incident; still a legitimate, separate backlog item.
- Sky and torch seeding - already efficient (see Design above); untouched.
- Any change to `MAX_LIGHT_LEVEL`, `TORCH_LIGHT_LEVEL`, decay rate, or the
  circular falloff shape/corner-cutting behavior itself - purely a change to
  which tiles become seeds, not how any seed's light propagates.

## Testing

`tests/test_lighting.cpp` already covers `recomputeAll`'s lava-seeding
behavior indirectly (e.g. "a lone Lava tile lights itself on the lava
channel only, decaying by 1 per step", "lava light is blocked entirely by a
solid tile") - these use single, isolated lava tiles (already boundary
tiles, since a lone lava tile's every neighbour is non-lava), so they must
keep passing unmodified and directly prove the single-lava-tile case is
untouched.

New coverage to add:

- **A fully-interior lava tile contributes no seed of its own, but the body
  still lights correctly**: build a solid block of Lava8 several tiles
  thick (e.g. 3x3 or larger, so it has genuine interior tiles), surrounded
  by open air, and confirm the resulting light field around the block's
  *edges* is identical to what it would be if every tile seeded
  independently (i.e., still reads `MAX_LIGHT_LEVEL` immediately outside the
  block, decaying by 1 per step outward) - the direct regression test that
  skipping interior seeds doesn't change the observable light field.
- **A single-tile-thick lava wall has no interior tiles and is unaffected**:
  a 1-tile-thick horizontal or vertical line of lava (every tile is a
  boundary tile, touching air on at least one side) should light identically
  before and after this change - confirms the interior check doesn't
  over-trigger on non-solid-blob shapes.

Verification (mandatory, not optional, given this fix's entire purpose is a
measured wall-clock improvement - same standard the design conversation
already set): re-run the same temporary `sf::Clock`-based timing
instrumentation used to diagnose this (around `fluids.tick` and the
lava-triggered `lighting.recomputeAll` call in `Game::fixedUpdate`, surfaced
via the window title, reverted before committing - not part of the shipped
code) against a freshly generated world, and report the actual before/after
`recomputeAll` per-call average. Full test suite (`Litharia_tests`) passing
is necessary but not sufficient for this cycle - the numeric before/after
comparison is the real gate.
