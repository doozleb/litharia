#include "LightRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "../Core/Constants.h"
#include "World.h"

namespace
{

constexpr sf::Color NIGHT_TINT(20, 25, 45);
constexpr sf::Color DAY_TINT(225, 235, 250);
constexpr sf::Color BLOCK_TINT(255, 180, 90);

sf::Color lerp(sf::Color a, sf::Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return sf::Color(
        static_cast<std::uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<std::uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<std::uint8_t>(a.b + (b.b - a.b) * t));
}

std::int64_t tileKey(int x, int y)
{
    return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::uint32_t>(y);
}

// Two triangles per tile: SFML 3 has no quad primitive. Matches Chunks.cpp's
// own appendQuad - small enough that each file keeping its own copy reads
// more clearly than sharing a one-off header for it.
void appendQuad(sf::VertexArray& vertices, float left, float top, float right, float bottom,
                sf::Color color)
{
    vertices.append({{left, top}, color});
    vertices.append({{right, top}, color});
    vertices.append({{right, bottom}, color});

    vertices.append({{left, top}, color});
    vertices.append({{right, bottom}, color});
    vertices.append({{left, bottom}, color});
}

} // namespace

void LightRenderer::draw(sf::RenderTarget& target, const sf::View& view, const World& world,
                          const Lighting& lighting, float daylightFactor,
                          const std::vector<std::pair<sf::Vector2i, int>>& heldTorchLight) const
{
    const sf::Vector2f center = view.getCenter();
    const sf::Vector2f size = view.getSize();

    const float viewLeft = center.x - size.x * 0.5f;
    const float viewTop = center.y - size.y * 0.5f;
    const float viewRight = center.x + size.x * 0.5f;
    const float viewBottom = center.y + size.y * 0.5f;

    const int firstX = std::max(0, static_cast<int>(std::floor(viewLeft / TILE_SIZE)) - 1);
    const int firstY = std::max(0, static_cast<int>(std::floor(viewTop / TILE_SIZE)) - 1);
    const int lastX = std::min(WORLD_WIDTH - 1, static_cast<int>(std::floor(viewRight / TILE_SIZE)) + 1);
    const int lastY = std::min(WORLD_HEIGHT - 1, static_cast<int>(std::floor(viewBottom / TILE_SIZE)) + 1);

    std::unordered_map<std::int64_t, int> heldMap;
    for (const auto& [tile, level] : heldTorchLight)
        heldMap[tileKey(tile.x, tile.y)] = level;

    const sf::Color skyTint = lerp(NIGHT_TINT, DAY_TINT, daylightFactor);

    sf::VertexArray vertices(sf::PrimitiveType::Triangles);

    for (int y = firstY; y <= lastY; ++y)
    {
        for (int x = firstX; x <= lastX; ++x)
        {
            // Unexcavated ground is terrain, not a "space" - it renders at
            // its normal color regardless of light, exactly as it did before
            // this feature existed. Only actual open air (caves, dug
            // tunnels, the sky) is ever darkened.
            if (world.isSolid(x, y))
                continue;

            const float skyEffective = lighting.skyLight(x, y) * daylightFactor;

            int blockEffective = lighting.blockLight(x, y);
            const auto it = heldMap.find(tileKey(x, y));
            if (it != heldMap.end())
                blockEffective = std::max(blockEffective, it->second);

            const float brightness = std::clamp(
                std::max(skyEffective, static_cast<float>(blockEffective)) / 8.0f, 0.0f, 1.0f);

            const sf::Color tint = blockEffective > skyEffective ? BLOCK_TINT : skyTint;
            const sf::Color overlay = lerp(sf::Color::Black, tint, brightness);

            const float left = static_cast<float>(x * TILE_SIZE);
            const float top = static_cast<float>(y * TILE_SIZE);
            appendQuad(vertices, left, top, left + TILE_SIZE, top + TILE_SIZE, overlay);
        }
    }

    target.draw(vertices, sf::BlendMultiply);
}
