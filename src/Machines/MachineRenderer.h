#pragma once

#include <SFML/Graphics.hpp>

class Machines;

// Draws the machine layer: a colored quad per machine, dimmed when unpowered, with
// a facing tick and a dot for any carried/output item. In the executable only.
class MachineRenderer
{
public:
    void draw(sf::RenderTarget& target, const Machines& machines) const;
};
