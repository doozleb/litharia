#pragma once

#include <SFML/Graphics.hpp>

#include <optional>

class Inventory;

// Draws the ten hotbar slots in screen space. If no font can be found the slots
// and their contents still render - only the stack counts are missing, and the
// game runs.
class Hud
{
public:
    Hud();

    void draw(sf::RenderWindow& window, const Inventory& inventory, int selectedSlot);

    bool hasFont() const { return font.has_value(); }

private:
    std::optional<sf::Font> font;
};
