# Crafting menu, crafting table, and inventory-filtered build mode

Date: 2026-07-16

## Purpose

Today building a machine is completely free: `buildMode`'s palette lists every
`MachineType` unconditionally and `Game::placeMachineAtCursor()` never checks
or consumes an inventory item. There is also no player-facing crafting system
at all — the only "recipe" concept in the codebase is the Smelter's internal
`SmeltRecipe` table, and the player simply starts equipped with a Pickaxe and
Axe (see the comment at `Player.cpp:86-87`).

This adds a real crafting loop: a bootstrap "Crafting Table" recipe reachable
from anywhere, a placed Crafting Table (the first multi-tile machine) that
unlocks recipes for the remaining machines, and a build-mode palette that only
ever shows machine types you're actually holding.

## Components

### 1. New craftable items, one per placeable machine

`ItemType` gains 7 entries, matching the `MachineType` names 1:1:
`CraftingTable`, `BurnerGenerator`, `Drill`, `Belt`, `Chute`, `Smelter`,
`Chest`. Each is non-world-placeable (`placeBlock = BlockType::Air`),
`toolType = ToolType::None`, and its `iconColor` matches that machine's
`MachineInfo::color` swatch so the bag icon and the placed machine read as the
same object.

A new lookup, mirroring the existing `itemForBlock(BlockType)`:

```cpp
// MachineType.h / MachineType.cpp
ItemType itemForMachine(MachineType type);
```

a flat table indexed by `MachineType`, the single source of truth linking a
placeable machine to the item that represents it in the bag.

### 2. `CraftRecipe` table

New `CraftIngredient{ItemType item, int count}` and `CraftRecipe{ItemType
output, std::array<CraftIngredient, 2> ingredients, float seconds, bool
requiresCraftingTable}` in `Machines/Recipes.h`/`.cpp` (same file as the
existing `SmeltRecipe`, since both are "what does X cost/produce" tables of
the same shape). Unused ingredient slots are `{ItemType::None, 0}`.

| Output | Ingredients | Time | Requires table |
|---|---|---|---|
| CraftingTable | 15 OakLog | 3s | no |
| Chest | 8 OakLog | 2s | yes |
| Belt | 1 IronPlate, 1 CopperPlate | 1s | yes |
| Chute | 2 Stone | 1s | yes |
| BurnerGenerator | 5 Stone, 2 IronPlate | 3s | yes |
| Drill | 5 IronPlate, 2 CopperPlate | 4s | yes |
| Smelter | 5 Stone, 3 CopperPlate | 4s | yes |

`std::span<const CraftRecipe> allCraftRecipes()` is the one accessor; callers
filter on `requiresCraftingTable` to pick basic vs. advanced view rather than
the table being split into two arrays, so there is exactly one place recipe
data can drift.

### 3. Crafting Table becomes the first multi-tile machine

`MachineInfo` gains `int width` (tile count along X, default `1`).
`MachineType::CraftingTable` (new entry, inserted before `Count`) registers
`width = 2`, `generator = consumer = transport = false`, `powerRating = 0`,
`actionTime = 0`, a wood-brown color.

`Machines::place()`, currently keyed by a single tile, is extended to occupy
`width` tiles starting at `(x, y)` going right:

- Before mutating anything, it checks `canPlace(x + dx, y)` for every `dx` in
  `[0, width)`; any failure aborts the whole placement (nothing partially
  placed).
- On success, `byTile[key(x + dx, y)] = index` for every occupied tile — not
  just the origin — so `at()`/`indexAt()` resolve correctly no matter which
  tile of the footprint the caller queries.

`Machines::remove(x, y)` currently erases exactly the one key it was called
with. It's extended to: resolve the machine via `indexAt(x, y)` (works from
any occupied tile already), read its *own* `x, y, type` (its origin), compute
its full footprint from `machineInfo(type).width`, and erase **every** tile
in that footprint. The existing swap-and-pop fixup (`machines[index] =
machines[last]`) also becomes footprint-aware: it must rewrite `byTile` for
every tile the *swapped-in* machine occupies, not just its origin, or a
multi-tile machine that gets relocated into a freed slot leaves a stale
`byTile` entry on its second tile.

`canPlace(int x, int y)` keeps its exact current signature and single-tile
meaning — `Player::place()`'s existing "can't place a block on a machine
tile" guard needs no change, since every footprint tile of a multi-tile
machine is now a real `byTile` entry.

`Game::placeMachineAtCursor()`'s `world.isSolid` pre-check (today a single
call) loops over `machineInfo(buildType).width` tiles, so a 2-wide table
can't be placed half-embedded in solid rock.

`MachineRenderer::draw()` draws the body rectangle `width * TILE_SIZE` wide
instead of the hardcoded square, and skips the four I/O side-ticks, the
bar, and the carried-item glyph for `CraftingTable` — it has an early
`continue`-style branch for it, since none of those concepts apply to a
machine with no input/output/progress.

### 4. Player crafting state and flow

New `Game` state: `bool crafting`, `int craftingRecipeIndex`, `float
craftProgress`, and `std::optional<sf::Vector2i> openCraftingTableTile`
(parallel to the existing `openChestTile`).

`Game::toggleInventory()`'s tile resolution becomes three-way instead of
two-way: cursor tile holds a `Chest` → `openChestTile` (unchanged); holds a
`CraftingTable` → `openCraftingTableTile`; anything else → neither (bag-only
view, which now also shows the basic Craft tab).

`Game::drawInventoryPanels()` always draws the bag panel, then:

- `openChestTile` set → chest panel (unchanged).
- `openCraftingTableTile` set → advanced recipe panel: every `CraftRecipe`
  with `requiresCraftingTable == true`.
- neither set → basic Craft tab: just the `CraftingTable` recipe.

Clicking a recipe button (new `Hud::hitTestCraftButton`, built the same way
as the existing `hitTestChestButton`, dispatched from the same
`inventoryOpen` branch of `handleEvents()`'s `MouseButtonPressed` case):

- No-op if `crafting` is already true, or the bag doesn't hold enough of
  every ingredient (`Inventory::count(ItemType)` already exists for this).
- Otherwise: deduct every ingredient immediately, set `crafting = true`,
  `craftingRecipeIndex` to the recipe, `craftProgress = 0`.

`Game::fixedUpdate()` gains `updateCrafting(dt)`: while `crafting`, advances
`craftProgress`; once it reaches the recipe's `seconds`, adds one output item
to the bag via `bag.add()`. Any leftover (full bag) spawns as a ground
`ItemEntity` drop at the player's position, the same `spawnDrop`-style path
mining already uses — nothing crafted is ever silently destroyed. Then resets
`crafting = false`.

Recipe buttons render greyed-out and are inert (per point above) whenever
`crafting` is true, including the in-progress one — one craft at a time, no
queue. The in-progress panel shows a fill-bar for `craftProgress /
recipe.seconds`, the same visual language `MachineRenderer`'s Fuel/Progress
bar already uses.

### 5. Build-mode palette: filtered, counted, consuming

`Hud::drawBuildPalette` changes from listing every `MachineType` to only
those where `bag.count(itemForMachine(type)) > 0`, and labels each swatch
with its count (`"Chest x3"`). Signature gains the bag (or a precomputed
`std::array<int, MachineType::Count>` of counts — implementation's call)
alongside the existing `selected` parameter.

`Game::cycleBuildType()` only cycles among types passing that same
`count > 0` filter, wrapping among just the held ones. If nothing is held,
cycling is a no-op and the palette draws empty — only reachable before the
player has crafted anything.

`Inventory` gains `bool removeOne(ItemType type)` — scans for the first slot
holding `type` and decrements/clears it, the by-type counterpart to the
existing by-slot `removeOne(int slot)` and to `count(ItemType)`.

`Game::placeMachineAtCursor()` gains one more guard alongside the existing
solid-tile/`canPlace` checks: `bag.count(itemForMachine(buildType)) > 0`. On
a successful placement it calls `bag.removeOne(itemForMachine(buildType))`.

`Game::removeMachineAtCursor()` — the build-mode right-click destroy path —
refunds on success: after `machines.remove(x, y)` succeeds, `bag.add({
itemForMachine(removedType), 1 })`, with the same overflow-to-ground-drop
handling as crafting (full bag → `ItemEntity` drop) so destroying a machine
never destroys the item it cost.

F1-F6 keep selecting their existing machine types unchanged; F7 is added for
`MachineType::CraftingTable`, matching the existing one-key-per-type
convenience. Pressing an F-key for a type you hold zero of still sets
`buildType` (unchanged behavior) — placement then simply fails the new
inventory guard until you craft one, the same silent-no-op the solid-tile
check already produces today.

## Data flow summary

- Basic Craft tab / advanced table panel → click recipe → ingredient check →
  deduct → `crafting` timer → `fixedUpdate` ticks it → bag gains item (or
  ground drop on overflow).
- E key → cursor tile → `Chest` / `CraftingTable` / neither → matching panel
  drawn alongside the bag.
- Build mode palette ↔ bag: palette reads `bag.count()` per type every frame
  (filter + label); placing reads-then-consumes; destroying refunds.
- `Machines::place`/`remove` now operate over a footprint (1 tile for every
  existing type, 2 for `CraftingTable`) instead of assuming exactly one tile.

## Error handling / invariants

- Nothing crafted, placed, or destroyed is ever silently lost: overflow from
  a finished craft or a machine-destroy refund becomes a ground drop, mirror-
  ing the existing drag-and-drop / chest-exchange invariant already documented
  in the logistics/inventory-UI spec.
- `Machines::place()` validates the *entire* footprint before mutating any
  state — a partially-blocked 2-wide table placement is rejected outright,
  never half-placed.
- `Machines::remove()`'s swap-and-pop fixup must rewrite every `byTile` entry
  of the machine being relocated into the freed slot, not just its origin —
  the concrete new failure mode multi-tile support introduces, and the
  sharpest thing to get right in this spec.
- `canPlace(int x, int y)`'s signature and single-tile meaning are unchanged,
  so every existing caller (including `Player::place()`'s block-placement
  guard) keeps compiling and behaving correctly with no edits.
- A craft cannot be started while one is already in progress; the UI both
  disables the buttons and the click handler independently no-ops, so there's
  no way to double-spend ingredients via a fast double-click.

## Testing

- `Machines`: place/remove/canPlace tests for a 2-wide footprint (occupies
  both tiles; blocks placement overlapping either tile from any direction;
  removing via either of its two tiles clears both `byTile` entries); a
  swap-and-pop regression test with three machines placed (one multi-tile)
  where removing the first forces the last (multi-tile) machine to relocate,
  asserting both its new `byTile` entries resolve correctly afterward.
- `Inventory`: `removeOne(ItemType)` unit tests (removes from the first
  matching slot, clears a single-count slot, decrements a stacked slot,
  no-ops when the type isn't present).
- Crafting: recipe affordability gate, ingredient deduction on start,
  progress-to-completion timing, output landing in the bag, overflow-to-
  ground-drop when the bag is full, "second click while crafting is a no-op"
  test, basic-vs-advanced recipe filtering by `requiresCraftingTable`.
- Build palette: cycling only visits held types and wraps correctly; empty-
  inventory cycling is a no-op; placement consumes exactly one item and fails
  cleanly at zero; destroying refunds exactly one item.
- Hud/Game UI (recipe button hit-testing, progress bar rendering, palette
  swatch counts, multi-tile body rendering): hit-test geometry is factored
  into a pure function and unit-tested, consistent with
  `hitTestChestButton`/`hitTestPanels`; rendering itself gets manual
  verification via the project's run/verify workflow, matching how the rest
  of the HUD is covered.

## Out of scope

- Rebalancing the proposed recipe costs/times beyond this table.
- Queued or parallel crafting (more than one recipe in flight at once).
- Rotating the Crafting Table's footprint, or any other non-`Right`-growing
  multi-tile orientation.
- Moving Pickaxe/Axe behind a recipe — they remain free starting items.
- Stack-splitting, multiple Crafting Table tiers, or Crafting Table having
  its own internal storage (ingredients always come straight from the bag).
