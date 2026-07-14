#include "Player.h"

#include <algorithm>
#include <cmath>

#include "../World/World.h"

namespace
{

constexpr float MOVE_ACCELERATION = 1800.0f; // px/s^2
constexpr float MAX_RUN_SPEED = 230.0f;      // px/s, ~14 tiles/s

constexpr float GROUND_FRICTION = 2400.0f; // px/s^2 bleeding off when not steering
constexpr float AIR_CONTROL = 0.45f;       // steering authority while airborne

constexpr float GRAVITY = 1800.0f;          // px/s^2
constexpr float TERMINAL_VELOCITY = 1100.0f; // px/s

constexpr float JUMP_SPEED = 470.0f; // px/s upward, clears roughly 3.5 tiles

float applyFriction(float speed, float amount)
{
    if (speed > 0.0f)
        return std::max(0.0f, speed - amount);

    return std::min(0.0f, speed + amount);
}

} // namespace

Player::Player(sf::Vector2f topLeft)
    : body{topLeft, {WIDTH, HEIGHT}}
{
}

void Player::update(const PlayerInput& input, const World& world, float dt)
{
    const float steer = (input.right ? 1.0f : 0.0f) - (input.left ? 1.0f : 0.0f);

    if (steer != 0.0f)
    {
        const float control = grounded ? 1.0f : AIR_CONTROL;

        speed.x += steer * MOVE_ACCELERATION * control * dt;
        speed.x = std::clamp(speed.x, -MAX_RUN_SPEED, MAX_RUN_SPEED);
    }
    else if (grounded)
    {
        // Friction only bites on the ground; in the air you keep your momentum.
        speed.x = applyFriction(speed.x, GROUND_FRICTION * dt);
    }

    // Jump is gated on being grounded, so it cannot be spammed in mid-air.
    if (input.jump && grounded)
        speed.y = -JUMP_SPEED;

    speed.y += GRAVITY * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);

    const physics::CollisionResult result = physics::moveAndCollide(body, speed, world, dt);

    grounded = result.grounded;
}
