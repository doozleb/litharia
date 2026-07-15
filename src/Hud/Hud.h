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
    Hud();

    void draw(sf::RenderWindow& window, const Inventory& inventory, int selectedSlot);

    // A small info panel anchored near the cursor, describing one machine's
    // current state: name, input/output, power/fuel state, bar percentage, and
    // (when idle/unpowered) a plain-English reason. Degrades like draw() does:
    // with no font the panel background still shows, just without text.
    void drawMachineTooltip(sf::RenderWindow& window,
                            const Machine& machine,
                            const MachineStatus& status,
                            sf::Vector2f screenPos);

    bool hasFont() const { return font.has_value(); }

private:
    std::optional<sf::Font> font;
};
