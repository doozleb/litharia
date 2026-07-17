# Furniture placement: Chest, Crafting Table, and Furnace move out of build mode

Date: 2026-07-17

## Purpose

Reported bug: a player who crafts a Crafting Table cannot place it. Root
cause traced in code: `Game::buildType` defaults to `MachineType::Belt` and
only ever changes via F1-F7 or scroll-wheel cycling while in build mode -
there is no click-to-select on the palette strip. A player who has never
pressed the right F-key or scrolled sees the Crafting Table swatch in the
palette (unselected) but every left-click still tries to place the stale,
unheld `buildType`, which silently no-ops.

Rather than patch `buildType` selection, the user's preferred fix changes the
interaction model: Chest, Crafting Table, and the new Furnace are not
"factory stuff" - they're stations you set down like a placed block, not
automation you dial in with a build palette. They move to the same
hotbar-select-and-right-click flow world blocks already use, and are removed
by mining them like a block. Build mode's palette keeps exactly the five
automation types: Burner Generator, Drill, Belt, Chute, Smelter.

## Components

### 1. A small "is this furniture" predicate, not a new registry field

`Game.cpp` gains a local helper (anonymous namespace, mirroring the file's
existing small predicates):

```cpp
MachineType furnitureMachineForItem(ItemType item)
{
    if (item == ItemType::Chest)         return MachineType::Chest;
    if (item == ItemType::CraftingTable) return MachineType::CraftingTable;
    if (item == ItemType::Furnace)       return MachineType::Furnace;
    return MachineType::None;
}
```

No new `MachineInfo` field: this is Game-level UX policy (which three types
get the "normal" treatment), not an intrinsic machine property, so it doesn't
belong in the shared registry every other file reads.

### 2. Placing furniture: same trigger as a world block

`Game::fixedUpdate()` captures its `PlayerInput` in a local instead of
passing `readInput()` straight through, so the same frame's `input.place`
can drive a second check after `player.update()`:

```cpp
void Game::fixedUpdate(float dt)
{
    const PlayerInput input = readInput();
    const ActionResult result = player.update(input, world, dt, &machines);
    ...
    placeFurnitureAtCursor(input);
    ...
}
```

`Player::place()` already no-ops for these items today (every furniture
`ItemInfo::placeBlock` is `BlockType::Air`, and `place()` bails immediately
on that), so there is no double-handling to guard against.

`Game::placeFurnitureAtCursor(const PlayerInput&)`: resolves the held hotbar
item to a `MachineType` via the helper above (returns immediately if it
isn't furniture), then applies the same rules world-block placement already
enforces - `player.inReach(tile)`, no overlap with the player's own body,
`world.isSolid` checked across the full footprint width (reusing
`machineInfo(type).width`, so Crafting Table's 2 tiles both get checked) -
before calling `machines.place(type, tile.x, tile.y, Direction::Right)` and
consuming the item on success. Facing is always `Direction::Right`; none of
the three furniture types read `facing` for anything.

### 3. Removing furniture: mine it like a block

Today `Player::mine()` operates purely on `World` block data and always
bails immediately over a machine tile (the world block underneath any placed
machine is `Air`, since machines can only be placed on non-solid tiles).
Furniture mining is added entirely in `Game`, parallel to `Player`'s own
mining state rather than extending `Player` (consistent with how
`placeMachineAtCursor`/`removeMachineAtCursor`/`interactAtCursor` already
keep all machine-interaction logic in `Game`, not `Player`):

New `Game` state: `bool miningFurniture`, `sf::Vector2i miningFurnitureTarget`,
`float miningFurnitureProgress`. Each fixed step, while `input.mine` is held
and the cursor targets a tile whose machine type resolves through the
furniture set, progress accumulates against a fixed duration (see the open
default below); reaching it calls `machines.remove()` (already
footprint-aware, so either tile of a placed Crafting Table works) and
refunds the item via the same overflow-to-ground-drop helper
`removeMachineAtCursor` already uses - factored out as
`Game::refundMachineItem(MachineType type)` so the two call sites (build-mode
destroy and mine-to-break) share it instead of duplicating the refund logic.

**Default assumption, not yet confirmed:** mining any of the three furniture
types takes a flat 1.0 second, with no tool requirement (bare hands break
them, unlike world blocks which gate on `ToolType`). This mirrors "you built
it, you can always take it back down" rather than treating placed furniture
like terrain. Flagging this explicitly since it's an easy value to change if
it doesn't feel right in play.

### 4. Build mode drops all three furniture types

`Hud::drawBuildPalette`'s `held` filter and `Game::cycleBuildType`'s search
both skip `Chest`, `CraftingTable`, and `Furnace` outright (checked before
the existing `bag.count(itemForMachine(type)) > 0` filter), regardless of how
many the player holds. The `F6` (Chest) and `F7` (Crafting Table) direct-select
hotkeys are removed; no hotkey is added for Furnace. `removeMachineAtCursor`
(build-mode right-click destroy) is left as-is - if a player is in build mode
and right-clicks a furniture machine anyway, it still destroys/refunds
correctly; this is a harmless redundant path, not a designed one, and not
worth special-casing out.

## Data flow summary

- Hotbar-select a furniture item -> right-click (same `input.place` gate as
  blocks: not in build mode, not in inventory, in reach) -> `machines.place()`
  -> item consumed.
- Left-click-and-hold on a placed furniture machine (outside build mode) ->
  `Game`-local mining progress -> `machines.remove()` -> `refundMachineItem()`.
- Build mode's palette/cycling universe shrinks from 7 types to the 5
  automation types; Chest/CraftingTable/Furnace never appear there again.

## Error handling / invariants

- Nothing is silently destroyed: the furniture refund path reuses the exact
  same overflow-to-ground-drop invariant already proven in
  `removeMachineAtCursor`.
- `placeFurnitureAtCursor`'s footprint/solid/reach checks must all pass
  before `machines.place()` is attempted, matching the existing build-mode
  placement's "validate everything, mutate nothing on failure" discipline.
- `Player::place()`'s existing early return for `placeBlock == BlockType::Air`
  items is depended upon here (it's what prevents double-handling); if that
  check is ever removed, this feature silently breaks - worth a code comment
  at both ends pointing at each other.

## Testing

- No automated tests: this entire feature lives in `Game.cpp` (not linked
  into `Litharia_tests`), same boundary as the rest of this plan's Game-level
  work. Manual verification: place a Chest/Crafting Table/Furnace via hotbar
  select + right-click at various distances (in reach, out of reach, onto
  solid ground, onto another machine); mine one down and confirm the item
  refunds; confirm build mode's palette and F-keys never offer the three
  furniture types; confirm the original reported bug (craft table, can't
  place it) is resolved end-to-end.

## Out of scope

- Any tool-requirement gating on furniture mining (flagged above as a
  default choice, open to revision).
- Changing `removeMachineAtCursor`'s scope to exclude furniture types from
  build-mode right-click-destroy.
- Multi-tile furniture other than the already-existing Crafting Table.
