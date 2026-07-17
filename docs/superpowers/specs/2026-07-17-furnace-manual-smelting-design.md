# Furnace: manual smelting to unblock the plate bootstrap cycle

Date: 2026-07-17

## Purpose

Verifying the crafting-menu plan (`2026-07-16-crafting-menu-and-build-filter-design.md`)
surfaced a softlock before any code shipped: the Smelter's own hand-craft
recipe costs 3 Copper Plate, Copper Plate is only ever produced by an
already-placed, powered Smelter (via the pre-existing `SmeltRecipe` table),
and there is no other source of plates anywhere in the game. The Drill, Belt,
and Burner Generator recipes all have the same problem. Nobody could ever
craft the first Smelter.

This adds a Furnace: a cheap, powerless, manually-operated machine craftable
from raw Stone alone. It lets a player convert ore into plates by hand -
slower than an automated Smelter, but with zero prerequisites - breaking the
cycle. The existing recipe table (Smelter/Drill/Belt/BurnerGenerator all
costing plates) is otherwise untouched.

## Components

### 1. `ItemType::Furnace` and `MachineType::Furnace`

Both enums gain one more entry, appended at the end (after `Chest` /
`CraftingTable` respectively), following the exact pattern the original
7 machine items/types already established: `ItemInfo{"Furnace", 10,
BlockType::Air, ToolType::None, <color>}` and `MachineInfo{"Furnace", <color>,
generator=false, consumer=false, transport=false, powerRating=0, actionTime=0,
width=1}`. `itemForMachine(MachineType::Furnace)` returns `ItemType::Furnace`.
Unlike the Crafting Table, the Furnace is a normal single-tile machine - there
is no reason for it to be wider.

### 2. Furnace joins the advanced (table) recipe list

One more `CraftRecipe` entry, `requiresCraftingTable = true` like the other
six: `{ItemType::Furnace, {{Stone, 20}, {}}, 4.0f, true}`. The 4.0f craft time
is not separately specified by the user; it's chosen to match the Drill/
Smelter tier, since 20 Stone is the priciest single-ingredient cost in the
table. This is the only new entry in `allCraftRecipes()` - the existing 7 are
unchanged.

### 3. `FurnaceRecipe`: a second, smaller recipe table

A new struct, deliberately separate from both `SmeltRecipe` (the automated
Smelter's recipe, 2.0f/3.5f seconds) and `CraftRecipe` (multi-ingredient hand
crafts): manual smelting has its own timing (5.0f/7.5f, slower than the
automated Smelter on purpose) and a single ingredient, so forcing it through
either existing shape would add complexity neither one needs.

```cpp
// Recipes.h
struct FurnaceRecipe
{
    ItemType in;
    ItemType out;
    float seconds;
};

std::span<const FurnaceRecipe> allFurnaceRecipes();
```

```cpp
// Recipes.cpp
constexpr std::array<FurnaceRecipe, 2> furnaceRecipes = {{
    {ItemType::CopperOre, ItemType::CopperPlate, 5.0f},
    {ItemType::IronOre,   ItemType::IronPlate,   7.5f},
}};
```

### 4. E-key resolution becomes four-way

`Game` gains `std::optional<sf::Vector2i> openFurnaceTile`, reset and
re-resolved in `toggleInventory()` exactly like `openChestTile` and
`openCraftingTableTile` are today - a fourth `else if (machine->type ==
MachineType::Furnace)` branch. All three tile optionals are mutually
exclusive (only one is ever set at a time); `drawInventoryPanels()` gains a
third branch: chest panel (if `openChestTile`), else craft panel (if
`openCraftingTableTile` OR no special tile - unchanged), else **furnace
panel** (if `openFurnaceTile`). Concretely: chest wins over furnace wins over
craft/bag-only, mirroring how chest already won over craft.

### 5. Furnace panel: a smaller sibling of the craft panel

`Hud::drawSmeltPanel(window, bag, smelting, smeltingRecipeIndex,
smeltProgress)` and `Hud::hitTestSmeltButton(screenPos, windowSize)` - no
`advanced` parameter (there's only one view, 2 buttons, no basic/advanced
split) and no ingredient-array loop (`FurnaceRecipe` has exactly one input).
Otherwise identical in spirit to `drawCraftPanel`/`hitTestCraftButton`: same
panel origin, same dimmed-while-disabled/in-progress styling, same
draw/hit-test row-loop symmetry.

### 6. Smelting flow: a parallel, not a shared, state machine

`Game` gains `bool smelting`, `int smeltingRecipeIndex`, `float
smeltProgress` - the Furnace's own single-slot, no-queue progress state,
independent of `crafting`/`craftingRecipeIndex`/`craftProgress`. `Game::
startSmelt(int recipeIndex)` and `Game::updateSmelting(float dt)` mirror
`startCraft`/`updateCrafting` exactly (affordability check before deduction,
background ticking, overflow-to-ground-drop on a full bag), just reading
`allFurnaceRecipes()` and writing the `smelting*` fields instead. Deliberately
not unified with the crafting state: both are small, already-reviewed, and
genuinely operate over different recipe shapes (single-ingredient vs.
up-to-two); forcing a shared abstraction now would mean refactoring shipped,
reviewed code for one additional consumer.

Click dispatch in `handleEvents()`'s inventory-open branch gains a third
case: chest button hit-test (unchanged) → else if `openFurnaceTile`, hit-test
smelt buttons → else hit-test craft buttons (unchanged).

## Data flow summary

- E key → four-way tile resolution → chest / furnace / craft(basic or
  advanced) panel drawn alongside the bag.
- Furnace panel click → `startSmelt` → affordability check → deduct → timer →
  `fixedUpdate`'s `updateSmelting(dt)` ticks it → bag gains a plate (or ground
  drop on overflow).
- Bootstrap order: craft table (15 logs, anywhere) → place it → E it → craft
  a Furnace (20 Stone) → place it → E it → manually smelt mined ore into
  plates → afford the Smelter/Drill/Belt/Burner Generator recipes at the
  table.

## Error handling / invariants

- Same invariants as the original plan's crafting flow: no double-spend (the
  `smelting` gate blocks a second smelt exactly like `crafting` blocks a
  second craft), nothing silently destroyed (overflow spawns a ground drop),
  full-footprint validation reuses the existing `Machines::place()` path
  unchanged (Furnace is single-tile, so this is the already-proven width=1
  case).
- The four tile-optionals (`openChestTile`, `openCraftingTableTile`,
  `openFurnaceTile`) must stay mutually exclusive; `toggleInventory()` resets
  all three before resolving which (if any) applies, exactly as the existing
  two-way version already does for the first two.

## Testing

- `Recipes`: `allFurnaceRecipes()` returns exactly 2 entries with the
  specified in/out/seconds; the new Furnace `CraftRecipe` entry has
  `requiresCraftingTable == true` and costs exactly 20 Stone.
- `MachineType`/`itemForMachine`: Furnace registers with `width == 1` and
  maps to `ItemType::Furnace`.
- `Hud`/`Game` changes (panel rendering/hit-testing, E-key four-way
  resolution, `startSmelt`/`updateSmelting`) get manual verification via the
  project's run/verify workflow, exactly as the original plan's equivalent
  pieces did - `Hud.cpp`/`Game.cpp` are not linked into the test binary.

## Out of scope

- Any change to the existing Smelter/Drill/Belt/Burner Generator recipe
  costs - they stay exactly as already implemented and reviewed.
- Multiple Furnace tiers, Furnace footprint changes, or Furnace ever needing
  power.
- Unifying the crafting and smelting state machines.
