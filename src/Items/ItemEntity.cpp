#include "ItemEntity.h"

#include <algorithm>

#include "../World/World.h"

namespace
{

constexpr float GRAVITY = 1400.0f;
constexpr float TERMINAL_VELOCITY = 900.0f;

// Drops settle quickly instead of sliding around the floor forever.
constexpr float GROUND_FRICTION = 900.0f;
constexpr float AIR_DRAG = 60.0f;

float applyFriction(float speed, float amount)
{
    if (speed > 0.0f)
        return std::max(0.0f, speed - amount);

    return std::min(0.0f, speed + amount);
}

} // namespace

ItemEntity::ItemEntity(ItemStack stack, sf::Vector2f position, sf::Vector2f velocity)
    : contents(stack)
    , body{position, {SIZE, SIZE}}
    , speed(velocity)
{
}

void ItemEntity::update(const World& world, float dt)
{
    speed.y += GRAVITY * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);

    speed.x = applyFriction(speed.x, (grounded ? GROUND_FRICTION : AIR_DRAG) * dt);

    const physics::CollisionResult result = physics::moveAndCollide(body, speed, world, dt);

    grounded = result.grounded;
}
