#pragma once

#include <cstdint>
#include <optional>

#include <SFML/System/Vector2.hpp>

#include "Enemy.h"

class World;
class TerrainGenerator;

// The camera's current visible rectangle, in world pixels - same convention
// as sf::View (top < bottom, left < right), but plain floats so this stays
// SFML-System-only and testable without a window.
struct ViewBounds
{
    float left;
    float top;
    float right;
    float bottom;
};

struct EnemySpawn
{
    EnemyType type;
    sf::Vector2f position;
};

inline constexpr int MAX_ENEMIES = 16;
inline constexpr int SPAWN_MARGIN_TILES = 3;
inline constexpr int SPAWN_VERTICAL_SEARCH_TILES = 40;
inline constexpr float SUNROAMER_SPAWN_CHANCE = 0.08f;
inline constexpr float SPAWN_ATTEMPT_INTERVAL = 3.0f;      // seconds, Game's own spawn timer
inline constexpr float CAVE_SPAWN_ATTEMPT_INTERVAL = 1.0f; // seconds, used while the player is underground
inline constexpr float NIGHT_THRESHOLD = 0.5f;             // daylightFactor() below this counts as night

// Deterministic given `salt` (vary per call, e.g. an incrementing counter -
// the same convention TerrainGenerator::randomSurfaceSpot uses). Picks a
// point just outside `view`, searches for a foothold, and rolls the
// night/cave/day spawn rules from the design doc. Returns nullopt if no
// foothold was found or nothing rolled eligible this attempt.
std::optional<EnemySpawn> attemptSpawn(const World& world,
                                        const TerrainGenerator& generator,
                                        ViewBounds view,
                                        float daylightFactor,
                                        int currentEnemyCount,
                                        std::uint32_t salt);
