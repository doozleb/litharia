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

    ItemEntity(ItemStack stack, sf::Vector2f position, sf::Vector2f velocity);

    void update(const World& world, float dt);

    const ItemStack& stack() const { return contents; }
    ItemStack& stack() { return contents; }

    const AABB& box() const { return body; }
    sf::Vector2f position() const { return body.position; }
    sf::Vector2f center() const { return body.center(); }

    bool isGrounded() const { return grounded; }

private:
    ItemStack contents;

    AABB body;
    sf::Vector2f speed;

    bool grounded = false;
};
