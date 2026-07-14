#pragma once

#include <SFML/System/Vector2.hpp>

#include "../Physics/Physics.h"
#include "Items.h"

class World;

// A dropped stack lying in the world: a stack, a box, and a velocity. It falls
// through Physics::moveAndCollide - the same function the player uses, not a copy
// of it.
class ItemEntity
{
public:
    static constexpr float SIZE = 8.0f;

    // Inside this distance the drop is pulled toward the player.
    static constexpr float MAGNET_RADIUS = 80.0f; // 5 tiles

    ItemEntity(ItemStack stack, sf::Vector2f position, sf::Vector2f velocity);

    // Falls, with no one to be attracted to.
    void update(const World& world, float dt);

    // Falls, and homes in on the player once inside the magnet radius.
    void update(const World& world, float dt, sf::Vector2f playerCenter);

    bool isMagnetized() const { return magnetized; }
    bool withinMagnetRange(sf::Vector2f playerCenter) const;

    const ItemStack& stack() const { return contents; }
    ItemStack& stack() { return contents; }

    const AABB& box() const { return body; }
    sf::Vector2f position() const { return body.position; }
    sf::Vector2f center() const { return body.center(); }
    sf::Vector2f velocity() const { return speed; }

    bool isGrounded() const { return grounded; }

private:
    void step(const World& world, float dt);

    ItemStack contents;

    AABB body;
    sf::Vector2f speed;

    bool grounded = false;
    bool magnetized = false;
};
