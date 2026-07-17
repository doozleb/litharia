#pragma once

#include <SFML/Graphics.hpp>

class Machines;

// Draws the machine layer: a colored quad per machine, dimmed when unpowered, with
// a facing tick and a dot for any carried/output item. In the executable only.
class MachineRenderer
{
public:
    // showSideTicks draws the input/output side markers - build-time
    // scaffolding, so the caller passes buildMode. The body, fuel/progress
    // bar, and carried-item glyph always draw, in both modes.
    void draw(sf::RenderTarget& target, const Machines& machines, bool showSideTicks) const;
};
