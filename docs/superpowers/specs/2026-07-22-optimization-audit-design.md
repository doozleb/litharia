# Whole-Game Optimization Audit

## Purpose

A code-review-based sweep of the entire game (~8,600 lines across World,
Machines, Hud, Player/Physics, Game) for performance anti-patterns, requested
as a general audit rather than a response to a specific reported symptom. No
profiler is available in this repo, so every finding below comes from reading
the code against a fixed checklist rather than measured timings.

This doc is a **backlog, not an implementation plan**. Each finding gets its
own design → plan → implementation cycle later, the same way the recent
lighting-perf work was done (`docs/superpowers/specs/2026-07-22-lighting-perf-design.md`).
Nothing in this doc has been implemented yet.

## Methodology

- The world is fully resident in memory: `WORLD_WIDTH=1000 * WORLD_HEIGHT=500`
  = 500,000 tiles (`src/Core/Constants.h`), chunked into `32x32` tiles.
  Physics/gameplay run a fixed 60Hz update; rendering targets 60fps
  (`window.setFramerateLimit(60)`, `src/Game/Game.cpp:99`).
- I read `src/Game/Game.cpp` myself first (it owns the frame loop and calls
  into every other subsystem), then dispatched one focused read-through per
  remaining subsystem, each checked against the same checklist: per-frame/
  per-tick heap allocations, O(n²) or unbounded loops over world-sized data,
  redundant recomputation, unnecessary large-container copies, and hot-path
  string/stream formatting.
- Findings already fixed by the in-flight lighting-perf work (generation-
  stamped `floodFill`/`ambientOutline` internal scratch buffers) were
  explicitly excluded from re-reporting; a couple of *new*, related findings
  the prior pass didn't cover turned up anyway (see Lighting section).
- Each finding lists: location, what's wrong, why it matters (frequency ×
  rough cost), and a fix direction — not a full design. Severity is
  **High** (runs every frame/tick unconditionally, or scales badly with
  normal play, e.g. a growing factory), **Medium** (runs often but bounded,
  or only bites under specific conditions like a large fluid body), or
  **Low** (small/rare enough that fixing it is polish, not urgency).

## Priority Backlog

Ordered by estimated impact, highest first. Items in the same tier are
peers, not strictly ordered against each other.

### Tier 1 — High impact, worth tackling first

1. **`LightRenderer::draw` rebuilds two `unordered_map`s from scratch every
   frame** (`src/World/LightRenderer.cpp:115-124`), then does up to 24
   hashed lookups per *solid* visible tile for the wall-penetration search
   (`LightRenderer.cpp:152-176`). `ambientOutline` runs unconditionally
   every frame and can be thousands of entries wide (screen-sized radius
   while a Torch is held, `Game.cpp:1140-1150`) — so this is thousands of
   individually-heap-allocated hash-map nodes, plus (visible-solid-tiles × 24)
   hash lookups, every single frame. This was flagged but explicitly left
   unaudited by the prior lighting-perf spec; the audit confirms it's likely
   the single largest per-frame cost in the game. Fix direction: replace the
   `unordered_map`s with a dense, viewport-sized array (or reuse a
   generation-stamped scratch grid like `Lighting`'s own) so lookups become
   array reads instead of hashing.

2. **Any chunk touched by flowing fluid gets a full mesh rebuild every
   frame it keeps flowing**, and that rebuild is itself quadratic in the
   fluid run. `Chunks::markDirty` only flags a whole 32×32 chunk
   (`src/World/Chunks.cpp:55-64`), `draw()` rebuilds it wholesale
   (`Chunks.cpp:72-123`), and `Game::fixedUpdate` calls `markDirty` for
   every tile `fluids.tick` reports changed (`Game.cpp:1011-1025`) —
   `FluidSim` internally steps at a fixed 20Hz (`TICK_INTERVAL=0.05s`,
   `src/World/FluidSim.h:37`, gating the 60Hz game tick down), so a chunk
   with active water/lava near the player rebuilds in full up to ~20
   times a second for as long as the flow continues, not once per real
   change. Each of those rebuilds pays `FluidSurface::
   fluidSurfaceHeight`'s cost (`src/World/FluidSurface.cpp:16-65`): every
   fluid-surface tile independently rescans up to 128 tiles left/right and
   re-averages, instead of computing a run's value once and sharing it —
   for a 32-wide dirty row through a wide lake this is on the order of
   10K+ extra tile reads per rebuild. These two compound directly. Fix
   direction: move `fluidSurfaceHeight` to a single per-run sweep computed
   once per rebuild; separately, consider throttling reactive rebuilds for
   chunks with continuously-active fluid rather than rebuilding same-frame
   on every tick's dirty mark.

3. **`FluidSim::scanRun` is an unbounded, uncapped horizontal scan re-run
   per tile in a resting run** (`src/World/FluidSim.cpp:198`, called from
   `equalizeAt`/`canMove`), unlike `FluidSurface` which caps at
   `MAX_RUN_SCAN=128`. For a wide settling body (up to `WORLD_WIDTH`=1000
   tiles) this is O(width²) in a single step — worst exactly when a large
   generated lava pool (the world generates several) is still settling,
   which is also when `Lighting`'s lava-triggered recompute is already
   running. Fix direction: compute each row's min/max once per step and
   share it across every tile in that run instead of rescanning per tile.

4. **`MachineRenderer::draw` has no viewport culling** — it loops over
   every placed machine unconditionally (`src/Machines/MachineRenderer.cpp:83`),
   unlike `chunks.draw` right above it in `Game::render`, which is
   view-aware. Cost is O(total machine count), not O(visible machine
   count), so it gets worse the more a player builds — exactly the failure
   mode a factory-building game should expect regular play to hit. The
   same loop also constructs two `sf::RectangleShape`s fresh per active
   machine per frame instead of reusing them like its sibling shapes do
   (`MachineRenderer.cpp:117,124`). Fix direction: skip machines outside
   `camera.view()` before drawing; hoist the two bar shapes above the loop.

5. **`Machines::tick` does five full O(total machine count) passes every
   60Hz tick** (`updatePower`, `tickGenerators`, `tickDrills`,
   `tickSmelters`, `tickTransport` — `src/Machines/Machines.cpp:569-576`),
   each re-filtering the whole flat machine list by type, and
   `updatePower` additionally allocates two fresh vectors and fully
   re-sorts by placement order every tick regardless of whether placement
   changed (`Machines.cpp:325-348`). Same growth concern as #4: cost scales
   with total factory size, not with what's actually doing work that tick.
   Fix direction: maintain per-type index lists populated at
   `place()`/`remove()` time so each tick phase only touches machines of
   its own kind; cache the placement-order list and update it
   incrementally instead of re-sorting from scratch.

6. **`Game::fixedUpdate` builds a window-title string and calls
   `window.setTitle` unconditionally every tick** (`Game.cpp:1043-1055`) —
   several heap-allocating string concatenations plus an OS call
   (`SetWindowTextW`-equivalent), 60 times a second, regardless of whether
   machine count, drop count, or held item actually changed since last
   tick. Purely wasted work with no gameplay dependency. Fix direction:
   cache the last-set values and only rebuild/call `setTitle` when one of
   them actually changes.

### Tier 2 — Real but more contextual/moderate

7. **`Lighting::floodFill` still allocates two per-call scratch vectors
   the prior lighting-perf fix didn't reach**: the returned `result`
   vector has no `reserve()` (unlike `ambientOutline`'s, which does — `
   Lighting.cpp:34` vs `:259`), and the per-seed `reached`/`localQueue`
   buffers are fresh locals every call, every seed (`Lighting.cpp:67-68`),
   not covered by the generation-stamped `floodStamp`/`floodBest`
   replacement. `heldTorchLight` calls this every frame a Torch is
   equipped. Fix direction: `reserve()` `result` based on the seed
   radius's disk size; promote `reached`/`localQueue` to persistent,
   generation-stamped members the same way `floodStamp` was.

8. **`FluidSim::step` discards its accumulated scratch-vector capacity
   every call** (`src/World/FluidSim.cpp:71-72`) — the `toProcess`/`active`
   swap uses a fresh local instead of a persistent member, so `active`
   rebuilds via repeated reallocation from empty every step (up to 20Hz,
   per `TICK_INTERVAL`), worst during a large pool's settle when thousands
   of tiles queue up. Fix direction: make the scratch buffer a persistent
   member, `clear()` it instead of swapping in a throwaway local.

9. **`Game::fixedUpdate`'s `fluidChanges` vector is a fresh local every
   tick** (`Game.cpp:1011`) instead of a reused member buffer, even on the
   majority of ticks where `fluids.tick` returns early (below its 20Hz
   `TICK_INTERVAL`) and it stays empty. Small on its own; same category as
   #8. Fix direction: hoist to a persistent scratch member, `clear()` each
   tick.

10. **`Chunks::rebuild` doesn't `reserve()` its vertex array before
    refilling** (`Chunks.cpp:74`) — compounds with #2's rebuild frequency:
    every rebuild past prior capacity re-triggers the underlying vector's
    growth/copy. Fix direction: `reserve(CHUNK_SIZE*CHUNK_SIZE*12)` once
    after construction.

11. **`Physics::restsInDeepFluid` scans an uncapped column down to
    `y=0`** (`src/Physics/Physics.cpp:132-146`), triggered on every jump
    input while grounded. Only needs to distinguish depth>1 from
    depth<=1, so a deep fluid column (up to `WORLD_HEIGHT`=500) gets fully
    walked for no reason. Low frequency (jump presses, not every tick) but
    trivially capped. Fix direction: stop the scan after 2 tiles.

### Tier 3 — Low impact / polish, defer until the above lands

12. `Hud::drawMachineTooltip` rebuilds a line list and several strings
    every frame while hovering a machine, even if its state hasn't
    changed (`src/Hud/Hud.cpp:719-796`). Small allocations only; the
    2026-07-17 HUD text-caching effort already covers all the geometry-
    heavy cases and remains intact (verified — no regressions found in
    code added since, including the newer health-tooltip feature).
13. `Hud::drawBuildPalette` re-scans the inventory for every `MachineType`
    every frame while build mode is open (`Hud.cpp:630-640,692`) — ~800
    trivial `O(40)` scans/frame at current item-count scale.
14. `Hud::drawCraftPanel` recomputes ingredient affordability every frame
    the panel is open (`Hud.cpp:801-857`) — negligible at current recipe
    counts.
15. Per-frame `sf::RectangleShape` construction in `Hud::drawSlot` and
    swatch/button loops instead of hoisting — the original HUD caching
    spec measured this as not worth it at current scale.
16. `TerrainGenerator::surfaceHeight` is recomputed uncached across
    multiple generation passes (`TerrainGenerator.cpp:142`) — startup-only
    cost (runs once at world-gen), thousands of redundant noise hashes but
    not seconds of wall-clock time.
17. Minor redundant `tileKey` recomputation in `LightRenderer::draw`
    (`LightRenderer.cpp:194`) — folds into whatever replaces the Tier 1 #1
    lookup structure anyway.

## Out of scope for this audit

- `Chunks.cpp`'s view-culling and world-gen's O(world-size) full-pass
  algorithms (`carveCaves`, etc.) — expected, not pathological, costs for
  what they do.
- `Physics.cpp`'s general tile-collision scanning, `Camera::follow`,
  `Inventory`/`ItemStack` value semantics, `ItemEntity` — reviewed, no
  issues found; these are already correctly bounded to actual footprint.
- Anything already fixed by the in-flight lighting-perf generation-stamp
  work (`docs/superpowers/specs/2026-07-22-lighting-perf-design.md`) or the
  2026-07-17 HUD text-caching work.
- Build/compiler-level optimization (optimization flags, LTO, PCH) — this
  audit is about algorithmic/allocation patterns in game code, not build
  configuration.
- Actual measurement/profiling — no profiler was available for this pass;
  findings are inferred from code structure and call frequency, not
  timed. Worth setting up a lightweight in-game frame-time overlay or
  profiler hookup as a follow-up so future fixes (and this doc's priority
  order) can be verified against real numbers instead of estimates.

## Suggested order of attack

Tier 1 items are independent of each other and can be tackled in any
order; #1 (LightRenderer maps) and #2 (chunk rebuild + fluid surface scan)
are the two most likely to be *felt* directly during normal play (moving
underground with a Torch; mining near water/lava), so starting there gives
the most noticeable improvement soonest. #4/#5 (machine draw/tick scaling)
matter most as a save file's factory grows, so they're worth doing before
they become a visible problem rather than after. #6 (window title) is a
trivial, zero-risk fix and could be picked up any time as a warm-up.
