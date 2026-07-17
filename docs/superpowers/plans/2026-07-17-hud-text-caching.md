# HUD Text Caching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the HUD rebuilding `sf::Text` geometry every frame, which costs a fixed ~2.8ms per text (Debug) and pins a full-bag inventory at ~6fps.

**Architecture:** One small `CachedText` helper keeps an `sf::Text` alive and re-sets its string only when the content actually differs — SFML rebuilds geometry on `setString`, but not on `setPosition`/`setFillColor`. Every per-frame `sf::Text` construction in `Hud.cpp` is replaced by a cache instance owned by `Hud`, sized and character-sized exactly as that site draws today. Strings that never change (button labels, the palette hint) become plain `sf::Text` members built once.

**Tech Stack:** C++20, SFML 3, CMake + Visual Studio generator (existing project stack — no new dependencies).

## Global Constraints

- Build (game): `cmake --build C:\Litharia\build --config Debug --target Litharia`. `cmake` is **not** on PATH; the full path is `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
- Build (tests): `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`. Test binary: `C:\Litharia\build\Debug\Litharia_tests.exe`.
- Baseline before this work: **218 test cases, 218 passed, 0 failed**, at commit `db12c01`. This work must leave that at exactly 218 — it touches only `Hud.h`/`Hud.cpp`, which are **not** linked into `Litharia_tests` (SFML Graphics/Window are executable-only per this project's CMake split).
- **No visual change.** Every panel must render identically: same font, same character sizes, same colours, same positions, same outlines. This is a rendering refactor whose success criterion is that nothing looks different.
- The existing no-font path must keep working: `loadFont()` can return `nullopt`, and every draw site already guards on `if (!font)`. Caches are populated **iff** `font` has a value, so those existing guards are what protect every cache access. Do not remove them.
- `setOutlineThickness` **does** dirty the geometry (`m_geometryNeedUpdate = true`) when the value changes, so outline thickness/colour must be set **once at build time**, never per frame. `setFillColor`, `setOutlineColor` and `setPosition` do **not** rebuild and stay per-frame.
- Spec: `docs/superpowers/specs/2026-07-17-hud-text-caching-design.md`.

## Note on the absence of automated tests

Every task below is build-verified rather than doctest-covered, and Task 4 is a
measurement rather than a test. This is not a shortcut: `Hud.cpp` compiles only
into the `Litharia` executable and is unreachable from `Litharia_tests` by this
project's own CMake split, exactly as with every prior HUD change in this
repo. `CachedText`'s logic is one string comparison; extracting it into
`Litharia_core` to make it testable would mean extracting `sf::Text`, which
depends on SFML Graphics and therefore cannot go there. The real risk in this
change is visual regression, which Task 4's measurement plus the human's own
look at the game is what catches.

---

## Task 1: `CachedText`, and the slot counts that cost 168ms

The dominant site by far: up to 60 `sf::Text` constructions per frame (10 hotbar
+ 30 bag + 20 storage), measured at 27.4ms + 141.3ms of a 173ms frame. This task
introduces the helper and applies it here.

**Files:**
- Modify: `src/Hud/Hud.h` (add `CachedText` above `class Hud`; add caches + `buildTextCaches` to Hud's private section)
- Modify: `src/Hud/Hud.cpp` (`drawSlot`, `Hud::Hud`, `draw`, `drawInventoryPanel`, `drawChestPanel`)

**Interfaces:**
- Consumes: nothing new (first task).
- Produces: `class CachedText` with `CachedText(const sf::Font& font, unsigned int characterSize)`, `sf::Text& with(const std::string& content)`, and `sf::Text& text()`. Tasks 2 and 3 both build caches out of it. Also `void Hud::buildTextCaches()`, which Tasks 2 and 3 extend with their own caches.

- [ ] **Step 1: Add `CachedText` to `Hud.h`**

In `src/Hud/Hud.h`, directly below `class Inventory;` and above the `// Draws the ten hotbar slots...` comment, add:

```cpp
// An sf::Text that only rebuilds when its content actually changes.
//
// SFML rebuilds a text's geometry whenever its string is set, at a fixed cost
// of ~2.8ms in a Debug build (~0.5ms in Release) - fixed meaning a
// one-character string costs the same as a sixteen-character one. Drawing a
// text that was NOT rebuilt costs ~0.05ms. A HUD that shows the same numbers
// frame after frame must therefore never re-set a string it hasn't changed:
// constructing 60 fresh texts a frame for the inventory measured at 168ms of
// a 173ms frame (~6fps).
//
// setPosition/setFillColor/setOutlineColor do not rebuild, so callers stay
// free to move and recolour the returned text every frame. setOutlineThickness
// DOES rebuild when the value changes, so set it once via text(), at build
// time, and never per frame.
class CachedText
{
public:
    CachedText(const sf::Font& font, unsigned int characterSize)
        : cached(font, "", characterSize)
    {
    }

    // The cached text, with `content` applied. The string - and so the
    // rebuild - is only set when it actually differs from last time.
    sf::Text& with(const std::string& content)
    {
        if (content != current)
        {
            current = content;
            cached.setString(current);
        }

        return cached;
    }

    // Direct access, for one-time setup at build time (outline thickness).
    sf::Text& text() { return cached; }

private:
    sf::Text cached;
    std::string current;
};
```

Add `#include <string>` to `Hud.h`'s include block, below `#include <optional>`.

- [ ] **Step 2: Add the slot-count caches to `Hud`**

In `src/Hud/Hud.h`, in the private section directly above `std::optional<sf::Font> font;`, add:

```cpp
    // Parallel to the slots each panel draws. Populated iff `font` has a
    // value, so the existing `if (!font)` guards at each draw site are what
    // keep these accesses safe.
    std::vector<CachedText> hotbarCounts;  // Inventory::HOTBAR_SIZE entries
    std::vector<CachedText> bagCounts;     // Inventory::SIZE - HOTBAR_SIZE entries
    std::vector<CachedText> storageCounts; // CHEST_SLOTS entries (an Item Acceptor uses the first 10)

    void buildTextCaches();
```

- [ ] **Step 3: Build the caches in the constructor**

In `src/Hud/Hud.cpp`, replace:

```cpp
Hud::Hud()
    : font(loadFont())
{
    buildRecipeLabels();
}
```

with:

```cpp
Hud::Hud()
    : font(loadFont())
{
    buildRecipeLabels();
    buildTextCaches();
}
```

Directly below `Hud::buildRecipeLabels()`'s closing brace, add:

```cpp
void Hud::buildTextCaches()
{
    if (!font)
        return;

    // Stack counts share one look: white with a dark outline. The outline is
    // set here and never touched again - changing its thickness would dirty
    // the geometry, which is the whole thing this cache exists to avoid.
    const auto makeCount = [this](std::vector<CachedText>& into, int howMany, unsigned int size) {
        into.reserve(static_cast<std::size_t>(howMany));

        for (int i = 0; i < howMany; ++i)
        {
            into.emplace_back(*font, size);
            into.back().text().setOutlineThickness(2.0f);
            into.back().text().setOutlineColor(sf::Color(10, 10, 12));
        }
    };

    makeCount(hotbarCounts, Inventory::HOTBAR_SIZE, COUNT_FONT_SIZE);
    makeCount(bagCounts, Inventory::SIZE - Inventory::HOTBAR_SIZE, COUNT_FONT_SIZE);
    makeCount(storageCounts, CHEST_SLOTS, CHEST_COUNT_FONT_SIZE);
}
```

`Hud.cpp` already includes `"../Items/Inventory.h"` and `"../Machines/MachineType.h"` (for `CHEST_SLOTS`), so no new includes are needed.

- [ ] **Step 4: Make `drawSlot` take a cache entry instead of the font**

In `src/Hud/Hud.cpp`, replace the whole `drawSlot` free function:

```cpp
// Draws one slot's background, item icon, and stack count - shared by the
// hotbar and the bag/chest panels so they render identically (aside from
// their own slot size and background color).
//
// `count` is the caller's cached text for this slot, or nullptr when no font
// loaded (in which case the slot and icon still draw, just without a number).
void drawSlot(sf::RenderWindow& window, CachedText* count, sf::Vector2f pos, const ItemStack& stack,
              bool highlighted, float slotSize, sf::Color backgroundColor)
{
    sf::RectangleShape slot({slotSize, slotSize});
    slot.setPosition(pos);
    slot.setFillColor(backgroundColor);
    slot.setOutlineThickness(highlighted ? -3.0f : -1.0f);
    slot.setOutlineColor(highlighted ? sf::Color(255, 236, 140) : sf::Color(90, 90, 105));
    window.draw(slot);

    if (stack.empty())
        return;

    sf::RectangleShape icon({slotSize - ICON_INSET * 2.0f, slotSize - ICON_INSET * 2.0f});
    icon.setPosition({pos.x + ICON_INSET, pos.y + ICON_INSET});
    icon.setFillColor(itemColor(stack.type));
    icon.setOutlineThickness(-1.0f);
    icon.setOutlineColor(sf::Color(20, 16, 14));
    window.draw(icon);

    if (count == nullptr)
        return;

    sf::Text& text = count->with(std::to_string(stack.count));
    text.setFillColor(sf::Color::White);

    // Free once the geometry is built: only the first getLocalBounds after a
    // string change does any work.
    const sf::FloatRect bounds = text.getLocalBounds();
    text.setPosition({pos.x + slotSize - bounds.size.x - 5.0f,
                      pos.y + slotSize - bounds.size.y - 10.0f});
    window.draw(text);
}
```

- [ ] **Step 5: Point the three slot-drawing panels at their caches**

In `src/Hud/Hud.cpp`, in `Hud::draw`, replace the loop body's `drawSlot` call:

```cpp
        drawSlot(window, font ? &hotbarCounts[static_cast<std::size_t>(i)] : nullptr, pos,
                 inventory.slot(i), i == selectedSlot, SLOT_SIZE, BAG_SLOT_BACKGROUND);
```

In `Hud::drawInventoryPanel`, replace its `drawSlot` call:

```cpp
        drawSlot(window, font ? &bagCounts[static_cast<std::size_t>(gridIndex)] : nullptr, pos,
                 inventory.slot(i), false, SLOT_SIZE, BAG_SLOT_BACKGROUND);
```

In `Hud::drawChestPanel`, replace its `drawSlot` call:

```cpp
        drawSlot(window, font ? &storageCounts[static_cast<std::size_t>(i)] : nullptr, pos,
                 chestStorage.slot(i), false, CHEST_SLOT_SIZE, CHEST_SLOT_BACKGROUND);
```

`drawChestPanel` loops `i < chestStorage.slotCount()`, which is `CHEST_SLOTS` (20) for a Chest and `ITEM_ACCEPTOR_SLOTS` (10) for an Item Acceptor — both within `storageCounts`'s 20 entries, so the index is always in range.

- [ ] **Step 6: Build the game**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean, no errors.

- [ ] **Step 7: Build and run the test suite**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 218 | 218 passed | 0 failed` — unchanged, since `Hud.cpp` is not linked into the test binary.

- [ ] **Step 8: Commit**

```bash
git add src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "perf: cache the HUD's slot-count text instead of rebuilding it per frame"
```

---

## Task 2: The machine tooltip's lines

Up to 7 texts rebuilt every frame the cursor rests on a machine — ~19ms in
Debug, on one of the most common actions in the game.

**Files:**
- Modify: `src/Hud/Hud.h` (one more cache)
- Modify: `src/Hud/Hud.cpp` (`buildTextCaches`, `drawMachineTooltip`)

**Interfaces:**
- Consumes: `CachedText` and `Hud::buildTextCaches()` (Task 1).
- Produces: `std::vector<CachedText> tooltipLines`. No later task depends on it.

- [ ] **Step 1: Add the cache**

In `src/Hud/Hud.h`, directly below the `storageCounts` declaration added in Task 1, add:

```cpp
    // One per tooltip row. drawMachineTooltip emits at most 7 (name, recipe
    // list, fuel/power, input, output, bar, idle reason); 8 leaves a row of
    // slack. Rows the current machine doesn't need simply aren't drawn.
    std::vector<CachedText> tooltipLines;
```

- [ ] **Step 2: Build it**

In `src/Hud/Hud.cpp`, inside `Hud::buildTextCaches()`, directly below the
`makeCount(storageCounts, ...)` line, add:

```cpp
    // No outline on tooltip rows, unlike the stack counts - the panel behind
    // them already provides the contrast.
    constexpr int TOOLTIP_MAX_LINES = 8;
    tooltipLines.reserve(TOOLTIP_MAX_LINES);

    for (int i = 0; i < TOOLTIP_MAX_LINES; ++i)
        tooltipLines.emplace_back(*font, 14);
```

- [ ] **Step 3: Draw from the cache**

In `src/Hud/Hud.cpp`, in `Hud::drawMachineTooltip`, replace:

```cpp
    if (font)
    {
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            sf::Text text(*font, lines[i].text, 14);
            text.setFillColor(lines[i].color);
            text.setPosition({pos.x + PADDING, pos.y + PADDING + static_cast<float>(i) * LINE_HEIGHT});
            window.draw(text);
        }
    }
```

with:

```cpp
    if (font)
    {
        // Guard rather than assume: a future tooltip row would otherwise run
        // off the end of the cache built in buildTextCaches().
        const std::size_t drawn = std::min(lines.size(), tooltipLines.size());

        for (std::size_t i = 0; i < drawn; ++i)
        {
            sf::Text& text = tooltipLines[i].with(lines[i].text);
            text.setFillColor(lines[i].color);
            text.setPosition({pos.x + PADDING, pos.y + PADDING + static_cast<float>(i) * LINE_HEIGHT});
            window.draw(text);
        }
    }
```

`Hud.cpp` already includes `<algorithm>` for `std::min`.

- [ ] **Step 4: Build the game**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 5: Run the test suite**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 218 | 218 passed | 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "perf: cache the machine tooltip's line text"
```

---

## Task 3: The build palette, the drag ghost, and the static labels

The remaining five sites: the palette's per-swatch counts (5) and machine name
(1), the drag ghost's count (1), and three strings that never change at all —
the two chest buttons and the palette's hint line.

**Files:**
- Modify: `src/Hud/Hud.h` (caches + static labels)
- Modify: `src/Hud/Hud.cpp` (`buildTextCaches`, `drawChestButton`, `drawChestButtons`, `drawDragGhost`, `drawBuildPalette`)

**Interfaces:**
- Consumes: `CachedText` and `Hud::buildTextCaches()` (Task 1).
- Produces: nothing later tasks depend on — this is the last code task.

- [ ] **Step 1: Add the caches and static labels**

In `src/Hud/Hud.h`, directly below the `tooltipLines` declaration added in
Task 2, add:

```cpp
    // The build palette shows at most VISIBLE (5) swatches at once.
    std::vector<CachedText> paletteCounts;
    std::optional<CachedText> paletteName;
    std::optional<CachedText> dragCount;

    // Strings that never change: built once, drawn as-is. Nothing to compare,
    // so these are plain texts rather than CachedText.
    std::optional<sf::Text> depositLabel;
    std::optional<sf::Text> collectLabel;
    std::optional<sf::Text> paletteHint;
```

- [ ] **Step 2: Build them**

In `src/Hud/Hud.cpp`, inside `Hud::buildTextCaches()`, directly below the
`tooltipLines` loop added in Task 2, add:

```cpp
    constexpr int PALETTE_VISIBLE = 5;
    paletteCounts.reserve(PALETTE_VISIBLE);

    for (int i = 0; i < PALETTE_VISIBLE; ++i)
    {
        paletteCounts.emplace_back(*font, 12);
        paletteCounts.back().text().setOutlineThickness(2.0f);
        paletteCounts.back().text().setOutlineColor(sf::Color(10, 10, 12));
    }

    paletteName.emplace(*font, 16);

    dragCount.emplace(*font, 14);
    dragCount->text().setOutlineThickness(2.0f);
    dragCount->text().setOutlineColor(sf::Color(10, 10, 12));

    depositLabel.emplace(*font, "Deposit All", 13);
    depositLabel->setFillColor(sf::Color::White);

    collectLabel.emplace(*font, "Collect All", 13);
    collectLabel->setFillColor(sf::Color::White);

    paletteHint.emplace(*font, "Left click: place    Right click: destroy", 14);
    paletteHint->setFillColor(sf::Color(220, 220, 220));
```

- [ ] **Step 3: Draw the chest buttons from the static labels**

In `src/Hud/Hud.cpp`, replace the whole `drawChestButton` free function:

```cpp
// Draws one chest action button: a labeled rectangle, sharing the bag/
// hotbar slot's dark background so it reads as part of the same UI family.
//
// `label` is the caller's prebuilt text, or nullptr when no font loaded.
void drawChestButton(sf::RenderWindow& window, sf::Text* label, sf::Vector2f pos)
{
    sf::RectangleShape button({Hud::CHEST_BUTTON_WIDTH, Hud::CHEST_BUTTON_HEIGHT});
    button.setPosition(pos);
    button.setFillColor(BAG_SLOT_BACKGROUND);
    button.setOutlineThickness(-1.0f);
    button.setOutlineColor(sf::Color(90, 90, 105));
    window.draw(button);

    if (label == nullptr)
        return;

    const sf::FloatRect bounds = label->getLocalBounds();
    label->setPosition({pos.x + (Hud::CHEST_BUTTON_WIDTH - bounds.size.x) * 0.5f,
                        pos.y + (Hud::CHEST_BUTTON_HEIGHT - bounds.size.y) * 0.5f});
    window.draw(*label);
}
```

And replace `Hud::drawChestButtons`'s two calls:

```cpp
    drawChestButton(window, depositLabel ? &*depositLabel : nullptr, origin);
    drawChestButton(window, collectLabel ? &*collectLabel : nullptr,
                     {origin.x, origin.y + CHEST_BUTTON_HEIGHT + SLOT_GAP});
```

- [ ] **Step 4: Draw the drag ghost's count from its cache**

In `src/Hud/Hud.cpp`, in `Hud::drawDragGhost`, replace:

```cpp
    if (font)
    {
        sf::Text count(*font, std::to_string(stack.count), 14);
        count.setFillColor(sf::Color::White);
        count.setOutlineThickness(2.0f);
        count.setOutlineColor(sf::Color(10, 10, 12));
        count.setPosition(screenPos + sf::Vector2f{8.0f, 8.0f});
        window.draw(count);
    }
```

with:

```cpp
    if (dragCount)
    {
        sf::Text& count = dragCount->with(std::to_string(stack.count));
        count.setFillColor(sf::Color::White);
        count.setPosition(screenPos + sf::Vector2f{8.0f, 8.0f});
        window.draw(count);
    }
```

- [ ] **Step 5: Draw the palette's counts, name and hint from their caches**

In `src/Hud/Hud.cpp`, in `Hud::drawBuildPalette`, replace the per-swatch text block:

```cpp
        if (font)
        {
            sf::Text count(*font, "x" + std::to_string(bag.count(itemForMachine(type))), 12);
            count.setFillColor(sf::Color::White);
            count.setOutlineThickness(2.0f);
            count.setOutlineColor(sf::Color(10, 10, 12));
            const sf::FloatRect cb = count.getLocalBounds();
            count.setPosition({pos.x + SWATCH - cb.size.x - 3.0f, pos.y + SWATCH - cb.size.y - 6.0f});
            window.draw(count);
        }
```

with:

```cpp
        if (font)
        {
            sf::Text& count =
                paletteCounts[static_cast<std::size_t>(slot)].with("x" + std::to_string(bag.count(itemForMachine(type))));
            count.setFillColor(sf::Color::White);
            const sf::FloatRect cb = count.getLocalBounds();
            count.setPosition({pos.x + SWATCH - cb.size.x - 3.0f, pos.y + SWATCH - cb.size.y - 6.0f});
            window.draw(count);
        }
```

(`slot` runs `0 .. visible-1`, and `visible` is `std::min(VISIBLE, total)` with
`VISIBLE == 5`, so it is always within `paletteCounts`'s 5 entries.)

Then replace the name and hint block:

```cpp
    sf::Text name(*font, std::string(machineInfo(displayed).name), 16);
    const sf::FloatRect nameBounds = name.getLocalBounds();
    name.setFillColor(sf::Color::White);
    name.setPosition({(windowSize.x - nameBounds.size.x) * 0.5f, y - 22.0f});
    window.draw(name);

    sf::Text hint(*font, "Left click: place    Right click: destroy", 14);
    const sf::FloatRect hintBounds = hint.getLocalBounds();
    hint.setFillColor(sf::Color(220, 220, 220));
    hint.setPosition({(windowSize.x - hintBounds.size.x) * 0.5f, y + SWATCH + 6.0f});
    window.draw(hint);
```

with:

```cpp
    sf::Text& name = paletteName->with(std::string(machineInfo(displayed).name));
    const sf::FloatRect nameBounds = name.getLocalBounds();
    name.setFillColor(sf::Color::White);
    name.setPosition({(windowSize.x - nameBounds.size.x) * 0.5f, y - 22.0f});
    window.draw(name);

    const sf::FloatRect hintBounds = paletteHint->getLocalBounds();
    paletteHint->setPosition({(windowSize.x - hintBounds.size.x) * 0.5f, y + SWATCH + 6.0f});
    window.draw(*paletteHint);
```

(Both dereferences are reached only past the function's existing
`if (!font) { window.setView(previous); return; }` guard, and the caches are
populated iff `font` has a value.)

- [ ] **Step 6: Build the game**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 7: Run the test suite**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 218 | 218 passed | 0 failed`.

- [ ] **Step 8: Confirm no per-frame text construction survives**

Run: `grep -n "sf::Text " src/Hud/Hud.cpp`
Expected: matches only inside `Hud::buildRecipeLabels` and `Hud::buildTextCaches`
(both run once, from the constructor), plus the `sf::Text&` **references** in the
draw functions. No `sf::Text <name>(` construction may remain in any `draw*`
function.

- [ ] **Step 9: Commit**

```bash
git add src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "perf: cache the build palette, drag ghost and static button text"
```

---

## Task 4: Prove the number moved

The spec's claim is "173ms -> ~11ms". That has to be demonstrated with the same
instrumentation that produced the 173ms, not asserted. This task adds it
temporarily, measures, and removes it — nothing here is committed except the
recorded result.

**Files:** none permanently. `src/Game/Game.cpp` is temporarily instrumented and then reverted with `git checkout`.

**Interfaces:**
- Consumes: the caches from Tasks 1-3.
- Produces: nothing.

- [ ] **Step 1: Confirm the working tree is clean**

Run: `git status --short`
Expected: empty. The instrumentation below is reverted with `git checkout` at
the end, so anything uncommitted would be lost — stop and commit it first if
this is not empty.

- [ ] **Step 2: Add the measurement harness**

In `src/Game/Game.cpp`, add `#include <iostream>` to the include block (below
`#include <cmath>`).

Replace:

```cpp
    player = Player(findSpawn());
    camera.snapTo(player.center());
}
```

with:

```cpp
    player = Player(findSpawn());
    camera.snapTo(player.center());

    // TEMP MEASURE - reverted at the end of this task. Worst realistic HUD
    // case: every bag slot holding something, inventory open, chest open.
    {
        Inventory& bag = player.inventory();
        for (int i = 0; i < bag.slotCount(); ++i)
            bag.exchange(i, {ItemType::Stone, 42});

        const sf::Vector2i st{static_cast<int>(player.center().x / TILE_SIZE),
                              static_cast<int>(player.center().y / TILE_SIZE)};
        machines.place(MachineType::Chest, st.x + 3, st.y, Direction::Right);

        Machine* chest = machines.at(st.x + 3, st.y);
        for (int i = 0; i < chest->storage.slotCount(); ++i)
            chest->storage.exchange(i, {ItemType::Coal, 7});

        openStorageTile = sf::Vector2i{st.x + 3, st.y};
        inventoryOpen = true;
    }
}
```

In `Game::render()`, replace:

```cpp
    hud.draw(window, player.inventory(), player.selectedSlot());

    if (buildMode)
        hud.drawBuildPalette(window, buildType, player.inventory());

    if (inventoryOpen)
        drawInventoryPanels();
```

with:

```cpp
    // TEMP MEASURE - reverted at the end of this task.
    {
        static float hotbarMs = 0.0f, panelsMs = 0.0f;
        static int n = 0;
        static sf::Clock mc;

        mc.restart();
        hud.draw(window, player.inventory(), player.selectedSlot());
        hotbarMs += mc.getElapsedTime().asSeconds() * 1000.0f;

        mc.restart();
        if (inventoryOpen)
            drawInventoryPanels();
        panelsMs += mc.getElapsedTime().asSeconds() * 1000.0f;

        if (++n == 60)
        {
            std::cerr << "MEASURE hotbar(10 texts)=" << (hotbarMs / 60.0f)
                      << "ms  bag+chest panels(50 texts)=" << (panelsMs / 60.0f) << "ms"
                      << std::endl;
            n = 0;
            hotbarMs = panelsMs = 0.0f;
        }
    }

    if (buildMode)
        hud.drawBuildPalette(window, buildType, player.inventory());
```

- [ ] **Step 3: Build and measure**

Run:
```bash
cmake --build C:\Litharia\build --config Debug --target Litharia
cd C:\Litharia\build\Debug && timeout 12 ./Litharia.exe 2>&1 | grep MEASURE | head -3
```

Expected: **hotbar well under 1ms and panels well under 5ms**, against the
recorded pre-fix baseline of `hotbar=27.4ms  panels=141.3ms`. Record the actual
numbers in the report.

If the panels are still tens of milliseconds, the cache is being defeated
somewhere — the likeliest cause is a `setString` (via `with()`) being handed
different content every frame, or a `setOutlineThickness` left in a draw path.
Report that rather than proceeding.

- [ ] **Step 4: Revert the instrumentation**

Run: `git checkout -- src/Game/Game.cpp`
Then confirm: `grep -c MEASURE src/Game/Game.cpp` → expected `0`, and
`git status --short` → expected empty.

- [ ] **Step 5: Hand the visual check to the human**

Nothing to commit. Report the before/after numbers, and note that the following
must be eyeballed in-game, since no automated test can cover `Hud.cpp`:

1. Hotbar stack counts — correct numbers, bottom-right of each slot, white with a dark outline.
2. Bag and chest/Item Acceptor panels — counts correct and correctly placed at both slot sizes.
3. Counts **update** when they change (pick an item up, place one, drag a stack) — a stale number means the cache's comparison is wrong.
4. Machine tooltip — all rows present, correct text, orange idle reason; a Smelter's "Progress: N%" still ticks up.
5. Build palette — per-swatch `xN` counts, the machine name, and the hint line.
6. Drag ghost — the count follows the cursor and shows the dragged stack's size.
7. Crafting/smelting panels — unchanged (already cached before this plan).
