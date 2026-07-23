#include "EnemySpawner.h"

#include <algorithm>

#include "../Core/Constants.h"
#include "../Core/Noise.h"
#include "../World/TerrainGenerator.h"
#include "../World/World.h"

std::optional<EnemySpawn> attemptSpawn(const World& world,
                                        const TerrainGenerator& generator,
                                        ViewBounds view,
                                        float daylightFactor,
                                        int currentEnemyCount,
                                        std::uint32_t salt)
{
    if (currentEnemyCount >= MAX_ENEMIES)
        return std::nullopt;

    // Pick the left or right edge of the view, offset further out by the
    // margin - "just outside the visible screen".
    const bool useRightEdge = noise::hashFloat(0, static_cast<int>(salt), salt) >= 0.5f;
    const float edgeX = useRightEdge ? view.right + SPAWN_MARGIN_TILES * TILE_SIZE
                                      : view.left - SPAWN_MARGIN_TILES * TILE_SIZE;
    const int x = std::clamp(static_cast<int>(edgeX / TILE_SIZE), 0, WORLD_WIDTH - 1);

    // Start the vertical search from the view's own vertical center - the
    // camera follows the player, so this naturally covers the cave case: if
    // the player is deep underground, "just outside their view" at the
    // view's own depth is still inside/near the same cave system.
    const int startY = static_cast<int>((view.top + view.bottom) * 0.5f / TILE_SIZE);

    int foundY = -1;
    for (int i = 0; i <= SPAWN_VERTICAL_SEARCH_TILES; ++i)
    {
        const int y = startY + i;
        if (y < 0 || y >= WORLD_HEIGHT - 1)
            continue;

        if (!world.isSolid(x, y) && world.isSolid(x, y + 1))
        {
            foundY = y;
            break;
        }
    }

    if (foundY < 0)
        return std::nullopt;

    const bool underground = foundY > generator.surfaceHeight(x);

    // Seat the enemy's bottom edge on top of the floor tile (foundY + 1)
    // rather than using foundY's own top - an Enemy's AABB extends `height`
    // px downward from `position`, so anchoring at foundY's top would leave
    // it embedded in the solid tile below by (height - TILE_SIZE) px.
    const auto seatedPosition = [x, foundY](EnemyType type) {
        const float height = enemyInfo(type).height;
        return sf::Vector2f{static_cast<float>(x) * TILE_SIZE,
                             static_cast<float>(foundY + 1) * TILE_SIZE - height};
    };

    if (underground)
        return EnemySpawn{EnemyType::Nightstalker, seatedPosition(EnemyType::Nightstalker)};

    if (daylightFactor < NIGHT_THRESHOLD)
        return EnemySpawn{EnemyType::Nightstalker, seatedPosition(EnemyType::Nightstalker)};

    // Daytime, above ground: Sunroamer, and only rarely.
    const float roll = noise::hashFloat(x, foundY, salt);
    if (roll < SUNROAMER_SPAWN_CHANCE)
        return EnemySpawn{EnemyType::Sunroamer, seatedPosition(EnemyType::Sunroamer)};

    return std::nullopt;
}
