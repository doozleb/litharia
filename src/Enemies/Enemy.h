#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <SFML/System/Vector2.hpp>

#include "../Physics/Physics.h"

class World;

enum class EnemyType : std::uint8_t
{
    Nightstalker,
    Sunroamer,
};

struct EnemyInfo
{
    std::string_view name;
    float width;
    float height;
    int maxHealth;
    float moveSpeed;
    float jumpSpeed;
    int contactDamage;
    float contactInterval;
};

const EnemyInfo& enemyInfo(EnemyType type);

// A mob that always chases the single player: no pathfinding, just a
// direct vector toward them plus an auto-jump when grounded and either
// blocked sideways or the player is above. Falls through
// physics::moveAndCollide, the same function Player and ItemEntity use.
class Enemy
{
public:
    Enemy(EnemyType type, sf::Vector2f topLeft);

    void update(const World& world, sf::Vector2f playerCenter, float dt);

    EnemyType type() const { return kind; }
    const AABB& box() const { return body; }
    sf::Vector2f position() const { return body.position; }
    sf::Vector2f center() const { return body.center(); }
    sf::Vector2f velocity() const { return speed; }
    bool isGrounded() const { return grounded; }

    int health() const { return hp; }
    bool isDead() const { return hp <= 0; }
    void applyDamage(int amount);

    // Overrides horizontal velocity and suspends chase AI for
    // KNOCKBACK_LOCK_SECONDS (see Enemy.cpp), so the impulse isn't
    // instantly overwritten by the next tick's chase logic.
    void applyKnockback(float vx);

    // Accumulates dt while touchingPlayer is true, resets to 0 the instant
    // it's false. Returns the total contact damage to apply this call (0,
    // one interval, or more if dt is unusually large) - Game feeds the
    // result into Player::takeDamage.
    int tickContactDamage(bool touchingPlayer, float dt);

private:
    EnemyType kind;
    AABB body;
    sf::Vector2f speed{0.0f, 0.0f};
    bool grounded = false;
    bool blockedHorizontally = false;
    int hp;
    float contactTimer = 0.0f;
    float knockbackTimer = 0.0f;
};
