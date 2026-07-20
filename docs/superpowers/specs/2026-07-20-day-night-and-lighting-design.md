# Day/Night Cycle and Cave Lighting

## Purpose

The game has no time-of-day and no concept of darkness: every tile renders at
full brightness regardless of depth or surroundings. This is scoped as the
foundation for a future Combat & Enemies spec (enemies will spawn based on
darkness), but stands alone as a feature: a day/night clock with a shifting
sky and a real per-tile lighting/visibility system, so caves read as actually
dark, an unconnected cave is invisible until you dig into it, and a craftable
Torch pushes back the dark. Combat, enemy spawning, and any lighting-driven
gameplay beyond visibility are explicitly out of scope here.

## Data model

### A separate light grid, not a `Tile` field

`Tile` is hard-capped at two bytes (`type` + `decoration`), enforced by a
`static_assert` in `Tile.h` - a 1000x500 world stays 1 MB. Light values change
constantly (every tick, from the day/night clock, and from the player moving
with a held Torch) and don't identify *what* a tile is, only how lit it
currently reads, so they don't belong on `Tile` at all - the same reasoning
that kept fluid levels encoded as `BlockType` variants instead of a new field,
except here there's no reasonable way to fold "current light level" into
`BlockType` (it would multiply every existing block type by 8+ light levels).

Instead, a new `Lighting` class (alongside `World`, `FluidSim`) owns a
parallel grid, one entry per tile:

```cpp
struct LightLevel
{
    std::uint8_t sky : 4;   // 0-8, this tile's sunlight exposure
    std::uint8_t block : 4; // 0-8, this tile's torch/lava exposure
};
static_assert(sizeof(LightLevel) == 1);
```

One byte per tile (1000x500 = 500 KB), matching `World`'s "tile storage and
nothing else" philosophy - `Lighting` reads `World` to compute these values
but is otherwise independent, just like `FluidSim`.

Two channels because they behave differently:

- **sky**: any column with an unbroken vertical run of `Air` from the tile up
  to the world top is exposure 8 at *every* depth along that shaft - this is
  what makes an open mineshaft stay lit far down rather than fading out after
  a few tiles, matching "daylight goes quite far down even if it's not very
  bright." From each such shaft tile, sky exposure spreads sideways/diagonally
  into adjoining open tiles, losing 1 level per step, blocked entirely by
  solid blocks - a side-pocket off that shaft dims with distance from it, and
  a cave with no path back to an open shaft reads 0 regardless of how close it
  is in a straight line to a lit one. This is the mechanism behind "can't see
  a cave to the right unless it's attached to yours."
- **block**: emitted by placed Torches and Lava tiles at a fixed level 8,
  spreading by the same one-per-step decay, blocked by solid tiles.

Actual rendered brightness at a tile is `max(block, sky * daylightFactor)`
(see Rendering below) - `sky` is stored as a flat 0-8 exposure value and only
scaled by the current time-of-day at render time, not baked in, since
`daylightFactor` changes every tick and shouldn't force a grid recompute.

## Day/night clock

A `daylightFactor()` float in `World` (or a small owned `DayNightClock`),
easing smoothly between 0 (deep night) and 1 (noon) over a 15-minute
real-time loop - 10 minutes of day, 5 of night, per the agreed pacing. Smooth
easing (not a hard day/night switch) so dawn and dusk read as gradual. This
factor drives three things each frame: the sky background gradient, the
`sky`-channel brightness multiplier in the lighting recombine, and nothing
else - it never touches the stored `sky`/`block` grid values themselves.

## Propagation and update strategy

Both channels are computed by BFS flood-fill from their sources, exactly the
same shape as a graph search bounded by solid-tile edges - conceptually
close to the existing fluid-level scan helpers, just spreading a light value
instead of a fluid level. "Blocked" means `World::isSolid`, which only looks
at a tile's `type` - the decoration layer (tree logs/leaves) already doesn't
block fluid flow, and light propagation follows the same rule for the same
reason: a decorated tile is still open space.

- **Static recompute**, seeded from the changed region only: run whenever a
  block is placed or broken (a solid block appearing/disappearing can open or
  close a light path) or a Torch is placed/removed. This is the same trigger
  `ChunkRenderer::markDirty` already fires on, so `Lighting` recomputes on
  exactly the events that already invalidate a chunk's render cache - no new
  trigger plumbing needed.
- **Dynamic recombine**, every frame, touching only rendering, never the
  stored grid: apply the current `daylightFactor` to `sky`, and add the
  player's held-Torch light (see below) as a small extra source confined to
  the tiles immediately around the player.

### Held Torch light

When the player's currently selected hotbar item is a Torch, the player's own
tile counts as a level-8 `block` source for as long as it stays selected -
checked via the existing `selectedSlot()` accessor, no new equip state. This
is folded into the per-frame dynamic recombine (it moves with the player every
frame) rather than the stored grid, exactly like the daylight factor.

## Torch item

New `ItemType::Torch` / `MachineType::Torch`: furniture-placed like
Chest/Furnace (1x1 footprint, non-solid, placed by right-click / mined like a
world block, per the existing `isFurniture` convention), a permanent
level-8 `block` source once placed. Recipe: 2 Stone + 1 Stick -> 2 Torch,
craftable at the Crafting Table alongside the other early recipes (fits the
existing `CraftRecipe` shape unchanged: two ingredients, `outputCount = 2`).

## Rendering

**LightRenderer**, a new class structured exactly like `ChunkRenderer`: one
chunk-sized vertex array of tile quads per chunk, storing an overlay color
instead of a block color, drawn last (after blocks, decorations, machines,
items, and the player) with `sf::BlendMultiply` - this darkens everything
already drawn without needing per-entity light-awareness. `markDirty` fires
on the same static-recompute triggers as `Lighting` itself; every frame,
`draw()` additionally recombines each *visible* chunk's stored `sky`/`block`
values with the current `daylightFactor` and the player's held-Torch radius,
updating only vertex color (not geometry) - bounded by the same view culling
`ChunkRenderer` already relies on, so this adds a comparable-sized second pass
rather than an unbounded one.

Tinting: `block` blends toward a warm glow (~`(255,180,90)`), `sky` toward a
cool daylight tone that itself shifts through a dawn/noon/dusk/night gradient
driven by `daylightFactor` - the same gradient reused for the sky background,
so sunlit tiles and the sky agree. Final tile color is the base block color
darkened toward black by `1 - max(sky * daylightFactor, block) / 8`, then
tinted toward whichever channel dominates.

**Sky background**: a full-viewport rectangle behind all world content,
recolored every frame from the same day/night gradient - no new asset, just a
color lerp across time-of-day keyframes.

**HUD**: a small sun/moon indicator added to the existing top-right HUD
cluster (per the 2026-07-15 HUD-repositioning spec), showing time-of-day
progress as a thin arc or bar - no numeric clock needed.

## Testing

`Lighting` is plain data/logic with no SFML dependency (mirrors `FluidSim`,
`Physics`), testable in the existing window-free test binaries:

- An open vertical shaft reads `sky = 8` at every depth along it, regardless
  of how deep the shaft goes.
- A cave with no connected path to an open shaft reads `sky = 0` even when
  geometrically close (same row/column) to a lit shaft, if a solid wall
  separates them.
- `sky` decays by exactly 1 per tile step as it spreads sideways from a lit
  shaft into an open side-pocket, and is blocked entirely by solid tiles.
- Placing a Torch sets `block = 8` on its tile and decays outward the same
  way; removing it clears that contribution and triggers a recompute of the
  affected region only.
- Breaking or placing a solid block re-triggers propagation for the affected
  region (opening a path lights a previously-dark pocket; sealing one darkens
  it).
- `daylightFactor()` eases smoothly across the 15-minute cycle and does not
  mutate the stored `sky`/`block` grid.
- The Torch recipe (2 Stone + 1 Stick -> 2 Torch) is present in
  `allCraftRecipes()`.

## Out of scope (for now)

Enemy spawning and combat (the next spec, reading this lighting data for its
spawn rules); colored/animated ambient effects beyond torch/lava/daylight
tinting; sleeping or skipping night; any lantern/lamp item beyond the single
Torch; smooth (non-tile-grid) light gradients.
