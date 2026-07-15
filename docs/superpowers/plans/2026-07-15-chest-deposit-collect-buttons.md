# Chest Deposit All / Collect All Buttons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add "Deposit All" and "Collect All" buttons to the left of the chest panel, letting the player bulk-transfer items between their bag and an open chest in one click.

**Architecture:** `Hud` gains a small `ChestButton` enum plus a draw method and a hit-test method that follow the exact same origin-sharing pattern already used for slots (`chestPanelOrigin` → `chestButtonsOrigin`, mirroring `hitTestPanels`). `Game` gains two methods, `depositAllToChest()`/`collectAllFromChest()`, built from `Inventory::take`/`add`/`exchange` — the same primitives `beginDrag`/`endDrag` already use — so nothing is ever destroyed if the destination doesn't have room. The mouse-down handler checks for a button hit before falling through to the existing drag-start logic.

**Tech Stack:** C++20, SFML 3, doctest (existing project stack — no new dependencies).

## Global Constraints

- Chest slot size is `Hud::CHEST_SLOT_SIZE = 36.0f`; chest panel is `Hud::SLOT_GAP = 4.0f` between its 2 rows (total height `2 * 36 + 4 = 76px`) — copied verbatim from `src/Hud/Hud.h`/`src/Hud/Hud.cpp` as they exist after the top-right HUD repositioning commit (`b96e9da`).
- Deposit All only touches the player's bag-panel slots (`Inventory::HOTBAR_SIZE .. bag.slotCount()-1`) — the hotbar is left untouched, per the approved design.
- Collect All sweeps every chest slot (`0 .. chest.slotCount()-1`) into the bag via `Inventory::add()`, which may land in an empty hotbar slot — that's existing, accepted `add()` behavior (ground-pickup already works this way).
- Nothing is ever silently destroyed: any stack that doesn't fully fit at the destination goes back into the exact slot it was taken from, mirroring the leftover-handling `beginDrag`/`endDrag` already do.
- This is rendering/hit-testing code (`Hud`) plus its `Game`-level wiring, neither of which is linked into the doctest binary (`Litharia_tests`) — same as the existing `hitTestPanels`/`beginDrag`/`endDrag`, which have zero doctest coverage today. Verification for both tasks below is manual (build, run, play-test), not a new automated test.

---

### Task 1: `Hud` — chest button constants, drawing, and hit-testing

**Files:**
- Modify: `src/Hud/Hud.h`
- Modify: `src/Hud/Hud.cpp`

**Interfaces:**
- Consumes: `Hud::CHEST_SLOT_SIZE`, `Hud::SLOT_GAP`, `Hud::MARGIN` (existing constants); the file-local `chestPanelOrigin(sf::Vector2f)` function already defined in `src/Hud/Hud.cpp`.
- Produces: `Hud::ChestButton` (enum class, values `DepositAll`, `CollectAll`); `Hud::CHEST_BUTTON_WIDTH` / `Hud::CHEST_BUTTON_HEIGHT` (`static constexpr float`); `void Hud::drawChestButtons(sf::RenderWindow& window)`; `std::optional<Hud::ChestButton> Hud::hitTestChestButton(sf::Vector2f screenPos, sf::Vector2f windowSize) const`. Task 2 calls all four of these.

- [ ] **Step 1: Add the button size constants and the `ChestButton` enum to `Hud.h`**

In `src/Hud/Hud.h`, right after the existing `CHEST_SLOT_SIZE` constant (currently lines 22-24):

```cpp
    // The chest panel renders smaller than the hotbar/bag slots, so it reads
    // as a visually distinct grid.
    static constexpr float CHEST_SLOT_SIZE = 36.0f;

    // The "Deposit All"/"Collect All" buttons sit to the chest panel's left,
    // stacked so together they span the same height as its 2 rows
    // (2 * CHEST_SLOT_SIZE + SLOT_GAP = 76px).
    static constexpr float CHEST_BUTTON_WIDTH = 76.0f;
    static constexpr float CHEST_BUTTON_HEIGHT = 36.0f;
```

Then, right after the `SlotHit` struct and its `hitTestPanels` declaration (currently lines 40-50), add:

```cpp
    enum class ChestButton { DepositAll, CollectAll };

    // Draws the two chest action buttons to the left of the chest panel.
    // Only meaningful to call while the chest panel itself is being drawn.
    void drawChestButtons(sf::RenderWindow& window);

    // Screen position -> which chest button it lands on, or nullopt.
    std::optional<ChestButton> hitTestChestButton(sf::Vector2f screenPos, sf::Vector2f windowSize) const;
```

- [ ] **Step 2: Add the `chestButtonsOrigin` helper to `Hud.cpp`**

In `src/Hud/Hud.cpp`, right after `chestPanelOrigin` (currently ends at line 123), add:

```cpp
// Where the chest action buttons sit: directly left of the chest panel,
// top-aligned with it and spanning its same height.
sf::Vector2f chestButtonsOrigin(sf::Vector2f windowSize)
{
    const sf::Vector2f chest = chestPanelOrigin(windowSize);
    const float x = chest.x - Hud::MARGIN - Hud::CHEST_BUTTON_WIDTH;

    return {x, chest.y};
}

// Draws one chest action button: a labeled rectangle, sharing the bag/
// hotbar slot's dark background so it reads as part of the same UI family.
void drawChestButton(sf::RenderWindow& window, const std::optional<sf::Font>& font, sf::Vector2f pos,
                      const std::string& label)
{
    sf::RectangleShape button({Hud::CHEST_BUTTON_WIDTH, Hud::CHEST_BUTTON_HEIGHT});
    button.setPosition(pos);
    button.setFillColor(BAG_SLOT_BACKGROUND);
    button.setOutlineThickness(-1.0f);
    button.setOutlineColor(sf::Color(90, 90, 105));
    window.draw(button);

    if (!font)
        return;

    sf::Text text(*font, label, 13);
    text.setFillColor(sf::Color::White);

    const sf::FloatRect bounds = text.getLocalBounds();
    text.setPosition({pos.x + (Hud::CHEST_BUTTON_WIDTH - bounds.size.x) * 0.5f,
                      pos.y + (Hud::CHEST_BUTTON_HEIGHT - bounds.size.y) * 0.5f});
    window.draw(text);
}
```

This goes in the same anonymous namespace as `chestPanelOrigin`, `drawSlot`, etc. (the one starting at line 15), so it has no `Hud::` qualifier needed for `BAG_SLOT_BACKGROUND` (already defined at line 22 in that namespace).

- [ ] **Step 3: Add `Hud::drawChestButtons` and `Hud::hitTestChestButton` to `Hud.cpp`**

Right after `Hud::drawChestPanel` (currently ends at line 222), add:

```cpp
void Hud::drawChestButtons(sf::RenderWindow& window)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    const sf::Vector2f origin = chestButtonsOrigin(sf::Vector2f(window.getSize()));

    drawChestButton(window, font, origin, "Deposit All");
    drawChestButton(window, font, {origin.x, origin.y + CHEST_BUTTON_HEIGHT + SLOT_GAP}, "Collect All");

    window.setView(previous);
}
```

Right after `Hud::hitTestPanels` (currently ends at line 283), add:

```cpp
std::optional<Hud::ChestButton> Hud::hitTestChestButton(sf::Vector2f screenPos, sf::Vector2f windowSize) const
{
    const sf::Vector2f origin = chestButtonsOrigin(windowSize);

    const sf::FloatRect depositRect({origin.x, origin.y}, {CHEST_BUTTON_WIDTH, CHEST_BUTTON_HEIGHT});
    if (depositRect.contains(screenPos))
        return ChestButton::DepositAll;

    const sf::FloatRect collectRect({origin.x, origin.y + CHEST_BUTTON_HEIGHT + SLOT_GAP},
                                     {CHEST_BUTTON_WIDTH, CHEST_BUTTON_HEIGHT});
    if (collectRect.contains(screenPos))
        return ChestButton::CollectAll;

    return std::nullopt;
}
```

- [ ] **Step 4: Build**

```bash
cmake --build build --config Debug --target Litharia
```

Expected: builds with no errors (there's nothing calling the two new `Hud` methods yet, so this only proves the new code compiles standalone).

- [ ] **Step 5: Commit**

```bash
git add src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "feat: add chest Deposit All/Collect All button drawing and hit-testing to Hud"
```

---

### Task 2: `Game` — wire deposit/collect logic and hook up the buttons

**Files:**
- Modify: `src/Game/Game.h`
- Modify: `src/Game/Game.cpp`

**Interfaces:**
- Consumes: `Hud::ChestButton`, `Hud::drawChestButtons`, `Hud::hitTestChestButton` (from Task 1); `Inventory::take(int)`, `Inventory::add(ItemStack)`, `Inventory::exchange(int, ItemStack)`, `Inventory::slotCount()`, `Inventory::HOTBAR_SIZE` (existing, `src/Items/Inventory.h`); `Game::openChestTile`, `Game::machines`, `Game::player`, `Game::hud`, `Game::inventoryOpen`, `Game::beginDrag()` (existing, `src/Game/Game.h`).
- Produces: `void Game::depositAllToChest()`; `void Game::collectAllFromChest()`. Nothing later depends on these — this is the last task.

- [ ] **Step 1: Declare the two new private methods on `Game`**

In `src/Game/Game.h`, right after `void endDrag();` (currently line 55):

```cpp
    void beginDrag();
    void endDrag();
    void depositAllToChest();
    void collectAllFromChest();
    sf::Vector2i cursorTile() const;
```

- [ ] **Step 2: Implement `depositAllToChest`/`collectAllFromChest` in `Game.cpp`**

Right after `Game::endDrag` (currently ends at line 302, right before `void Game::tickMachines(float dt)`), add:

```cpp
void Game::depositAllToChest()
{
    if (!openChestTile.has_value())
        return;

    Inventory& chest = machines.at(openChestTile->x, openChestTile->y)->storage;
    Inventory& bag = player.inventory();

    // Bag-panel slots only (HOTBAR_SIZE..slotCount()-1) - the hotbar is left
    // alone, same as the hotbar being excluded from what Deposit All sweeps.
    for (int i = Inventory::HOTBAR_SIZE; i < bag.slotCount(); ++i)
    {
        const ItemStack taken = bag.take(i);
        if (taken.empty())
            continue;

        // Whatever doesn't fit in the chest goes right back into the slot it
        // came from - take() already emptied it, so this can only refill it,
        // never swap with something else.
        const int leftover = chest.add(taken);
        if (leftover > 0)
            bag.exchange(i, {taken.type, leftover});
    }
}

void Game::collectAllFromChest()
{
    if (!openChestTile.has_value())
        return;

    Inventory& chest = machines.at(openChestTile->x, openChestTile->y)->storage;
    Inventory& bag = player.inventory();

    for (int i = 0; i < chest.slotCount(); ++i)
    {
        const ItemStack taken = chest.take(i);
        if (taken.empty())
            continue;

        const int leftover = bag.add(taken);
        if (leftover > 0)
            chest.exchange(i, {taken.type, leftover});
    }
}
```

- [ ] **Step 3: Draw the buttons alongside the chest panel**

In `src/Game/Game.cpp`, `Game::drawInventoryPanels` (currently lines 234-250) currently ends with:

```cpp
    hud.drawInventoryPanel(window, player.inventory());

    if (openChestTile.has_value())
        hud.drawChestPanel(window, machines.at(openChestTile->x, openChestTile->y)->storage);
}
```

Change the last two lines to:

```cpp
    hud.drawInventoryPanel(window, player.inventory());

    if (openChestTile.has_value())
    {
        hud.drawChestPanel(window, machines.at(openChestTile->x, openChestTile->y)->storage);
        hud.drawChestButtons(window);
    }
}
```

- [ ] **Step 4: Route button clicks ahead of drag-start in the mouse-down handler**

In `src/Game/Game.cpp`, `Game::handleEvents`, the `MouseButtonPressed` branch currently reads (around line 430-438):

```cpp
        else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
        {
            if (buildMode && mouse->button == sf::Mouse::Button::Left)
                placeMachineAtCursor();
            else if (buildMode && mouse->button == sf::Mouse::Button::Right)
                removeMachineAtCursor();
            else if (inventoryOpen && mouse->button == sf::Mouse::Button::Left)
                beginDrag();
        }
```

Change the last `else if` branch to:

```cpp
        else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
        {
            if (buildMode && mouse->button == sf::Mouse::Button::Left)
                placeMachineAtCursor();
            else if (buildMode && mouse->button == sf::Mouse::Button::Right)
                removeMachineAtCursor();
            else if (inventoryOpen && mouse->button == sf::Mouse::Button::Left)
            {
                const auto chestButton = openChestTile.has_value()
                    ? hud.hitTestChestButton(sf::Vector2f(sf::Mouse::getPosition(window)),
                                              sf::Vector2f(window.getSize()))
                    : std::nullopt;

                if (chestButton == Hud::ChestButton::DepositAll)
                    depositAllToChest();
                else if (chestButton == Hud::ChestButton::CollectAll)
                    collectAllFromChest();
                else
                    beginDrag();
            }
        }
```

- [ ] **Step 5: Build**

```bash
cmake --build build --config Debug --target Litharia
```

Expected: builds with no errors.

- [ ] **Step 6: Run the full doctest suite to confirm no regressions**

```bash
./build/Debug/Litharia_tests.exe
```

Expected: `test cases: 160 | 160 passed | 0 failed` (this feature adds no new doctest cases per the Global Constraints note above, but every existing case must still pass).

- [ ] **Step 7: Manual play-test**

Launch `build/Debug/Litharia.exe`. Enter build mode, place a chest, exit build mode, mine a few blocks so the bag panel (not just the hotbar) holds some stacks. Hover the chest and press `E`.

- Confirm two labeled buttons ("Deposit All", "Collect All") appear to the left of the chest's 2 rows, top-aligned with it.
- Click **Deposit All**: confirm every bag-panel stack moves into the chest (merging onto matching stacks, filling empty slots), and the hotbar is untouched.
- Click **Collect All**: confirm everything in the chest moves back into the bag (and/or hotbar, if a hotbar slot happens to be empty).
- Fill the chest until it's nearly full, then Deposit All with more items than it has room for: confirm the excess stays in its original bag slot rather than vanishing.
- Fill the bag until it's nearly full, then Collect All with more items in the chest than the bag has room for: confirm the excess stays in its original chest slot rather than vanishing.
- Confirm dragging individual items between bag and chest (from the earlier feature) still works after this change.

- [ ] **Step 8: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: add chest Deposit All/Collect All buttons"
```
