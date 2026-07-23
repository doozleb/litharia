#include "Player.h"

#include <algorithm>
#include <cmath>

#include "../Core/Constants.h"
#include "../Items/Items.h"
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

// A water jump reuses the 0.3x water gravity below but with its own, lower
// launch speed, so it caps at roughly 2 tiles above the land jump's ~3.84-tile
// apex instead of the ~12.8 tiles JUMP_SPEED would reach under weak gravity.
constexpr float WATER_JUMP_SPEED = 317.5f; // px/s upward

// Fall damage: a landing under FALL_SAFE_TILES of net drop does no harm. Above
// it, damage scales with the actual vertical distance fallen (not impact
// speed), tuned so a FALL_LETHAL_TILES drop removes all of MAX_HEALTH.
//
// Distance, not speed, is what lets FALL_LETHAL_TILES exceed what
// TERMINAL_VELOCITY could ever produce as an impact speed - a fall far past
// terminal velocity keeps getting more dangerous even though the player's
// speed itself is capped. Trade-off versus the old impact-speed model: a fall
// that passes through fluid on the way down (which only slows descent, not
// the total distance covered) no longer gets any partial cushioning against
// fall damage from that.
constexpr int FALL_SAFE_TILES = 14;
constexpr int FALL_LETHAL_TILES = 63;
const float FALL_DAMAGE_SCALE =
    static_cast<float>(Player::MAX_HEALTH) / static_cast<float>(FALL_LETHAL_TILES - FALL_SAFE_TILES);

// Lava: 20 damage per 0.5 s of contact, so three hits (0.5 / 1.0 / 1.5 s) kill.
constexpr int LAVA_DAMAGE = 20;
constexpr float LAVA_DAMAGE_INTERVAL = 0.5f;

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

bool isTreePart(BlockType type)
{
    return type == BlockType::OakLog || type == BlockType::OakLeaves;
}

// Flood-fills from the tile that was just broken through 4-connected
// OakLog/OakLeaves neighbors, moving sideways or up but never down. That
// asymmetry is what makes cutting a trunk partway up only take the top half:
// the fill can never step back below the tile that started it, so the
// untouched trunk beneath the cut is never reached.
void collectTreeBreak(World& world, int startX, int startY, std::vector<BrokenTile>& out)
{
    std::vector<sf::Vector2i> stack{sf::Vector2i{startX, startY}};

    while (!stack.empty())
    {
        const sf::Vector2i pos = stack.back();
        stack.pop_back();

        const BlockType type = world.getDecoration(pos.x, pos.y);

        if (!isTreePart(type))
            continue;

        // Clearing immediately doubles as the visited marker: a neighbor
        // reached a second time from another direction is already Air, so it
        // fails the isTreePart check above and is skipped rather than
        // reprocessed or double-counted.
        world.setDecoration(pos.x, pos.y, BlockType::Air);
        out.push_back({type, pos.x, pos.y});

        stack.push_back(sf::Vector2i{pos.x - 1, pos.y});
        stack.push_back(sf::Vector2i{pos.x + 1, pos.y});
        stack.push_back(sf::Vector2i{pos.x, pos.y - 1});
    }
}

// Felling 4+ logs in one cascade adds a flat bonus scaled by the axe's
// tier (Wood +0 ... Obsidian +4 - ToolTier's own underlying value is
// exactly this bonus, so no separate table is needed). Below 4 logs (a
// partial chop high up the trunk) there is no bonus at all - this closes
// the exploit of chopping one log at a time to farm the bonus repeatedly.
void applyAxeLogBonus(std::vector<BrokenTile>& broken, ToolTier axeTier)
{
    int logCount = 0;
    int lastLogX = 0;
    int lastLogY = 0;

    for (const BrokenTile& tile : broken)
    {
        if (tile.block != BlockType::OakLog)
            continue;

        ++logCount;
        lastLogX = tile.x;
        lastLogY = tile.y;
    }

    if (logCount < 4)
        return;

    const int bonus = static_cast<int>(axeTier);

    for (int i = 0; i < bonus; ++i)
        broken.push_back({BlockType::OakLog, lastLogX, lastLogY});
}

} // namespace

Player::Player(sf::Vector2f topLeft)
    : body{topLeft, {WIDTH, HEIGHT}}
    , fallStartY(topLeft.y)
{
    // Mining is gated on holding the right tool, so the player starts with
    // both rather than unable to break anything at all.
    bag.exchange(0, {ItemType::WoodPickaxe, 1});
    bag.exchange(1, {ItemType::WoodAxe, 1});

    // Testing convenience: one of each station, so a fresh world can exercise
    // crafting, smelting and storage without first felling trees for 15 logs.
    //
    // Parked at the far end of the hotbar rather than next to the tools.
    // Inventory::add() fills the first free slot, so the low slots are where
    // tests implicitly put things: slot 2 takes the first add() (and mining's
    // "nothing in hand" test wants it empty), slot 3 takes the second. Sitting
    // in either silently displaces what a test meant to hold and fails it
    // somewhere unrelated. Leaving 2-6 clear keeps that headroom.
    bag.exchange(7, {ItemType::CraftingTable, 1});
    bag.exchange(8, {ItemType::Chest, 1});
    bag.exchange(9, {ItemType::Furnace, 1});
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

    damageTakenThisTick = 0;

    move(input, world, dt);
    applyLavaDamage(world, dt);

    mine(input, world, result, dt);
    place(input, world, machines, result);

    result.damageTaken = damageTakenThisTick;

    return result;
}

void Player::move(const PlayerInput& input, const World& world, float dt)
{
    if (input.left != input.right)
        facingDir = input.right ? Direction::Right : Direction::Left;

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

    const bool inFluid = physics::overlapsFluid(body, world);

    // Jump is gated on being grounded, so it cannot be spammed in mid-air. A
    // puddle only one tile deep or less doesn't count as "in water" for this
    // purpose - the floaty, capped water jump only kicks in over genuinely
    // deep water, so wading through ankle-deep water still gets a normal jump.
    if (input.jump && grounded)
        speed.y = physics::restsInDeepFluid(body, world) ? -WATER_JUMP_SPEED : -JUMP_SPEED;

    const float gravity = inFluid ? GRAVITY * 0.3f : GRAVITY;
    speed.y += gravity * dt;
    speed.y = std::min(speed.y, TERMINAL_VELOCITY);

    const bool wasGrounded = grounded;
    const float priorFallStartY = fallStartY;

    const physics::CollisionResult result = physics::moveAndCollide(body, speed, world, dt);

    grounded = result.grounded;

    // Fall damage fires once, on the airborne->grounded transition, scaled by
    // the net vertical distance fallen since fallStartY was last updated (the
    // takeoff height, held fixed for the whole airborne excursion below).
    if (!wasGrounded && grounded)
    {
        const float tilesFallen = (body.position.y - priorFallStartY) / TILE_SIZE;

        if (tilesFallen > FALL_SAFE_TILES)
        {
            const int damage = static_cast<int>(
                std::lround((tilesFallen - FALL_SAFE_TILES) * FALL_DAMAGE_SCALE));
            applyDamage(damage);
        }
    }

    // Keep fallStartY tracking the most recent resting height, ready for the
    // next airborne excursion. Updating this AFTER the check above (not
    // before) is what lets that check see the pre-landing takeoff height.
    if (grounded)
        fallStartY = body.position.y;
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

    // A decoration (tree log/leaves) sits on top of whatever terrain is
    // underneath it and is what the cursor targets first, if present.
    const BlockType decoration = world.getDecoration(tileX, tileY);
    const BlockType block = decoration != BlockType::Air ? decoration : world.get(tileX, tileY);

    const ItemInfo& heldInfo = itemInfo(bag.slot(selected).type);
    const BlockInfo& targetInfo = blockInfo(block);

    const bool wrongKind = block != BlockType::Air && targetInfo.requiredTool != heldInfo.toolType;
    const bool tooLowTier = heldInfo.toolType == ToolType::Pickaxe &&
                             !meetsTier(heldInfo.tier, targetInfo.requiredTier);
    const bool wrongTool = wrongKind || tooLowTier;

    // Not holding the button, nothing solid under the cursor, out of arm's
    // reach, the wrong kind of tool, or a pickaxe below the block's required
    // tier: no progress, and any progress already made is thrown away. A
    // block cannot be chipped away by hand, the wrong tool, or an
    // underpowered one - it simply does not break. Axes never gate on tier:
    // every axe tier can chop any tree, tier only changes speed (and, later,
    // log yield).
    if (!input.mine || block == BlockType::Air || !inReach(tileX, tileY) || wrongTool)
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

    targetHardness = targetInfo.hardness / toolTierSpeedMultiplier(heldInfo.tier);
    progress += dt;

    if (progress < targetHardness)
        return;

    // Broken.
    if (isTreePart(block))
    {
        collectTreeBreak(world, tileX, tileY, result.broken);
        applyAxeLogBonus(result.broken, heldInfo.tier);
    }
    else
    {
        world.set(tileX, tileY, BlockType::Air);
        result.broken.push_back({block, tileX, tileY});
    }

    mining = false;
    progress = 0.0f;

    result.broke = true;
}

void Player::applyDamage(int amount)
{
    const int actual = std::clamp(amount, 0, hp);
    hp -= actual;
    damageTakenThisTick += actual;
}

void Player::applyLavaDamage(const World& world, float dt)
{
    if (physics::overlapsLava(body, world))
    {
        lavaTimer += dt;

        while (lavaTimer >= LAVA_DAMAGE_INTERVAL)
        {
            applyDamage(LAVA_DAMAGE);
            lavaTimer -= LAVA_DAMAGE_INTERVAL;
        }
    }
    else
    {
        lavaTimer = 0.0f;
    }
}

void Player::respawn(sf::Vector2f topLeft)
{
    body.position = topLeft;
    speed = {0.0f, 0.0f};
    grounded = false;
    hp = MAX_HEALTH;
    lavaTimer = 0.0f;
    fallStartY = topLeft.y;
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

    // Only into empty space - which now includes not having a decoration
    // (tree log/leaves) there, since those no longer occupy world.get() - and
    // only within reach.
    if (world.get(tileX, tileY) != BlockType::Air ||
        world.getDecoration(tileX, tileY) != BlockType::Air || !inReach(tileX, tileY))
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
