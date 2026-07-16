# Tools and Trees

## Purpose

Right now every block breaks the same way regardless of what's in the player's
hand. This adds a Pickaxe and an Axe as real items, gates mining behind holding
the right one, and introduces trees: a fellable, axe-only structure that drops
oak logs.

## Data model

### `ToolType`

A new enum, alongside `BlockType` in `Blocks.h` (both `BlockInfo` and
`ItemInfo` need it, and `Items` already depends on `Blocks`, not the other way
round):

```cpp
enum class ToolType : std::uint8_t { None, Pickaxe, Axe };
```

### Blocks

`BlockInfo` gains a `requiredTool` field. New registry:

| Block | requiredTool | solid | drop |
|---|---|---|---|
| Air | None | false | Air |
| Grass | Pickaxe | true | Dirt |
| Dirt | Pickaxe | true | Dirt |
| Stone | Pickaxe | true | Stone |
| CopperOre | Pickaxe | true | CopperOre |
| IronOre | Pickaxe | true | IronOre |
| Coal | Pickaxe | true | Coal |
| **OakLog** (new) | **Axe** | **false** | OakLog |
| **OakLeaves** (new) | **Axe** | **false** | Air (drops nothing) |

`OakLog` and `OakLeaves` are non-solid: the whole tree is non-collidable, so
the player walks straight through trunk and canopy alike.

### Items

`ItemInfo` gains two fields:

- `toolType` (`ToolType`, default `None`) — what tool this item *is*.
- `iconColor` (`BlockColor`) — replaces deriving an item's on-screen color
  from `placeBlock`. Placeable items keep the color their block already has
  (no visual change); this also fixes the existing bug where Copper Plate and
  Iron Plate render as black squares because their `placeBlock` is `Air`.

New items:

| Item | maxStack | placeBlock | toolType | iconColor |
|---|---|---|---|---|
| Pickaxe | 1 | Air | Pickaxe | grey/steel |
| Axe | 1 | Air | Axe | brown/steel |
| Oak Log | 99 | Air | None | log brown |

The three duplicated `itemColor()` helpers (`Game.cpp`, `Hud.cpp`,
`MachineRenderer.cpp`) change from `toColor(blockInfo(itemInfo(type).placeBlock).color)`
to `toColor(itemInfo(type).iconColor)`.

### Starting inventory

There is no crafting system yet, so the player spawns with a Pickaxe and an
Axe already placed in the hotbar (slots 0 and 1) at construction time.

## Tool gating

In `Player::mine()`, before mining can start or continue on a tile, the held
item's `toolType` must equal the target block's `requiredTool`. If it
doesn't (including an empty/wrong-tool hand), no progress accrues — this
folds into the existing early-reset branch (today: not holding the mine
button, target is Air, or out of reach; tomorrow: also wrong tool). There is
no "mines slowly" fallback: the wrong tool means the block cannot be broken at
all, same as being unreachable.

## Trees and the break-cascade

### Shape

A tree is a single-column trunk of `OakLog`, 4-6 tiles tall (per-column
hashed, deterministic from world seed, same technique as ore vein sizing),
topped with a fixed 7-tile canopy of `OakLeaves`:

```
      L          topY-2  (single apex tile)
   L  L  L        topY-1  (3-wide row)
   L [T] L        topY    (3-wide row; center is the top trunk tile)
      T
      T
      T
      T           surface - 1  (bottom log, sits directly above the grass tile)
   grass          surface
```

The canopy shape is fixed regardless of trunk height — only the trunk's
length varies.

### Breaking a tree

When a `OakLog` or `OakLeaves` tile is broken (always requires the Axe):

1. Flood-fill from the broken tile through 4-connected neighbors that are
   also `OakLog` or `OakLeaves`, restricted to `y <= brokenY` (upward only).
   This is what makes a mid-trunk break only take the top half down: cutting
   a log severs the fill from the trunk below it, so nothing below the break
   point is touched.
2. Every tile the fill reaches is cleared to `Air`.
3. One Oak Log item drops per `OakLog` tile in the collected set, each
   spawned at that tile's position exactly like a normal single-tile break
   today. `OakLeaves` tiles are cleared but never spawn a drop.

Breaking the bottom log fells the whole tree and drops `h` logs (the trunk
height). Breaking log *k* tiles up drops only the `h - k` logs from that
point upward; the canopy always comes down with whatever it was still
attached to.

### `ActionResult` change

`ActionResult::broke` currently carries a single `(brokenBlock, brokenX,
brokenY)`. It becomes:

```cpp
struct BrokenTile
{
    BlockType block;
    int x;
    int y;
};

std::vector<BrokenTile> broken; // usually size 1; size N during a tree cascade
```

`Game.cpp` loops over `result.broken`, spawning a drop and marking a chunk
dirty per entry, instead of doing it once for a single tile.

## Terrain generation

A new `scatterTrees` pass in `TerrainGenerator`, run after `generateBase`
(surface + grass must already exist).

### Density

A new low-frequency `fbm1D` channel (own salt, wider wavelength than the
surface-height noise) produces a *forest factor* in `[0, 1]` per x-position,
the same technique `surfaceHeight` uses for hills. Each candidate column
rolls a hash against a density threshold that scales with the local forest
factor: high-factor stretches place trees often (dense pockets), low-factor
stretches almost never (bare stretches). Because the underlying factor is
continuous noise, forest density blends smoothly across the world rather
than snapping between "forest" and "clearing."

### Spacing

Regardless of what the density roll allows, a candidate trunk is rejected if
it falls within 3 tiles of the last placed trunk. Canopies are 3 tiles wide,
so this guarantees two trees' canopies can never touch — which is what makes
the break-cascade's flood-fill safe from bleeding into a neighbor tree even
in the densest patches.

### Placement

For each accepted column, the trunk starts at `surface(x) - 1` and extends
upward for its rolled height; the canopy is added above that. Columns too
close to the world's horizontal edge for the canopy to fit are skipped.

## Testing

- `test_mining.cpp`: tool gating (pickaxe can't touch a tree, axe can't touch
  stone, bare hands touch nothing); cascade behavior (bottom break fells the
  whole tree and drops the right log count; a mid-trunk break only drops the
  logs from the break point up and leaves the lower trunk standing; leaves
  never produce a drop, whether broken directly or via cascade).
- `test_terrain.cpp`: generated canopies from adjacent trees never overlap;
  trunk height stays within 4-6.
- `test_placing.cpp` / existing item tests: new items report the right
  `toolType`/`iconColor`, non-placeable as expected.
