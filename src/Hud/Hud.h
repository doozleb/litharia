#pragma once

#include <SFML/Graphics.hpp>

#include <optional>
#include <string>
#include <vector>

#include "../Machines/Machine.h"
#include "../Machines/MachineStatus.h"

class Inventory;

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

    // At most this many recipe buttons show at once - the advanced (table)
    // recipe list is long enough to run off the bottom of the window
    // otherwise. Game scrolls through the rest via craftPanelScroll.
    static constexpr int CRAFT_PANEL_VISIBLE_ROWS = 8;

    // The health bar's fixed size, top-left. Shared between drawHealth (which
    // draws it) and isHealthBarHovered (which hit-tests the same rect), so the
    // two can never disagree about where the bar actually is.
    static constexpr float HEALTH_BAR_WIDTH = 200.0f;
    static constexpr float HEALTH_BAR_HEIGHT = 18.0f;

    Hud();

    // Every cached sf::Text (the recipe labels and the count/line caches
    // below) holds a pointer into `font`, which is a member of this same Hud.
    // Copying would leave the copy's texts pointing at the original's font,
    // dangling the moment the original dies. Nothing copies a Hud today -
    // Game owns an sf::RenderWindow and so is non-copyable itself - so this
    // states the invariant rather than fixing a live bug.
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    void draw(sf::RenderWindow& window, const Inventory& inventory, int selectedSlot);

    // A fixed health bar in the top-left corner: a red fill proportional to
    // health/maxHealth over a dark back. Shapes only, so it renders even with no
    // font loaded.
    void drawHealth(sf::RenderWindow& window, int health, int maxHealth);

    // True if the given screen position lands on the health bar (see
    // drawHealth/HEALTH_BAR_WIDTH/HEALTH_BAR_HEIGHT). Game calls this each
    // frame against the mouse position to decide whether to also call
    // drawHealthTooltip.
    bool isHealthBarHovered(sf::Vector2f screenPos) const;

    // A small panel just below the health bar showing the exact "current /
    // max" reading. Only meaningful to call while isHealthBarHovered is true.
    // Degrades like the rest of the HUD: with no font loaded, only the panel
    // background shows.
    void drawHealthTooltip(sf::RenderWindow& window, int health, int maxHealth);

    // A small bar in the top-right HUD cluster (just above the hotbar)
    // showing the day/night clock's current daylightFactor as a fill
    // fraction. Shapes only, like drawHealth - renders fine with no font.
    void drawDayNightIndicator(sf::RenderWindow& window, float daylightFactor);

    // A floating "-N" text bound to the HUD's font, for Game to position and
    // fade over its own lifetime as a damage popup. The returned sf::Text
    // holds a pointer into this Hud's font (see the copy-ban note above) - it
    // must not outlive it, which every Game outlives its Hud member never
    // does. nullopt if no font is loaded, matching how the rest of the HUD
    // degrades rather than drawing broken text.
    std::optional<sf::Text> makeDamagePopupText(int amount) const;

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
    // shows a fill bar for craftProgress / that recipe's seconds. Only
    // CRAFT_PANEL_VISIBLE_ROWS matching recipes starting at `scrollOffset`
    // are drawn - Game owns and clamps the scroll position.
    void drawCraftPanel(sf::RenderWindow& window, const Inventory& bag, bool advanced, bool crafting,
                         int craftingRecipeIndex, float craftProgress, int scrollOffset);

    // Screen position -> index into allCraftRecipes() for the button it lands
    // on, filtered and scrolled identically to drawCraftPanel (same order,
    // same `advanced` split, same `scrollOffset`) so drawing and hit-testing
    // can never disagree. nullopt if the point misses every visible button.
    std::optional<int> hitTestCraftButton(sf::Vector2f screenPos, sf::Vector2f windowSize, bool advanced,
                                           int scrollOffset) const;

    // How many defined recipes currently match `advanced` (basic: just the
    // Crafting Table; advanced: every recipe that requires one) - what
    // scrollOffset is clamped against. Shared by drawCraftPanel,
    // hitTestCraftButton, and Game's scroll-wheel handler so none of the
    // three can disagree about the list's true length.
    static int craftRecipeCount(bool advanced);

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

    // Parallel to the slots each panel draws. Populated iff `font` has a
    // value, so the existing `if (!font)` guards at each draw site are what
    // keep these accesses safe.
    std::vector<CachedText> hotbarCounts;  // Inventory::HOTBAR_SIZE entries
    std::vector<CachedText> bagCounts;     // Inventory::SIZE - HOTBAR_SIZE entries
    std::vector<CachedText> storageCounts; // CHEST_SLOTS entries (an Item Acceptor uses the first 10)

    // One per tooltip row. drawMachineTooltip emits at most 7 (name, recipe
    // list, fuel/power, input, output, bar, idle reason); 8 leaves a row of
    // slack. Rows the current machine doesn't need simply aren't drawn.
    std::vector<CachedText> tooltipLines;

    // The build palette shows at most PALETTE_VISIBLE (5) swatches at once.
    std::vector<CachedText> paletteCounts;
    std::optional<CachedText> paletteName;
    std::optional<CachedText> dragCount;

    // The health tooltip's "current / max" text. One instance, since only one
    // is ever shown at a time.
    std::optional<CachedText> healthTooltipText;

    // Strings that never change: built once, drawn as-is. Nothing to compare,
    // so these are plain texts rather than CachedText.
    std::optional<sf::Text> depositLabel;
    std::optional<sf::Text> collectLabel;
    std::optional<sf::Text> paletteHint;

    void buildTextCaches();

    std::optional<sf::Font> font;
};
