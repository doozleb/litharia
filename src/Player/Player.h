#pragma once

#include <SFML/System/Vector2.hpp>

#include "../Blocks/Blocks.h"
#include "../Physics/Physics.h"

class World;

// What the player is being told to do this tick. Reading the keyboard and mouse is
// Game's job; the player takes the result as data, which keeps it out of SFML
// Window and inside the test binary.
struct PlayerInput
{
    bool left = false;
    bool right = false;
    bool jump = false;

    // Hold to break the block under the cursor.
    bool mine = false;

    // Cursor position in world pixels.
    sf::Vector2f cursor{0.0f, 0.0f};
};

// What mining did this tick. The player breaks the block, but spawning the drop is
// Game's business, so the broken block is handed back rather than acted on here.
struct MineResult
{
    bool broke = false;

    BlockType block = BlockType::Air;

    int tileX = 0;
    int tileY = 0;
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

    // How far the player can reach to mine, in tiles.
    static constexpr float REACH_TILES = 5.0f;

    explicit Player(sf::Vector2f topLeft);

    MineResult update(const PlayerInput& input, World& world, float dt);

    const AABB& box() const { return body; }
    sf::Vector2f position() const { return body.position; }
    sf::Vector2f center() const { return body.center(); }
    sf::Vector2f velocity() const { return speed; }

    bool isGrounded() const { return grounded; }

    // True while a block is actively being broken.
    bool isMining() const { return mining; }
    sf::Vector2i miningTarget() const { return target; }

    // 0 to 1, how far through breaking the target block we are.
    float miningProgress() const;

    // True if that tile is close enough to mine.
    bool inReach(int tileX, int tileY) const;

private:
    void move(const PlayerInput& input, const World& world, float dt);
    MineResult mine(const PlayerInput& input, World& world, float dt);

    AABB body;
    sf::Vector2f speed{0.0f, 0.0f};

    bool grounded = false;

    bool mining = false;
    sf::Vector2i target{0, 0};
    float progress = 0.0f;
    float targetHardness = 0.0f;
};
