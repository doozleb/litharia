#pragma once

#include <SFML/System/Vector2.hpp>

#include "../Physics/Physics.h"

class World;

// What the player is being told to do this tick. Reading the keyboard is Game's
// job; the player takes the result as data, which keeps it out of SFML Window and
// inside the test binary.
struct PlayerInput
{
    bool left = false;
    bool right = false;
    bool jump = false;
};

class Player
{
public:
    // 30 x 46 px: reads as the intended 2-wide, 3-tall body, but deliberately a
    // hair under 2 full tiles. A box exactly 2 * TILE_SIZE wide cannot reliably
    // pass through a 2-tile gap once floating-point rounding enters the picture,
    // and would wedge in its own corridors.
    static constexpr float WIDTH = 30.0f;
    static constexpr float HEIGHT = 46.0f;

    explicit Player(sf::Vector2f topLeft);

    void update(const PlayerInput& input, const World& world, float dt);

    const AABB& box() const { return body; }
    sf::Vector2f position() const { return body.position; }
    sf::Vector2f center() const { return body.center(); }
    sf::Vector2f velocity() const { return speed; }

    bool isGrounded() const { return grounded; }

private:
    AABB body;
    sf::Vector2f speed{0.0f, 0.0f};

    bool grounded = false;
};
