# Item Acceptor: a network-facing storage box, separate from the player's Chest

Date: 2026-07-17

## Purpose

Today `Chest` does double duty: it's the player's manually-accessed storage
(open with `E`, drag items, Deposit All/Collect All) *and* the one machine
belts/chutes/the player's F-key can push items into or pull items out of,
via `Machines::tryInsert`/`tryExtract`/`putBack`. This adds a second,
purpose-built machine — the Item Acceptor — that takes over the
network-facing role entirely: a 10-slot (one row) storage box with all 4
sides accepting pushed items, placed through build mode like the other
factory machines. The Chest keeps its player-storage role (E-panel,
drag-and-drop, hotbar-place, mine-to-remove) but stops being a valid target
for `tryInsert`/`tryExtract` — it is no longer part of the machine network.

## Components

### 1. `ItemType::ItemAcceptor` and `MachineType::ItemAcceptor`

Both enums gain one more entry, appended at the end (after `Furnace`),
following the same pattern every prior machine/item pair has used:
`MachineInfo{"Item Acceptor", <color>, generator=false, consumer=false,
transport=false, powerRating=0, actionTime=0, width=1, height=1}` — a plain
single-tile machine, not furniture. `itemForMachine(MachineType::
ItemAcceptor)` returns `ItemType::ItemAcceptor`. `isFurniture` is
unaffected (Item Acceptor is not in that set — it stays in build mode).

A new constant `ITEM_ACCEPTOR_SLOTS = 10` lives in `MachineType.h` alongside
the existing `CHEST_SLOTS = 20`.

### 2. Storage sizing moves to cover both types

`Machines::place()` currently special-cases `if (type == MachineType::Chest)
m.storage = Inventory(CHEST_SLOTS);`. This becomes a small switch (or two
`if`s): `Chest` still sizes to `CHEST_SLOTS` (20, unchanged — existing
chests keep their capacity), `ItemAcceptor` sizes to `ITEM_ACCEPTOR_SLOTS`
(10). Every other type keeps the default 0-slot `storage`.

### 3. The network special-case moves from Chest to Item Acceptor

`Machines::tryInsert`, `tryExtract`, and `putBack` each currently have a
`if (m->type == MachineType::Chest) { ... }` branch. All three move that
branch to `if (m->type == MachineType::ItemAcceptor)` instead — same body,
same semantics (accepts from any side, no directional restriction; extract
takes the whole first non-empty slot's stack; putBack hands a stack into the
first slot `exchange()` will accept). Chest no longer has a branch in any of
the three, so it falls through to each function's default (`tryInsert`
returns `false`; `tryExtract` returns an empty stack; `putBack` falls to its
generic `m->output = stack` branch, which is not itself a no-op, but is
unreachable for a Chest in practice — `putBack` is only ever called after a
non-empty `tryExtract` at the same tile, and `tryExtract` on a Chest always
returns empty, so nothing ever reaches it).

This is the entire mechanism. Consequences fall out of it directly, not from
any separate code path:

- Belts/chutes can no longer feed a Chest (their `tryInsert` calls simply
  fail against it, same as they already fail against, say, a Belt with
  something already carried).
- **The player's own F-key quick-interact (`Game::interactAtCursor`) shares
  this exact code path** (`machines.tryInsert`/`tryExtract`), so F-key
  insert/extract on a Chest stops working too. This is a real, visible
  behavior change, not a hidden side effect — flagging it explicitly since
  it wasn't separately requested, but it's what "stop being a network node"
  necessarily means once the two share one mechanism.
- The `E`-panel (open bag + chest side-by-side, drag-and-drop, Deposit
  All/Collect All) is **unaffected** — `Game::depositAllToChest`/
  `collectAllFromChest`/`beginDrag`/`endDrag` already manipulate
  `machine->storage` directly via `Inventory::add`/`take`/`exchange`, never
  going through `tryInsert`/`tryExtract` at all. This is why the Chest can
  keep full player-storage functionality while losing network access with
  no changes needed to that code.

### 4. Generalizing the storage panel to two machine types

Rather than duplicate the entire chest-panel UI (drawing, hit-testing,
Deposit/Collect All, drag-and-drop) a second time for the Item Acceptor's
differently-sized inventory, the existing machinery generalizes to cover
both:

- `Game::openChestTile` is renamed `openStorageTile`, and every place that
  currently checks `machine->type == MachineType::Chest` to decide whether
  to open/keep open this panel instead checks `machine->type ==
  MachineType::Chest || machine->type == MachineType::ItemAcceptor`
  (`toggleInventory`'s resolution, `drawInventoryPanels`'s staleness guard).
- `depositAllToChest`/`collectAllFromChest` are renamed
  `depositAllToStorage`/`collectAllFromStorage` — same bodies, just reading
  `openStorageTile` instead of `openChestTile`. `beginDrag`/`endDrag`
  likewise read `openStorageTile`.
- `Hud::drawChestPanel` already draws exactly `chestStorage.slotCount()`
  slots row-major at `CHEST_SLOT_SIZE`/10 columns — for a 10-slot inventory
  this already lays out as a single row with no changes needed.
- `Hud::hitTestPanels`'s hard-coded `CHEST_ROWS = 2` is the one real gap: a
  10-slot panel only draws 1 row, but the old hit-test would still treat a
  second (undrawn) row as clickable, silently absorbing clicks into
  out-of-range slot indices that `Inventory`'s bounds checks safely no-op
  on — not a crash, but dead, confusing hit-space. `hitTestPanels` gains a
  `chestRows` parameter (computed by the caller as `1 + (slotCount() - 1) /
  10`) instead of the hardcoded `2`, so a 10-slot Item Acceptor panel and a
  20-slot Chest panel each hit-test exactly the rows they draw.

`Hud::drawChestButtons` (Deposit All / Collect All) and
`hitTestChestButton` need no changes — they're already generic over
"whatever storage panel is currently open," not type-specific.

### 5. Item Acceptor joins build mode; Chest's recipe changes

`Item Acceptor` is a normal factory machine in build mode: appears in the
palette when held (subject to the same held-count filter as Drill/Belt/
etc.), cyclable by scroll-wheel, and gets `F6` as its direct-select hotkey
(free since the furniture-placement plan removed the old Chest/Crafting
Table F6/F7).

Two `CraftRecipe` changes in `Recipes.cpp`:

- **Item Acceptor** (new entry): `10x Stone, 3x CopperPlate`, `3.0s`,
  `requiresCraftingTable = true`.
- **Chest** (existing entry, ingredients change): `8x OakLog, 2x
  CopperPlate` (was `8x OakLog` alone) — craft time stays `2.0s`,
  unchanged.

### 6. Rendering

No `MachineRenderer` changes needed: `isOutputSide`'s switch only special-
cases `Drill`/`Smelter`/`Belt`/`Chute`; every other type (including the new
`ItemAcceptor`, same as `Chest`/`BurnerGenerator` today) falls through to
`default: return false;`, meaning every side renders as an input tick
automatically — exactly the "4 inputs on every side" the Item Acceptor is
for.

## Data flow summary

- Belt/chute output tick or player F-key → `Machines::tryInsert`/
  `tryExtract` → succeeds only against an `ItemAcceptor` (or a processing
  machine's own input/output buffer) → never against a `Chest`.
- `E` on a Chest or Item Acceptor tile → `openStorageTile` → shared panel
  (bag + that machine's `storage`, sized to its own `slotCount()`) →
  Deposit All/Collect All/drag-and-drop manipulate `storage` directly,
  regardless of which of the two types is open.
- Build mode: Item Acceptor craftable at the table (10 Stone, 3 Copper
  Plate), then placed/cycled/consumed/refunded exactly like every other
  factory machine.

## Error handling / invariants

- Nothing silently destroyed: the storage-panel generalization changes no
  drag-and-drop/exchange logic, only which machine types it applies to, so
  the existing "leftover always routes back to source" invariant is
  untouched.
- A Chest with existing contents (from before this change, or filled via
  the E-panel) is completely unaffected by no longer accepting `tryInsert` —
  its `storage` Inventory and everything in it stays exactly as-is; only the
  *insertion/extraction path* changes, not the data.
- `hitTestPanels`'s new `chestRows` parameter must be computed identically
  by every caller from the same `slotCount()` the corresponding
  `drawChestPanel` call used, or hit-testing and drawing would disagree —
  same discipline this codebase already applies to every other paired
  draw/hit-test function.

## Testing

- `MachineType`/`itemForMachine`: `ItemAcceptor` registers `width=1,
  height=1` and maps to `ItemType::ItemAcceptor`.
- `Machines`: a placed `ItemAcceptor` gets `ITEM_ACCEPTOR_SLOTS` (10) empty
  slots; a placed `Chest` still gets `CHEST_SLOTS` (20, unchanged).
  `tryInsert`/`tryExtract`/`putBack` accept against an `ItemAcceptor` from
  every side (mirroring the existing chest tests, retargeted) and reject
  against a `Chest` (new tests proving the old behavior is gone, not just
  that the new behavior exists).
- `Recipes`: Item Acceptor's recipe is exactly `10x Stone, 3x CopperPlate,
  3.0s, requiresCraftingTable=true`; Chest's recipe is exactly `8x OakLog,
  2x CopperPlate, 2.0s` (ingredient list changed, craft time unchanged).
- `Hud`/`Game` changes (renamed storage-panel state/methods, generalized
  `hitTestPanels` row count, build-mode palette/F6) get manual verification
  via the project's run/verify workflow — `Hud.cpp`/`Game.cpp` aren't linked
  into the test binary, consistent with every prior plan's equivalent
  pieces.

## Out of scope

- Any automatic extraction (a belt "pulling" from an Item Acceptor) — this
  codebase has never had that for any machine; Item Acceptor doesn't add it.
- Multiple Item Acceptor tiers, or Item Acceptor ever needing power.
- Changing Chest's storage capacity (`CHEST_SLOTS` stays 20) or its
  placement/removal model (still furniture — hotbar-place, mine-to-remove).
- Migrating existing save data — this project has no save/load system, so
  there is no existing-world Chest-contents migration concern.
