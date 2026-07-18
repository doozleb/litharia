# Copper/Iron machine tiers: Drill, Belt, Chute, Smelter

Date: 2026-07-18

## Purpose

Today `Drill`, `Belt`, `Chute`, and `Smelter` each exist as a single
`MachineType`/`ItemType` with one fixed speed. This adds a Copper and an Iron
tier for each of the four: Copper is a slower, cheaper starter version
reachable right after the manual Furnace bootstrap; Iron is a noticeably
faster upgrade gated behind Iron Plate. Today's numbers become the Iron
tier's numbers unchanged; Copper is new and slower.

Burner Generator, Chest, Crafting Table, Furnace (manual), and Item Acceptor
are explicitly **not** tiered by this spec - they either have no
speed/capacity dimension that fits the pattern (Crafting Table, manual
Furnace) or were deliberately dropped from scope during design (Burner
Generator, Chest, Item Acceptor).

## Components

### 1. Eight machine/item types replacing four

`MachineType` and `ItemType` each replace their single `Drill` / `Belt` /
`Chute` / `Smelter` entry with two: `CopperDrill`/`IronDrill`,
`CopperBelt`/`IronBelt`, `CopperChute`/`IronChute`,
`CopperSmelter`/`IronSmelter`. `itemForMachine()` gains a case for each.
There is no save/load system in this codebase yet, so there is nothing to
migrate - the rename/insert is a pure source-level change.

Four new family-check helpers live alongside the enum (`MachineType.h`/`.cpp`,
next to the existing `isFurniture()`):

```cpp
bool isDrill(MachineType type);   // CopperDrill or IronDrill
bool isBelt(MachineType type);    // CopperBelt or IronBelt
bool isChute(MachineType type);   // CopperChute or IronChute
bool isSmelter(MachineType type); // CopperSmelter or IronSmelter
```

Every existing single-type equality check that means "this is a drill" (etc.)
becomes a call to the matching helper, so behavior code is written once per
family regardless of tier:

- `Machines.cpp`: mining-reach status check, drill tick loop, smelter
  input-gating and tick loop, chute's always-down output direction.
- `MachineRenderer.cpp`'s `isOutputSide()` switch: the existing
  `case MachineType::Drill: case MachineType::Smelter:` grouping (and the
  `Belt`/`Chute` cases) gains the four new labels in the same groups - the
  switch already expresses family grouping, so this is new `case` labels, not
  new branches.
- `MachineStatus.cpp`'s `barStatus()`: the Drill branch reads
  `machineInfo(m.type).actionTime` (generic on `m.type`, not the hardcoded
  `MachineType::Drill` literal it uses today) so it resolves correctly for
  either tier; the Smelter branch's fraction becomes `m.progress /
  (recipe->seconds * machineInfo(m.type).speedMultiplier)` (see Component 3).
- `Hud.cpp`'s tooltip builder (~line 607-610): same family-check swap for the
  "Smelts:"/"Mines:" lines.

### 2. Registry: one slowdown constant, not four hand-tuned numbers

A new constant in `MachineType.h`, next to `DRILL_REACH`:

```cpp
// Copper-tier machines run this much slower than their Iron-tier equivalent.
inline constexpr float COPPER_TIER_SLOWDOWN = 1.75f;
```

`MachineRegistry.cpp`'s table gets 8 rows instead of 4. Iron-tier rows keep
today's exact `actionTime` values; Copper-tier rows compute theirs as
`<iron value> * COPPER_TIER_SLOWDOWN` at table-definition time, so the ratio
between every Copper/Iron pair stays identical and retuning later is a
one-line change:

| Machine | Iron actionTime (= today) | Copper actionTime |
|---|---|---|
| Drill | 3.0s / ore | 5.25s / ore |
| Belt | 0.5s / tile | 0.875s / tile |
| Chute | 0.5s / tile | 0.875s / tile |

`powerRating` is identical between a machine's Copper and Iron rows - only
speed changes; this feature deliberately does not touch the power budget.

Each pair keeps its family's base color, tinted toward Copper Ore's hue
(`{201, 116, 56}`) for the Copper row or Iron Ore's hue (`{166, 174, 190}`)
for the Iron row, so the two tiers read as visually related but
distinguishable at a glance.

### 3. Smelter timing: one shared recipe table, a per-machine multiplier

The Smelter is the one family whose timing isn't a flat `MachineInfo::
actionTime` - it comes from the shared `SmeltRecipe` table (`recipe->seconds`)
since one Smelter processes either Copper Ore or Iron Ore. Duplicating that
table per tier would let the ore->plate mapping drift out of sync, so instead
`MachineInfo` gains one new field:

```cpp
float speedMultiplier = 1.0f; // only meaningful for Smelter-family types today
```

`IronSmelter` registers `1.0f`; `CopperSmelter` registers
`COPPER_TIER_SLOWDOWN`. `Machines.cpp`'s smelt tick
(`tickSmelters`, currently `m.progress >= recipe->seconds`) becomes
`m.progress >= recipe->seconds * machineInfo(m.type).speedMultiplier`. The
`SmeltRecipe` table itself (`CopperOre->CopperPlate` 2.0s,
`IronOre->IronPlate` 3.5s) is unchanged and stays the single source of the
ore/plate mapping; only the multiplier varies by which Smelter tier runs it.

Resulting effective smelt times:

| Recipe | Iron Smelter (×1.0) | Copper Smelter (×1.75) |
|---|---|---|
| Copper Ore -> Copper Plate | 2.0s | 3.5s |
| Iron Ore -> Iron Plate | 3.5s | 6.125s |

A Copper Smelter can still process Iron Ore (just slowly) - this is what
lets a player reach the Iron tier's crafting cost (Iron Plate) without a
chicken-and-egg dependency on already owning an Iron Smelter.

### 4. Recipe costs: own-tier plate + Stone, symmetric

`Recipes.cpp`'s `craftRecipes` table gets 8 entries in place of today's 4
(`Belt`, `Chute`, `Drill`, `Smelter`). Every recipe costs exactly its own
tier's plate plus Stone - no cross-metal mixing, and no recipe needs more
than the existing 2-ingredient-slot `CraftRecipe` shape:

| Output | Ingredients | Craft time | Requires table |
|---|---|---|---|
| Copper Belt | 2 Copper Plate, 2 Stone | 1.0s | yes |
| Iron Belt | 2 Iron Plate, 2 Stone | 1.0s | yes |
| Copper Chute | 1 Copper Plate, 2 Stone | 1.0s | yes |
| Iron Chute | 1 Iron Plate, 2 Stone | 1.0s | yes |
| Copper Drill | 4 Copper Plate, 2 Stone | 4.0s | yes |
| Iron Drill | 4 Iron Plate, 2 Stone | 4.0s | yes |
| Copper Smelter | 3 Copper Plate, 5 Stone | 4.0s | yes |
| Iron Smelter | 3 Iron Plate, 5 Stone | 4.0s | yes |

This replaces today's Belt/Chute/Drill/Smelter costs (which mixed Copper and
Iron Plate together) rather than layering on top of them - the bootstrap
order becomes: craft table -> Furnace (Stone only) -> manually smelt Copper
Ore -> craft the entire Copper tier -> mine Iron Ore -> auto-smelt it on a
Copper Smelter (or hand-smelt it on the Furnace) -> craft the Iron tier.

### 5. Build palette and hotkeys

`Hud::drawBuildPalette` and `Game::cycleBuildType()` are unaffected in logic -
both already iterate `MachineType::Count` and filter to held items, so 8
transport/production types instead of 4 need no code change.

F-key bindings change: F2-F5 (today: Drill, Belt, Chute, Smelter) are
removed. F1 (Burner Generator), F6 (Item Acceptor), and F7 (Crafting Table)
are unchanged. Selecting a specific Copper or Iron variant is scroll-cycle or
palette-click only. `Game.h`'s default `buildType` (currently
`MachineType::Belt`) becomes `MachineType::CopperBelt`.

`Game.cpp`'s belt-facing-reset guard (today `buildType == MachineType::Belt`,
two call sites) becomes `isBelt(buildType)` so it still applies to whichever
Belt tier is selected.

## Data flow summary

- Registry lookup (`machineInfo(type)`) is still the single source of a
  machine's speed/power/color/footprint - Copper and Iron are just two more
  rows, not a parallel system.
- Drill/Belt/Chute: `actionTime` read directly off the machine's own
  registry row - no new runtime branching, tier is baked into which row a
  placed machine points at.
- Smelter: ore/plate mapping from the shared `SmeltRecipe` table x a
  per-machine `speedMultiplier` from the registry row.
- Crafting: `allCraftRecipes()` gains 4 rows; existing craft-panel/palette
  code iterates the table generically and needs no shape change.
- Behavior dispatch (`Machines.cpp`, `MachineRenderer.cpp`,
  `MachineStatus.cpp`, `Hud.cpp`): family-check helpers replace single-type
  equality checks; the underlying behavior per family is untouched.

## Error handling / invariants

- `speedMultiplier` defaults to `1.0f` for every non-Smelter row, so existing
  `machineInfo(type).actionTime`-driven timing (Drill/Belt/Chute) is
  unaffected by this field's introduction.
- The `SmeltRecipe` table (ore->plate mapping and base seconds) stays single-
  sourced; only the multiplier varies by which Smelter tier is running it -
  there is exactly one place the ore/plate mapping can drift, matching the
  existing invariant for that table.
- `isDrill`/`isBelt`/`isChute`/`isSmelter` are the only places a `MachineType`
  is tested against "is this family" - no call site should reintroduce a
  direct `== MachineType::CopperDrill` (or `IronDrill`) style check for
  behavior that's identical across the pair.
- A Copper Smelter can process both ore types (slowly) - the Iron tier's
  crafting cost is always reachable without owning an Iron Smelter first.
- Power draw (`powerRating`) is identical between a machine's two tiers; this
  is a deliberate scope boundary, not an oversight (see Out of scope).

## Testing

- `MachineType`/`MachineRegistry`: `isDrill`/`isBelt`/`isChute`/`isSmelter`
  return true for both tier values and false for every other type;
  `itemForMachine` maps all 8 new types correctly; each Copper row's
  `actionTime` equals its Iron row's `actionTime * COPPER_TIER_SLOWDOWN`;
  `CopperSmelter.speedMultiplier == COPPER_TIER_SLOWDOWN` and
  `IronSmelter.speedMultiplier == 1.0f`; every other row's `speedMultiplier`
  is `1.0f`.
- `Recipes`: `allCraftRecipes()` contains exactly the 8 rows in the table
  above with the specified ingredients/costs/times; `allSmeltRecipes()` is
  unchanged (still 2 entries, unaffected by the multiplier living on
  `MachineInfo` instead of the recipe).
- `Machines`: drill mining-reach/tick tests parameterized over both
  `CopperDrill`/`IronDrill` confirm each ticks at its own registry
  `actionTime`; smelter tick test confirms
  `CopperSmelter` takes `recipe->seconds * 1.75` and `IronSmelter` takes
  `recipe->seconds` unmultiplied for both ore recipes; chute-always-down and
  belt-facing-output tests parameterized over both tiers of each.
- `Game`: `cycleBuildType()` visits all 8 new held types (plus the
  unaffected Generator/Item Acceptor/Crafting Table) and skips unheld ones,
  same as today's test but with the larger type count; default `buildType`
  is `CopperBelt`; belt-facing-reset guard fires for both `CopperBelt` and
  `IronBelt`.
- `MachineRenderer`/`MachineStatus`/`Hud` changes (switch-case grouping,
  tooltip text, progress-bar fraction) get manual verification via the
  project's run/verify workflow, consistent with how this codebase already
  treats non-test-linked rendering/HUD code.

## Out of scope

- Tiering Burner Generator, Chest, Crafting Table, manual Furnace, or Item
  Acceptor - explicitly excluded during design.
- Changing `powerRating`/power draw between a machine's two tiers.
- Any tier beyond Copper/Iron (e.g. a future Gold or Diamond tier).
- An in-place upgrade mechanic (Copper and Iron are distinct placeable
  machines, not one machine that levels up) or any migration path for a
  hypothetical existing save (no save system exists yet).
- Rebalancing `COPPER_TIER_SLOWDOWN`, the recipe cost table, or craft times
  beyond the values specified here - all are one easily-retuned constant/table
  if playtesting says otherwise.
- New or reassigned F-key bindings for the 8 split types - palette/scroll
  selection only.
