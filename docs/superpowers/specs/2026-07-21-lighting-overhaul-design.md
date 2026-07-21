# Lighting Overhaul: Real Occlusion, Colored Light, Ambient Outlines

## Purpose

The lighting feature landed (day/night cycle, Torch, per-tile sky/block light,
a `BlendMultiply` darkness overlay) but two problems make it read as broken
rather than atmospheric:

1. **You can see through walls.** A same-day bug-fix (`cb2a9eb`) made
   `LightRenderer` skip solid tiles entirely, because darkening every tile
   uniformly made all unmined ground render pitch black. The fix was correct
   for the symptom but removed the *only* thing gating solid-terrain
   visibility: `ChunkRenderer` draws every tile in the camera's view
   unconditionally, so with `LightRenderer` no longer touching solid tiles at
   all, ore veins, cave shapes, and room layouts are always fully visible at
   full brightness the instant they're in view - independent of light,
   distance, or whether you've ever mined anywhere near them. Only empty air
   pockets are still hidden by darkness.
2. **Everything reads as one grey tone.** `MAX_LIGHT_LEVEL` was shortened from
   8 to 3, crushing the brightness gradient into three visible steps.
   Torch and Lava also share a single `block` channel and an identical tint
   (`BLOCK_TINT`), so a lava pool and a torch look the same, and the
   sky/block tint choice is a hard "whichever's higher wins" switch with no
   blending - there's no gradient of hue, only a gradient of grey.

This spec fixes both without touching Torch's item/recipe/day-night-clock
plumbing (all correct as-is) or reopening the recompute-trigger/rate-limit
logic from `cb2a9eb`/`cbb8485` (also correct as-is).

Confirmed with the project owner: no "explored tile memory" (a tile that's
gone dark again renders exactly as if never seen - no persistent fog-of-war),
a 9-tile real light radius (up from 3, closer to the original 8), and beyond
that radius a much larger, much dimmer "you can make out shapes and ore, nothing
more" band around the player.

## Data model

### Three light channels, not two

`LightLevel` becomes:

```cpp
struct LightLevel
{
    std::uint16_t sky : 5;   // 0-MAX_LIGHT_LEVEL, this tile's sunlight exposure
    std::uint16_t torch : 5; // 0-MAX_LIGHT_LEVEL, this tile's Torch exposure
    std::uint16_t lava : 5;  // 0-MAX_LIGHT_LEVEL, this tile's Lava exposure
};
static_assert(sizeof(LightLevel) == 2, "Two bytes per tile: a 1000x500 world stays 1 MB.");
```

Splitting `block` into `torch`/`lava` is what lets Torch and Lava render as
different colors (warm yellow vs. hot red-orange) instead of being
indistinguishable. Same reasoning as the original two-channel split: these
are independent sources that decay and combine differently at render time,
so they don't get folded into one number just because it happens to save a
few bits. 5 bits per channel comfortably covers the new `MAX_LIGHT_LEVEL = 9`;
still 2 bytes total, matching `Tile`'s own "hard-capped struct, cheap at
world scale" precedent.

`recomputeAll` gains a third BFS pass (torch seeded from `MachineType::Torch`
machines, lava from `isLava` tiles - previously merged into one `blockSeeds`
list, now two independent seed lists and two independent `floodFill` calls
alongside the existing sky pass). Same shared `floodFill` helper, same
decay/occlusion rule, same full-grid-rebuild strategy, same triggers
(block/Torch edit; lava movement, rate-limited) - nothing about *when*
`recomputeAll` runs changes, only that it now produces three grids instead of
two.

`Lighting::MAX_LIGHT_LEVEL` moves from `3` to `9`: still linear 1-per-step
decay, just reaching further, matching the "9-ish tiles of real light" target
approved above. `blockLight(x, y)` is replaced by `torchLight(x, y)` and
`lavaLight(x, y)` (nothing outside `Lighting`/`LightRenderer`/its tests reads
`blockLight`, so there's no call site left needing a combined accessor).

### Ore, for the outline bonus

`Blocks.h` gains a small `isOre(BlockType)` helper (`CopperOre`, `IronOre`,
`Coal` - the three existing non-Stone mineable solids) alongside the existing
`isLava`/`isWater`. Used only by the new ambient-outline pass below, to make
ore read as very slightly brighter than plain Stone/Dirt at long range - "you
can tell there's something here worth digging" without giving away which ore
or how much.

## Two-tier visibility: real light vs. ambient outline

This is the actual fix for "see through walls." Two independent brightness
sources feed into what a tile renders as, and - critically - a tile with
*neither* renders fully hidden (matching today's "disconnected cave reads
black" behavior, now correctly extended to solid ground too):

- **Real light** (sky/torch/lava, extended to reach solid tiles - see below):
  full-color, full-detail, decays over `MAX_LIGHT_LEVEL` (9) steps from its
  source, blocked entirely by solid tiles for propagation purposes (light
  still can't pass through a wall into the next room).
- **Ambient outline** (new): a flat, dim, uncolored "you're standing nearby,
  your eyes make out shapes" floor, valid up to `AMBIENT_OUTLINE_RADIUS` (20)
  tiles from the player's current position, reachable only by passing through
  *open* tiles (the same connectivity rule as real light - a sealed pocket
  with no path to the player gets nothing, real or ambient, exactly like
  today). Solid tiles bordering that reachable space get a flat outline
  brightness too (slightly higher if the tile is ore), so you can see the
  vague shape and mineral content of the rock immediately around your dug-out
  space even with no torch lit. Unlike real light, this does not decay with
  distance inside its radius - it's a flat "barely lightens" floor, not a
  gradient - matching "push way beyond that but it barely lightens it, just
  shows outlines."

Computed fresh every frame from the player's tile
(`Lighting::ambientOutline(world, playerTile)`, same shape and cost class as
the existing per-frame `heldTorchLight` query - a single-source BFS, bounded
by `AMBIENT_OUTLINE_RADIUS`, never touching the stored `levels` grid or
forcing a `recomputeAll`). Returns a sparse
`vector<pair<Vector2i,int>>` of every reachable open tile and every solid
tile bordering the reachable region, each tagged with `AMBIENT_OUTLINE_LEVEL`
(plain terrain) or `AMBIENT_OUTLINE_ORE_LEVEL` (ore neighbor) - both small
values on the same 0-9 scale real light uses, e.g. 1 and 2, so "barely
lightens" is literal: about a tenth of full brightness.

### Real light now reaches solid tiles too

The other half of the fix: solid tiles need their own real-light brightness
for rendering, derived (at render time, not stored) from their open
neighbors: a solid tile's rendered sky/torch/lava value is
`max(0, that channel's best orthogonal open neighbor - 1)` - one step dimmer
than the brightest open tile touching it, the same decay rule as everywhere
else, just evaluated on demand instead of via `floodFill`/BFS (solid tiles
still never propagate light *through* themselves - a wall between two rooms
still doesn't leak one room's brightness into the other's non-adjacent
tiles). This is what makes a torch-lit tunnel's walls actually look lit,
instead of either pitch black (the pre-`cb2a9eb` bug) or always-full-bright
regardless of the torch (the current bug).

Final rule per visible tile, solid or open: take the brighter of (a) real
light's blended color+brightness (see below), (b) the flat ambient-outline
grey. Real light always wins when both apply - a wall lit by an actual torch
looks torch-colored, not outline-grey. A tile with neither renders fully
black/hidden.

## Color

Three tint colors instead of one binary switch:

- `lava` → hot red-orange (e.g. `(255, 90, 40)`)
- `torch` → warm yellow (e.g. `(255, 200, 110)`, distinct from lava's redder
  hue)
- `sky` → the existing night-to-day blue-to-white gradient, unchanged

Blended proportionally by each channel's relative brightness at that tile
(a weighted average of whichever channels are non-zero, weighted by their own
value) rather than "whichever channel is numerically highest wins outright" -
a tile lit by both a nearby torch and daylight actually looks like a blend of
the two, not a hard cutover the moment one edges out the other. Ambient
outline uses its own fixed desaturated grey (e.g. `(90, 90, 100)`), never
blended with the tint colors - it's meant to read as "dim, uncolored
visibility," not as light.

## Performance

Nothing about the expensive path changes: `recomputeAll` still only runs on a
block/Torch edit or rate-limited lava movement, same as `cb2a9eb`/`cbb8485`
left it - it now does three BFS passes instead of two, but all three are the
same bounded-by-`MAX_LIGHT_LEVEL` shape as before, just reaching 9 steps
instead of 3 (still trivial next to the ~85ms full-grid-rebuild cost that
was already being rate-limited).

The new per-frame work is `ambientOutline`, a single bounded BFS from one
seed (the player's tile), same cost class as the `heldTorchLight` query
already run every frame - `AMBIENT_OUTLINE_RADIUS` (20) is larger than
`MAX_LIGHT_LEVEL` (9) but still a tiny, camera-local flood fill, not a
world-scale one. The solid-tile "borrow brightness from your brightest open
neighbor" rendering rule is a 4-neighbor lookup evaluated only for tiles
`LightRenderer` is already visiting inside the view-culled render loop, so it
adds no new unbounded work.

## Testing

`Lighting` stays plain data/logic, window-free, doctest-covered:

- Torch and Lava seed independent channels: a lone Lava tile sets `lavaLight`
  but leaves `torchLight` at 0 on the same tile, and vice versa for a placed
  Torch.
- `MAX_LIGHT_LEVEL` is 9; existing decay/occlusion/out-of-bounds tests
  already reference `Lighting::MAX_LIGHT_LEVEL` symbolically rather than
  hardcoding 3 or 8, so they keep passing unchanged once the constant moves.
- `ambientOutline`: a solid tile bordering the player's reachable open space
  gets `AMBIENT_OUTLINE_LEVEL`; an ore tile in the same position gets
  `AMBIENT_OUTLINE_ORE_LEVEL` instead; a solid tile beyond
  `AMBIENT_OUTLINE_RADIUS` steps (through open tiles) from the player gets
  neither; a solid tile bordering a sealed pocket with no open path back to
  the player gets neither, even if geometrically close (same invariant as
  the existing disconnected-cave sky-light test); an open tile within
  `AMBIENT_OUTLINE_RADIUS` but beyond any real light's reach gets
  `AMBIENT_OUTLINE_LEVEL` rather than 0.
- `ambientOutline` never writes to the stored grid (same "pure query" test
  shape as the existing `heldTorchLight` test).

`LightRenderer`'s solid-tile brightness-borrowing and the three-way tint
blend are SFML Graphics code with no window-free test target, same as today
- verified by the executable building and by manually running the game (dark
cave with a lit torch: walls near it read warm and visible; walls a few
tiles further read a dim grey outline; walls with no path back to the player
at all read pure black), not by an automated check. This matches the
project's existing convention for `LightRenderer`/`ChunkRenderer`/
`MachineRenderer` - noted explicitly here since it's the same gap the
original spec's testing section already called out.

## Out of scope (unchanged from the original spec, plus one more)

Enemy spawning and combat; animated ambient effects beyond
torch/lava/daylight/outline tinting; sleeping or skipping night; any
lantern/lamp item beyond Torch; smooth non-tile-grid gradients. Additionally,
now explicitly ruled out per the approved design: **persistent "explored
tile" memory/fog-of-war** - visibility is always computed from the player's
current position and current light state, never remembered once a tile goes
dark again.
