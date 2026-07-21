# Torch-Equipped Screen-Wide Ambient Vision, and a Steeper Wall-Penetration Fade

## Purpose

Two follow-up tweaks to the lighting system, requested after playing with the
just-shipped wall-light-penetration feature:

1. **Equipping a Torch should widen your "eyes adjusted to the dark" vision
   to the whole screen**, not just the current fixed 20-tile bubble around
   the player (`Lighting::AMBIENT_OUTLINE_RADIUS`). Right now
   `Lighting::ambientOutline` already gives every open tile reachable within
   20 tiles - and every solid tile bordering that reachable region - a flat,
   dim, uncolored floor, always on regardless of what's equipped. The
   request: when a Torch is currently selected in the hotbar, that same
   mechanism should reach as far as the camera can actually see, on top of
   whatever the Torch's own real light contributes near the player.
   Confirmed with the project owner: this must still respect the existing
   connectivity rule (only tiles reachable through open tiles you've
   actually dug into) - it's a bigger bubble, not a return of x-ray vision
   into sealed, undug pockets.
2. **The 3-tile wall-penetration fade (added in the immediately-prior spec)
   doesn't read as a fade** - all three tiles look too close to the same
   colour. Confirmed direction: tile 1 should stay reasonably bright, tile 2
   should read as "just dark," tile 3 as "very dark" (a bare hint of light
   left, only for a genuinely bright source), and tile 4+ stays fully black -
   which is already guaranteed today purely by the search never reaching
   past distance 3, unaffected by this spec.

## 1. Torch-equipped ambient radius

### Current shape

`Lighting::ambientOutline(const World&, sf::Vector2i playerTile)`
(`src/World/Lighting.h:104-105`) hardcodes its search bound to
`AMBIENT_OUTLINE_RADIUS` (20) internally. `Game::render` calls it
unconditionally every frame with just the player's tile, regardless of what
item is selected.

### Change

`ambientOutline` gains a third parameter, `int radius`, replacing the
internal use of `AMBIENT_OUTLINE_RADIUS` in its BFS step-cutoff - callers
decide how far it reaches, the method itself no longer hardcodes a distance.
`AMBIENT_OUTLINE_RADIUS` stays as the named default for the common (no Torch
equipped) case.

In `Game::render`, right where `playerTile` is already computed once (for
both `heldTorchLight` and `ambientOutline`, per the prior feature's wiring):
when the currently-selected hotbar item is a Torch (the exact same
`held.type == ItemType::Torch` check `heldTorchLight` already uses), compute
a screen-covering radius from the camera's *current* view size instead of
using the default 20 - `camera.view().getSize()` is already available at
that call site (the very next line already passes `camera.view()` into
`lightRenderer.draw`). Convert the view's half-diagonal from pixels to
tiles (`size / TILE_SIZE`, plus a small margin so a tile right at the
screen's corner isn't shortchanged by rounding) and pass that as `radius`;
otherwise pass `Lighting::AMBIENT_OUTLINE_RADIUS` as today.

The BFS cutoff is a *Manhattan*-distance step count (each step moves
orthogonally by one tile), not a Euclidean radius - so covering a
rectangular screen all the way to its corners needs `halfWidthInTiles +
halfHeightInTiles`, not the (shorter) diagonal distance. Concretely:
`halfWidthInTiles = ceil((size.x / 2) / TILE_SIZE)`,
`halfHeightInTiles = ceil((size.y / 2) / TILE_SIZE)`, and the radius passed
in is their sum plus a small margin (a tile or two, so corner tiles aren't
shortchanged by rounding) - using half-width-plus-half-height instead of the
diagonal is what actually guarantees every corner of the screen is within
the BFS's reach, not just the middle of each edge.

This means the covered area always exactly matches whatever's currently on
screen, at any window size or future zoom level, rather than a guessed
magic constant - and critically, it stays cheap: the BFS is still bounded by
a tile-count cutoff from the player, just one sized to the viewport instead
of a fixed 20, so a giant contiguous open cavern (or the world's entire
open-air surface layer, which is one contiguous reachable region) still
never gets scanned beyond what the screen can actually show, regardless of
world size. The connectivity rule itself - BFS through open tiles only,
solid tiles only get the floor by bordering a reached open tile - is
completely unchanged; only the distance cutoff passed in varies.

### Interaction with the rest of the pipeline

Nothing else changes: `ambientOutline`'s result still only matters where
`LightRenderer` currently falls back to it (`total > 0.0f` real light still
always wins outright, unchanged), and the result is still folded into
`outlineMap` in `LightRenderer::draw` exactly as today.

## 2. Steeper, front-loaded wall-penetration fade

### Current shape

In the solid-tile branch of `LightRenderer::draw`
(`src/World/LightRenderer.cpp:126-173`), every candidate in the
`wallPenetrationOffsets()` diamond contributes `channelValue - distance` -
a flat, linear, `-1`-per-tile penalty, identical to the general light decay
rate used everywhere else in this system.

### Change

Replace the flat `- distance` penalty with a per-distance lookup, applied
only inside this wall-penetration search (the general `Lighting` decay rate
- used for how far light spreads through open space in the first place -
is completely untouched):

| Distance | Penalty |
|----------|---------|
| 1        | 2       |
| 2        | 5       |
| 3        | 8       |

Concretely: `skyBest`/`torchBest`/`lavaBest` becomes
`max(0, channelValue - penaltyForDistance(distance))` instead of
`max(0, channelValue - distance)`, evaluated per offset in the existing
loop. For a maximum-brightness (level 9) source glimpsed through rock, this
reads as roughly 78% brightness at tile 1, 44% at tile 2, and 11% at tile 3 -
a clearly distinct step down each tile, front-loaded so the first tile still
reads as "you can see something's there" while the third is nearly (not
always exactly) black, matching "tile 2 just dark, tile 3 very dark." Dimmer
sources fade out completely sooner, which is expected - the steeper curve
only matters where there's enough brightness budget left to show it. Tile 4
and beyond remain fully unaffected by real light (falling back to the
ambient-outline floor from Part 1, or fully black) exactly as today, purely
because the search never looks past distance 3 - no change needed there.

## Testing

Both changes live in `LightRenderer`/the `Game`-`Lighting` wiring, none of
which has a window-free unit test today (SFML Graphics/Window code, matches
the project's existing convention for `ChunkRenderer`/`MachineRenderer`/the
prior wall-penetration task). Verification is a clean build of both targets
plus a manual/visual check:

- With no Torch equipped, ambient vision still cuts off at the same 20-tile
  bubble as before this spec.
- With a Torch equipped, the dim floor visibly extends to the edges of the
  screen (not just 20 tiles), and still goes fully black the moment you look
  toward a sealed pocket with no dug path to it.
- A wall 1 tile from a bright light source looks about the same as before
  this spec; 2 tiles in is noticeably dimmer; 3 tiles in is only barely
  visible (for a bright source) or fully black (for a dimmer one); 4+ tiles
  in is unaffected.

## Out of scope

Any change to `Lighting`'s stored grid, `recomputeAll`, or its recompute
triggers; any change to the general (non-wall-penetration) light decay
rate; any change to `heldTorchLight`'s own radius or behaviour; extending
the *un-equipped* baseline ambient-outline radius beyond 20 (it stays the
same - only the Torch-equipped case changes).
