#include "Player.h"

#include <algorithm>
#include <cmath>

#include "../Core/Constants.h"
#include "../Machines/Machines.h"
#include "../World/World.h"

namespace
{

constexpr float MOVE_ACCELERATION = 1800.0f; // px/s^2
constexpr float MAX_RUN_SPEED = 230.0f;      // px/s, ~14 tiles/s

constexpr float GROUND_FRICTION = 2400.0f; // px/s^2 bleeding off when not steering
constexpr float AIR_CONTROL = 0.45f;       // steering authority while airborne

constexpr float GRAVITY = 1800.0f;           // px/s^2
constexpr float TERMINAL_VELOCITY = 1100.0f; // px/s

constexpr float JUMP_SPEED = 470.0f; // px/s upward, clears roughly 3.5 tiles

float applyFriction(float speed, float amount)
{
    if (speed > 0.0f)
        return std::max(0.0f, speed - amount);

    return std::min(0.0f, speed + amount);
}

int tileOf(float pixels)
{
    return static_cast<int>(std::floor(pixels / TILE_SIZE));
}

AABB tileBox(int tileX, int tileY)
{
    return AABB{{static_cast<float>(tileX * TILE_SIZE), static_cast<float>(tileY * TILE_SIZE)},
                {TILE_SIZE, TILE_SIZE}};
}

} // namespace

Player::Player(sf::Vector2f topLeft)
    : body{topLeft, {WIDTH, HEIGHT}}
{
    // There is no crafting system yet, so the player starts equipped rather
    // than unable to mine anything at all.
    bag.exchange(0, {ItemType::Pickaxe, 1});
    bag.exchange(1, {ItemType::Axe, 1});
}

void Player::setSelectedSlot(int slot)
{
    if (slot >= 0 && slot < Inventory::HOTBAR_SIZE)
        selected = slot;
}

void Player::cycleSelectedSlot(int delta)
{
    constexpr int size = Inventory::HOTBAR_SIZE;

    // Wraps in both directions: scrolling off either end of the hotbar comes back
    // round rather than sticking.
    selected = ((selected + delta) % size + size) % size;
}

ActionResult Player::update(const PlayerInput& input, World& world, float dt,
                             const Machines* machines)
{
    ActionResult result;

    move(input, world, dt);

    mine(input, world, result, dt);
    place(input, world, machines, result);

    return result;
}

void Player::move(const PlayerInput& input, const World& world, float dt)
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

bool Player::inReach(int tileX, int tileY) const
{
    const sf::Vector2f tileCenter{(tileX + 0.5f) * TILE_SIZE, (tileY + 0.5f) * TILE_SIZE};
    const sf::Vector2f offset = tileCenter - center();

    const float reach = REACH_TILES * TILE_SIZE;

    return (offset.x * offset.x + offset.y * offset.y) <= reach * reach;
}

float Player::miningProgress() const
{
    if (!mining || targetHardness <= 0.0f)
        return 0.0f;

    return std::clamp(progress / targetHardness, 0.0f, 1.0f);
}

void Player::mine(const PlayerInput& input, World& world, ActionResult& result, float dt)
{
    const int tileX = tileOf(input.cursor.x);
    const int tileY = tileOf(input.cursor.y);

    const BlockType block = world.get(tileX, tileY);

    // Not holding the button, nothing solid under the cursor, or out of arm's
    // reach: no progress, and any progress already made is thrown away.
    if (!input.mine || block == BlockType::Air || !inReach(tileX, tileY))
    {
        mining = false;
        progress = 0.0f;
        return;
    }

    // Targeting a different tile than last tick also resets: progress is per-block,
    // not a pool the player carries between blocks.
    if (!mining || target.x != tileX || target.y != tileY)
    {
        mining = true;
        target = {tileX, tileY};
        progress = 0.0f;
    }

    targetHardness = blockInfo(block).hardness;
    progress += dt;

    if (progress < targetHardness)
        return;

    // Broken.
    world.set(tileX, tileY, BlockType::Air);

    mining = false;
    progress = 0.0f;

    result.broke = true;
    result.brokenBlock = block;
    result.brokenX = tileX;
    result.brokenY = tileY;
}

void Player::place(const PlayerInput& input, World& world, const Machines* machines,
                    ActionResult& result)
{
    if (!input.place)
        return;

    const ItemStack& held = bag.slot(selected);

    if (held.empty())
        return;

    const BlockType block = itemInfo(held.type).placeBlock;

    if (block == BlockType::Air)
        return;

    const int tileX = tileOf(input.cursor.x);
    const int tileY = tileOf(input.cursor.y);

    // Only into empty space, and only within reach.
    if (world.get(tileX, tileY) != BlockType::Air || !inReach(tileX, tileY))
        return;

    // Never onto a tile a piece of factory equipment already occupies.
    if (machines != nullptr && !machines->canPlace(tileX, tileY))
        return;

    // A block may not be placed inside the player: it would trap them in a solid
    // tile, which the physics has no correct way to push out of.
    if (physics::overlaps(tileBox(tileX, tileY), body))
        return;

    world.set(tileX, tileY, block);

    // Placing is the only thing that removes from the inventory.
    bag.removeOne(selected);

    result.placed = true;
    result.placedX = tileX;
    result.placedY = tileY;
}
