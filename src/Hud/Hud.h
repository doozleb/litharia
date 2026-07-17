#pragma once

#include <SFML/Graphics.hpp>

#include <optional>
#include <vector>

#include "../Machines/Machine.h"
#include "../Machines/MachineStatus.h"

class Inventory;

// Draws the ten hotbar slots in screen space. If no font can be found the slots
// and their contents still render - only the stack counts are missing, and the
// game runs.
class Hud
{
public:
    static constexpr float SLOT_SIZE = 48.0f;
    static constexpr float SLOT_GAP = 4.0f;
    static constexpr float MARGIN = 12.0f;

    // The chest panel renders smaller than the hotbar/bag slots, so it reads
    // as a visually distinct grid.
    static constexpr float CHEST_SLOT_SIZE = 36.0f;

    // Stack-count text size. The chest panel's slots are 75% the size of the
    // hotbar/bag slots (CHEST_SLOT_SIZE / SLOT_SIZE), so its counts shrink by
    // the same ratio to match.
    static constexpr unsigned int COUNT_FONT_SIZE = 14;
    static constexpr unsigned int CHEST_COUNT_FONT_SIZE =
        static_cast<unsigned int>(COUNT_FONT_SIZE * (CHEST_SLOT_SIZE / SLOT_SIZE) + 0.5f);

    // The "Deposit All"/"Collect All" buttons sit to the storage panel's
    // left, stacked to a fixed 76px (2 * CHEST_SLOT_SIZE + SLOT_GAP) tall -
    // sized for the Chest's 2 rows; a 1-row Item Acceptor panel leaves them
    // overhanging its bottom edge rather than resizing to match.
    static constexpr float CHEST_BUTTON_WIDTH = 76.0f;
    static constexpr float CHEST_BUTTON_HEIGHT = 36.0f;

    // Crafting recipe buttons: one per row, wide enough for a name + cost
    // string.
    static constexpr float CRAFT_BUTTON_WIDTH = 240.0f;
    static constexpr float CRAFT_BUTTON_HEIGHT = 40.0f;

    Hud();

    void draw(sf::RenderWindow& window, const Inventory& inventory, int selectedSlot);

    // The 3 rows of the bag beyond the hotbar (slots HOTBAR_SIZE..slotCount()-1),
    // shown only while the player has the inventory open.
    void drawInventoryPanel(sf::RenderWindow& window, const Inventory& inventory);

    // A Chest's (20) or Item Acceptor's (10) own slots, shown alongside the
    // bag panel while one of the two is open - drawn at chestStorage's own
    // slotCount(), not a fixed size.
    void drawChestPanel(sf::RenderWindow& window, const Inventory& chestStorage);

    // The stack currently being dragged, drawn centered on the live cursor.
    void drawDragGhost(sf::RenderWindow& window, const ItemStack& stack, sf::Vector2f screenPos);

    struct SlotHit
    {
        bool isStorage = false; // false: the player's bag; true: the open chest/item acceptor
        int index = -1;         // index into that Inventory
    };

    // Screen position -> which open panel/slot it lands on, or nullopt if
    // neither. `openStorageSlots` is the slot count of whichever storage
    // panel (chest or item acceptor) is currently drawn alongside the bag,
    // or 0 if none is open - must match what drawChestPanel was actually
    // called with this frame, so hit-testing and drawing never disagree.
    std::optional<SlotHit> hitTestPanels(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                          int openStorageSlots) const;

    enum class ChestButton { DepositAll, CollectAll };

    // Draws the two chest action buttons to the left of the chest panel.
    // Only meaningful to call while the chest panel itself is being drawn.
    void drawChestButtons(sf::RenderWindow& window);

    // Screen position -> which chest button it lands on, or nullopt.
    std::optional<ChestButton> hitTestChestButton(sf::Vector2f screenPos, sf::Vector2f windowSize) const;

    // The hand-crafting panel: one button per visible recipe (basic: just the
    // Crafting Table; advanced: every recipe that requires one), each showing
    // its name and ingredient cost. Every button renders dimmed/disabled while
    // `crafting` is true (one craft at a time); the in-progress one additionally
    // shows a fill bar for craftProgress / that recipe's seconds.
    void drawCraftPanel(sf::RenderWindow& window, const Inventory& bag, bool advanced, bool crafting,
                         int craftingRecipeIndex, float craftProgress);

    // Screen position -> index into allCraftRecipes() for the button it lands
    // on, filtered identically to drawCraftPanel (same order, same `advanced`
    // split) so drawing and hit-testing can never disagree. nullopt if the
    // point misses every button.
    std::optional<int> hitTestCraftButton(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                           bool advanced) const;

    // The Furnace's manual-smelting panel: one button per FurnaceRecipe
    // (always both ore->plate conversions - no basic/advanced split, since
    // there is only one view). Same dimmed-while-smelting/in-progress-fill-bar
    // behavior as drawCraftPanel.
    void drawSmeltPanel(sf::RenderWindow& window, const Inventory& bag, bool smelting,
                         int smeltingRecipeIndex, float smeltProgress);

    // Screen position -> index into allFurnaceRecipes() for the button it
    // lands on. nullopt if the point misses every button.
    std::optional<int> hitTestSmeltButton(sf::Vector2f screenPos, sf::Vector2f windowSize) const;

    // A small info panel anchored near the cursor, describing one machine's
    // current state: name, input/output, power/fuel state, bar percentage, and
    // (when idle/unpowered) a plain-English reason. Degrades like draw() does:
    // with no font the panel background still shows, just without text.
    void drawMachineTooltip(sf::RenderWindow& window,
                            const Machine& machine,
                            const MachineStatus& status,
                            sf::Vector2f screenPos);

    // The build-mode picker: a strip of machine-type swatches centered on
    // `selected`, plus a "left click: place / right click: destroy" caption.
    // Only lists MachineTypes the bag currently holds >= 1 of, labeling each
    // swatch with its count; empty if the bag holds none of anything craftable
    // yet.
    void drawBuildPalette(sf::RenderWindow& window, MachineType selected, const Inventory& bag);

    bool hasFont() const { return font.has_value(); }

private:
    // One recipe button's two rows of text, built once and redrawn every
    // frame. Constructing an sf::Text forces SFML to rebuild its geometry on
    // the next draw/getLocalBounds, which measures at ~2.7ms per text in a
    // Debug build (~0.5ms in Release) - a fixed cost, independent of how long
    // the string is. Rebuilding 16 of them per frame cost ~45ms and pinned the
    // crafting menu at ~20fps. A text that isn't rebuilt draws for ~0.05ms, so
    // these are cached: recipe names and costs are compile-time constants and
    // never change. setPosition/setFillColor don't trigger a rebuild, so the
    // per-frame work stays free.
    struct RecipeLabel
    {
        sf::Text title;
        sf::Text subtitle;
    };

    // Parallel to allCraftRecipes() / allFurnaceRecipes() by index. Empty if
    // no font loaded, in which case nothing draws text anyway.
    std::vector<RecipeLabel> craftLabels;
    std::vector<RecipeLabel> smeltLabels;

    void buildRecipeLabels();
    void drawRecipeLabel(sf::RenderWindow& window, RecipeLabel& label, sf::Vector2f pos,
                          bool disabled);

    std::optional<sf::Font> font;
};
