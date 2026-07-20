# Obsidian machine tier: Drill, Belt, Chute, Smelter

**Date:** 2026-07-20
**Status:** Approved design, ready for implementation

## Goal

Add a third, faster tier above Iron for the same four machine families the
2026-07-18 Copper/Iron spec tiered: Drill, Belt, Chute, Smelter. Obsidian is
already mineable (requires an Iron Pickaxe) and already used directly (no
smelted "plate") in the Obsidian Pickaxe/Axe recipes - the machine tier
follows that same raw-material pattern rather than inventing an Obsidian
Plate.

This is an extension of the existing tier system, not a new one: it adds a
third registry row per family and a third enum value per family, following
exactly the pattern `isDrill`/`isBelt`/`isChute`/`isSmelter` and the
`speedMultiplier` field already established.

## Decisions (from brainstorming)

- **Speed:** `OBSIDIAN_TIER_SPEEDUP = 0.6f`, multiplying Iron's `actionTime`
  (Drill/Belt/Chute) or `speedMultiplier` (Smelter) down - i.e. Obsidian runs
  at ~1.67x Iron's speed. Chosen to roughly mirror Copper's 1.75x slowdown in
  the other direction, so the three tiers read as an even progression
  (Copper 1.75x actionTime, Iron 1x, Obsidian 0.6x).
- **Material:** raw `Obsidian` item, not a new "Obsidian Plate" - Obsidian
  needs no smelting step today (unlike Copper/Iron Ore), and the existing
  Obsidian tool recipes already consume it raw. No new `SmeltRecipe` entries.
- **Recipe costs:** mirror the Iron row's ingredient counts exactly, swapping
  `IronPlate` for `Obsidian` (Belt: 2, Chute: 1, Drill: 4, Smelter: 3), same
  Stone counts and craft times as Iron. This is the simplest default: no
  strong reason to price Obsidian machines differently from Iron ones since
  Obsidian itself already gates behind an Iron Pickaxe and natural scarcity
  (it only forms where lava meets water). Easy to retune later if playtesting
  says otherwise.
- **Colors:** each family's own color, tinted toward Obsidian's hue
  (`{40, 20, 55}`) the way Copper/Iron tint toward their ore colors:
  - Obsidian Drill `{90, 70, 100}`
  - Obsidian Belt `{80, 60, 95}`
  - Obsidian Chute `{75, 55, 90}`
  - Obsidian Smelter `{95, 65, 100}`

## Components

### 1. Enum additions

`MachineType` (`MachineType.h`) gains `ObsidianDrill`, `ObsidianBelt`,
`ObsidianChute`, `ObsidianSmelter` (appended after their Iron counterpart,
keeping each family's two existing entries adjacent to the new third).
`ItemType` (`Items.h`) gains the matching four entries in the same relative
position as `MachineType`.

`isDrill`/`isBelt`/`isChute`/`isSmelter` (`MachineRegistry.cpp`) each gain an
`|| type == MachineType::Obsidian...` arm. Every call site already goes
through these helpers (confirmed: `Machines.cpp`'s mining-reach/tick/smelter/
chute-direction checks, `MachineStatus.cpp`, `Hud.cpp`'s tooltip builder, and
`Game.cpp`'s belt-facing-reset guard all use the helpers already, not direct
`MachineType` equality) - none of them need their own edits beyond the
helper.

`MachineRenderer.cpp`'s `isOutputSide()` switch is the one place with
explicit per-type `case` labels grouped by family; each of the four groups
gains its `case MachineType::Obsidian...:` label alongside its existing
Copper/Iron pair.

### 2. Registry: one new constant, four new rows

`MachineType.h`, next to `COPPER_TIER_SLOWDOWN`:

```cpp
// Obsidian-tier machines run this much faster than their Iron-tier
// equivalent (multiplies actionTime/speedMultiplier down, not up).
inline constexpr float OBSIDIAN_TIER_SPEEDUP = 0.6f;
```

`MachineRegistry.cpp`'s table gets 4 more rows, each placed directly after
its family's Iron row:

| Machine | Iron actionTime | Obsidian actionTime (`×0.6`) |
|---|---|---|
| Drill | 3.0s / ore | 1.8s / ore |
| Belt | 0.5s / tile | 0.3s / tile |
| Chute | 0.5s / tile | 0.3s / tile |

`ObsidianSmelter` registers `speedMultiplier = OBSIDIAN_TIER_SPEEDUP`
(`0.6f`), same mechanism as `CopperSmelter`'s `COPPER_TIER_SLOWDOWN`:

| Recipe | Iron Smelter (×1.0) | Obsidian Smelter (×0.6) |
|---|---|---|
| Copper Ore -> Copper Plate | 2.0s | 1.2s |
| Iron Ore -> Iron Plate | 3.5s | 2.1s |

`powerRating` matches the family's existing value (unchanged across tiers,
same as Copper/Iron) - this spec doesn't touch the power budget either.

`itemForMachine` (`MachineRegistry.cpp`) gains the four new
`MachineType -> ItemType` cases.

### 3. Recipes: own-tier material + Stone, symmetric with Iron

`Recipes.cpp`'s `craftRecipes` table gets 4 more rows, directly mirroring
their Iron row with `Obsidian` in place of `IronPlate`:

| Output | Ingredients | Craft time | Requires table |
|---|---|---|---|
| Obsidian Belt | 2 Obsidian, 2 Stone | 1.0s | yes |
| Obsidian Chute | 1 Obsidian, 2 Stone | 1.0s | yes |
| Obsidian Drill | 4 Obsidian, 2 Stone | 4.0s | yes |
| Obsidian Smelter | 3 Obsidian, 5 Stone | 4.0s | yes |

### 4. Items registry

`Items.cpp` gains 4 rows (name, maxStack matching the family's existing
value - 50 for Belt/Chute, 10 for Drill/Smelter - `BlockType::Air`
`placeBlock`, `ToolType::None`, and the tinted colors from Decisions above).

### 5. Build palette and hotkeys

Unaffected in logic, same as the Copper/Iron spec: `Hud::drawBuildPalette`
and `Game::cycleBuildType()` iterate `MachineType::Count` generically. No
F-key changes (none were assigned to Copper/Iron tiers either) - Obsidian
variants are palette/scroll-cycle selection only.

## Data flow summary

- Registry lookup (`machineInfo(type)`) stays the single source of truth;
  Obsidian is a third row per family, not a parallel system.
- Drill/Belt/Chute: `actionTime` read directly off the row, same as today -
  no new runtime branching.
- Smelter: unchanged mechanism, `speedMultiplier` now has a third value in
  circulation (`0.6f`) alongside `1.0f` and `1.75f`.
- Behavior dispatch: no source changes needed outside the helpers and the
  renderer's switch, per the file-by-file check above.

## Testing

- `MachineType`/`MachineRegistry`: `isDrill`/`isBelt`/`isChute`/`isSmelter`
  now also return true for the Obsidian value and still false for unrelated
  types; `itemForMachine` maps all 4 new types; each Obsidian row's
  `actionTime` equals its Iron row's `actionTime * OBSIDIAN_TIER_SPEEDUP`;
  `ObsidianSmelter.speedMultiplier == OBSIDIAN_TIER_SPEEDUP`.
- `Recipes`: `allCraftRecipes()` contains the 4 new rows with the specified
  ingredients/costs/times; `allSmeltRecipes()` is unchanged.
- `Machines`: existing Copper/Iron-parameterized tests (drill tick, smelter
  tick, chute-direction, belt-facing) gain a third Obsidian case each,
  confirming it ticks at its own registry `actionTime`/`speedMultiplier`.
- `MachineRenderer`/`MachineStatus`/`Hud`: manual verification via the
  project's run/verify workflow, same as the Copper/Iron spec treated them.

## Out of scope

- Any tier beyond Obsidian.
- An Obsidian Plate item or a new `SmeltRecipe` entry - Obsidian is used raw.
- Changing `powerRating` for any tier.
- Rebalancing `OBSIDIAN_TIER_SPEEDUP`, the recipe costs, or the tint colors
  beyond the values specified here - easily retuned constants if playtesting
  says otherwise, same stance the Copper/Iron spec took.
- New F-key bindings.
