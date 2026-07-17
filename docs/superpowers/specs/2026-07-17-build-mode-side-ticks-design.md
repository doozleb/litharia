# Show machine side ticks only in build mode

Date: 2026-07-17

## Purpose

Every placed machine draws four small input/output side ticks (light blue for
an input side, light red for an output side) so the player can see how items
flow through it. Those ticks are build-time scaffolding: useful while laying
out a factory, visual clutter while watching one run. This makes them appear
only in build mode. Outside build mode a machine shows just its body, its
fuel/progress bar, and any item riding it — the live status you want while the
factory runs — but none of the side markers.

## Components

### `MachineRenderer::draw` gains a `showSideTicks` flag

`MachineRenderer` is stateless today: `draw` is `const` and the class holds no
members. Keep it that way — add a parameter rather than a `setShowTicks()`
setter, so the class stays stateless, the dependency is visible at the call
site, and no mutable flag can drift out of sync with `buildMode`.

```cpp
// MachineRenderer.h
void draw(sf::RenderTarget& target, const Machines& machines, bool showSideTicks) const;
```

The renderer is told *what* to draw, not *why*. It receives `showSideTicks`,
not `buildMode` — the caller owns the reason.

Inside `draw`, the existing four-tick loop:

```cpp
for (Direction side : ALL_SIDES)
{
    sf::RectangleShape& tick = isOutputSide(m, side) ? outputTick : inputTick;
    tick.setPosition({cx + dirDX(side) * 5.0f, cy + dirDY(side) * 5.0f});
    target.draw(tick);
}
```

is wrapped in `if (showSideTicks) { ... }`. Nothing else in the per-machine
draw changes: the body, the fuel/progress bar block, and the carried/output
item glyph all still draw unconditionally (subject to their own existing
conditions), in both modes.

### `Game::render` passes `buildMode`

The single call site becomes:

```cpp
machineRenderer.draw(window, machines, buildMode);
```

`buildMode` is already a `Game` member. No other call site exists.

## Data flow summary

`Game::render()` reads its own `buildMode` and hands it to
`MachineRenderer::draw` as `showSideTicks`. Every frame in build mode draws the
ticks; every frame outside it skips them. There is no state to toggle and
nothing to keep in sync — the flag is recomputed from `buildMode` each frame.

## Error handling / invariants

- Furniture (Chest, Crafting Table, Furnace) already `continue`s before the
  tick loop and so shows no ticks in either mode. That is unchanged and
  correct: those types accept nothing from the machine network, so a tick
  would advertise a connection `Machines::tryInsert` refuses (the exact reason
  the Chest's stale input ticks were removed in a prior change). The Item
  Acceptor is a real network machine and keeps showing its four input ticks —
  in build mode only, like every other network machine.
- No change to the bar or item-glyph rendering: a running smelter, a fuelled
  generator, and an item travelling a belt all still read the same outside
  build mode as inside it. Only the side markers hide.

## Testing

`MachineRenderer.cpp` compiles only into the `Litharia` executable, not into
`Litharia_tests` (it uses SFML Graphics, executable-only by this project's
CMake split), so as with every prior renderer change this is build-verified
rather than doctest-covered, plus a manual visual check by the human:

- Press `B`: the four side ticks appear on every network machine (drill,
  smelter, belt, chute, burner generator, item acceptor).
- Leave build mode: the ticks vanish; bodies, fuel/progress bars, and belt
  items still show.
- Furniture shows no ticks in either mode (unchanged).
- The existing 218 tests stay green — they cannot cover `MachineRenderer.cpp`,
  but confirm nothing else was disturbed.

## Out of scope

- Hiding the fuel/progress bar or the carried-item glyph outside build mode —
  those are live status the player wants while the factory runs, deliberately
  kept visible.
- Any change to which sides count as input vs. output (`isOutputSide`), or to
  the ticks' colour, size, or position.
- A toggle independent of build mode (e.g. an "always show ticks" setting).
