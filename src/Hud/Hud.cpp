#include "Hud.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <string>

#include "../Blocks/Blocks.h"
#include "../Items/Inventory.h"

namespace
{

constexpr float SLOT_SIZE = 48.0f;
constexpr float SLOT_GAP = 4.0f;
constexpr float MARGIN = 12.0f;

constexpr float ICON_INSET = 10.0f;

sf::Color toColor(BlockColor c)
{
    return sf::Color(c.r, c.g, c.b);
}

// The block an item places is also what it looks like in the slot.
sf::Color itemColor(ItemType type)
{
    return toColor(blockInfo(itemInfo(type).placeBlock).color);
}

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
    window.setView(window.getDefaultView());

    const float totalWidth =
        Inventory::HOTBAR_SIZE * SLOT_SIZE + (Inventory::HOTBAR_SIZE - 1) * SLOT_GAP;

    const float startX = (window.getDefaultView().getSize().x - totalWidth) * 0.5f;
    const float y = window.getDefaultView().getSize().y - SLOT_SIZE - MARGIN;

    for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i)
    {
        const float x = startX + i * (SLOT_SIZE + SLOT_GAP);

        const bool isSelected = (i == selectedSlot);

        sf::RectangleShape slot({SLOT_SIZE, SLOT_SIZE});
        slot.setPosition({x, y});
        slot.setFillColor(sf::Color(20, 20, 28, 170));
        slot.setOutlineThickness(isSelected ? -3.0f : -1.0f);
        slot.setOutlineColor(isSelected ? sf::Color(255, 236, 140) : sf::Color(90, 90, 105));

        window.draw(slot);

        const ItemStack& stack = inventory.slot(i);

        if (stack.empty())
            continue;

        // The item itself, as a coloured tile.
        sf::RectangleShape icon({SLOT_SIZE - ICON_INSET * 2.0f, SLOT_SIZE - ICON_INSET * 2.0f});
        icon.setPosition({x + ICON_INSET, y + ICON_INSET});
        icon.setFillColor(itemColor(stack.type));
        icon.setOutlineThickness(-1.0f);
        icon.setOutlineColor(sf::Color(20, 16, 14));

        window.draw(icon);

        if (!font)
            continue;

        sf::Text count(*font, std::to_string(stack.count), 14);
        count.setFillColor(sf::Color::White);
        count.setOutlineThickness(2.0f);
        count.setOutlineColor(sf::Color(10, 10, 12));

        const sf::FloatRect bounds = count.getLocalBounds();

        count.setPosition({x + SLOT_SIZE - bounds.size.x - 5.0f,
                           y + SLOT_SIZE - bounds.size.y - 10.0f});

        window.draw(count);
    }

    window.setView(previous);
}
