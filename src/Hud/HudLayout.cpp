#include "HudLayout.h"

namespace hudLayout
{

sf::Vector2f tooltipTopLeft(sf::Vector2f anchor, sf::Vector2f panelSize, float verticalMargin)
{
    return {anchor.x - panelSize.x * 0.5f, anchor.y - panelSize.y - verticalMargin};
}

sf::Vector2f gridSlotPosition(sf::Vector2f origin, int index, int columns, float slotSize,
                               float gap)
{
    const int col = index % columns;
    const int row = index / columns;
    const float pitch = slotSize + gap;

    return {origin.x + static_cast<float>(col) * pitch, origin.y + static_cast<float>(row) * pitch};
}

int hitTestGrid(sf::Vector2f point, sf::Vector2f origin, int columns, int rows, float slotSize,
                 float gap)
{
    const sf::Vector2f local = point - origin;

    if (local.x < 0.0f || local.y < 0.0f)
        return -1;

    const float pitch = slotSize + gap;

    const int col = static_cast<int>(local.x / pitch);
    const int row = static_cast<int>(local.y / pitch);

    if (col < 0 || col >= columns || row < 0 || row >= rows)
        return -1;

    // Reject a point that landed in the gap between slots, not on a slot itself.
    const float withinCol = local.x - static_cast<float>(col) * pitch;
    const float withinRow = local.y - static_cast<float>(row) * pitch;

    if (withinCol > slotSize || withinRow > slotSize)
        return -1;

    return row * columns + col;
}

} // namespace hudLayout
