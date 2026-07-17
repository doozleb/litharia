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

// Where the crafting/smelting panel's first button sits: directly below the
// bag panel, sharing its left edge. This deliberately sits to the LEFT of
// where the chest panel would be (which hugs the right border instead) - the
// two are mutually exclusive, so they never collide, and storage reads on the
// right while crafting/smelting reads on the left.
sf::Vector2f craftPanelOrigin(sf::Vector2f windowSize)
{
    constexpr int BAG_ROWS = 3;
    const sf::Vector2f bagOrigin = bagPanelOrigin(windowSize);
    const float y =
        bagOrigin.y + BAG_ROWS * Hud::SLOT_SIZE + (BAG_ROWS - 1) * Hud::SLOT_GAP + Hud::MARGIN;

    return {bagOrigin.x, y};
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

// "5x Stone, 2x Iron Plate" - a recipe's cost, skipping its unused ingredient
// slots. Kept separate from the recipe's name so the two can occupy their own
// rows of a button (see drawRecipeLabel).
std::string formatIngredientCost(const CraftRecipe& recipe)
{
    std::string cost;

    for (const CraftIngredient& ing : recipe.ingredients)
    {
        if (ing.item == ItemType::None)
            continue;

        if (!cost.empty())
            cost += ", ";

        cost += std::to_string(ing.count) + "x " + std::string(itemInfo(ing.item).name);
    }

    return cost;
}

// Recipe label geometry, shared by the craft and smelt panels so their rows
// line up.
constexpr float LABEL_PADDING = 8.0f;
constexpr unsigned int LABEL_TITLE_SIZE = 13;
constexpr unsigned int LABEL_SUBTITLE_SIZE = 11;
constexpr float LABEL_TITLE_Y = 4.0f;
constexpr float LABEL_SUBTITLE_Y = 21.0f; // TITLE_Y + TITLE_SIZE + a 4px breather

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
    buildRecipeLabels();
    buildTextCaches();
}

void Hud::buildRecipeLabels()
{
    if (!font)
        return;

    // Built once: every string here is a compile-time constant, so the
    // expensive part (SFML's text geometry build) never has to run again.
    for (const CraftRecipe& recipe : allCraftRecipes())
        craftLabels.push_back(
            {sf::Text(*font, std::string(itemInfo(recipe.output).name), LABEL_TITLE_SIZE),
             sf::Text(*font, formatIngredientCost(recipe), LABEL_SUBTITLE_SIZE)});

    for (const FurnaceRecipe& recipe : allFurnaceRecipes())
        smeltLabels.push_back(
            {sf::Text(*font, std::string(itemInfo(recipe.out).name), LABEL_TITLE_SIZE),
             sf::Text(*font, "from " + std::string(itemInfo(recipe.in).name), LABEL_SUBTITLE_SIZE)});
}

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

// Only ever moves and recolours the cached text - never re-sets its string,
// which is what would force the geometry rebuild this cache exists to avoid.
void Hud::drawRecipeLabel(sf::RenderWindow& window, RecipeLabel& label, sf::Vector2f pos,
                           bool disabled)
{
    label.title.setFillColor(disabled ? sf::Color(150, 150, 150) : sf::Color::White);
    label.title.setPosition({pos.x + LABEL_PADDING, pos.y + LABEL_TITLE_Y});
    window.draw(label.title);

    if (label.subtitle.getString().isEmpty())
        return;

    label.subtitle.setFillColor(disabled ? sf::Color(120, 120, 120) : sf::Color(200, 200, 200));
    label.subtitle.setPosition({pos.x + LABEL_PADDING, pos.y + LABEL_SUBTITLE_Y});
    window.draw(label.subtitle);
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
        drawSlot(window, font ? &hotbarCounts[static_cast<std::size_t>(i)] : nullptr, pos,
                 inventory.slot(i), i == selectedSlot, SLOT_SIZE, BAG_SLOT_BACKGROUND);
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
        drawSlot(window, font ? &bagCounts[static_cast<std::size_t>(gridIndex)] : nullptr, pos,
                 inventory.slot(i), false, SLOT_SIZE, BAG_SLOT_BACKGROUND);
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
        drawSlot(window, font ? &storageCounts[static_cast<std::size_t>(i)] : nullptr, pos,
                 chestStorage.slot(i), false, CHEST_SLOT_SIZE, CHEST_SLOT_BACKGROUND);
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
                                                int openStorageSlots) const
{
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;
    constexpr int BAG_ROWS = 3;

    if (openStorageSlots > 0)
    {
        const int storageRows = (openStorageSlots + COLUMNS - 1) / COLUMNS;
        const int storageIndex = hudLayout::hitTestGrid(screenPos, chestPanelOrigin(windowSize), COLUMNS,
                                                          storageRows, CHEST_SLOT_SIZE, SLOT_GAP);
        if (storageIndex >= 0 && storageIndex < openStorageSlots)
            return SlotHit{true, storageIndex};
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

        if (isFurniture(type))
            continue;

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
    const bool selectedHeld = found != held.end();
    // If the selected type isn't held (e.g. its last unit was just placed, or
    // it was chosen via an F-key before ever crafting one), fall back to
    // index 0 purely to pick which window of the strip to center - but never
    // let that substitution be mistaken for an actual selection below.
    const int selectedIndex = selectedHeld ? static_cast<int>(found - held.begin()) : 0;
    const int half = visible / 2;

    const float totalWidth = visible * SWATCH + (visible - 1) * GAP;
    const sf::Vector2f windowSize(window.getSize());
    const float startX = (windowSize.x - totalWidth) * 0.5f;
    const float y = windowSize.y - SLOT_SIZE - MARGIN - SWATCH - MARGIN * 2.0f;

    // Always name the actual buildType, even when it isn't held - showing a
    // different, arbitrary held type here (and gold-highlighting its swatch
    // below) would lie about what a click will attempt to place.
    const MachineType displayed = selected;

    for (int slot = 0; slot < visible; ++slot)
    {
        const int index = ((selectedIndex + slot - half) % total + total) % total;
        const MachineType type = held[index];
        const MachineInfo& info = machineInfo(type);
        const bool isSelected = selectedHeld && (index == selectedIndex);

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

void Hud::drawCraftPanel(sf::RenderWindow& window, const Inventory& bag, bool advanced, bool crafting,
                          int craftingRecipeIndex, float craftProgress)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    const std::span<const CraftRecipe> all = allCraftRecipes();
    const sf::Vector2f origin = craftPanelOrigin(sf::Vector2f(window.getSize()));

    int row = 0;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].requiresCraftingTable != advanced)
            continue;

        const CraftRecipe& recipe = all[i];
        const sf::Vector2f pos{origin.x, origin.y + row * (CRAFT_BUTTON_HEIGHT + SLOT_GAP)};
        ++row;

        bool affordable = true;
        for (const CraftIngredient& ing : recipe.ingredients)
            if (ing.item != ItemType::None && bag.count(ing.item) < ing.count)
                affordable = false;

        const bool disabled = crafting || !affordable;
        const bool inProgress = crafting && static_cast<int>(i) == craftingRecipeIndex;

        sf::RectangleShape button({CRAFT_BUTTON_WIDTH, CRAFT_BUTTON_HEIGHT});
        button.setPosition(pos);
        button.setFillColor(disabled ? sf::Color(40, 40, 46, 170) : BAG_SLOT_BACKGROUND);
        button.setOutlineThickness(-1.0f);
        button.setOutlineColor(sf::Color(90, 90, 105));
        window.draw(button);

        if (inProgress)
        {
            const float fraction = std::clamp(craftProgress / recipe.seconds, 0.0f, 1.0f);
            sf::RectangleShape fill({CRAFT_BUTTON_WIDTH * fraction, CRAFT_BUTTON_HEIGHT});
            fill.setPosition(pos);
            fill.setFillColor(sf::Color(90, 200, 230, 120));
            window.draw(fill);
        }

        if (!font)
            continue;

        // Two rows: what you get, then what it costs. As one line, a recipe
        // like "Burner Generator (5x Stone, 2x Iron Plate)" runs well past the
        // button's right edge - the cost is what makes it long, so it wraps to
        // its own row rather than being truncated or shrunk to fit.
        drawRecipeLabel(window, craftLabels[i], pos, disabled);
    }

    window.setView(previous);
}

std::optional<int> Hud::hitTestCraftButton(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                            bool advanced) const
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const sf::Vector2f origin = craftPanelOrigin(windowSize);

    int row = 0;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].requiresCraftingTable != advanced)
            continue;

        const sf::Vector2f pos{origin.x, origin.y + row * (CRAFT_BUTTON_HEIGHT + SLOT_GAP)};
        ++row;

        const sf::FloatRect rect(pos, {CRAFT_BUTTON_WIDTH, CRAFT_BUTTON_HEIGHT});
        if (rect.contains(screenPos))
            return static_cast<int>(i);
    }

    return std::nullopt;
}

void Hud::drawSmeltPanel(sf::RenderWindow& window, const Inventory& bag, bool smelting,
                          int smeltingRecipeIndex, float smeltProgress)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    const std::span<const FurnaceRecipe> all = allFurnaceRecipes();
    const sf::Vector2f origin = craftPanelOrigin(sf::Vector2f(window.getSize()));

    for (std::size_t i = 0; i < all.size(); ++i)
    {
        const FurnaceRecipe& recipe = all[i];
        const sf::Vector2f pos{origin.x,
                               origin.y + static_cast<float>(i) * (CRAFT_BUTTON_HEIGHT + SLOT_GAP)};

        const bool affordable = bag.count(recipe.in) > 0;
        const bool disabled = smelting || !affordable;
        const bool inProgress = smelting && static_cast<int>(i) == smeltingRecipeIndex;

        sf::RectangleShape button({CRAFT_BUTTON_WIDTH, CRAFT_BUTTON_HEIGHT});
        button.setPosition(pos);
        button.setFillColor(disabled ? sf::Color(40, 40, 46, 170) : BAG_SLOT_BACKGROUND);
        button.setOutlineThickness(-1.0f);
        button.setOutlineColor(sf::Color(90, 90, 105));
        window.draw(button);

        if (inProgress)
        {
            const float fraction = std::clamp(smeltProgress / recipe.seconds, 0.0f, 1.0f);
            sf::RectangleShape fill({CRAFT_BUTTON_WIDTH * fraction, CRAFT_BUTTON_HEIGHT});
            fill.setPosition(pos);
            fill.setFillColor(sf::Color(90, 200, 230, 120));
            window.draw(fill);
        }

        if (!font)
            continue;

        // Same two-row shape as the craft panel: what you get, then what it
        // costs - rather than one "Copper Ore -> Copper Plate" line.
        drawRecipeLabel(window, smeltLabels[i], pos, disabled);
    }

    window.setView(previous);
}

std::optional<int> Hud::hitTestSmeltButton(sf::Vector2f screenPos, sf::Vector2f windowSize) const
{
    const std::span<const FurnaceRecipe> all = allFurnaceRecipes();
    const sf::Vector2f origin = craftPanelOrigin(windowSize);

    for (std::size_t i = 0; i < all.size(); ++i)
    {
        const sf::Vector2f pos{origin.x,
                               origin.y + static_cast<float>(i) * (CRAFT_BUTTON_HEIGHT + SLOT_GAP)};
        const sf::FloatRect rect(pos, {CRAFT_BUTTON_WIDTH, CRAFT_BUTTON_HEIGHT});

        if (rect.contains(screenPos))
            return static_cast<int>(i);
    }

    return std::nullopt;
}
