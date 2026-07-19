# Tool Tiers

## Purpose

Today there is exactly one Pickaxe and one Axe, gated only on tool *kind*
(`ToolType::Pickaxe`/`Axe`) with no notion of tier. This adds a five-tier
progression - Wood, Stone, Copper, Iron, Obsidian - for both tools, gates
what a pickaxe can mine behind its tier, and gives every tier a mining-speed
bonus. Axes never gate what they can chop (every tier chops any tree); axe
tier instead affects chop speed and how many logs a big fell yields.

This spec covers tools only. It does *not* cover how Obsidian blocks come to
exist in the world - that is a separate, not-yet-designed liquid/lava-water
reaction spec. Obsidian is added here as a minable block, item, and tool
tier so the recipes and gating are fully wired up, but until that later spec
ships there is no way to obtain Obsidian in-game. This is a known,
intentional gap.

## Data model

### `ToolTier`

A new enum, alongside `ToolType` in `Blocks.h` (both `BlockInfo` and
`ItemInfo` need it):

```cpp
enum class ToolTier : std::uint8_t
{
    Wood,
    Stone,
    Copper,
    Iron,
    Obsidian,
};
```

Ordering is the enum's underlying value: `Stone > Wood`, `Obsidian > Iron`,
etc. A free function does the comparison so call sites don't cast by hand:

```cpp
// True if a tool of `held` tier can mine a block that requires `required`.
bool meetsTier(ToolTier held, ToolTier required);
```

### Blocks

`BlockInfo` gains a `requiredTier` field (meaningful only when
`requiredTool != ToolType::None`; defaults to `Wood`, i.e. "any tool of the
right kind works"). `requiredTool` is unchanged - this is an additive gate
on top of it.

Updated/new registry rows:

| Block | requiredTool | requiredTier | notes |
|---|---|---|---|
| Grass, Dirt, Stone, Coal | Pickaxe | Wood | unchanged behavior |
| CopperOre | Pickaxe | **Stone** | was Wood-equivalent (ungated); now needs Stone+ |
| IronOre | Pickaxe | **Copper** | was Wood-equivalent; now needs Copper+ |
| **Obsidian** (new) | Pickaxe | **Iron** | new block; see Purpose gap note |
| OakLog, OakLeaves | Axe | Wood | tier never gates axes; field present but unused for gating |

Obsidian block registry row: high hardness (above Iron Ore's 2.00f, e.g.
3.00f), a dark color distinct from Coal, `drop = BlockType::Obsidian`.

### Items

`ItemInfo` gains a `ToolTier tier` field (meaningful only when
`toolType != ToolType::None`). Existing `Pickaxe`/`Axe` items are renamed in
place to `WoodPickaxe`/`WoodAxe` (same enum position, same stats, same
starting-inventory slots - just now explicitly tier `Wood`). Eight new
items are added, following the naming pattern already established by
`CopperChute`/`IronChute` etc.:

| Item | toolType | tier |
|---|---|---|
| WoodPickaxe *(renamed from Pickaxe)* | Pickaxe | Wood |
| WoodAxe *(renamed from Axe)* | Axe | Wood |
| StonePickaxe | Pickaxe | Stone |
| StoneAxe | Axe | Stone |
| CopperPickaxe | Pickaxe | Copper |
| CopperAxe | Axe | Copper |
| IronPickaxe | Pickaxe | Iron |
| IronAxe | Axe | Iron |
| ObsidianPickaxe | Pickaxe | Obsidian |
| ObsidianAxe | Axe | Obsidian |

Plus three non-tool items: `Stick` (crafting ingredient, `toolType = None`),
`SharpRock` (crafting ingredient, `toolType = None`), and `Obsidian`
(placeable-block item, mirroring how Stone/CopperOre items work today).

Wood tools are never crafted - they remain the fixed starting hotbar items
(`bag.exchange(0, {ItemType::WoodPickaxe, 1})` /
`bag.exchange(1, {ItemType::WoodAxe, 1})` in `Player::Player()`).

## Mining: gating and speed

### Gating (`Player::mine()`)

The existing wrong-tool check (kind mismatch = zero progress) gets an
additional tier check, pickaxe-only:

```cpp
const ItemInfo& heldInfo = itemInfo(bag.slot(selected).type);
const BlockInfo& info = blockInfo(block);

const bool wrongKind = block != BlockType::Air && info.requiredTool != heldInfo.toolType;
const bool tooLowTier = heldInfo.toolType == ToolType::Pickaxe
                      && !meetsTier(heldInfo.tier, info.requiredTier);

const bool wrongTool = wrongKind || tooLowTier;
```

Everything downstream (the early-reset branch, the "no partial credit"
rule) is unchanged - `tooLowTier` just folds into the same `wrongTool` bit
that already resets progress and blocks mining entirely. Axes never set
`tooLowTier`: any axe tier can start and continue chopping any tree.

### Speed

A constant table (own header spot near `COPPER_TIER_SLOWDOWN` in
`MachineType.h`, but this one belongs beside `ToolTier` since it's a tool
concept, not a machine one) gives each tier a mining-speed multiplier:

```cpp
// Indexed by ToolTier. Applies to both Pickaxe and Axe mining time.
inline constexpr std::array<float, 5> TOOL_TIER_SPEED_MULTIPLIER = {
    0.6f,  // Wood
    1.0f,  // Stone
    1.25f, // Copper
    1.5f,  // Iron
    1.75f, // Obsidian
};
```

`Player::mine()`'s `targetHardness` becomes:

```cpp
targetHardness = blockInfo(block).hardness / TOOL_TIER_SPEED_MULTIPLIER[static_cast<std::size_t>(heldInfo.tier)];
```

A held item with `toolType == None` (bare hands) never reaches this line -
`wrongTool` is already true for any solid block.

## Axe log-yield bonus

Felling a tree drops all logs from the break point up in one cascade
(unchanged mechanic from the tools-and-trees design: bottom break drops the
full trunk, height 4-6). When that cascade's log count is **4 or more**,
the axe's tier adds a flat bonus on top:

| Tier | Bonus |
|---|---|
| Wood | +0 |
| Stone | +1 |
| Copper | +2 |
| Iron | +3 |
| Obsidian | +4 |

A 5-log fell yields 5/6/7/8/9 logs with Wood/Stone/Copper/Iron/Obsidian
respectively. Below 4 logs in the cascade (a partial chop high up the
trunk), no bonus applies - this closes the exploit of chopping one log at a
time to repeatedly farm the bonus.

Implementation: in `Player::mine()`'s tree-break branch, after
`collectTreeBreak` populates `result.broken`, count the `OakLog` entries in
the just-added slice. If the count is >= 4, push that many additional
synthetic `BrokenTile{BlockType::OakLog, x, y}` entries (position of the
last log tile is fine - `spawnDrop` only reads `.block` to map to an item,
position just decides where the drop physically spawns). `Game::spawnDrop`
needs no changes: it already spawns one stack per `BrokenTile` regardless
of how it got into `result.broken`.

## Recipes

All new recipes require the Crafting Table (`requiresCraftingTable =
true`), consistent with every existing `CraftRecipe` except the table
itself. Each fits the existing 2-ingredient-slot `CraftRecipe` shape:

| Output | Ingredients | Seconds |
|---|---|---|
| Stick x4 | 1 Oak Log | 1.0f |
| StonePickaxe | 2 Stick, 2 SharpRock | 2.0f |
| StoneAxe | 2 Stick, 1 SharpRock | 2.0f |
| CopperPickaxe | 2 Stick, 4 CopperPlate | 2.0f |
| CopperAxe | 2 Stick, 2 CopperPlate | 2.0f |
| IronPickaxe | 2 Stick, 4 IronPlate | 2.0f |
| IronAxe | 2 Stick, 2 IronPlate | 2.0f |
| ObsidianPickaxe | 2 Stick, 3 Obsidian | 2.0f |
| ObsidianAxe | 2 Stick, 2 Obsidian | 2.0f |

(Craft time of 2.0f matches the existing Belt/Chute/Drill recipes; Stick at
1.0f matches the fastest existing table recipes. Not a hard requirement,
just consistent with today's pacing.)

## Sharp Rocks (world spawn)

### Placement

A new `TerrainGenerator` pass, run after `scatterTrees` (so it can avoid
canopy tiles), places exactly 9 Sharp Rock spawn points at random surface
columns using the same hashed-candidate + minimum-spacing technique as
`scatterTrees`. Unlike ore/trees, this pass does not mutate any block - it
returns a `std::vector<sf::Vector2i>` of surface positions (one tile above
`surfaceHeight(x)`) for `Game` to turn into entities:

```cpp
std::vector<sf::Vector2i> scatterSharpRocks(const World& world) const;
```

### Materialization

`Game::Game()`, right after `generator.generate(world)`, calls
`scatterSharpRocks` and pushes one `ItemEntity{{ItemType::SharpRock, 1},
position, {0.f, 0.f}}` into `drops` per point returned. Physics (gravity +
ground collision, already implemented for every other drop) settles them
onto the surface exactly like a normal mined-block drop.

### Respawn

A renewable resource, not a one-time 9. `Game` gains:

```cpp
static constexpr float SHARP_ROCK_RESPAWN_INTERVAL = 300.0f; // 5 minutes
static constexpr int SHARP_ROCK_TARGET_COUNT = 9;

float sharpRockRespawnTimer = 0.0f;
```

In `fixedUpdate`, accumulate `sharpRockRespawnTimer += dt`. Whenever it
crosses `SHARP_ROCK_RESPAWN_INTERVAL`, reset it to 0 and: count
`ItemType::SharpRock` stacks currently in `drops` (ground only - the
player's bag is not counted, so collected rocks don't block new ones from
appearing); if that count is below `SHARP_ROCK_TARGET_COUNT`, spawn one
more at a freshly rolled random surface column (reusing the same placement
logic as world-gen, seeded from current world state rather than a fixed
list since this happens at runtime, not generation).

## Testing

Following the established per-family pattern (`test_mining.cpp`,
`test_terrain.cpp`, `test_recipes.cpp`, `test_items.cpp`):

- **Gating**: a Wood pickaxe cannot mine Copper Ore or Iron Ore; a Stone
  pickaxe can mine Copper Ore but not Iron Ore; a Copper pickaxe can mine
  Iron Ore but not Obsidian; an Iron pickaxe can mine Obsidian; every axe
  tier can chop every tree regardless of tier.
- **Speed**: mining time for a given block scales by
  `TOOL_TIER_SPEED_MULTIPLIER` per tier (via `doctest::Approx`).
- **Log bonus**: a 5-log fell with each axe tier yields 5/6/7/8/9 logs; a
  2-log partial chop yields exactly 2 logs regardless of axe tier (no
  bonus below the 4-log floor).
- **Recipes**: each of the 9 new recipes costs exactly its documented
  ingredients and requires the Crafting Table; Stick recipe yields 4 from
  1 Oak Log.
- **Sharp Rock spawn**: world generation produces exactly 9 spawn points,
  each on the surface, respecting minimum spacing; the respawn timer tops
  up toward 9 without exceeding it, and does not fire early.
- **Items**: renamed `WoodPickaxe`/`WoodAxe` and all 8 new tool items
  report the right `toolType`/`tier`/`iconColor`; `Stick`, `SharpRock`,
  `Obsidian` report `toolType = None` as expected.
