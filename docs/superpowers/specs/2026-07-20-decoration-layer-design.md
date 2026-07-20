# Decoration layer for trees

**Date:** 2026-07-20
**Status:** Approved design, ready for implementation

## Goal

Water currently either gets blocked by tree tiles (`OakLog`/`OakLeaves`) the
player already walks straight through, or - after today's earlier fix -
flows into them and destroys them. Neither is right: water should flow
through a tree the same way the player does, and the tree should still be
there afterward. The single `BlockType type` per tile can't represent "water
here, log also here" at once, so trees move to a second, independent field
that fluid (and placement) never touches.

As a visual side effect the user asked for directly: fluid renders with a bit
of transparency now, generally - not only over decorations - so a submerged
log is genuinely visible through the water on top of it.

## Decisions (from brainstorming)

- Reuse `BlockType` for the decoration field rather than a new enum - trees
  are the only decoration today, `blockInfo()` (hardness, tool, drop) already
  keys off `BlockType`, and nothing new is needed to look those up.
- Breaks `Tile`'s `sizeof == 1` invariant (2 bytes now: 500KB -> 1MB for the
  full world). Accepted - trivial in absolute terms.
- Fluid sim's openness checks revert to the plain `== BlockType::Air` they
  had before today's `isOpenForFluid` detour - decorations no longer occupy
  the terrain grid at all, so the detour is no longer needed.
- Fluid renders at partial alpha unconditionally (not only when a decoration
  is present underneath) - simpler than a conditional special case, and it's
  what the user asked for as a general look.

## Data model

`src/Tile/Tile.h`:

```cpp
struct Tile
{
    BlockType type = BlockType::Air;
    BlockType decoration = BlockType::Air;
};

static_assert(sizeof(Tile) == 2, "...");
```

`World` (`src/World/World.h/.cpp`) gains, mirroring `get`/`set`:

```cpp
BlockType getDecoration(int x, int y) const; // Air out of bounds, like get()
void setDecoration(int x, int y, BlockType type); // no-op out of bounds, like set()
```

## Generation

`TerrainGenerator::placeTree` (`src/World/TerrainGenerator.cpp`): every
`world.set(..., OakLog/OakLeaves)` becomes `world.setDecoration(...)`.
`scatterTrees`'s ground check (`world.get(x, surface) != Grass`) is
unchanged - it tests the terrain a tree grows from, not the tree itself.

## Fluid sim

`src/World/FluidSim.cpp`: revert every `isOpenForFluid(t)` call added earlier
today back to `t == BlockType::Air` (the pre-existing behavior). Since
decorations never occupy `type` anymore, plain Air checks already let fluid
flow through a decorated tile without ever reading or writing the decoration
field - `FluidSim` does not need to know decorations exist at all.

## Mining

`src/Player/Player.cpp`:

- `Player::mine`: the targeted block becomes
  `const BlockType decoration = world.getDecoration(tileX, tileY); const BlockType block = decoration != BlockType::Air ? decoration : world.get(tileX, tileY);`
  Tool/hardness/progress logic is unchanged below that - it already just
  operates on a `BlockType block`.
- `collectTreeBreak`'s flood fill reads/writes `getDecoration`/`setDecoration`
  instead of `get`/`set`. `isTreePart`, `applyAxeLogBonus`, and
  `result.broken` bookkeeping are unchanged - they only ever carried
  `BlockType` values, not which layer they came from.

## Placement

`src/Player/Player.cpp`, `Player::place`: the existing
`world.get(tileX, tileY) != BlockType::Air` guard gains a second condition,
`|| world.getDecoration(tileX, tileY) != BlockType::Air`, so a block can no
longer be placed through a tree that no longer occupies the terrain slot.

## Rendering

`src/World/Chunks.cpp`, `ChunkRenderer::rebuild`: per tile, decoration draws
first (full opacity, same quad geometry as today's solid-block path) if
present, then terrain draws on top. When terrain is fluid, its color's alpha
is set to a new `FLUID_ALPHA = 200` (of 255) constant instead of full
opacity, so it blends over whatever was drawn beneath it (a decoration, or
the background if there was none) - SFML's default blend mode handles this
correctly as long as the decoration's triangles are appended to the chunk's
vertex array before the fluid's, which a single top-to-bottom per-tile
draw (decoration, then terrain) guarantees.

The existing partial-height fluid-surface logic (`fluidSurfaceHeight`) is
unchanged; it still only affects the terrain/fluid quad, drawn over a
full-height decoration quad if one exists underneath.

## Testing

Core-testable, no window:

- `World::getDecoration`/`setDecoration`: round-trips a value, returns Air
  out of bounds, is independent of `get`/`set` on the same cell (setting
  decoration does not change `type` and vice versa).
- Fluid sim: a fluid tile falls through / spreads into a tile that has a
  decoration set, and the decoration is still there (unchanged) afterward -
  replaces today's two "isOpenForFluid" tests, which now assert
  non-destruction instead of displacement.
- Mining: chopping a tree (decoration layer) still walks the flood fill,
  still awards the axe-log bonus, and leaves `type` at that tile untouched.
  Existing `test_mining.cpp` assertions that read `world.get(...)` for tree
  tiles move to `world.getDecoration(...)`.
- Placement: placing a block onto a tile with a decoration (and empty
  terrain) is rejected, same as placing onto occupied terrain today.

Rendering (alpha blending, draw order) is not unit-tested, same as the rest
of `Chunks.cpp` - verified by build and a manual visual check.

## Out of scope (for now)

Any decoration other than `OakLog`/`OakLeaves`, decorations that are ever
solid, and fluid interacting with the decoration layer in any way (e.g.
decorations don't slow fluid, don't burn in lava, etc. - purely cosmetic and
inert to physics/fluid, exactly as they are today).
