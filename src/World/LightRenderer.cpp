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
constexpr sf::Color TORCH_TINT(255, 200, 110);
constexpr sf::Color LAVA_TINT(255, 90, 40);
constexpr sf::Color OUTLINE_TINT(90, 90, 100);

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
                          const std::vector<std::pair<sf::Vector2i, int>>& heldTorchLight,
                          const std::vector<std::pair<sf::Vector2i, int>>& ambientOutline) const
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

    std::unordered_map<std::int64_t, int> outlineMap;
    for (const auto& [tile, level] : ambientOutline)
    {
        int& slot = outlineMap[tileKey(tile.x, tile.y)];
        slot = std::max(slot, level);
    }

    const sf::Color skyTint = lerp(NIGHT_TINT, DAY_TINT, daylightFactor);

    sf::VertexArray vertices(sf::PrimitiveType::Triangles);

    for (int y = firstY; y <= lastY; ++y)
    {
        for (int x = firstX; x <= lastX; ++x)
        {
            int skyRaw;
            int torchRaw;
            int lavaRaw;

            if (world.isSolid(x, y))
            {
                // A solid tile borrows one step dimmer than its brightest
                // open neighbour, per channel - Lighting's BFS never
                // assigns a solid tile its own light, so a neighbour that's
                // itself solid always reads 0 here already, no separate
                // isSolid check needed on the neighbours themselves.
                //
                // Torch is the one channel with a "held" equivalent: the
                // player's held Torch (heldMap) lights open tiles the same
                // way a placed Torch would, but never touches the stored
                // grid, so a neighbour lookup that only reads
                // lighting.torchLight would miss it - fold heldMap into the
                // neighbour lookup too, same as the tile-itself case below.
                const auto torchAt = [&lighting, &heldMap](int nx, int ny) {
                    int level = lighting.torchLight(nx, ny);
                    const auto it = heldMap.find(tileKey(nx, ny));
                    if (it != heldMap.end())
                        level = std::max(level, it->second);
                    return level;
                };

                const int skyN = std::max({lighting.skyLight(x - 1, y), lighting.skyLight(x + 1, y),
                                            lighting.skyLight(x, y - 1), lighting.skyLight(x, y + 1)});
                const int torchN = std::max({torchAt(x - 1, y), torchAt(x + 1, y),
                                              torchAt(x, y - 1), torchAt(x, y + 1)});
                const int lavaN = std::max({lighting.lavaLight(x - 1, y), lighting.lavaLight(x + 1, y),
                                             lighting.lavaLight(x, y - 1), lighting.lavaLight(x, y + 1)});

                skyRaw = std::max(0, skyN - 1);
                torchRaw = std::max(0, torchN - 1);
                lavaRaw = std::max(0, lavaN - 1);
            }
            else
            {
                skyRaw = lighting.skyLight(x, y);
                torchRaw = lighting.torchLight(x, y);
                lavaRaw = lighting.lavaLight(x, y);
            }

            const auto heldIt = heldMap.find(tileKey(x, y));
            if (heldIt != heldMap.end())
                torchRaw = std::max(torchRaw, heldIt->second);

            const float skyEffective = static_cast<float>(skyRaw) * daylightFactor;
            const float torchEffective = static_cast<float>(torchRaw);
            const float lavaEffective = static_cast<float>(lavaRaw);

            const float total = skyEffective + torchEffective + lavaEffective;

            sf::Color overlay;

            if (total > 0.0f)
            {
                const float brightness = std::clamp(
                    std::max({skyEffective, torchEffective, lavaEffective}) / Lighting::MAX_LIGHT_LEVEL,
                    0.0f, 1.0f);

                const sf::Color blended(
                    static_cast<std::uint8_t>(
                        (skyTint.r * skyEffective + TORCH_TINT.r * torchEffective + LAVA_TINT.r * lavaEffective) /
                        total),
                    static_cast<std::uint8_t>(
                        (skyTint.g * skyEffective + TORCH_TINT.g * torchEffective + LAVA_TINT.g * lavaEffective) /
                        total),
                    static_cast<std::uint8_t>(
                        (skyTint.b * skyEffective + TORCH_TINT.b * torchEffective + LAVA_TINT.b * lavaEffective) /
                        total));

                overlay = lerp(sf::Color::Black, blended, brightness);
            }
            else
            {
                const auto outlineIt = outlineMap.find(tileKey(x, y));
                const int outlineLevel = outlineIt != outlineMap.end() ? outlineIt->second : 0;
                const float brightness =
                    std::clamp(static_cast<float>(outlineLevel) / Lighting::MAX_LIGHT_LEVEL, 0.0f, 1.0f);

                overlay = lerp(sf::Color::Black, OUTLINE_TINT, brightness);
            }

            const float left = static_cast<float>(x * TILE_SIZE);
            const float top = static_cast<float>(y * TILE_SIZE);
            appendQuad(vertices, left, top, left + TILE_SIZE, top + TILE_SIZE, overlay);
        }
    }

    target.draw(vertices, sf::BlendMultiply);
}
