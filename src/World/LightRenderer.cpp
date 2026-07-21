#include "LightRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>
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

// How far real light (sky, Torch, or Lava alike) can be glimpsed through
// solid rock: a solid tile borrows the best `channelValue - distance`
// candidate from every tile within this many orthogonal steps, not just its
// 4 immediate neighbours - a direct generalization of "borrow one step
// dimmer than the brightest neighbour" out to a wider radius, so a wall a
// couple of tiles from a lit cavity glows faintly instead of reading fully
// dark. Distance 1 alone reproduces today's 4-neighbour behaviour exactly.
constexpr int WALL_PENETRATION_DEPTH = 3;

// Per-distance brightness penalty for the wall-penetration search above -
// steeper than the general "-1 per step" light-decay rate used everywhere
// else in this system, so the fade across those 3 tiles actually reads as a
// fade instead of three near-identical shades: distance 1 stays reasonably
// bright, distance 2 reads as "just dark," distance 3 as "very dark" (only
// a source at or near Lighting::MAX_LIGHT_LEVEL has any brightness budget
// left by then). Indexed by distance - 1, since wallPenetrationOffsets only
// ever produces distances 1..WALL_PENETRATION_DEPTH.
constexpr int WALL_PENETRATION_PENALTY[WALL_PENETRATION_DEPTH] = {2, 5, 8};

// Every (dx, dy, distance) offset within Manhattan distance 1..WALL_PENETRATION_DEPTH
// of a tile - a 24-cell diamond (4 tiles at distance 1, 8 at distance 2, 12
// at distance 3). Built once (see the function-local static in draw()); no
// isSolid check on the offset tile itself is needed here - Lighting's BFS
// never assigns a solid tile its own light, so an offset that happens to
// land on solid ground already reads 0 from every channel accessor and can
// never win over an actually-lit open tile.
std::vector<std::tuple<int, int, int>> wallPenetrationOffsets()
{
    std::vector<std::tuple<int, int, int>> offsets;

    for (int dx = -WALL_PENETRATION_DEPTH; dx <= WALL_PENETRATION_DEPTH; ++dx)
    {
        for (int dy = -WALL_PENETRATION_DEPTH; dy <= WALL_PENETRATION_DEPTH; ++dy)
        {
            const int distance = std::abs(dx) + std::abs(dy);
            if (distance >= 1 && distance <= WALL_PENETRATION_DEPTH)
                offsets.push_back({dx, dy, distance});
        }
    }

    return offsets;
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
                // A solid tile borrows the best (channelValue - distance)
                // candidate from anywhere within WALL_PENETRATION_DEPTH
                // orthogonal steps, per channel - see wallPenetrationOffsets
                // for why no separate isSolid check is needed on the
                // candidate tiles themselves.
                //
                // Torch is the one channel with a "held" equivalent: the
                // player's held Torch (heldMap) lights open tiles the same
                // way a placed Torch would, but never touches the stored
                // grid, so a lookup that only reads lighting.torchLight
                // would miss it - fold heldMap into the lookup too, same as
                // the tile-itself case below.
                const auto torchAt = [&lighting, &heldMap](int nx, int ny) {
                    int level = lighting.torchLight(nx, ny);
                    const auto it = heldMap.find(tileKey(nx, ny));
                    if (it != heldMap.end())
                        level = std::max(level, it->second);
                    return level;
                };

                static const std::vector<std::tuple<int, int, int>> penetrationOffsets =
                    wallPenetrationOffsets();

                int skyBest = 0;
                int torchBest = 0;
                int lavaBest = 0;

                for (const auto& [dx, dy, distance] : penetrationOffsets)
                {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    const int penalty = WALL_PENETRATION_PENALTY[distance - 1];

                    skyBest = std::max(skyBest, lighting.skyLight(nx, ny) - penalty);
                    torchBest = std::max(torchBest, torchAt(nx, ny) - penalty);
                    lavaBest = std::max(lavaBest, lighting.lavaLight(nx, ny) - penalty);
                }

                // skyBest/torchBest/lavaBest start at 0 and are only ever
                // raised by std::max, so they can never go negative - no
                // separate clamp needed here (unlike the old single-neighbour
                // version, which subtracted 1 *after* taking the max and so
                // needed an explicit std::max(0, ...) guard).
                skyRaw = skyBest;
                torchRaw = torchBest;
                lavaRaw = lavaBest;
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
