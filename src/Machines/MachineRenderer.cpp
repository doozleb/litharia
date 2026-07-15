#include "MachineRenderer.h"

#include "../Core/Constants.h"
#include "../Core/Direction.h"
#include "../Items/Items.h"
#include "Machines.h"

namespace
{

sf::Color toColor(BlockColor c, std::uint8_t alpha = 255)
{
    return sf::Color(c.r, c.g, c.b, alpha);
}

// The color of the item riding a machine, matching how drops look on the ground.
sf::Color itemColor(ItemType type)
{
    const BlockType block = itemInfo(type).placeBlock;

    // Plates are not placeable; give them a bright refined tint.
    if (block == BlockType::Air)
        return sf::Color(220, 220, 235);

    return toColor(blockInfo(block).color);
}

} // namespace

void MachineRenderer::draw(sf::RenderTarget& target, const Machines& machines) const
{
    sf::RectangleShape body({static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)});
    body.setOutlineThickness(-1.0f);
    body.setOutlineColor(sf::Color(20, 20, 24));

    sf::RectangleShape facing({4.0f, 4.0f});
    facing.setFillColor(sf::Color(250, 250, 210));

    sf::CircleShape item(3.0f);
    item.setOrigin({3.0f, 3.0f});

    for (const Machine& m : machines.all())
    {
        const MachineInfo& info = machineInfo(m.type);

        const float px = static_cast<float>(m.x * TILE_SIZE);
        const float py = static_cast<float>(m.y * TILE_SIZE);

        // Consumers dim when they have no power.
        const std::uint8_t alpha = (info.consumer && !m.powered) ? 120 : 255;

        body.setPosition({px, py});
        body.setFillColor(toColor(info.color, alpha));
        target.draw(body);

        // A small tick showing which way it faces / outputs.
        const float cx = px + TILE_SIZE * 0.5f - 2.0f;
        const float cy = py + TILE_SIZE * 0.5f - 2.0f;
        facing.setPosition({cx + dirDX(m.facing) * 5.0f, cy + dirDY(m.facing) * 5.0f});
        target.draw(facing);

        // The carried transport item, or the output buffer's item.
        ItemType shown = m.carried;
        if (shown == ItemType::None && !m.output.empty())
            shown = m.output.type;

        if (shown != ItemType::None)
        {
            item.setPosition({px + TILE_SIZE * 0.5f, py + TILE_SIZE * 0.5f});
            item.setFillColor(itemColor(shown));
            target.draw(item);
        }
    }
}
