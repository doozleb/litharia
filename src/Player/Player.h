#pragma once

#include <SFML/System/Vector2.hpp>

#include "../Blocks/Blocks.h"
#include "../Items/Inventory.h"
#include "../Physics/Physics.h"

class World;
class Machines;

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

    // Place the selected hotbar block at the cursor.
    bool place = false;

    // Cursor position in world pixels.
    sf::Vector2f cursor{0.0f, 0.0f};
};

// What the player's actions did to the world this tick. The player mutates the
// world, but spawning drops and rebuilding chunks is Game's business, so what
// happened is handed back rather than acted on here.
struct ActionResult
{
    bool broke = false;
    BlockType brokenBlock = BlockType::Air;
    int brokenX = 0;
    int brokenY = 0;

    bool placed = false;
    int placedX = 0;
    int placedY = 0;
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

    // How far the player can reach to mine or place, in tiles.
    static constexpr float REACH_TILES = 5.0f;

    explicit Player(sf::Vector2f topLeft);

    ActionResult update(const PlayerInput& input, World& world, float dt,
                         const Machines* machines = nullptr);

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

    // True if that tile is close enough to mine or place in.
    bool inReach(int tileX, int tileY) const;

    Inventory& inventory() { return bag; }
    const Inventory& inventory() const { return bag; }

    int selectedSlot() const { return selected; }
    void setSelectedSlot(int slot);

    // Steps the hotbar selection by +1 / -1, wrapping round.
    void cycleSelectedSlot(int delta);

private:
    void move(const PlayerInput& input, const World& world, float dt);
    void mine(const PlayerInput& input, World& world, ActionResult& result, float dt);
    void place(const PlayerInput& input, World& world, const Machines* machines,
               ActionResult& result);

    AABB body;
    sf::Vector2f speed{0.0f, 0.0f};

    bool grounded = false;

    bool mining = false;
    sf::Vector2i target{0, 0};
    float progress = 0.0f;
    float targetHardness = 0.0f;

    Inventory bag;
    int selected = 0;
};
