# Reposition hotbar/bag/chest panels to the top-right

Date: 2026-07-15

## Purpose

The hotbar, bag panel, and chest panel currently anchor to the bottom-center
of the window, growing upward from the hotbar. This moves all three to the
top-right corner instead, and gives the chest panel its own smaller,
grey-backed slot style so it reads visually as a distinct container from the
player's own bag.

## Layout

All three panels form a single column hugging the top-right corner, in this
order top to bottom: hotbar, then the bag panel (3 rows), then the chest
panel (2 rows, only drawn while a chest is open). Each panel is separated
from the next by the existing `Hud::MARGIN` (12px) gap, same as today's
bottom-up stack, just inverted.

Today, every panel's origin is horizontally centered:
`startX = (windowSize.x - totalWidth) * 0.5f`. That becomes right-edge
anchored instead: `startX = windowSize.x - totalWidth - MARGIN`. The hotbar's
`y` changes from `windowSize.y - SLOT_SIZE - MARGIN` (bottom-anchored) to
`MARGIN` (top-anchored).

The hotbar and bag panel share the same slot size (48px) and column count
(10), so they share the same width and therefore the same left *and* right
edge — one visually clean column. The chest panel uses smaller 36px slots but
the same 10 columns, so it's narrower than the column above it; it computes
its own right-edge-anchored `startX` from its own (smaller) total width, so
its right edge still lines up flush with the hotbar/bag column's right edge,
inset further on the left. This is what makes the whole stack "hug the right
border" uniformly regardless of each panel's width.

Concretely, in `Hud.cpp`:

- `hotbarOrigin(windowSize)`: `x = windowSize.x - totalWidth - MARGIN`,
  `y = MARGIN`.
- `bagPanelOrigin(windowSize)`: same `x` as `hotbarOrigin` (identical width),
  `y = hotbar.y + SLOT_SIZE + MARGIN`.
- `chestPanelOrigin(windowSize)`: its own right-anchored `x` (using the
  chest's smaller total width), `y = bag.y + BAG_ROWS * SLOT_SIZE +
  (BAG_ROWS - 1) * SLOT_GAP + MARGIN`.

No changes are needed in `HudLayout.cpp` — `gridSlotPosition` and
`hitTestGrid` already take slot size and gap as parameters, so the chest's
smaller size is just a different argument at the call site.

## Chest visual distinction

Chest slots render with a grey slot-background rectangle in place of the
current dark navy one (`sf::Color(20, 20, 28, 170)`). The item icon drawn on
top keeps its normal item-color fill, so items are still identifiable by
color inside a chest — only the backdrop behind the icon changes, marking the
panel as chest storage at a glance.

`drawSlot` currently hardcodes `Hud::SLOT_SIZE` and the dark background
color. It gains two parameters — `slotSize` and `backgroundColor` — so the
bag/hotbar call sites pass `Hud::SLOT_SIZE` and the existing dark color
(unchanged behavior), and the chest call site passes the smaller size and
grey. The icon inset (`ICON_INSET = 10px`) stays a fixed constant rather than
scaling with slot size — simpler, and still leaves a reasonable icon-to-slot
ratio at 36px. Stack-count text size is left unchanged (14pt) since it's
legible at both slot sizes.

## Out of scope

- The build-mode palette and machine tooltip stay where they are today
  (bottom-center / anchored to the hovered machine) — not mentioned by the
  user, unaffected by this change.
- The hotbar continues to render every frame regardless of `inventoryOpen`,
  unchanged — only its position moves.
- No change to drag-and-drop logic itself (`beginDrag`/`endDrag`,
  `hitTestPanels`'s return values) — only the geometry the hit-testing and
  drawing functions are computed against.
