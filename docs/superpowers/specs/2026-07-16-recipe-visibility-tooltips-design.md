# Recipe visibility in Smelter/Drill tooltips

Date: 2026-07-16

## Purpose

This is the first of a series of beginner-clarity improvements to the factory
system (recipe visibility → palette descriptions → control legend → on-tile
status glyph → power overlay → guided first-machine nudges), scoped and
sequenced with the user as one arc, each getting its own spec.

Today a Smelter's tooltip never says what it accepts — an idle Smelter just
shows "Waiting for ore." with no hint that Copper Ore or Iron Ore is what it
wants. Likewise a Drill's tooltip never lists which ore types count; its idle
reason ("No ore within 4 tiles below.") doesn't say Copper Ore, Iron Ore, and
Coal are the three it can mine. A first-time player has no way to learn either
list without reading source or trial-and-error. This spec adds a short,
always-visible line to each tooltip stating what the machine accepts.

## Components

### 1. Smelter recipes become enumerable

`Recipes.h` currently exposes only `smeltRecipeFor(ItemType in)`, a lookup by
a single input. Add:

```cpp
std::span<const SmeltRecipe> allSmeltRecipes();
```

implemented in `Recipes.cpp` as a view over the existing `recipes` array (no
change to how recipes are defined or looked up by `smeltRecipeFor`, which
keeps its current signature and behaviour). This is the only new entry point;
the array stays the single source of truth for smelter recipes.

### 2. Drill's mineable ore list becomes a named, shared constant

`Machines.cpp` has a private `isOre(BlockType b)` helper (checks for
`CopperOre`, `IronOre`, `Coal`) used by `tickDrills` and `idleReason`. It's
correct but invisible outside that file. Add to `MachineType.h`, alongside
`DRILL_REACH`:

```cpp
// Ore item types a Drill can mine, in display order.
inline constexpr std::array<ItemType, 3> DRILL_ORES = {
    ItemType::CopperOre, ItemType::IronOre, ItemType::Coal};
```

(`MachineType.h` gains an `#include "../Items/Items.h"` for `ItemType`.)

`Machines.cpp`'s `isOre(BlockType b)` is rewritten to derive from
`DRILL_ORES` instead of hardcoding the same three types a second time:

```cpp
bool isOre(BlockType b)
{
    const ItemType item = itemForBlock(b);
    return std::find(DRILL_ORES.begin(), DRILL_ORES.end(), item) != DRILL_ORES.end();
}
```

One source of truth: the mining logic and the tooltip can't drift apart.

### 3. Two new tooltip lines, always shown

`Hud::drawMachineTooltip` gains a line right after the name line (before the
Fuel/Powered line), conditioned on machine type:

- Smelter: `"Smelts: Copper Ore -> Copper Plate, Iron Ore -> Iron Plate"`
- Drill: `"Mines: Copper Ore, Iron Ore, Coal"`

Both use `->` (not `→`) to avoid non-ASCII glyphs in the bundled font. Each
line is built by a small pure function so it's unit-testable without a live
`sf::RenderWindow`, mirroring how hit-testing was factored out in the
logistics/inventory-UI spec:

```cpp
// Hud.h / Hud.cpp (or a small free function near the tooltip code)
std::string formatSmeltRecipeList(std::span<const SmeltRecipe> recipes);
std::string formatDrillOreList(std::span<const ItemType> ores);
```

`formatSmeltRecipeList` joins `itemInfo(r.in).name + " -> " + itemInfo(r.out).name`
for each recipe with `", "`. `formatDrillOreList` joins `itemInfo(ore).name`
for each entry with `", "`. `drawMachineTooltip` prefixes the result with
`"Smelts: "` / `"Mines: "` and pushes it as a `TooltipLine` the same way
existing lines are pushed.

These lines are unconditional (always shown for their machine type), unlike
Input/Output/bar/reason, which keep their existing conditional logic
untouched.

## Data flow summary

No new state and no change to any tick/update logic beyond the `isOre`
rewrite (which is behaviourally identical — same three block types accepted,
just expressed once instead of twice). This is purely additive display logic:
`drawMachineTooltip` reads `allSmeltRecipes()` / `DRILL_ORES` and formats them
via the two new pure functions.

## Error handling / invariants

- `allSmeltRecipes()` and `DRILL_ORES` are both compile-time-sized; there is
  no empty/missing case to guard against a tooltip formatting function
  running off a real machine's type.
- The `isOre` rewrite must accept exactly the same three `BlockType`s as
  before — covered by existing drill-mining tests, which continue to pass
  unchanged.

## Testing

- `formatSmeltRecipeList` / `formatDrillOreList`: unit tests asserting exact
  output strings against `allSmeltRecipes()` and `DRILL_ORES` (e.g. "Copper
  Ore -> Copper Plate, Iron Ore -> Iron Plate" and "Copper Ore, Iron Ore,
  Coal").
- `Machines`: existing drill-mining tests continue to pass, confirming the
  `isOre` rewrite is behaviour-preserving.
- Tooltip rendering itself (the panel, line layout) is graphical and gets
  manual verification via the project's run/verify workflow, consistent with
  how the rest of the HUD is covered in this codebase.

## Out of scope

- Palette machine descriptions, control legend, on-tile status glyphs, power
  network overlay, and guided first-machine nudges — later specs in this
  arc.
- Recipe/ore lines for the BurnerGenerator (its idle reason already names
  "coal" explicitly, so it doesn't have the same gap) or Chest (no recipe
  concept).
- Any change to how recipes are authored, added, or balanced.
