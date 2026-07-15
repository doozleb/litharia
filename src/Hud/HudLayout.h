#pragma once

#include <SFML/System/Vector2.hpp>

// Pure screen-space layout math for the HUD: no SFML Graphics or Window, so it
// lives in the core library and is unit-testable without a real render window.
// Hud.cpp (which does own a window) calls these so drawing and hit-testing can
// never drift out of sync with each other.
namespace hudLayout
{

// Where a panel's top-left corner should sit so its bottom edge is centered a
// fixed `verticalMargin` above `anchor` - used to anchor the machine tooltip
// above the machine's own tile instead of near the mouse.
sf::Vector2f tooltipTopLeft(sf::Vector2f anchor, sf::Vector2f panelSize, float verticalMargin);

// The top-left corner of grid slot `index` (0-based, filled row-major) inside
// a `columns`-wide grid whose first slot starts at `origin`.
sf::Vector2f gridSlotPosition(sf::Vector2f origin, int index, int columns, float slotSize,
                               float gap);

// Which slot of a `columns` x `rows` grid (first slot at `origin`) contains
// `point`, or -1 if `point` falls outside every slot, including the gaps
// between them.
int hitTestGrid(sf::Vector2f point, sf::Vector2f origin, int columns, int rows, float slotSize,
                 float gap);

} // namespace hudLayout
