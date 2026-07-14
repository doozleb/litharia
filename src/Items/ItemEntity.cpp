#include "ItemEntity.h"

#include <algorithm>
#include <cmath>

#include "../World/World.h"

namespace
{

constexpr float GRAVITY = 1400.0f;
constexpr float TERMINAL_VELOCITY = 900.0f;

// Drops settle quickly instead of sliding around the floor forever.
constexpr float GROUND_FRICTION = 900.0f;
constexpr float AIR_DRAG = 60.0f;

// How hard a magnetized drop is pulled toward the player.
constexpr float MAGNET_ACCELERATION = 1500.0f;
constexpr float MAGNET_MAX_SPEED = 500.0f;

float applyFriction(float speed, float amount)
{
    if (speed > 0.0f)
        return std::max(0.0f, speed - amount);

    return std::min(0.0f, speed + amount);
}

float length(sf::Vector2f v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

} // namespace

ItemEntity::ItemEntity(ItemStack stack, sf::Vector2f position, sf::Vector2f velocity)
    : contents(stack)
    , body{position, {SIZE, SIZE}}
    , speed(velocity)
{
}

bool ItemEntity::withinMagnetRange(sf::Vector2f playerCenter) const
{
    const sf::Vector2f offset = playerCenter - center();

    return (offset.x * offset.x + offset.y * offset.y) <= MAGNET_RADIUS * MAGNET_RADIUS;
}

void ItemEntity::update(const World& world, float dt)
{
    magnetized = false;

    speed.y += GRAVITY * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);

    speed.x = applyFriction(speed.x, (grounded ? GROUND_FRICTION : AIR_DRAG) * dt);

    step(world, dt);
}

void ItemEntity::update(const World& world, float dt, sf::Vector2f playerCenter)
{
    magnetized = withinMagnetRange(playerCenter);

    if (!magnetized)
    {
        update(world, dt);
        return;
    }

    // Inside the radius the drop flies to the player: it accelerates toward them and
    // gravity stops mattering, so it can climb out of the hole it fell into.
    const sf::Vector2f offset = playerCenter - center();
    const float distance = length(offset);

    if (distance > 0.0f)
    {
        const sf::Vector2f direction = offset / distance;

        speed += direction * MAGNET_ACCELERATION * dt;

        const float current = length(speed);

        if (current > MAGNET_MAX_SPEED)
            speed *= MAGNET_MAX_SPEED / current;
    }

    step(world, dt);
}

void ItemEntity::step(const World& world, float dt)
{
    const physics::CollisionResult result = physics::moveAndCollide(body, speed, world, dt);

    grounded = result.grounded;
}
