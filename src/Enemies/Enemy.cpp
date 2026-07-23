#include "Enemy.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include "../Core/Constants.h"
#include "../World/World.h"

namespace
{

// File-local, matching Player.cpp's own GRAVITY/TERMINAL_VELOCITY constants
// (no shared physics-constants header exists in this codebase yet).
constexpr float GRAVITY = 1800.0f;           // px/s^2
constexpr float TERMINAL_VELOCITY = 1100.0f; // px/s

constexpr std::array<EnemyInfo, 2> registry = {{
    {"Nightstalker", 28.0f, 42.0f, 40, 190.0f, 470.0f, 8, 0.6f},
    {"Sunroamer",     24.0f, 34.0f, 20, 140.0f, 420.0f, 4, 0.6f},
}};

} // namespace

const EnemyInfo& enemyInfo(EnemyType type)
{
    return registry[static_cast<std::size_t>(type)];
}

Enemy::Enemy(EnemyType type, sf::Vector2f topLeft)
    : kind(type)
    , body{topLeft, {enemyInfo(type).width, enemyInfo(type).height}}
    , hp(enemyInfo(type).maxHealth)
{
}

void Enemy::update(const World& world, sf::Vector2f playerCenter, float dt)
{
    const EnemyInfo& info = enemyInfo(kind);

    const float dx = playerCenter.x - center().x;
    if (dx > 1.0f)
        speed.x = info.moveSpeed;
    else if (dx < -1.0f)
        speed.x = -info.moveSpeed;
    else
        speed.x = 0.0f;

    // "Automatically jump if needed": jump whenever grounded and either the
    // last move was blocked sideways, or the player sits more than a tile
    // above. No pathfinding beyond this - if a solid ceiling separates the
    // two, this condition keeps firing every time the enemy lands from its
    // last hop, so it just keeps jumping into the ceiling's underside
    // rather than ever routing around it.
    const bool playerAbove = (playerCenter.y - center().y) < -static_cast<float>(TILE_SIZE);

    if (grounded && (blockedHorizontally || playerAbove))
        speed.y = -info.jumpSpeed;

    speed.y += GRAVITY * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);

    const physics::CollisionResult result = physics::moveAndCollide(body, speed, world, dt);

    grounded = result.grounded;
    blockedHorizontally = result.hitX;
}

void Enemy::applyDamage(int amount)
{
    hp = std::max(0, hp - amount);
}

int Enemy::tickContactDamage(bool touchingPlayer, float dt)
{
    if (!touchingPlayer)
    {
        contactTimer = 0.0f;
        return 0;
    }

    contactTimer += dt;

    const EnemyInfo& info = enemyInfo(kind);
    int totalDamage = 0;

    while (contactTimer >= info.contactInterval)
    {
        totalDamage += info.contactDamage;
        contactTimer -= info.contactInterval;
    }

    return totalDamage;
}
