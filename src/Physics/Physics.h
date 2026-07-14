#pragma once

#include <SFML/System/Vector2.hpp>

class World;

// An axis-aligned box. Position is the top-left corner, in pixels.
struct AABB
{
    sf::Vector2f position;
    sf::Vector2f size;

    float left() const { return position.x; }
    float right() const { return position.x + size.x; }
    float top() const { return position.y; }
    float bottom() const { return position.y + size.y; }

    sf::Vector2f center() const { return {position.x + size.x * 0.5f, position.y + size.y * 0.5f}; }
};

namespace physics
{

struct CollisionResult
{
    bool hitX = false;
    bool hitY = false;

    // True only when the box was stopped by something below it.
    bool grounded = false;
};

// Moves the box by velocity * dt against the world's solid tiles, resolving one
// axis at a time, and zeroes the velocity component on any axis that was blocked.
//
// Free function over (box, velocity, world) on purpose: the player and dropped
// items need exactly the same behavior. One implementation, two callers.
CollisionResult moveAndCollide(AABB& box, sf::Vector2f& velocity, const World& world, float dt);

// True if any solid tile overlaps the box.
bool overlapsSolid(const AABB& box, const World& world);

} // namespace physics
