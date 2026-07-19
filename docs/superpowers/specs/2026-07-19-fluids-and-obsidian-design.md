# Fluids and Obsidian Generation

## Purpose

The tool-tiers work added `BlockType::Obsidian` and Iron/Obsidian-tier tools
gated on being able to mine it, but nothing in the world can actually produce
Obsidian yet - that gap is intentionally closed here. This adds a real fluid
simulation (Water and Lava, each with 8 discrete levels, flowing and
settling like a simple grid-based liquid), scatters finite pools of both
around the world (lakes on the surface, pools underground, lava pools deep),
and gives Water and Lava a reaction: wherever Lava is adjacent to Water, the
Lava solidifies into Obsidian and the Water is partially consumed.

## Data model

### Fluid levels are `BlockType` values, not a new per-tile field

`Tile` is hard-capped at exactly one byte:

```cpp
struct Tile
{
    BlockType type = BlockType::Air;
};

static_assert(sizeof(Tile) == 1, "A tile must stay one byte: a 1000x500 world is 500 KB.");
```

Adding a separate "fluid level" field to `Tile` would either break that
invariant or double the world's memory footprint. Instead, a fluid's level
*is* which specific `BlockType` occupies the tile - the same convention
every previous feature (ores, trees, Obsidian itself) already uses:

```cpp
enum class BlockType : std::uint8_t
{
    Air, Grass, Dirt, Stone, CopperOre, IronOre, Coal, OakLog, OakLeaves, Obsidian,

    Water1, Water2, Water3, Water4, Water5, Water6, Water7, Water8,
    Lava1,  Lava2,  Lava3,  Lava4,  Lava5,  Lava6,  Lava7,  Lava8,

    Count
};
```

`Water8`/`Lava8` is full (source-like); `Water1`/`Lava1` is almost
depleted - losing its last level turns the tile to `Air`. All 16 rows are
non-solid, zero hardness, `requiredTool = None` (fluids can't be mined - see
Interaction below), with color chosen per level (a full tile reads as a
richer blue/orange; a nearly-empty one paler) so the simulation's state is
visible at a glance.

Free helper functions (in `Blocks.h`, alongside the existing `isSolidBlock`):

```cpp
bool isWater(BlockType type);          // true for Water1..Water8
bool isLava(BlockType type);           // true for Lava1..Lava8
bool isFluid(BlockType type);          // isWater || isLava
int  fluidLevel(BlockType type);       // 1-8 for a fluid tile, 0 otherwise
BlockType waterAtLevel(int level);     // level 1-8 -> Water1..Water8
BlockType lavaAtLevel(int level);      // level 1-8 -> Lava1..Lava8
```

## Flow simulation

A new `FluidSim` component owns an **active-tile queue** - not a full-world
scan every tick, which would be far too slow over a 1000x500 grid. A fluid
tile is "active" whenever it was just placed, just changed, or sits next to
a tile that just changed; it drops out of the active set once a tick finds
nothing useful to do at that position, and re-enters if disturbed later
(mining exposes a new opening, a neighboring tile changes, etc).

The tick itself runs on its own timer, independent of the 60Hz physics
step - roughly 10 times a second - so flow reads as a visible process rather
than an instant teleport. For each active fluid tile, in order:

1. **Fall.** If the tile directly below is `Air`, the fluid moves down
   wholesale: below becomes this tile's level, this tile becomes `Air`. A
   plain transfer, nothing created or destroyed.
2. **Fall onto matching fluid.** If below is the same fluid type at a lower
   level, one level transfers down (below's level +1, this tile's level -1;
   this tile clears to `Air` if that reaches 0).
3. **Spread.** If neither fall applies (blocked below), and a
   horizontally-adjacent tile is `Air`, the fluid spreads into it at one
   level lower than its own (classic Minecraft-style falloff). A level-1
   tile cannot spread further.

This is deliberately lossy at the margins - spreading doesn't conserve
volume tile-for-tile, which is what makes a finite pool thin out and settle
into a resting shape instead of spreading forever. Because every pool is
finite (no infinite source tiles anywhere), pools visibly shrink as they
spread, drain into an opened cave, or react with the other fluid.

Water and Lava never merge with each other via fall/spread - contact between
them is handled entirely by the reaction below, checked before the
fall/spread rules each tick.

## Player interaction

Both fluids are **non-solid** - the player walks/swims through either
freely, no dedicated swim controls. While the player's box overlaps *any*
fluid tile, gravity is halved (matches the confirmed "just make it float a
bit" feel, nothing more). There is no damage or health system in this game
today, so Lava does not hurt or kill the player - it's a mechanical/visual
hazard for this feature, not a threat.

Fluids are **not minable** - `requiredTool = ToolType::None` on every fluid
row means the mining button does nothing when aimed at one, matching how
`Air` already behaves. There is no bucket item and no way to collect fluid
into the inventory. The only ways a fluid tile changes are the flow
simulation itself and the Obsidian reaction.

## The Obsidian reaction

Checked each simulation tick, before the fall/spread rules run, for every
active **Lava** tile that is 4-adjacent to any **Water** tile:

- The Lava tile solidifies completely into `BlockType::Obsidian`, regardless
  of its own level - contact with water quenches the whole exposed tile at
  once, not a fraction of it.
- The adjacent Water tile loses one level (clearing to `Air` if that was its
  last level).

Because Obsidian is solid, it immediately blocks fall/spread through that
tile on every later tick - the reaction naturally seals itself into a crust
at the contact line rather than needing a hand-tuned yield count. For two
pool-sized bodies (see World Generation below) meeting, this should land
somewhere around "about 10 Obsidian" as an emergent range from the contact
perimeter that forms before the crust seals it off - not a hardcoded
constant, and not expected to hit exactly 10 every time. The later
implementation plan should size pools so a full-pool-meets-full-pool test
lands in a reasonable band (e.g. 5-20), and should assert that range rather
than an exact count.

## World generation

A new `scatterFluids` pass, run after `scatterOre` and before
`scatterTrees` in `TerrainGenerator::generate()`:

```
generateBase -> carveSpecialCaves -> scatterOre -> scatterFluids -> scatterTrees
```

Ordering matters: after `scatterOre` so pools don't need to compete with or
overwrite ore veins; before `scatterTrees` so a lake correctly blocks a tree
from growing on top of it (the existing `scatterTrees` pass already skips
any column whose surface tile isn't `Grass`, which a lake will have already
overwritten).

Each pool is carved as a rounded blob (the same technique `growVein` already
uses for ore) and filled **entirely** with source-level-8 fluid at
generation time - no separate air-gap layer, no waiting for the simulation
to fill it from empty. The runtime flow tick only takes over from the
moment the world is loaded onward (settling any rough edges, reacting where
lava and water end up close together, responding to the player mining into
a pool).

For the current 1000-tile-wide world:

- **5 surface lakes.** Anchored to each chosen column's existing
  `surfaceHeight(x)`, carved a few tiles down into the hill contour and
  filled with Water. Roughly one per 200 tiles of world width, same spacing
  idea as the existing tree/ore density passes.
- **10 underground water pools.** Placed within the existing cave-depth
  range, above the lava band (roughly Y 100-300, overlapping where Coal and
  shallow Iron already generate) - carved into solid Stone like an ore vein,
  filled with Water.
- **15 lava pools.** Placed below the iron layer (roughly Y 350-495, the
  world's lower band), with placement **biased toward the deeper end** of
  that range - more of the 15 pools land near the world's bottom than its
  top, so lava gets progressively more common (and mining progressively
  more hazardous) the deeper the player digs, per the explicit ask.

Exact Y-bands, blob-size constants, and the density-bias curve are
implementation-plan details to tune against the "~10 Obsidian, lava
noticeably more common deep down" targets - not fixed here.

## Testing

Following the established pattern (`test_world.cpp`, `test_terrain.cpp`,
`test_mining.cpp`): fluid level helpers (`isWater`/`isLava`/`isFluid`/
`fluidLevel` correct for every relevant `BlockType`); fall behavior (a
fluid tile over open air moves down, not sideways, in one tick); spread
behavior (a blocked fluid tile spreads sideways at level-1 below its own,
never spreads at level 1); finite depletion (a pool with no source
regeneration visibly shrinks/settles rather than spreading indefinitely);
the reaction (a single Lava tile adjacent to Water becomes Obsidian, the
Water tile loses exactly one level, and the newly-solid Obsidian blocks
further flow through that tile); world generation (exactly 5/10/15 pools of
each kind at world generation, lava pool depth distribution skews toward
the bottom of its band, a full lava pool meeting a full water pool yields
Obsidian in a reasonable range rather than an exact hardcoded count);
player interaction (gravity is halved while overlapping any fluid tile,
mining input does nothing when aimed at a fluid tile).
