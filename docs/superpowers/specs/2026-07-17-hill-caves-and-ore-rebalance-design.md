# Hill caves and iron/copper depth rebalancing

Date: 2026-07-17

## Purpose

Today's caves are a single pass of 2D noise-threshold "swiss cheese" — no
guaranteed structure, no guaranteed entrances near spawn. This adds 4
hand-placed cave systems, each anchored on a real hill peak, each a winding
branching tunnel reaching down to the iron layer: one near spawn on each
side (a short walk away), and one far from spawn on each side (a proper
trek). Everywhere else in the world, cave generation is unchanged.

Alongside this, iron becomes occasionally findable shallow and copper
occasionally findable deep — each ore's normal depth band is untouched, but
a second, much rarer band now lets an ore turn up outside its usual zone.

## Components

### 1. Cave placement — hill anchoring

Four target X offsets from `spawnX = WORLD_WIDTH / 2` (500):

```cpp
// TerrainGenerator.h
static constexpr int SPECIAL_CAVE_NEAR_OFFSET = 100; // ~7-12s run from spawn
static constexpr int SPECIAL_CAVE_FAR_OFFSET = 350;  // ~25-40s run from spawn

static constexpr int HILL_SEARCH_RADIUS = 30;
```

Target X positions: `spawnX - 350`, `spawnX - 100`, `spawnX + 100`,
`spawnX + 350` → 150, 400, 600, 850. Each stays comfortably clear of the
world edges (0 and 999) even after the ±30 search window.

For each target, `findHillPeak(int targetX) const` scans
`[targetX - HILL_SEARCH_RADIUS, targetX + HILL_SEARCH_RADIUS]` and returns
the x with the smallest `surfaceHeight()` (smaller y = higher elevation, the
same convention `surfaceHeight` already uses). Ties break toward the first x
found — deterministic, no special handling needed. The cave entrance is
carved at that peak, not the raw target x, so it sits visibly on a hilltop.

### 2. Cave carving — trunk + branches random walk

```cpp
// TerrainGenerator.h
static constexpr float SPECIAL_CAVE_RADIUS = 2.5f;

static constexpr int TRUNK_MAX_STEPS = 3000; // loop-safety cap, not a target

static constexpr int BRANCH_MIN_COUNT = 3;
static constexpr int BRANCH_MAX_COUNT = 6;
static constexpr int BRANCH_MIN_STEPS = 15;
static constexpr int BRANCH_MAX_STEPS = 40;

static constexpr std::uint32_t SALT_SPECIAL_CAVE = 0x8000u;
```

`carveSpecialCaves(World& world) const`, called from `generateBase` right
after the existing `carveCaves` pass (so ore scattering still only ever
sees Stone where these tunnels didn't reach):

- For each of the 4 caves (indexed 0-3), find its hill peak, then walk a
  **trunk**: starting a couple tiles under the surface, each step carves a
  circle of radius `SPECIAL_CAVE_RADIUS` (same rule as ore veins — never
  overwrites Air, only Stone/Dirt) and moves one tile. `dy` is weighted
  downward (65% down, 20% flat, 15% up) so the trunk reliably descends;
  `dx` is a uniform `{-1, 0, 1}` meander. The trunk stops the first step its
  y reaches `IRON_MIN_Y` (320) — "down to iron layer minimum." `TRUNK_MAX_STEPS`
  exists purely so a pathological random stream can't loop forever; the
  downward bias means it is never expected to bind.
- Along the way, the trunk records every step position. Once it's done, a
  random count in `[BRANCH_MIN_COUNT, BRANCH_MAX_COUNT]` of **branches** are
  spawned from random points on that recorded path. A branch walks with
  `dx` and `dy` both uniform `{-1, 0, 1}` (no downward bias — it wanders
  freely) for a random length in `[BRANCH_MIN_STEPS, BRANCH_MAX_STEPS]`
  steps, carving the same radius, then simply stops. That stop is the dead
  end. Branches don't spawn further branches — one level deep, so the
  system stays bounded.
- All randomness is `noise::hashFloat(x, y, seed)` calls, following the
  file's existing pure-function-of-seed pattern: the step index goes in
  `x`, a small integer "lane" tag (which random decision this is — trunk
  dy, trunk dx, branch start pick, branch dy, branch dx) goes in `y`, and
  the cave index is folded into the seed (`worldSeed + SALT_SPECIAL_CAVE +
  caveIndex * 997u`) so the 4 caves' walks never correlate with each other.

### 3. Ore rebalancing — a second, rare band per ore

Rather than widening `IRON_MIN_Y`/`COPPER_MAX_Y` (which would silently
weaken the existing "each ore stays inside its own depth band" test and
blur what "the iron layer" means for the cave-depth target above), add a
second band per ore at reduced density, reusing `scatterOre`'s existing
band/density/cell-hash mechanism as-is:

```cpp
// TerrainGenerator.h
static constexpr int IRON_SHALLOW_MIN_Y = 90;
static constexpr int IRON_SHALLOW_MAX_Y = 319; // just above IRON_MIN_Y, no overlap

static constexpr int COPPER_DEEP_MIN_Y = 341; // just below COPPER_MAX_Y, no overlap
static constexpr int COPPER_DEEP_MAX_Y = 495;

constexpr std::uint32_t SALT_IRON_SHALLOW = 0x4001u;
constexpr std::uint32_t SALT_COPPER_DEEP = 0x3001u;
```

`IRON_DENSITY` and `COPPER_DENSITY` stay exactly as they are for the
existing (common) bands. Two more entries go in `scatterOre`'s `ores[]`
array, at `IRON_DENSITY / 5.0f` and `COPPER_DENSITY / 5.0f` — a ~5x rarer
roll, tuned to feel like a real "jackpot" find rather than a routine one.
`IRON_MIN_Y`/`MAX_Y` and `COPPER_MIN_Y`/`MAX_Y` are untouched, so every
existing test and the cave-depth target above keep meaning exactly what
they mean today.

The two new bands are non-overlapping with their ore's common band (319/320
and 340/341 are the seams), so no tile is ever double-rolled by both bands
for the same ore.

## Data flow summary

`generateBase` becomes: `generateSurface` → `carveCaves` (unchanged, all
columns) → `carveSpecialCaves` (new, only touches the 4 hill windows and
their tunnels downward). `scatterOre` runs after both cave passes exactly as
today, so it still only ever places ore in Stone — the special tunnels
correctly block ore from spawning inside them, exactly like the noise caves
already do. `scatterOre` itself just gains 2 more `Ore` entries in its
existing loop; no change to `growVein` or the cell-hash mechanism.

## Error handling / invariants

- `findHillPeak`'s search window (±30) plus the closest target (150 from
  spawn) never comes within 90 tiles of a world edge, so no bounds-clamping
  is needed there.
- `world.set`/`world.get` are already bounds-safe (existing invariant,
  unchanged); the trunk/branch walk doesn't need its own bounds checks
  beyond what those calls already provide, though a walk is expected to
  stay well inside `[0, WORLD_WIDTH) x [0, WORLD_HEIGHT)` given the offsets
  and step caps above.
- A branch's carve, like a trunk's, only ever overwrites Stone/Dirt — it
  can carve into another branch's or the trunk's own tunnel (that's fine,
  tunnels are allowed to reconnect) but never into ore, since ore isn't
  placed yet at this pass.
- `TRUNK_MAX_STEPS` is a defensive loop bound only; if it ever binds in
  practice that's a sign the downward-bias weighting needs revisiting, not
  a normal code path.

## Testing

`TerrainGenerator.cpp` is core (no SFML), linked into `Litharia_tests` and
already covered by `tests/test_terrain.cpp` — this feature gets real
doctest coverage, not just a manual check:

- **Determinism**: existing "the same seed produces a byte-identical world"
  keeps passing unmodified — the new passes are pure functions of the seed
  like everything else.
- **New: each of the 4 target windows has a hill entrance** — the surface
  height at the found peak is at or below (i.e. at least as elevated as)
  every other point in its search window.
- **New: each of the 4 target windows reaches iron depth** — there exists
  at least one air tile at `y >= IRON_MIN_Y` within that window's column
  range.
- **New: rare-band ore exists but stays rare** — shallow iron count and deep
  copper count are both `> 0` but small relative to their common-band
  counts (e.g. under 1/2 of the common count, given a 5x density cut).
- Existing "each ore stays inside its own depth band", "ore tiles only ever
  replace stone", "iron sits deeper than copper on average", and "caves
  carve air below the surface" all keep passing unmodified — `IRON_MIN_Y`/
  `MAX_Y` and `COPPER_MIN_Y`/`MAX_Y` didn't move, and the special caves
  only add more air, they don't remove any of the invariants those tests
  check.
- 218 existing tests stay green throughout.

## Out of scope

- Any change to the general swiss-cheese noise cave pass (`carveCaves`)
  outside the 4 special windows — "rest of world do it as usual."
- Multi-level branching (a branch spawning its own sub-branches).
- Coal's depth band or density — untouched.
- Widening the existing `IRON_MIN_Y`/`COPPER_MAX_Y` common bands — the rare
  bands are separate constants precisely so the common bands, and the tests
  and cave-depth target that depend on them, don't shift.
- Any visual/UI change (minimap, cave markers) pointing the player at the
  cave locations — the player finds them by walking and looking for hills,
  same as any other terrain feature today.
