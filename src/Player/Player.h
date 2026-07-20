#pragma once

#include <SFML/System/Vector2.hpp>

#include <vector>

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

// One tile a mining action turned to air, and what it was before that.
struct BrokenTile
{
    BlockType block;
    int x;
    int y;
};

// What the player's actions did to the world this tick. The player mutates the
// world, but spawning drops and rebuilding chunks is Game's business, so what
// happened is handed back rather than acted on here.
//
// broken is usually one tile, but felling a tree reports every log/leaf tile
// the cascade took down in the same action.
struct ActionResult
{
    bool broke = false;
    std::vector<BrokenTile> broken;

    bool placed = false;
    int placedX = 0;
    int placedY = 0;

    // Actual health lost this tick (fall and/or lava), already clamped to
    // what the player had left. 0 most ticks. Game reads this to spawn a
    // floating damage popup.
    int damageTaken = 0;
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

    // Full health. The player dies at 0 and is respawned by Game.
    static constexpr int MAX_HEALTH = 50;

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

    int health() const { return hp; }
    bool isDead() const { return hp <= 0; }

    // Restores full health at `topLeft`, velocity cleared. Called by Game on death.
    void respawn(sf::Vector2f topLeft);

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
    void applyDamage(int amount);
    void applyLavaDamage(const World& world, float dt);

    AABB body;
    sf::Vector2f speed{0.0f, 0.0f};

    bool grounded = false;
    int hp = MAX_HEALTH;
    float lavaTimer = 0.0f;

    // Actual health lost so far this tick, accumulated across every
    // applyDamage call (fall, lava). Reset at the top of update() and copied
    // into ActionResult::damageTaken before it returns.
    int damageTakenThisTick = 0;

    // The Y position where the player was last resting on solid ground. Kept
    // up to date every grounded tick and left untouched while airborne, so it
    // holds the takeoff height for the whole of a jump or a fall off a ledge -
    // fall damage is the distance from here to where the player lands.
    float fallStartY;

    bool mining = false;
    sf::Vector2i target{0, 0};
    float progress = 0.0f;
    float targetHardness = 0.0f;

    Inventory bag;
    int selected = 0;
};
