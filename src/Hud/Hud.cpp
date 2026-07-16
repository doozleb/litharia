#include "Hud.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "../Blocks/Blocks.h"
#include "../Items/Inventory.h"
#include "../Machines/MachineType.h"
#include "../Machines/Recipes.h"
#include "HudLayout.h"

namespace
{

constexpr float ICON_INSET = 10.0f;

// The bag/hotbar slot backdrop. The chest panel uses a lighter grey instead,
// so its slots read as a distinct container at a glance.
constexpr sf::Color BAG_SLOT_BACKGROUND(20, 20, 28, 170);
constexpr sf::Color CHEST_SLOT_BACKGROUND(140, 140, 145, 170);

sf::Color toColor(BlockColor c)
{
    return sf::Color(c.r, c.g, c.b);
}

// sf::RenderTarget::getDefaultView() is frozen at the window's size when it was
// created and never follows a resize, but Game's hit-testing uses the window's
// live size - so HUD drawing must too, or panels render in one coordinate space
// while clicks are tested in another and every click on a visible slot misses.
sf::View currentWindowView(const sf::RenderWindow& window)
{
    return sf::View(sf::FloatRect({0.0f, 0.0f}, sf::Vector2f(window.getSize())));
}

sf::Color itemColor(ItemType type)
{
    return toColor(itemInfo(type).iconColor);
}

// Draws one slot's background, item icon, and stack count - shared by the
// hotbar and the bag/chest panels so they render identically (aside from
// their own slot size and background color).
void drawSlot(sf::RenderWindow& window, const std::optional<sf::Font>& font, sf::Vector2f pos,
              const ItemStack& stack, bool highlighted, float slotSize, sf::Color backgroundColor,
              unsigned int countFontSize)
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

    if (!font)
        return;

    sf::Text count(*font, std::to_string(stack.count), countFontSize);
    count.setFillColor(sf::Color::White);
    count.setOutlineThickness(2.0f);
    count.setOutlineColor(sf::Color(10, 10, 12));

    const sf::FloatRect bounds = count.getLocalBounds();
    count.setPosition({pos.x + slotSize - bounds.size.x - 5.0f,
                       pos.y + slotSize - bounds.size.y - 10.0f});
    window.draw(count);
}

// Where the hotbar's first slot sits: hugging the top-right corner. Shared by
// drawing and hit-testing so the two can never drift apart - the bag panel
// anchors below this same origin.
sf::Vector2f hotbarOrigin(sf::Vector2f windowSize)
{
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;

    const float totalWidth = COLUMNS * Hud::SLOT_SIZE + (COLUMNS - 1) * Hud::SLOT_GAP;
    const float startX = windowSize.x - totalWidth - Hud::MARGIN;
    const float y = Hud::MARGIN;

    return {startX, y};
}

// Where the bag panel's first slot sits: directly below the hotbar, sharing
// its right edge (same slot size and column count, so same width).
sf::Vector2f bagPanelOrigin(sf::Vector2f windowSize)
{
    const sf::Vector2f hotbar = hotbarOrigin(windowSize);
    const float bagY = hotbar.y + Hud::SLOT_SIZE + Hud::MARGIN;

    return {hotbar.x, bagY};
}

// Where the chest panel's first slot sits: directly below the bag panel. The
// chest's slots are smaller, so its own width is anchored to the right edge
// independently - narrower than the column above it, but still flush against
// the same right border.
sf::Vector2f chestPanelOrigin(sf::Vector2f windowSize)
{
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;
    constexpr int BAG_ROWS = 3;

    const sf::Vector2f bagOrigin = bagPanelOrigin(windowSize);
    const float chestY =
        bagOrigin.y + BAG_ROWS * Hud::SLOT_SIZE + (BAG_ROWS - 1) * Hud::SLOT_GAP + Hud::MARGIN;

    const float chestWidth = COLUMNS * Hud::CHEST_SLOT_SIZE + (COLUMNS - 1) * Hud::SLOT_GAP;
    const float chestX = windowSize.x - chestWidth - Hud::MARGIN;

    return {chestX, chestY};
}

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

struct TooltipLine
{
    std::string text;
    sf::Color color;
};

std::optional<sf::Font> loadFont()
{
    // The project's own font first; a system font is only a convenience fallback so
    // the counts still show on a fresh clone with an empty assets/ folder.
    const std::array<const char*, 3> candidates = {
        "assets/font.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };

    for (const char* path : candidates)
    {
        // Checked first so SFML does not print its own error for every candidate we
        // merely tried.
        if (!std::filesystem::exists(path))
            continue;

        sf::Font font;

        if (font.openFromFile(path))
            return font;
    }

    // Not fatal: the HUD simply goes without numbers.
    std::cerr << "Litharia: no HUD font found (tried assets/font.ttf). "
                 "Hotbar will render without stack counts.\n";

    return std::nullopt;
}

} // namespace

Hud::Hud()
    : font(loadFont())
{
}

void Hud::draw(sf::RenderWindow& window, const Inventory& inventory, int selectedSlot)
{
    // The HUD lives in screen space, not world space, so it does not scroll with
    // the camera.
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    const sf::Vector2f origin = hotbarOrigin(sf::Vector2f(window.getSize()));
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;

    for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i)
    {
        const sf::Vector2f pos = hudLayout::gridSlotPosition(origin, i, COLUMNS, SLOT_SIZE, SLOT_GAP);
        drawSlot(window, font, pos, inventory.slot(i), i == selectedSlot, SLOT_SIZE, BAG_SLOT_BACKGROUND,
                 COUNT_FONT_SIZE);
    }

    window.setView(previous);
}

void Hud::drawInventoryPanel(sf::RenderWindow& window, const Inventory& inventory)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    const sf::Vector2f origin = bagPanelOrigin(sf::Vector2f(window.getSize()));
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;

    for (int i = Inventory::HOTBAR_SIZE; i < inventory.slotCount(); ++i)
    {
        const int gridIndex = i - Inventory::HOTBAR_SIZE;
        const sf::Vector2f pos =
            hudLayout::gridSlotPosition(origin, gridIndex, COLUMNS, SLOT_SIZE, SLOT_GAP);
        drawSlot(window, font, pos, inventory.slot(i), false, SLOT_SIZE, BAG_SLOT_BACKGROUND,
                 COUNT_FONT_SIZE);
    }

    window.setView(previous);
}

void Hud::drawChestPanel(sf::RenderWindow& window, const Inventory& chestStorage)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    const sf::Vector2f origin = chestPanelOrigin(sf::Vector2f(window.getSize()));
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;

    for (int i = 0; i < chestStorage.slotCount(); ++i)
    {
        const sf::Vector2f pos =
            hudLayout::gridSlotPosition(origin, i, COLUMNS, CHEST_SLOT_SIZE, SLOT_GAP);
        drawSlot(window, font, pos, chestStorage.slot(i), false, CHEST_SLOT_SIZE, CHEST_SLOT_BACKGROUND,
                 CHEST_COUNT_FONT_SIZE);
    }

    window.setView(previous);
}

void Hud::drawChestButtons(sf::RenderWindow& window)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    const sf::Vector2f origin = chestButtonsOrigin(sf::Vector2f(window.getSize()));

    drawChestButton(window, font, origin, "Deposit All");
    drawChestButton(window, font, {origin.x, origin.y + CHEST_BUTTON_HEIGHT + SLOT_GAP}, "Collect All");

    window.setView(previous);
}

void Hud::drawDragGhost(sf::RenderWindow& window, const ItemStack& stack, sf::Vector2f screenPos)
{
    if (stack.empty())
        return;

    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    constexpr float SIZE = SLOT_SIZE - ICON_INSET * 2.0f;

    sf::RectangleShape icon({SIZE, SIZE});
    icon.setPosition(screenPos - sf::Vector2f{SIZE * 0.5f, SIZE * 0.5f});
    icon.setFillColor(itemColor(stack.type));
    icon.setOutlineThickness(-1.0f);
    icon.setOutlineColor(sf::Color(240, 240, 240));
    window.draw(icon);

    if (font)
    {
        sf::Text count(*font, std::to_string(stack.count), 14);
        count.setFillColor(sf::Color::White);
        count.setOutlineThickness(2.0f);
        count.setOutlineColor(sf::Color(10, 10, 12));
        count.setPosition(screenPos + sf::Vector2f{8.0f, 8.0f});
        window.draw(count);
    }

    window.setView(previous);
}

std::optional<Hud::SlotHit> Hud::hitTestPanels(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                                bool chestOpen) const
{
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;
    constexpr int BAG_ROWS = 3;
    constexpr int CHEST_ROWS = 2;

    if (chestOpen)
    {
        const int chestIndex = hudLayout::hitTestGrid(screenPos, chestPanelOrigin(windowSize), COLUMNS,
                                                        CHEST_ROWS, CHEST_SLOT_SIZE, SLOT_GAP);
        if (chestIndex >= 0)
            return SlotHit{true, chestIndex};
    }

    const int bagGridIndex = hudLayout::hitTestGrid(screenPos, bagPanelOrigin(windowSize), COLUMNS,
                                                      BAG_ROWS, SLOT_SIZE, SLOT_GAP);
    if (bagGridIndex >= 0)
        return SlotHit{false, Inventory::HOTBAR_SIZE + bagGridIndex};

    // The hotbar is drawn every frame, not just while a panel is open, but it's
    // still a bag slot - drag-and-drop must reach it too, or items get stuck
    // there with no way back into the bag/chest grid.
    const int hotbarIndex =
        hudLayout::hitTestGrid(screenPos, hotbarOrigin(windowSize), COLUMNS, 1, SLOT_SIZE, SLOT_GAP);
    if (hotbarIndex >= 0)
        return SlotHit{false, hotbarIndex};

    return std::nullopt;
}

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

void Hud::drawBuildPalette(sf::RenderWindow& window, MachineType selected, const Inventory& bag)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    constexpr int FIRST = 1; // skip MachineType::None

    std::vector<MachineType> held;
    for (int i = FIRST; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineType type = static_cast<MachineType>(i);
        if (bag.count(itemForMachine(type)) > 0)
            held.push_back(type);
    }

    if (held.empty())
    {
        window.setView(previous);
        return;
    }

    constexpr int VISIBLE = 5;
    constexpr float SWATCH = 40.0f;
    constexpr float GAP = 6.0f;

    const int total = static_cast<int>(held.size());
    const int visible = std::min(VISIBLE, total);

    const auto found = std::find(held.begin(), held.end(), selected);
    const int selectedIndex = (found != held.end()) ? static_cast<int>(found - held.begin()) : 0;
    const int half = visible / 2;

    const float totalWidth = visible * SWATCH + (visible - 1) * GAP;
    const sf::Vector2f windowSize(window.getSize());
    const float startX = (windowSize.x - totalWidth) * 0.5f;
    const float y = windowSize.y - SLOT_SIZE - MARGIN - SWATCH - MARGIN * 2.0f;

    const MachineType displayed = held[selectedIndex];

    for (int slot = 0; slot < visible; ++slot)
    {
        const int index = ((selectedIndex + slot - half) % total + total) % total;
        const MachineType type = held[index];
        const MachineInfo& info = machineInfo(type);
        const bool isSelected = (index == selectedIndex);

        const sf::Vector2f pos{startX + slot * (SWATCH + GAP), y};

        sf::RectangleShape swatch({SWATCH, SWATCH});
        swatch.setPosition(pos);
        swatch.setFillColor(toColor(info.color));
        swatch.setOutlineThickness(isSelected ? -3.0f : -1.0f);
        swatch.setOutlineColor(isSelected ? sf::Color(255, 236, 140) : sf::Color(90, 90, 105));
        window.draw(swatch);

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
    }

    if (!font)
    {
        window.setView(previous);
        return;
    }

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

    window.setView(previous);
}

void Hud::drawMachineTooltip(sf::RenderWindow& window,
                              const Machine& machine,
                              const MachineStatus& status,
                              sf::Vector2f screenPos)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    const MachineInfo& info = machineInfo(machine.type);

    std::vector<TooltipLine> lines;
    lines.push_back({std::string(info.name), sf::Color::White});

    if (machine.type == MachineType::Smelter)
        lines.push_back({"Smelts: " + formatSmeltRecipeList(allSmeltRecipes()), sf::Color::White});
    else if (machine.type == MachineType::Drill)
        lines.push_back({"Mines: " + formatDrillOreList(DRILL_ORES), sf::Color::White});

    if (info.generator)
        lines.push_back({"Fuel: " + std::to_string(static_cast<int>(machine.fuel)) + "s remaining",
                          sf::Color::White});
    else if (info.consumer)
        lines.push_back({machine.powered ? "Powered: yes" : "Powered: no", sf::Color::White});

    if (!machine.input.empty())
        lines.push_back({"Input: " + std::to_string(machine.input.count) + "x " +
                              std::string(itemInfo(machine.input.type).name),
                          sf::Color::White});

    if (!machine.output.empty())
        lines.push_back({"Output: " + std::to_string(machine.output.count) + "x " +
                              std::string(itemInfo(machine.output.type).name),
                          sf::Color::White});

    if (status.bar != MachineBar::None)
        lines.push_back({(status.bar == MachineBar::Fuel ? std::string("Fuel: ")
                                                           : std::string("Progress: ")) +
                              std::to_string(static_cast<int>(status.fraction * 100.0f)) + "%",
                          sf::Color::White});

    if (!status.reason.empty())
        lines.push_back({status.reason, sf::Color(255, 200, 120)});

    constexpr float PADDING = 8.0f;
    constexpr float LINE_HEIGHT = 18.0f;
    constexpr float CHAR_WIDTH = 7.0f; // rough estimate; only sizes the background panel

    std::size_t longest = 0;
    for (const TooltipLine& line : lines)
        longest = std::max(longest, line.text.size());

    const float width = static_cast<float>(longest) * CHAR_WIDTH + PADDING * 2.0f;
    const float height = static_cast<float>(lines.size()) * LINE_HEIGHT + PADDING * 2.0f;

    constexpr float TOOLTIP_MARGIN = 10.0f;
    const sf::Vector2f pos = hudLayout::tooltipTopLeft(screenPos, {width, height}, TOOLTIP_MARGIN);

    sf::RectangleShape panel({width, height});
    panel.setPosition(pos);
    panel.setFillColor(sf::Color(20, 20, 28, 220));
    panel.setOutlineThickness(-1.0f);
    panel.setOutlineColor(sf::Color(90, 90, 105));
    window.draw(panel);

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

    window.setView(previous);
}
