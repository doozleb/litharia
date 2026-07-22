# LightRenderer: Stop Rebuilding Hash Maps Every Frame

## Purpose

This is item #1 (and the related item #17) from the whole-game optimization
audit (`docs/superpowers/specs/2026-07-22-optimization-audit-design.md`),
flagged there as the single largest per-frame cost in the game.

`LightRenderer::draw` (`src/World/LightRenderer.cpp:97-242`) runs every
single rendered frame, unconditionally, from `Game::render`
(`src/Game/Game.cpp:1152-1153`). At the top of every call it builds two
`std::unordered_map<std::int64_t, int>`s from scratch — `heldMap` from
`heldTorchLight`, `outlineMap` from `ambientOutline`
(`LightRenderer.cpp:115-124`) — then, for every *solid* visible tile, probes
`heldMap` up to `WALL_PENETRATION_DEPTH * 8 = 24` times via the `torchAt`
lambda's wall-penetration search (`:152-176`), plus one more `heldMap`
lookup and one `outlineMap` lookup per tile overall (`:194`, `:227`).

`ambientOutline` runs unconditionally every frame and, per
`Game::render`'s own comment (`Game.cpp:1131-1147`), is sized to cover the
*entire current view* whenever a Torch is equipped — potentially thousands
of entries. Building `heldMap`/`outlineMap` from that many entries means
thousands of individually heap-allocated hash-map nodes every frame, on top
of the hashing cost of the lookups themselves — most of which land in the
underground/mining case, where a large fraction of visible tiles are solid
and therefore pay the full 24-probe search.

This mirrors exactly the problem the prior lighting-perf spec
(`docs/superpowers/specs/2026-07-22-lighting-perf-design.md`) fixed inside
`Lighting` itself (per-call full-world scratch allocation) — except here the
per-frame cost lives one layer up, in how `LightRenderer` consumes
`Lighting`'s already-cheap output. That prior spec explicitly flagged this
file as an unaudited follow-up; this spec is that follow-up.

## Design

### The mechanism: the same generation-stamp trick, one layer up

`LightRenderer` gains persistent scratch state, sized once (in a new
constructor, mirroring `Lighting`'s own) rather than rebuilt per call:

```cpp
// For heldTorchLight lookups (torch level, 0-TORCH_LIGHT_LEVEL fits in int8_t):
mutable std::vector<std::uint32_t> heldStamp;  // WORLD_WIDTH * WORLD_HEIGHT
mutable std::vector<std::int8_t>   heldValue;  // WORLD_WIDTH * WORLD_HEIGHT
mutable std::uint32_t              heldGeneration = 0;

// For ambientOutline lookups (outline level, 0-AMBIENT_OUTLINE_ORE_LEVEL fits in int8_t):
mutable std::vector<std::uint32_t> outlineStamp;  // WORLD_WIDTH * WORLD_HEIGHT
mutable std::vector<std::int8_t>   outlineValue;  // WORLD_WIDTH * WORLD_HEIGHT
mutable std::uint32_t              outlineGeneration = 0;
```

Each call to `draw()` increments both generation counters instead of
clearing the maps. Filling the scratch from the input vectors becomes plain
indexed writes instead of hashed inserts:

```cpp
++heldGeneration;
for (const auto& [tile, level] : heldTorchLight)
{
    const std::size_t idx = static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x;
    heldStamp[idx] = heldGeneration;
    heldValue[idx] = static_cast<std::int8_t>(level);
}
```

`heldTorchLight` never contains the same tile twice — `floodFill`'s BFS
"improves a tile at most once" invariant (`Lighting.h:133-141`) guarantees
this — so plain overwrite is correct and matches `heldMap`'s current
behavior (`heldMap[tileKey(...)] = level;`, an unconditional assign).
`ambientOutline` *can* list the same solid border tile more than once (once
per open neighbour that reaches it), so `outlineValue` reproduces the
existing `std::max` merge instead of a plain overwrite:

```cpp
++outlineGeneration;
for (const auto& [tile, level] : ambientOutline)
{
    const std::size_t idx = static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x;
    if (outlineStamp[idx] != outlineGeneration)
    {
        outlineStamp[idx] = outlineGeneration;
        outlineValue[idx] = static_cast<std::int8_t>(level);
    }
    else
    {
        outlineValue[idx] = std::max(outlineValue[idx], static_cast<std::int8_t>(level));
    }
}
```

Lookups become array reads gated by the stamp, with the same
`x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT` bounds check
`Lighting::skyLight`/`torchLight`/`lavaLight` already use
(`Lighting.cpp:173-193`) — necessary here because, unlike those accessors,
the wall-penetration search's offset tiles (`torchAt`'s `nx, ny`) can land
outside `[0, WORLD_WIDTH) x [0, WORLD_HEIGHT)` near the world's edges (a
tile at `firstX` with `dx = -3` reaches `x = -3`):

```cpp
int heldLevelAt(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    const std::size_t idx = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
    return (heldStamp[idx] == heldGeneration) ? heldValue[idx] : 0;
}
```

`torchAt`'s `heldMap.find(...)` call becomes `heldLevelAt(nx, ny)`; the
standalone `heldMap`/`outlineMap` lookups at `LightRenderer.cpp:194` and
`:227` become the equivalent `heldLevelAt(x, y)` / `outlineLevelAt(x, y)`
calls. This also folds in audit item #17 (redundant `tileKey(x, y)`
recomputation at `:194`) for free — there's no `tileKey` at all anymore,
just direct array indexing.

`draw()` keeps its exact signature and stays `const` — the new scratch
members are `mutable`, the same standard already established by `Lighting`
for implementation-detail caches that don't affect a `const` method's
observable behavior.

### Sizing and memory

Four new vectors: two `uint32_t` stamps and two `int8_t` value arrays, each
`WORLD_WIDTH * WORLD_HEIGHT` (500,000) elements — `(4+1) * 2 * 500,000` ≈
5MB, allocated once at `LightRenderer` construction (mirrors `Game`'s
existing `lighting` member construction) instead of thousands of per-frame
node allocations.

### Generation wraparound

Same handling as the prior lighting-perf spec: `heldGeneration`/
`outlineGeneration` are `std::uint32_t`, incremented at most a few times per
rendered frame (well under 60/sec), so wraparound is on the order of years
of continuous play — implausible, but cheap to close off anyway. If
incrementing either counter ever produces `0` (the sentinel stamps start
at), that buffer's `stamp` vector is cleared once and the counter restarts
at `1`.

### Why this doesn't change any observable behavior

The brightness/tint math in `draw()` (the blend, the wall-penetration
`std::max` search, the fallback to the outline floor) is completely
untouched — only how "is tile `(x,y)` present in this frame's
`heldTorchLight`/`ambientOutline`, and at what level" gets answered changes,
from a hash lookup to a stamped array read. Both are exactly equivalent
views of the same information, including the existing merge behavior for
duplicate `ambientOutline` entries.

## Rejected alternative: bounding-box-local scratch sized to the viewport

Size the scratch arrays to the current view (plus `WALL_PENETRATION_DEPTH`
padding) instead of the full world, with local coordinate translation.
Smaller footprint per call, but still needs a per-frame allocation (or a
persistent buffer resized/re-translated whenever the view moves or the
window resizes) and adds coordinate-translation bookkeeping the full-world
version doesn't need. Rejected in favor of the full-world generation-stamp
approach for the same reason the original `Lighting` spec rejected the
equivalent local-buffer option: a fixed one-time cost beats continuous
per-frame churn, for less implementation complexity, and it keeps
`LightRenderer`'s new scratch state structurally identical to `Lighting`'s.

## Out of scope

- **`Lighting::floodFill`'s own remaining per-call allocations** (audit
  item #7: the missing `reserve()` on its returned vector, and the
  per-seed `reached`/`localQueue` scratch not covered by the existing
  `floodStamp`/`floodBest` fix) — a separate backlog item, independent of
  this one.
- **Any other audit backlog item** (chunk/fluid rebuild cost, machine
  draw/tick scaling, etc.) — each gets its own cycle.
- Any change to brightness constants, tint colors, decay rates, or the
  wall-penetration depth/penalty curve — purely a performance change to how
  `LightRenderer` looks up per-tile light data internally.

## Testing

No existing test file covers `LightRenderer` (`tests/` has
`test_lighting.cpp` for `Lighting` itself, but nothing rendering-specific) —
confirm this before writing tests, and if true, add
`tests/test_light_renderer.cpp` covering:

- **Cross-call isolation**: call `draw()` (or a testable seam around the
  lookup logic) twice with different `heldTorchLight`/`ambientOutline`
  inputs and assert the second call's lookups don't retain anything from
  the first — the direct regression test for the generation stamp actually
  invalidating stale entries.
- **Duplicate-tile merge**: feed `ambientOutline` the same tile twice at
  different levels and assert the higher level wins, matching the current
  `std::max` merge behavior.
- **Out-of-bounds offset lookup**: assert `heldLevelAt`/`outlineLevelAt`
  return `0` for coordinates outside `[0, WORLD_WIDTH) x [0, WORLD_HEIGHT)`,
  since the wall-penetration search can generate such coordinates near
  world edges.

Since `draw()` itself needs an `sf::RenderTarget` and full `World`/
`Lighting` setup, the lookup logic may need extracting into a small
testable helper (e.g. a private struct or free functions taking the
stamp/value vectors directly) rather than testing through `draw()`'s full
SFML rendering path — follow whatever seam `test_lighting.cpp` already
establishes for testing `Lighting`'s private BFS logic, if any, otherwise
keep the helper package-private the same way.

Verification: existing test suite passing (`ctest`), plus a manual in-game
check that frame time underground with a Torch equipped, moving through
open caverns, is visibly smoother — same manual-check caveat as the prior
lighting-perf spec, since there's no profiler/benchmark harness in this
repo yet (see the audit doc's "out of scope" section).
