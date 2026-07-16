#include "MachineRenderer.h"

#include <array>

#include "../Core/Constants.h"
#include "../Core/Direction.h"
#include "../Items/Items.h"
#include "Machines.h"
#include "MachineStatus.h"

namespace
{

sf::Color toColor(BlockColor c, std::uint8_t alpha = 255)
{
    return sf::Color(c.r, c.g, c.b, alpha);
}

// The color of the item riding a machine, matching how drops look on the ground.
sf::Color itemColor(ItemType type)
{
    return toColor(itemInfo(type).iconColor);
}

// Every side of every machine is either an input (light blue) or an output
// (light red) side - which one depends on the machine type:
//  - Drill/Smelter: facing is input (an ore vein, a feeder belt); the other 3
//    sides round-robin output (Machines::insertOutput).
//  - Belt: facing is where it carries its item to; the other 3 sides can feed
//    it (tryInsert doesn't care which side a push comes from).
//  - Chute: always drops straight down regardless of facing, so Down is
//    always its output side.
//  - BurnerGenerator/Chest: never push an item out on their own - every side
//    is an input side.
bool isOutputSide(const Machine& m, Direction side)
{
    switch (m.type)
    {
        case MachineType::Drill:
        case MachineType::Smelter:
            return side != m.facing;

        case MachineType::Belt:
            return side == m.facing;

        case MachineType::Chute:
            return side == Direction::Down;

        default:
            return false;
    }
}

} // namespace

void MachineRenderer::draw(sf::RenderTarget& target, const Machines& machines) const
{
    sf::RectangleShape body;
    body.setOutlineThickness(-1.0f);
    body.setOutlineColor(sf::Color(20, 20, 24));

    // Every machine's 4 sides are shown split into input (light blue) and
    // output (light red) ticks - see isOutputSide() for what that split means
    // per machine type.
    sf::RectangleShape inputTick({4.0f, 4.0f});
    inputTick.setFillColor(sf::Color(140, 200, 255));

    sf::RectangleShape outputTick({4.0f, 4.0f});
    outputTick.setFillColor(sf::Color(255, 120, 120));

    sf::CircleShape item(3.0f);
    item.setOrigin({3.0f, 3.0f});

    for (const Machine& m : machines.all())
    {
        const MachineInfo& info = machineInfo(m.type);

        const float px = static_cast<float>(m.x * TILE_SIZE);
        const float py = static_cast<float>(m.y * TILE_SIZE);

        // Consumers dim when they have no power.
        const std::uint8_t alpha = (info.consumer && !m.powered) ? 120 : 255;

        body.setSize({static_cast<float>(info.width * TILE_SIZE), static_cast<float>(TILE_SIZE)});
        body.setPosition({px, py});
        body.setFillColor(toColor(info.color, alpha));
        target.draw(body);

        if (m.type == MachineType::CraftingTable)
            continue;

        const MachineStatus status = barStatus(m);

        if (status.bar != MachineBar::None)
        {
            constexpr float BAR_WIDTH = 4.0f;
            constexpr float BAR_MARGIN = 2.0f;

            const float barAreaHeight = static_cast<float>(TILE_SIZE) - BAR_MARGIN * 2.0f;
            const float barFillHeight = barAreaHeight * status.fraction;

            sf::RectangleShape barBackground({BAR_WIDTH, barAreaHeight});
            barBackground.setPosition({px + BAR_MARGIN, py + BAR_MARGIN});
            barBackground.setFillColor(sf::Color(20, 20, 24, 200));
            target.draw(barBackground);

            // Fills from the bottom up, like a fuel gauge. Fuel is amber, progress
            // is cyan, so the two are never visually confused.
            sf::RectangleShape barFill({BAR_WIDTH, barFillHeight});
            barFill.setPosition({px + BAR_MARGIN, py + BAR_MARGIN + (barAreaHeight - barFillHeight)});
            barFill.setFillColor(status.bar == MachineBar::Fuel ? sf::Color(230, 140, 40)
                                                                 : sf::Color(90, 200, 230));
            target.draw(barFill);
        }

        const float cx = px + TILE_SIZE * 0.5f - 2.0f;
        const float cy = py + TILE_SIZE * 0.5f - 2.0f;

        static constexpr std::array<Direction, 4> ALL_SIDES = {Direction::Up, Direction::Down,
                                                                 Direction::Left, Direction::Right};
        for (Direction side : ALL_SIDES)
        {
            sf::RectangleShape& tick = isOutputSide(m, side) ? outputTick : inputTick;
            tick.setPosition({cx + dirDX(side) * 5.0f, cy + dirDY(side) * 5.0f});
            target.draw(tick);
        }

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
