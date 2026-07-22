# Lighting: A Lava-Only Recompute Path for the Lava-Triggered Call Site

## Purpose

After the `floodFill` shared-search rewrite
(`docs/superpowers/specs/2026-07-22-floodfill-shared-search-design.md`)
brought `Lighting::recomputeAll` from ~6.6s down to ~167.5ms average per
call, the user reported the game still lag-spikes roughly every 3/4 of a
second. Systematic debugging (this session) added temporary per-channel
timing instrumentation directly in `recomputeAll` and measured live, over a
60-second run with sustained lava activity (101 calls):

| Phase | Total (101 calls) | Per call |
|---|---|---|
| **skyFlood** | 16,947ms | **~167.8ms** |
| lavaFlood | 953ms | ~9.4ms |
| skySeed | 668ms | ~6.6ms |
| lavaSeed | 380ms | ~3.8ms |
| forceSet | 145ms | ~1.4ms |
| torchSeed/Flood | 0ms | 0 (no torches placed) |

The sky channel's flood fill accounts for **~89% of the entire per-call
cost** — and this is on the *lava-triggered* recompute path
(`src/Game/Game.cpp:1039`, inside `if (lavaLightingChanged &&
lavaLightingCooldown <= 0.0f)`), which fires roughly every
`LAVA_LIGHTING_COOLDOWN_SECONDS` (0.5s) while lava is actively settling —
exactly matching the user's observed "~3/4 second" cadence (0.5s cooldown +
~0.19s of blocking work ≈ 0.69s, close enough given jitter). `recomputeAll`
unconditionally rebuilds and re-floods **all three** channels (sky, torch,
lava) every single call, but on this specific call path, sky and torch data
haven't changed at all — only lava moved.

## Design

### The mechanism: factor out a lava-only channel, add a lava-only recompute entry point

`recomputeAll`'s lava-channel logic (the interior-skip seed-building loop,
the `floodFill` call, and the force-set pass — currently
`src/World/Lighting.cpp:142-201`, roughly) moves into a new private helper:

```cpp
void Lighting::recomputeLavaChannel(const World& world)
{
    std::vector<LightSeed> lavaSeeds;

    // ...exactly the existing interior-skip seed-building loop...

    const auto lavaResult = floodFill(world, lavaSeeds);

    // Reset only the lava channel - sky/torch are untouched, unlike
    // recomputeAll's full std::fill(levels..., LightLevel{0,0,0}).
    for (LightLevel& level : levels)
        level.lava = 0;

    for (const auto& [tile, level] : lavaResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].lava =
            static_cast<std::uint16_t>(level);

    // ...exactly the existing force-set pass...
}
```

`recomputeAll` calls this helper for its own lava step — identical
behavior to today, just factored out; the sky and torch steps, and the
`std::fill` that zeroes all three channels before the sky/torch write-back
loops, are otherwise untouched. This means `recomputeAll`'s lava field gets
zeroed twice in a row when it calls through to `recomputeLavaChannel` (once
by the existing full-struct `std::fill`, once by the helper's own
lava-only zero loop) — a harmless, negligible redundancy confined to the
already-existing full-recompute path, not worth restructuring further to
avoid; the new `recomputeLava` fast path (below) only ever does the single
lava-only zero. A new public method:

```cpp
// Recomputes only the lava channel, leaving sky and torch untouched - for
// the lava-triggered call site (Game::fixedUpdate), which never needs to
// touch sky or torch since only lava moved. See docs/superpowers/specs/
// 2026-07-22-lighting-lava-only-recompute-design.md.
void recomputeLava(const World& world);
```

calls `recomputeLavaChannel` directly. It takes no `Machines` parameter -
lava recompute never touches torches, so there's nothing to look up.

### The one call-site change

`Game::fixedUpdate`'s lava-triggered block (`Game.cpp:1037-1041`):

```cpp
if (lavaLightingChanged && lavaLightingCooldown <= 0.0f)
{
    lighting.recomputeAll(world, machines);
    lavaLightingCooldown = LAVA_LIGHTING_COOLDOWN_SECONDS;
}
```

becomes:

```cpp
if (lavaLightingChanged && lavaLightingCooldown <= 0.0f)
{
    lighting.recomputeLava(world);
    lavaLightingCooldown = LAVA_LIGHTING_COOLDOWN_SECONDS;
}
```

The other two call sites are untouched, and deliberately so:

- `Game::Game()`'s constructor (`Game.cpp:106`) — startup needs every
  channel seeded from scratch (nothing has been computed yet).
- The `lightingDirty` block/Torch-edit path (`Game.cpp:1002`) — a placed or
  mined block can open or seal a sky column, and a placed or removed Torch
  obviously changes the torch channel; both remain plausible on this path,
  so it keeps calling full `recomputeAll`. This path is also not the one
  the user's reported stutter comes from (it fires on player-caused edits,
  not on a fixed ~0.5s cadence), so there's no performance case for
  narrowing it in this cycle.

### Why this doesn't change `recomputeAll`'s behavior, and the one known limitation where it changes `recomputeLava`'s

`recomputeLavaChannel`'s body is `recomputeAll`'s existing lava logic,
moved, not rewritten — same interior-skip condition, same `floodFill` call,
same force-set pass, same conservative "zero then reflood" pattern (just
scoped to one channel's field instead of the whole `LightLevel` struct).
`recomputeAll` calling through this helper produces byte-for-byte identical
output to today, for all three channels, on every call site that still uses
it.

`recomputeLava`'s only behavioral difference from `recomputeAll` is that it
doesn't touch `.sky`/`.torch` at all — correct for the vast majority of what
triggers its one call site (lava *movement*: falling, spreading, leveling,
none of which change any tile's solidity), but **not entirely correct**:
`lavaLightingChanged` (`Game.cpp:1022-1030`) also fires when a fluid change
produces **Obsidian** (lava reacting with water), and Obsidian is solid
(`Blocks.cpp`) where the lava/water it replaced was not. A newly-solid
Obsidian tile can occlude a sky or torch light path that used to run through
that spot - `recomputeLava` won't refresh sky/torch to reflect that new
occlusion, leaving them transiently **over-bright** (showing the more-open
pre-Obsidian light) until the next full `recomputeAll` - which happens on
the very next block or Torch edit (`Game.cpp:1002`, the `lightingDirty`
path), a common, frequent event in normal play. This is a purely cosmetic,
self-healing staleness window, not a crash or a reintroduction of the
performance problem this cycle fixes - accepted as a reasonable tradeoff
for this cycle rather than adding solidity-change tracking to correctly
distinguish "lava moved" from "lava became Obsidian" on this path, which
would need its own design pass if the staleness window ever proves
noticeable in practice.

## Rejected alternative: a dirty-flag parameter on recomputeAll

Instead of a new method, add a `bool recomputeSky = true, recomputeTorch =
true` (or similar) parameter set to `recomputeAll`, and have the
lava-triggered call site pass `false, false`. Functionally equivalent, but
a boolean-flag signature invites future misuse (a caller forgetting to
pass the right flags, or the meaning of "true/true/false" becoming unclear
at a call site without reading the declaration) in a way a named method
(`recomputeLava`) doesn't. Rejected in favor of the clearer, narrower
public method.

## Out of scope

- **Distinguishing "lava moved" from "lava became Obsidian" on the
  lava-triggered path**, so that only genuine occlusion changes fall back
  to a full `recomputeAll` - see the known limitation in Design above. A
  future cycle if the transient over-bright staleness this causes ever
  proves noticeable in practice; not pursued now since it's cosmetic,
  self-healing on the next block edit, and adding it now would mean
  tracking solidity-change state through `FluidSim`'s `changedTiles` batch,
  a real (if small) increase in scope and risk for a low-observed-impact
  edge case.
- The block/Torch-edit path (`Game.cpp:1002`) keeps calling full
  `recomputeAll` — see Design above for why.
- `floodFill` itself, the interior-lava-seed skip, and the force-set pass's
  own logic are all unchanged — only *where* the lava logic lives and *when*
  sky/torch get touched change.
- Any further reduction of `recomputeLava`'s own ~14.6ms cost (seed-building
  + flood + force-set) — a reasonable future target if it's ever still
  noticeable, but this cycle's whole point is eliminating the ~167.8ms of
  now-provably-unnecessary sky work on this path, not further optimizing
  the lava work itself.

## Testing

`tests/test_lighting.cpp`'s existing `recomputeAll`-based tests are
untouched and must keep passing unmodified — they exercise the still-intact
full-recompute path, now indirectly via `recomputeLavaChannel`, so any
regression in the moved logic would show up there.

New coverage to add, directly against the new `recomputeLava` method:

- **`recomputeLava` updates the lava channel and leaves sky/torch alone**:
  call `recomputeAll` once (establishing some sky and torch state via a
  placed Torch and an open shaft), then call `recomputeLava` after moving
  a lava tile - assert the lava channel reflects the new lava position
  while `skyLight`/`torchLight` at the same tiles are unchanged from the
  first call. This is the direct regression test for "recomputeLava
  touches only `.lava`."
- **`recomputeLava` alone (never preceded by `recomputeAll`) still lights
  lava correctly**: on a fresh `Lighting`, call `recomputeLava` directly
  (skipping `recomputeAll` entirely) and confirm `lavaLight` reads
  correctly for a lone lava tile - confirms the extracted helper doesn't
  implicitly depend on anything `recomputeAll`'s sky/torch steps set up.
- **Cross-call consistency**: call `recomputeAll` then `recomputeLava` then
  `recomputeAll` again on the same `Lighting` instance with the same world
  state, and confirm the lava channel reads identically after the second
  `recomputeAll` as it did after the `recomputeLava` call in between - the
  direct regression test for "the factored-out helper produces the exact
  same result whichever entry point calls it."

Mandatory empirical verification, same standard as the prior two cycles:
re-run the same per-channel `sf::Clock` timing instrumentation technique
used to diagnose this (temporary, reverted before finishing, not
committed) against a freshly generated world with active lava, and report
the new `lightMs`-equivalent for the lava-triggered path specifically -
expect it to now track close to the `lavaSeed + lavaFlood + forceSet`
figures already measured (~14.6ms), not the ~167-189ms total. State
plainly whether the recurring stutter is resolved or, if not, why not,
rather than assuming success from the code change alone.
