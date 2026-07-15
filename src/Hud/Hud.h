#pragma once

#include <SFML/Graphics.hpp>

#include <optional>

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

    // The "Deposit All"/"Collect All" buttons sit to the chest panel's left,
    // stacked so together they span the same height as its 2 rows
    // (2 * CHEST_SLOT_SIZE + SLOT_GAP = 76px).
    static constexpr float CHEST_BUTTON_WIDTH = 76.0f;
    static constexpr float CHEST_BUTTON_HEIGHT = 36.0f;

    Hud();

    void draw(sf::RenderWindow& window, const Inventory& inventory, int selectedSlot);

    // The 3 rows of the bag beyond the hotbar (slots HOTBAR_SIZE..slotCount()-1),
    // shown only while the player has the inventory open.
    void drawInventoryPanel(sf::RenderWindow& window, const Inventory& inventory);

    // A chest's own 20 slots, shown alongside the bag panel while a chest is open.
    void drawChestPanel(sf::RenderWindow& window, const Inventory& chestStorage);

    // The stack currently being dragged, drawn centered on the live cursor.
    void drawDragGhost(sf::RenderWindow& window, const ItemStack& stack, sf::Vector2f screenPos);

    struct SlotHit
    {
        bool isChest = false; // false: the player's bag; true: the open chest
        int index = -1;       // index into that Inventory
    };

    // Screen position -> which open panel/slot it lands on, or nullopt if
    // neither. `chestOpen` must match whether drawChestPanel was actually
    // called this frame, so hit-testing and drawing never disagree.
    std::optional<SlotHit> hitTestPanels(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                          bool chestOpen) const;

    enum class ChestButton { DepositAll, CollectAll };

    // Draws the two chest action buttons to the left of the chest panel.
    // Only meaningful to call while the chest panel itself is being drawn.
    void drawChestButtons(sf::RenderWindow& window);

    // Screen position -> which chest button it lands on, or nullopt.
    std::optional<ChestButton> hitTestChestButton(sf::Vector2f screenPos, sf::Vector2f windowSize) const;

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
    void drawBuildPalette(sf::RenderWindow& window, MachineType selected);

    bool hasFont() const { return font.has_value(); }

private:
    std::optional<sf::Font> font;
};
