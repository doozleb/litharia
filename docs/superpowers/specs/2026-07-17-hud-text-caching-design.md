# HUD text caching: stop rebuilding sf::Text every frame

Date: 2026-07-17

## Purpose

Opening the crafting menu dropped the game to ~20fps. Profiling that bug
found the cause — SFML rebuilds an `sf::Text`'s geometry whenever its string
is set, at a fixed cost of ~2.8ms in a Debug build (~0.5ms in Release),
independent of how long the string is — and fixed it for the recipe labels
only (commit `f30ef5b`, 40-66ms -> 17ms). The same waste remains at every
other text site in the HUD.

Measuring the whole frame in the worst realistic case (every bag slot full,
inventory open, chest open) shows how bad it gets:

| Frame component (Debug) | Cost |
|---|---|
| `sf::Text` rebuilds (60 texts) | **168 ms** |
| World + machines + entities + player | 4.2 ms |
| Tooltip (not hovering) | 0.02 ms |

That is ~173ms/frame — about **6 fps**, with text accounting for **97.5%** of
the frame. Release is ~5x cheaper and still lands at ~34ms (~29fps), so this
is not merely a Debug artifact.

Two other candidates were measured and are **not** problems, so this spec
deliberately leaves them alone:

- `tickMachines` with **301 machines**: **0.25ms**. The per-tick vector
  allocation and sort in `updatePower()` that an earlier review flagged as a
  Minor concern is, at real scale, irrelevant.
- World rendering, chunk meshes, entities, player: **4.2ms** combined.

The entire performance problem in this game is HUD text. This spec covers
only that.

## Components

### 1. `CachedText`: a text that only rebuilds when its content changes

A small helper, private to `Hud`:

```cpp
// Rebuilding an sf::Text's geometry costs a fixed ~2.8ms (Debug) / ~0.5ms
// (Release) no matter how short the string, and SFML rebuilds whenever the
// string is set. A HUD that shows the same numbers frame after frame must
// therefore never re-set a string it hasn't changed. Drawing a text that
// wasn't rebuilt costs ~0.05ms - 56x cheaper.
class CachedText
{
public:
    CachedText(const sf::Font& font, unsigned int characterSize);

    // The cached text, with `content` applied. setString - and so the
    // rebuild - only happens when content actually differs from last time.
    sf::Text& with(const std::string& content);

private:
    sf::Text text;
    std::string current;
};
```

`setPosition` and `setFillColor` do not trigger a rebuild, so callers stay
free to move and recolour the returned text every frame. This is the same
mechanism already proven on `craftLabels`/`smeltLabels`; those stay as they
are (their strings are compile-time constants and are never re-set).

### 2. Every per-frame `sf::Text` site becomes a cached instance

`Hud` gains one cache per text site, sized and character-sized to match what
that site draws today. No visual change: same font, same sizes, same colours,
same positions.

| Site (`Hud.cpp`) | Instances | Size | Content |
|---|---|---|---|
| `drawSlot` count - hotbar | 10 | 14 | changes with stack count |
| `drawSlot` count - bag panel | 30 | 14 | changes with stack count |
| `drawSlot` count - storage panel | 20 | 11 | changes with stack count |
| `drawMachineTooltip` lines | 8 | 14 | changes with machine state |
| `drawBuildPalette` counts | 5 | 12 | changes with held counts |
| `drawBuildPalette` name | 1 | 16 | changes with selection |
| `drawDragGhost` count | 1 | 14 | changes with dragged stack |
| `drawChestButton` labels | 2 | 13 | **static** |
| `drawBuildPalette` hint | 1 | 14 | **static** |

The two static rows never change, so they are plain `sf::Text` members built
once in the constructor rather than `CachedText` - there is nothing to
compare.

`drawSlot` is currently a free function taking `const std::optional<sf::Font>&`
and a `countFontSize`. It becomes a member (or takes a `CachedText*`), since
it now needs the caller's cache entry rather than the font; `countFontSize`
disappears into the `CachedText`'s own character size. The three call sites
(`draw`, `drawInventoryPanel`, `drawChestPanel`) each pass their own cache.
A null/absent cache means no font loaded, which is already the existing
"draw everything except the text" path.

The tooltip's line count varies (name, recipe list, power, input, output,
bar, reason). Its cache is sized to the maximum the code can emit and indexed
by line; unused entries simply aren't drawn.

### 3. Rebuilds become rare, not eliminated

A count that genuinely changes still pays one rebuild - correctly. Picking up
an item dirties one slot, not sixty. The pathological case is a mass
inventory change (Deposit All / Collect All) dirtying many slots in a single
frame, costing one rebuild each; that is a one-frame hitch on an explicit
user action, not a sustained 6fps.

## Data flow summary

Unchanged, except for where the text comes from: each draw function asks its
cache for a text with the content it wants, then positions, colours and draws
it exactly as it does today. The cache decides internally whether that
required a rebuild.

## Error handling / invariants

- No visual change. Every panel must render identically - same glyphs, sizes,
  colours, positions. This is the whole risk of the change: it is a rendering
  refactor whose success criterion is that nothing looks different.
- The no-font path (`loadFont()` returning `nullopt`, which the HUD already
  degrades to gracefully) must keep working: slots, icons and panel
  backgrounds still draw, only the text is missing.
- A cache entry is only ever read by the site that owns it; indices must
  match that site's own loop, or a slot would show another slot's number.

## Testing

`Hud.cpp` is not linked into `Litharia_tests` (SFML Graphics/Window only, per
this project's CMake split), so as with every prior HUD change this is
build-verified rather than doctest-covered, plus:

- **A before/after measurement**, using the same temporary instrumentation
  that produced the numbers above: a full bag + inventory + chest open, timing
  the frame breakdown. The claim "173ms -> ~11ms" must be demonstrated, not
  asserted. The instrumentation is removed before the work is committed.
- **Manual visual check** by the human: the hotbar, bag, chest, crafting,
  smelting, build palette and machine tooltip must all look exactly as they
  did.
- The existing 218 tests must stay green (they cannot cover `Hud.cpp`, but
  they guard against collateral damage elsewhere).

## Out of scope

- Batching the HUD's ~120 slot/icon rectangles into a single `VertexArray`.
  They cost ~4ms of the projected ~11ms Debug frame; 60fps is met without it,
  and it is a far more invasive change. Worth revisiting only if more headroom
  is wanted later.
- The simulation, chunk renderer, and `updatePower()`'s per-tick allocations -
  measured above and fast.
- Any change to what the HUD draws, its layout, or its behaviour.
- Structural cleanup of `Game.cpp` (941 lines) / `Hud.cpp` (751 lines). Real,
  but a separate concern from performance.
