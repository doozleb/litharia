# Factory logistics, chests, and inventory UI

Date: 2026-07-15

## Purpose

Today a drill/smelter's output only ever tries to push into the single tile it
faces, there is no storage machine, the player's bag has 30 slots of hidden
capacity with no UI to see them, blocks can be placed on top of factory
equipment, build mode has no visual picker, and the machine tooltip anchors to
the raw mouse position instead of the machine it describes. This spec covers
all six, since they share the same underlying seams (machine I/O, the
`Inventory` type, and Game's input/HUD layer) and were scoped together with
the user as one arc.

## Components

### 1. Round-robin multi-side machine output

`Machines::insertOutputAhead()` currently computes a single target tile from
`m.facing` and tries to insert there. It's called once per tick for every
drill and smelter that has output.

Change: `Machine` gains `Direction outputCursor`, seeded to `facing` when the
machine is placed. Output delivery tries up to 4 tiles, in cyclic order
starting at `outputCursor` (`Up, Down, Left, Right` rotated so the cursor's
direction is first), stopping at the first `tryInsert` that succeeds. On
success, `outputCursor` is advanced to the direction *after* the one that
worked, so the next tick starts there. On total failure (all 4 sides refuse,
or there's nothing on any of them), `outputCursor` is left where it is and the
output stays put for next tick.

This gives the round-robin behaviour requested: a smelter with belts on its
left and right will alternate feeding them, because each successful send
advances the cursor past the side it just used, landing on the other one.
`facing` keeps its existing meaning (the *first* side tried, and what the
renderer's direction tick shows) but is no longer the *only* side tried.

Applies to `tickDrills` and `tickSmelters`, both of which already call
`insertOutputAhead`. No other machine currently produces standalone output, so
no other call sites change. A generator has no output stack and is unaffected.

### 2. Chests

New `MachineType::Chest`, inserted before `Count`. Registry entry: distinct
color, `generator = false`, `consumer = false`, `transport = false`,
`powerRating = 0`, `actionTime = 0`.

A chest needs multi-slot storage; the current `Machine` struct only has one
`ItemStack` each for input and output. Rather than add a fixed-size array that
every non-chest machine would carry for nothing, `Machine` gains an
`Inventory storage` field (see the `Inventory` change below) that stays at 0
slots for every machine type except `Chest`. `Machines::place()` special-cases
`Chest` to construct `storage` with `CHEST_SLOTS` (20) slots.

`Machines::tryInsert`, `tryExtract`, and `putBack` each get a `Chest` branch:

- `tryInsert`: `storage.add({item, 1})`, succeeds if nothing is leftover.
- `tryExtract`: takes the *entire stack* from the first non-empty slot found
  (mirrors the existing "take everything" semantics `tryExtract` already has
  for a drill/smelter's single output buffer — a chest just has more than one
  place to look).
- `putBack`: hands a stack back into the first slot `exchange()` will accept
  (mirrors `tryExtract`'s counterpart contract of never destroying items).

Because these are the same three entry points belts and the F key already go
through, a belt whose output side (per the round-robin above) lands on a
chest just works, and F still does its existing quick single-item swap
against a chest with no special-casing anywhere else.

`CHEST_SLOTS = 20` lives in `MachineType.h` alongside the existing
`COAL_BURN_SECONDS` / `DRILL_REACH` constants.

### 3. `Inventory` becomes variably sized

Backing store changes from `std::array<ItemStack, SIZE>` to
`std::vector<ItemStack>`, sized by a constructor argument:
`explicit Inventory(int slotCount = SIZE)`. `SIZE = 40` remains the default
(the player's bag keeps constructing as `Inventory bag;` with no call-site
changes), and a chest constructs `Inventory(CHEST_SLOTS)`. `HOTBAR_SIZE = 10`
is unaffected — it's a convention about the first 10 slots, not a size.

Two new primitives, needed by drag-and-drop (component 6) and reused by the
chest's `tryExtract`/`putBack`:

- `ItemStack take(int index)` — empties the slot, returns what was there.
- `ItemStack exchange(int index, ItemStack incoming)` — merges `incoming`
  onto a matching stack as far as it fits, or swaps wholesale if the slot
  holds a different item. Returns whatever doesn't end up in the slot: the
  merge remainder, or the swapped-out stack. Empty stack in means "just empty
  the slot and hand back what was there," so `take` is expressible as
  `exchange(index, {})` but is kept as a named method for callers that don't
  want to construct an empty `ItemStack` themselves.

`add()`, `removeOne()`, `slot()`, `isEmpty()`, `count()` keep their existing
signatures and behaviour. A new `slotCount() const` lets callers (Hud, drag
logic) iterate an inventory without hardcoding 40.

### 4. Placement blocked on machine tiles

`Player::place()` has no visibility into `Machines` today. `Player::update()`
gains a trailing parameter: `const Machines* machines = nullptr`. Defaulted
and trailing means every existing test call site (`player.update(input,
world, STEP)`) keeps compiling with unchanged behaviour (no machine, no
collision check). `place()` adds one more rejection alongside its existing
solid-tile and player-overlap checks: `machines != nullptr &&
!machines->canPlace(tileX, tileY)` refuses the placement. `Game::fixedUpdate`
passes `&machines`.

### 5. Inventory/chest panels and the E key

New `Game` state:

- `bool inventoryOpen`
- `std::optional<sf::Vector2i> openChestTile` — set when the panel opened
  because a chest was under the cursor; `nullopt` for a bag-only view.

E toggles: if `inventoryOpen`, close it. If closed, check
`machines.at(cursorTile())`; if it's a `Chest`, open with
`openChestTile = tile` (chest + bag side by side); otherwise open bag-only.
Opening forces `buildMode = false`; toggling `buildMode` on with `B` forces
`inventoryOpen = false` — the two are mutually exclusive since both repurpose
mouse clicks.

While a chest panel is open, `openChestTile` is re-resolved via
`machines.at()` every frame rather than caching a `Machine*` (pointers into
`Machines` are documented as invalidated by `remove()`). If the tile no
longer holds a chest (removed some other way), the panel closes itself back
to bag-only or fully closed.

`readInput()`'s `mine`/`place` gating extends from `!buildMode` to
`!buildMode && !inventoryOpen`: clicks while a panel is open are UI
interactions, not world actions.

### 6. Drag-and-drop

Game tracks: `bool dragging`, `ItemStack dragStack`, and where it came from
(`Panel { Bag, Chest }` + slot index) so a cancelled drag (released outside
any slot) can go back where it started instead of vanishing.

Mouse-down while a panel is open hit-tests the click against the visible
panels; hitting a non-empty slot starts a drag via `take()`. Mouse-up
hit-tests again; hitting a slot calls `exchange()` on the destination
inventory (bag or chest, whichever panel), with any returned leftover routed
back to the source slot via a second `exchange()` (so nothing is ever
destroyed — same invariant `tryExtract`/`putBack` already uphold elsewhere in
this codebase). Releasing outside any slot is treated as releasing on the
source slot.

Hud exposes a hit-test function (screen position → panel + slot index) built
from the exact same geometry constants `draw()` uses, so hit-boxes and
drawing never drift apart. Hud also gains a `drawDragGhost()` used by
`render()` to draw the held stack at the live cursor position while
`dragging` is true, and a generalized grid-drawing routine for an arbitrary
`Inventory` (used for the 4-row bag panel and the 2-row chest panel; the
always-visible hotbar strip is untouched).

### 7. Build-mode palette

While `buildMode` is on, the mouse wheel now cycles `buildType` (skipping
`MachineType::None`, wrapping) instead of the hotbar slot; hotbar cycling is
unchanged outside build mode. Hud draws a small horizontal strip of machine
types (color swatch + name), highlighting the current `buildType`, windowed/
centered on the selection so it reads as scrollable even with today's small
machine count, plus a caption: "Left click: place · Right click: destroy".

### 8. Tooltip anchoring fix

Root cause: `Game::drawMachineTooltip()` positions the panel at
`sf::Mouse::getPosition(window) + (16, 16)` — raw screen-space cursor plus a
fixed offset, which is why it sits down-right of the cursor and drifts from
the machine as the camera scrolls.

Fix: convert the machine's *world* position (top-center of its tile) through
the camera to screen space via `window.mapCoordsToPixel(...)`, and change
`Hud::drawMachineTooltip`'s layout from "offset down-right of pos" to
"bottom-center of the panel sits a fixed margin above pos, horizontally
centered." The tooltip then tracks the machine's tile directly instead of the
mouse.

## Data flow summary

- Drill/smelter tick → `insertOutputAhead` tries `outputCursor`'s side, then
  round-robins → `tryInsert` on whatever's there (belt, chute, chest, another
  processor's input) → success advances the cursor.
- Belt tick → same `tryInsert` path → a chest's `Chest` branch → `storage.add`.
- E key → resolve cursor tile → open bag-only or chest+bag panel.
- Mouse down/up while a panel is open → hit-test → `Inventory::take` /
  `Inventory::exchange` → panels re-rendered from the mutated `Inventory`
  state next frame.
- Player `place` → existing checks + `Machines::canPlace` → world mutation.

## Error handling / invariants

- Nothing is ever silently destroyed: `exchange()`'s leftover always routes
  back to the source slot on a failed/partial drop, matching the existing
  `tryExtract`/`putBack` contract used elsewhere in `Machines`.
- A chest that's removed while its panel is open (not currently reachable in
  play, since build mode and inventory are mutually exclusive, but kept as a
  guard) closes the panel rather than operating on a stale tile.
- `Player::update`'s new parameter defaults to `nullptr`, so no existing test
  changes behaviour by omission.

## Testing

- `Machines`: round-robin output test (two belts flanking a smelter alternate
  receiving items over several ticks); chest `tryInsert`/`tryExtract` tests
  (fills across multiple slots, overflow when full); belt-into-chest
  integration test.
- `Inventory`: `exchange()`/`take()` unit tests (merge with room, merge with
  overflow leftover, swap onto a different item, exchange into an empty
  slot); variable-size construction test (a 20-slot `Inventory` reports
  `slotCount() == 20` and rejects/accepts consistently with that bound).
- `Player`: "cannot place onto a machine tile" test constructing a real
  `Machines` with one machine placed, passed into `update()`.
- Hud/Game UI (hit-testing, drag state, tooltip position, build palette):
  the hit-test geometry function is factored out so it's callable without a
  live `sf::RenderWindow` and gets unit tests; the rest (rendering, actual
  mouse-driven drag) is graphical and gets manual verification via the
  project's run/verify workflow rather than automated tests, consistent with
  how the rest of the HUD is already covered in this codebase.

## Out of scope

- Stack-splitting (right-click for half a stack) in drag-and-drop.
- Chest capacity beyond 20 slots, or multiple chest tiers.
- Any UI scrollbar dragging for the build palette beyond mouse-wheel cycling.
- Multi-tile machine footprints (chests remain single-tile, per the existing
  "single tile" note on `Machine`).
