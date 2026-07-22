#include "Game.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

#include "../Core/Constants.h"
#include "../Core/Noise.h"
#include "../Machines/MachineType.h"

namespace
{

constexpr unsigned WINDOW_WIDTH = 1280;
constexpr unsigned WINDOW_HEIGHT = 720;

// A fresh world every launch: the seed is drawn from the OS entropy source at
// startup rather than being a fixed constant, so no two boots generate the
// same terrain. Used both to seed the generator and for the drop-scatter hash
// below, so a single value keeps those consistent within one run.
const std::uint32_t WORLD_SEED = std::random_device{}();

// Physics runs at exactly this rate no matter what the display does.
constexpr float FIXED_STEP = 1.0f / 60.0f;

// If the game stalls, simulate at most this much time before giving up and
// dropping the rest, rather than spiralling into an ever-growing catch-up.
constexpr float MAX_FRAME_TIME = 0.25f;

// How long mining down a placed Chest/Crafting Table/Furnace takes - flat,
// no tool requirement, unlike world blocks: you built it, you can always
// take it back down.
constexpr float MINING_FURNITURE_SECONDS = 1.0f;

// A damage popup rises this fast (world px/s) and is gone after this long -
// shared by updateDamagePopups (aging/motion) and drawDamagePopups (the fade
// curve), so the two can never disagree about when a popup has expired.
constexpr float DAMAGE_POPUP_RISE_SPEED = 40.0f;
constexpr float DAMAGE_POPUP_LIFETIME = 1.0f;

// Minimum real time between lava-triggered lighting recomputes (see
// Game::lavaLightingCooldown) - a full-world recompute is too expensive to
// run on every tick lava moves, which is most ticks while a pool settles.
constexpr float LAVA_LIGHTING_COOLDOWN_SECONDS = 0.5f;

sf::Color toColor(BlockColor c)
{
    return sf::Color(c.r, c.g, c.b);
}

// An item's own color, independent of what it places (most non-placeable
// items, like tools or logs, don't place anything at all).
sf::Color itemColor(ItemType type)
{
    return toColor(itemInfo(type).iconColor);
}

// The item-to-machine reverse lookup only furniture placement needs -
// itemForMachine() goes the other way and is used by everything else.
MachineType furnitureMachineForItem(ItemType item)
{
    switch (item)
    {
        case ItemType::Chest:         return MachineType::Chest;
        case ItemType::CraftingTable: return MachineType::CraftingTable;
        case ItemType::Furnace:       return MachineType::Furnace;
        case ItemType::Torch:         return MachineType::Torch;
        default:                     return MachineType::None;
    }
}

constexpr sf::Color NIGHT_SKY(15, 18, 35);
constexpr sf::Color DAY_SKY(122, 184, 240); // the game's original fixed sky color

sf::Color lerpColor(sf::Color a, sf::Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return sf::Color(
        static_cast<std::uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<std::uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<std::uint8_t>(a.b + (b.b - a.b) * t));
}

sf::Color skyColor(float daylightFactor)
{
    return lerpColor(NIGHT_SKY, DAY_SKY, daylightFactor);
}

} // namespace

Game::Game()
    : window(sf::VideoMode({WINDOW_WIDTH, WINDOW_HEIGHT}), "Litharia")
    , generator(WORLD_SEED)
    , chunks(world)
    , camera({static_cast<float>(WINDOW_WIDTH), static_cast<float>(WINDOW_HEIGHT)})
    , player({0.0f, 0.0f})
{
    window.setFramerateLimit(60);

    generator.generate(world);
    chunks.markAllDirty();

    spawnSharpRocks();
    fluids.activateAll(world);
    lighting.recomputeAll(world, machines);

    player = Player(findSpawn());
    camera.snapTo(player.center());
}

sf::Vector2f Game::findSpawn() const
{
    const int spawnTileX = WORLD_WIDTH / 2;
    const int surface = generator.surfaceHeight(spawnTileX);

    // Standing on the grass: bottom of the box flush with the top of the surface tile.
    const float x = spawnTileX * TILE_SIZE + (TILE_SIZE - Player::WIDTH) * 0.5f;
    const float y = surface * TILE_SIZE - Player::HEIGHT;

    return {x, y};
}

sf::Vector2f Game::cursorWorldPosition() const
{
    // The mouse is in pixels; the world is in world units under the camera's view.
    return window.mapPixelToCoords(sf::Mouse::getPosition(window), camera.view());
}

PlayerInput Game::readInput() const
{
    using Key = sf::Keyboard::Key;

    PlayerInput input;

    input.left = sf::Keyboard::isKeyPressed(Key::A) || sf::Keyboard::isKeyPressed(Key::Left);
    input.right = sf::Keyboard::isKeyPressed(Key::D) || sf::Keyboard::isKeyPressed(Key::Right);
    input.jump = sf::Keyboard::isKeyPressed(Key::Space);

    const bool focused = window.hasFocus();

    input.mine = !buildMode && !inventoryOpen && focused
        && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    input.place = !buildMode && !inventoryOpen && focused
        && sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);

    input.cursor = cursorWorldPosition();

    return input;
}

void Game::spawnDrop(const ActionResult& result)
{
    for (const BrokenTile& tile : result.broken)
    {
        const ItemType type = itemForBlock(tile.block);

        if (type == ItemType::None)
            continue;

        // Pop out of the ground with a small hashed kick, so a row of drops does
        // not land in a perfectly straight line.
        const float roll = noise::hashFloat(tile.x, tile.y, WORLD_SEED);

        const sf::Vector2f velocity{(roll - 0.5f) * 90.0f, -140.0f};

        // Centred in the tile it came from.
        const sf::Vector2f position{tile.x * TILE_SIZE + (TILE_SIZE - ItemEntity::SIZE) * 0.5f,
                                    tile.y * TILE_SIZE + (TILE_SIZE - ItemEntity::SIZE) * 0.5f};

        drops.emplace_back(ItemStack{type, 1}, position, velocity);
    }
}

void Game::spawnSharpRocks()
{
    for (const auto& [x, y] : generator.scatterSharpRocks(world))
    {
        const sf::Vector2f position{(x + 0.5f) * TILE_SIZE, (y - 1.0f) * TILE_SIZE};
        drops.emplace_back(ItemStack{ItemType::SharpRock, 1}, position, sf::Vector2f{0.0f, 0.0f});
    }
}

void Game::respawnSharpRocksIfNeeded(float dt)
{
    sharpRockRespawnTimer += dt;

    if (sharpRockRespawnTimer < SHARP_ROCK_RESPAWN_INTERVAL)
        return;

    sharpRockRespawnTimer = 0.0f;

    int groundCount = 0;
    for (const ItemEntity& drop : drops)
        if (drop.stack().type == ItemType::SharpRock)
            ++groundCount;

    if (groundCount >= TerrainGenerator::SHARP_ROCK_COUNT)
        return;

    ++sharpRockSpawnCounter;
    const auto [x, y] =
        generator.randomSurfaceSpot(world, static_cast<std::uint32_t>(sharpRockSpawnCounter));

    const sf::Vector2f position{(x + 0.5f) * TILE_SIZE, (y - 1.0f) * TILE_SIZE};
    drops.emplace_back(ItemStack{ItemType::SharpRock, 1}, position, sf::Vector2f{0.0f, 0.0f});
}

void Game::updateDrops(float dt)
{
    for (ItemEntity& drop : drops)
        drop.update(world, dt, player.center());

    // Absorb anything touching the player. A full bag returns leftovers, and those
    // entities stay on the ground and keep trying rather than being destroyed.
    for (ItemEntity& drop : drops)
    {
        if (!physics::overlaps(drop.box(), player.box()))
            continue;

        const int leftover = player.inventory().add(drop.stack());

        drop.stack().count = leftover;
    }

    // A stack absorbed in full is gone; one that only partly fitted stays on the
    // ground holding its leftovers.
    std::erase_if(drops, [](const ItemEntity& drop) { return drop.stack().empty(); });
}

void Game::spawnDamagePopup(int amount)
{
    std::optional<sf::Text> text = hud.makeDamagePopupText(amount);

    // Degrades like the rest of the HUD: with no font loaded, there is
    // nothing sensible to draw, so no popup is tracked at all.
    if (!text.has_value())
        return;

    const sf::Vector2f spawnPos{player.center().x, player.position().y - 10.0f};

    damagePopups.push_back({std::move(*text), spawnPos});
}

void Game::updateDamagePopups(float dt)
{
    for (DamagePopup& popup : damagePopups)
    {
        popup.age += dt;
        popup.worldPos.y -= DAMAGE_POPUP_RISE_SPEED * dt;
    }

    std::erase_if(damagePopups, [](const DamagePopup& p) { return p.age >= DAMAGE_POPUP_LIFETIME; });
}

void Game::drawDamagePopups()
{
    for (DamagePopup& popup : damagePopups)
    {
        // Fades out over its whole life rather than snapping away, so it
        // reads as dissolving rather than disappearing.
        const float alphaFrac = std::clamp(1.0f - popup.age / DAMAGE_POPUP_LIFETIME, 0.0f, 1.0f);
        const std::uint8_t alpha = static_cast<std::uint8_t>(alphaFrac * 255.0f);

        sf::Color fill = popup.text.getFillColor();
        fill.a = alpha;
        popup.text.setFillColor(fill);

        sf::Color outline = popup.text.getOutlineColor();
        outline.a = alpha;
        popup.text.setOutlineColor(outline);

        popup.text.setPosition(popup.worldPos);
        window.draw(popup.text);
    }
}

sf::Vector2i Game::cursorTile() const
{
    const sf::Vector2f world = cursorWorldPosition();
    return {static_cast<int>(std::floor(world.x / TILE_SIZE)),
            static_cast<int>(std::floor(world.y / TILE_SIZE))};
}

void Game::placeMachineAtCursor()
{
    const sf::Vector2i tile = cursorTile();
    const MachineInfo& info = machineInfo(buildType);

    for (int dy = 0; dy < info.height; ++dy)
        for (int dx = 0; dx < info.width; ++dx)
            if (world.isSolid(tile.x + dx, tile.y + dy))
                return;

    const ItemType item = itemForMachine(buildType);
    if (player.inventory().count(item) <= 0)
        return;

    if (machines.place(buildType, tile.x, tile.y, buildFacing) == nullptr)
        return;

    player.inventory().removeOne(item);
}

bool Game::placeFurnitureAtCursor(const PlayerInput& input)
{
    if (!input.place)
        return false;

    const ItemStack& held = player.inventory().slot(player.selectedSlot());
    const MachineType type = furnitureMachineForItem(held.type);

    if (type == MachineType::None)
        return false;

    const sf::Vector2i tile = cursorTile();

    if (!player.inReach(tile.x, tile.y))
        return false;

    const MachineInfo& info = machineInfo(type);

    for (int dy = 0; dy < info.height; ++dy)
        for (int dx = 0; dx < info.width; ++dx)
        {
            if (world.isSolid(tile.x + dx, tile.y + dy))
                return false;

            const AABB tileBox{{static_cast<float>((tile.x + dx) * TILE_SIZE),
                                static_cast<float>((tile.y + dy) * TILE_SIZE)},
                               {static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)}};

            if (physics::overlaps(tileBox, player.box()))
                return false;
        }

    if (machines.place(type, tile.x, tile.y, Direction::Right) == nullptr)
        return false;

    player.inventory().removeOne(held.type);
    return true;
}

bool Game::mineFurnitureAtCursor(const PlayerInput& input, float dt)
{
    const sf::Vector2i tile = cursorTile();
    const Machine* target = machines.at(tile.x, tile.y);
    const bool validTarget = input.mine && target != nullptr && isFurniture(target->type)
        && player.inReach(tile.x, tile.y);

    if (!validTarget)
    {
        miningFurniture = false;
        miningFurnitureProgress = 0.0f;
        return false;
    }

    if (!miningFurniture || miningFurnitureTarget.x != tile.x || miningFurnitureTarget.y != tile.y)
    {
        miningFurniture = true;
        miningFurnitureTarget = tile;
        miningFurnitureProgress = 0.0f;
    }

    miningFurnitureProgress += dt;
    if (miningFurnitureProgress < MINING_FURNITURE_SECONDS)
        return false;

    const MachineType type = target->type;
    const Inventory storage = target->storage;
    if (!machines.remove(tile.x, tile.y))
        return false;

    refundMachineItem(type);
    spillInventoryToGround(storage);

    miningFurniture = false;
    miningFurnitureProgress = 0.0f;
    return true;
}

void Game::removeMachineAtCursor()
{
    const sf::Vector2i tile = cursorTile();
    const Machine* target = machines.at(tile.x, tile.y);
    if (target == nullptr)
        return;

    const MachineType type = target->type;
    const Inventory storage = target->storage;
    if (!machines.remove(tile.x, tile.y))
        return;

    refundMachineItem(type);
    spillInventoryToGround(storage);
}

void Game::refundMachineItem(MachineType type)
{
    const ItemType item = itemForMachine(type);
    const int leftover = player.inventory().add({item, 1});

    dropAtPlayer(ItemStack{item, leftover});
}

void Game::dropAtPlayer(ItemStack stack)
{
    if (stack.empty())
        return;

    const sf::Vector2f position =
        player.center() - sf::Vector2f{ItemEntity::SIZE * 0.5f, ItemEntity::SIZE * 0.5f};
    drops.emplace_back(stack, position, sf::Vector2f{0.0f, -60.0f});
}

void Game::spillInventoryToGround(const Inventory& inventory)
{
    for (int i = 0; i < inventory.slotCount(); ++i)
        dropAtPlayer(inventory.slot(i));
}

void Game::cycleBuildType(int delta)
{
    constexpr int first = 1; // skip MachineType::None
    const int count = static_cast<int>(MachineType::Count) - first;

    const Inventory& bag = player.inventory();
    int index = static_cast<int>(buildType) - first;

    for (int step = 0; step < count; ++step)
    {
        index = ((index + delta) % count + count) % count;
        const MachineType candidate = static_cast<MachineType>(first + index);

        if (isFurniture(candidate))
            continue;

        if (bag.count(itemForMachine(candidate)) > 0)
        {
            setBuildType(candidate);
            return;
        }
    }
    // Nothing held: leave buildType where it is, and the palette draws empty.
}

void Game::setBuildType(MachineType type)
{
    buildType = type;

    // A conveyor belt only ever outputs sideways - chutes already own straight-
    // down movement - so a facing left over from another machine type must
    // never leave a freshly-selected belt pointed up or down.
    if (isBelt(buildType)
        && (buildFacing == Direction::Up || buildFacing == Direction::Down))
        buildFacing = Direction::Right;
}

void Game::interactAtCursor()
{
    const sf::Vector2i tile = cursorTile();
    Inventory& bag = player.inventory();
    const int slot = player.selectedSlot();
    const ItemStack& held = bag.slot(slot);

    if (!held.empty())
    {
        // Holding something: offer it to whatever's under the cursor.
        if (machines.tryInsert(tile.x, tile.y, held.type))
            bag.removeOne(slot);

        return;
    }

    // Empty-handed: try to take from whatever's under the cursor instead.
    const ItemStack extracted = machines.tryExtract(tile.x, tile.y);
    if (extracted.empty())
        return;

    const int leftover = bag.add(extracted);
    if (leftover > 0)
        machines.putBack(tile.x, tile.y, {extracted.type, leftover});
}

void Game::toggleInventory()
{
    if (dragging)
        return;

    if (inventoryOpen)
    {
        inventoryOpen = false;
        openStorageTile.reset();
        openCraftingTableTile.reset();
        openFurnaceTile.reset();
        craftPanelScroll = 0;
        return;
    }

    const sf::Vector2i tile = cursorTile();
    const Machine* machine = machines.at(tile.x, tile.y);

    openStorageTile.reset();
    openCraftingTableTile.reset();
    openFurnaceTile.reset();
    craftPanelScroll = 0;

    if (machine != nullptr
        && (machine->type == MachineType::Chest || machine->type == MachineType::ItemAcceptor))
        openStorageTile = tile;
    else if (machine != nullptr && machine->type == MachineType::CraftingTable)
        openCraftingTableTile = tile;
    else if (machine != nullptr && machine->type == MachineType::Furnace)
        openFurnaceTile = tile;

    inventoryOpen = true;
    buildMode = false;
}

void Game::drawInventoryPanels()
{
    if (openStorageTile.has_value())
    {
        const Machine* storage = machines.at(openStorageTile->x, openStorageTile->y);

        // The machine might have vanished by other means while the panel was
        // open; fall back to the bag-only view rather than touch a stale tile.
        if (storage == nullptr
            || (storage->type != MachineType::Chest && storage->type != MachineType::ItemAcceptor))
            openStorageTile.reset();
    }

    if (openCraftingTableTile.has_value())
    {
        const Machine* table = machines.at(openCraftingTableTile->x, openCraftingTableTile->y);

        if (table == nullptr || table->type != MachineType::CraftingTable)
            openCraftingTableTile.reset();
    }

    if (openFurnaceTile.has_value())
    {
        const Machine* furnace = machines.at(openFurnaceTile->x, openFurnaceTile->y);

        if (furnace == nullptr || furnace->type != MachineType::Furnace)
            openFurnaceTile.reset();
    }

    hud.drawInventoryPanel(window, player.inventory());

    if (openStorageTile.has_value())
    {
        hud.drawChestPanel(window, machines.at(openStorageTile->x, openStorageTile->y)->storage);
        hud.drawChestButtons(window);
    }
    else if (openFurnaceTile.has_value())
    {
        hud.drawSmeltPanel(window, player.inventory(), smelting, smeltingRecipeIndex, smeltProgress);
    }
    else
    {
        hud.drawCraftPanel(window, player.inventory(), openCraftingTableTile.has_value(), crafting,
                            craftingRecipeIndex, craftProgress, craftPanelScroll);
    }
}

void Game::beginDrag()
{
    const sf::Vector2f screenPos(sf::Mouse::getPosition(window));
    const sf::Vector2f windowSize(window.getSize());

    const int openSlots = openStorageTile.has_value()
        ? machines.at(openStorageTile->x, openStorageTile->y)->storage.slotCount()
        : 0;

    const auto hit = hud.hitTestPanels(screenPos, windowSize, openSlots);
    if (!hit.has_value())
        return;

    Inventory& source = hit->isStorage ? machines.at(openStorageTile->x, openStorageTile->y)->storage
                                        : player.inventory();

    const ItemStack taken = source.take(hit->index);
    if (taken.empty())
        return;

    dragging = true;
    dragStack = taken;
    dragSourcePanel = hit->isStorage ? InventoryPanel::Storage : InventoryPanel::Bag;
    dragSourceSlot = hit->index;
}

void Game::endDrag()
{
    const sf::Vector2f screenPos(sf::Mouse::getPosition(window));
    const sf::Vector2f windowSize(window.getSize());

    Inventory& sourceInventory = (dragSourcePanel == InventoryPanel::Storage)
        ? machines.at(openStorageTile->x, openStorageTile->y)->storage
        : player.inventory();

    const int openSlots = openStorageTile.has_value()
        ? machines.at(openStorageTile->x, openStorageTile->y)->storage.slotCount()
        : 0;

    const auto hit = hud.hitTestPanels(screenPos, windowSize, openSlots);

    if (!hit.has_value())
    {
        // Released outside any slot: put it back where it came from.
        sourceInventory.exchange(dragSourceSlot, dragStack);
        dragging = false;
        return;
    }

    Inventory& destInventory = hit->isStorage ? machines.at(openStorageTile->x, openStorageTile->y)->storage
                                               : player.inventory();

    const ItemStack leftover = destInventory.exchange(hit->index, dragStack);

    if (!leftover.empty())
        sourceInventory.exchange(dragSourceSlot, leftover);

    dragging = false;
}

void Game::depositAllToStorage()
{
    if (!openStorageTile.has_value())
        return;

    Inventory& storage = machines.at(openStorageTile->x, openStorageTile->y)->storage;
    Inventory& bag = player.inventory();

    // Bag-panel slots only (HOTBAR_SIZE..slotCount()-1) - the hotbar is left
    // alone, same as the hotbar being excluded from what Deposit All sweeps.
    for (int i = Inventory::HOTBAR_SIZE; i < bag.slotCount(); ++i)
    {
        const ItemStack taken = bag.take(i);
        if (taken.empty())
            continue;

        // Whatever doesn't fit goes right back into the slot it came from -
        // take() already emptied it, so this can only refill it, never swap
        // with something else.
        const int leftover = storage.add(taken);
        if (leftover > 0)
            bag.exchange(i, {taken.type, leftover});
    }
}

void Game::collectAllFromStorage()
{
    if (!openStorageTile.has_value())
        return;

    Inventory& storage = machines.at(openStorageTile->x, openStorageTile->y)->storage;
    Inventory& bag = player.inventory();

    for (int i = 0; i < storage.slotCount(); ++i)
    {
        const ItemStack taken = storage.take(i);
        if (taken.empty())
            continue;

        // Bag panel only, same as Deposit All - the hotbar is never a
        // Collect All destination.
        const int leftover = bag.add(taken, Inventory::HOTBAR_SIZE);
        if (leftover > 0)
            storage.exchange(i, {taken.type, leftover});
    }
}

void Game::scrollCraftPanel(int delta)
{
    const int count = Hud::craftRecipeCount(openCraftingTableTile.has_value());
    const int maxScroll = std::max(0, count - Hud::CRAFT_PANEL_VISIBLE_ROWS);

    craftPanelScroll = std::clamp(craftPanelScroll + delta, 0, maxScroll);
}

void Game::startCraft(int recipeIndex)
{
    if (crafting)
        return;

    const std::span<const CraftRecipe> recipes = allCraftRecipes();
    if (recipeIndex < 0 || recipeIndex >= static_cast<int>(recipes.size()))
        return;

    const CraftRecipe& recipe = recipes[recipeIndex];
    Inventory& bag = player.inventory();

    for (const CraftIngredient& ing : recipe.ingredients)
        if (ing.item != ItemType::None && bag.count(ing.item) < ing.count)
            return;

    for (const CraftIngredient& ing : recipe.ingredients)
    {
        if (ing.item == ItemType::None)
            continue;

        for (int i = 0; i < ing.count; ++i)
            bag.removeOne(ing.item);
    }

    crafting = true;
    craftingRecipeIndex = recipeIndex;
    craftProgress = 0.0f;
}

void Game::updateCrafting(float dt)
{
    if (!crafting)
        return;

    const std::span<const CraftRecipe> recipes = allCraftRecipes();
    const CraftRecipe& recipe = recipes[craftingRecipeIndex];

    craftProgress += dt;
    if (craftProgress < recipe.seconds)
        return;

    Inventory& bag = player.inventory();
    const int leftover = bag.add({recipe.output, recipe.outputCount});

    dropAtPlayer(ItemStack{recipe.output, leftover});

    crafting = false;
    craftingRecipeIndex = -1;
    craftProgress = 0.0f;
}

void Game::startSmelt(int recipeIndex)
{
    if (smelting)
        return;

    const std::span<const FurnaceRecipe> recipes = allFurnaceRecipes();
    if (recipeIndex < 0 || recipeIndex >= static_cast<int>(recipes.size()))
        return;

    const FurnaceRecipe& recipe = recipes[recipeIndex];
    Inventory& bag = player.inventory();

    if (bag.count(recipe.in) <= 0)
        return;

    bag.removeOne(recipe.in);

    smelting = true;
    smeltingRecipeIndex = recipeIndex;
    smeltProgress = 0.0f;
}

void Game::updateSmelting(float dt)
{
    if (!smelting)
        return;

    const std::span<const FurnaceRecipe> recipes = allFurnaceRecipes();
    const FurnaceRecipe& recipe = recipes[smeltingRecipeIndex];

    smeltProgress += dt;
    if (smeltProgress < recipe.seconds)
        return;

    Inventory& bag = player.inventory();
    const int leftover = bag.add({recipe.out, 1});

    dropAtPlayer(ItemStack{recipe.out, leftover});

    smelting = false;
    smeltingRecipeIndex = -1;
    smeltProgress = 0.0f;
}

void Game::tickMachines(float dt)
{
    std::vector<sf::Vector2i> mined;
    machines.tick(world, dt, mined);

    // Any tile a machine destroyed must be rebuilt in the chunk mesh. Currently
    // always empty - drilling no longer destroys the block it mines.
    for (const sf::Vector2i& t : mined)
        chunks.markDirty(t.x, t.y);
}

void Game::drawMachineTooltip()
{
    const sf::Vector2i tile = cursorTile();
    const Machine* machine = machines.at(tile.x, tile.y);

    if (machine == nullptr)
        return;

    const MachineStatus status = machines.inspect(tile.x, tile.y, world);

    // Anchor to the machine's own tile in screen space, not the raw mouse
    // position, so the tooltip tracks the machine (and the camera) instead of
    // drifting toward wherever the cursor happens to be.
    const sf::Vector2f tileTopCenter{(tile.x + 0.5f) * TILE_SIZE,
                                     static_cast<float>(tile.y * TILE_SIZE)};
    const sf::Vector2f screenPos(window.mapCoordsToPixel(tileTopCenter, camera.view()));

    hud.drawMachineTooltip(window, *machine, status, screenPos);
}

void Game::run()
{
    sf::Clock clock;
    float accumulator = 0.0f;

    while (window.isOpen())
    {
        const float frameTime = std::min(clock.restart().asSeconds(), MAX_FRAME_TIME);

        handleEvents();

        // Fixed timestep: consume the frame's time in whole 1/60 s steps and carry
        // the remainder into the next frame.
        accumulator += frameTime;

        while (accumulator >= FIXED_STEP)
        {
            fixedUpdate(FIXED_STEP);
            accumulator -= FIXED_STEP;
        }

        render();
    }
}

void Game::handleEvents()
{
    using Key = sf::Keyboard::Key;

    while (const auto event = window.pollEvent())
    {
        if (event->is<sf::Event::Closed>())
        {
            window.close();
        }
        else if (const auto* resized = event->getIf<sf::Event::Resized>())
        {
            camera.setViewSize({static_cast<float>(resized->size.x),
                                static_cast<float>(resized->size.y)});
        }
        else if (const auto* scroll = event->getIf<sf::Event::MouseWheelScrolled>())
        {
            const bool craftPanelShown =
                inventoryOpen && !openStorageTile.has_value() && !openFurnaceTile.has_value();

            if (craftPanelShown)
                scrollCraftPanel(scroll->delta > 0.0f ? -1 : 1);
            else if (buildMode)
                cycleBuildType(scroll->delta > 0.0f ? -1 : 1);
            else
                // Scroll up moves toward slot 1, scroll down toward slot 0.
                player.cycleSelectedSlot(scroll->delta > 0.0f ? -1 : 1);
        }
        else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
        {
            if (key->code == Key::Escape)
                window.close();

            // Number keys 1-9 select slots 0-8, and 0 selects slot 9.
            if (key->code >= Key::Num1 && key->code <= Key::Num9)
                player.setSelectedSlot(static_cast<int>(key->code) - static_cast<int>(Key::Num1));

            if (key->code == Key::Num0)
                player.setSelectedSlot(9);

            if (key->code == Key::B && !dragging)
            {
                buildMode = !buildMode;
                if (buildMode)
                {
                    inventoryOpen = false;
                    openStorageTile.reset();
                }
            }

            if (key->code == Key::R)
            {
                // A belt only ever rotates between its two horizontal facings -
                // chutes already own straight-down movement, so a belt never
                // gets to output up or down. Every other machine type still
                // cycles through all 4.
                if (isBelt(buildType))
                    buildFacing = (buildFacing == Direction::Right) ? Direction::Left : Direction::Right;
                else
                    buildFacing = rotateCW(buildFacing);
            }

            if (key->code == Key::F)
                interactAtCursor();

            if (key->code == Key::E)
                toggleInventory();

            if (key->code == Key::F1) setBuildType(MachineType::BurnerGenerator);
            if (key->code == Key::F6) setBuildType(MachineType::ItemAcceptor);
        }
        else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
        {
            if (buildMode && mouse->button == sf::Mouse::Button::Left)
                placeMachineAtCursor();
            else if (buildMode && mouse->button == sf::Mouse::Button::Right)
                removeMachineAtCursor();
            else if (inventoryOpen && mouse->button == sf::Mouse::Button::Left)
            {
                const sf::Vector2f screenPos(sf::Mouse::getPosition(window));
                const sf::Vector2f windowSize(window.getSize());

                if (openStorageTile.has_value())
                {
                    const auto chestButton = hud.hitTestChestButton(screenPos, windowSize);

                    if (chestButton == Hud::ChestButton::DepositAll)
                        depositAllToStorage();
                    else if (chestButton == Hud::ChestButton::CollectAll)
                        collectAllFromStorage();
                    else
                        beginDrag();
                }
                else if (openFurnaceTile.has_value())
                {
                    const auto smeltHit = hud.hitTestSmeltButton(screenPos, windowSize);

                    if (smeltHit.has_value())
                        startSmelt(*smeltHit);
                    else
                        beginDrag();
                }
                else
                {
                    const auto craftHit = hud.hitTestCraftButton(
                        screenPos, windowSize, openCraftingTableTile.has_value(), craftPanelScroll);

                    if (craftHit.has_value())
                        startCraft(*craftHit);
                    else
                        beginDrag();
                }
            }
        }
        else if (const auto* release = event->getIf<sf::Event::MouseButtonReleased>())
        {
            if (dragging && release->button == sf::Mouse::Button::Left)
                endDrag();
        }
    }
}

void Game::fixedUpdate(float dt)
{
    dayNightClock.tick(dt);

    const PlayerInput input = readInput();
    const ActionResult result = player.update(input, world, dt, &machines);

    // Spawn at the position the hit landed, before a fatal hit's respawn (just
    // below) moves the player away from it.
    if (result.damageTaken > 0)
        spawnDamagePopup(result.damageTaken);

    if (player.isDead())
    {
        player.respawn(findSpawn());
        camera.snapTo(player.center());
    }

    bool lightingDirty = result.broke || result.placed;

    if (result.broke)
    {
        // The player mutated one or more tiles; the renderer has to be told.
        // A newly-opened tile might let a neighboring fluid tile fall or
        // spread into it, so reactivate around it too.
        for (const BrokenTile& tile : result.broken)
        {
            chunks.markDirty(tile.x, tile.y);
            fluids.activateAround(tile.x, tile.y);
        }

        spawnDrop(result);
    }

    if (result.placed)
    {
        chunks.markDirty(result.placedX, result.placedY);
        fluids.activateAround(result.placedX, result.placedY);
    }

    if (placeFurnitureAtCursor(input))
        lightingDirty = true;
    if (mineFurnitureAtCursor(input, dt))
        lightingDirty = true;

    if (lightingDirty)
        lighting.recomputeAll(world, machines);

    updateDrops(dt);
    updateDamagePopups(dt);
    respawnSharpRocksIfNeeded(dt);
    tickMachines(dt);
    updateCrafting(dt);
    updateSmelting(dt);

    std::vector<sf::Vector2i> fluidChanges;
    fluids.tick(world, dt, fluidChanges);

    // Lava is a mobile light source (see Lighting::recomputeAll's Lava
    // seeding) - a recompute triggered only by block/Torch edits would leave
    // flowing lava's glow anchored to wherever it started. FluidSim's
    // fall/spread/cascade rules are conservative (a lava tile vanishing from
    // one spot always shows up as either Obsidian there or still-Lava on
    // another tile in this same batch), so checking the whole batch for any
    // currently-Lava-or-Obsidian tile is enough to catch every lava-light
    // change without tracking pre-tick state.
    bool lavaLightingChanged = false;
    for (const sf::Vector2i& t : fluidChanges)
    {
        chunks.markDirty(t.x, t.y);

        const BlockType changedType = world.get(t.x, t.y);
        if (isLava(changedType) || changedType == BlockType::Obsidian)
            lavaLightingChanged = true;
    }

    lavaLightingCooldown = std::max(0.0f, lavaLightingCooldown - dt);

    // Rate-limited: recomputeAll is a full-world flood fill, too expensive
    // to re-run on every tick lava moves (which is most ticks while any of
    // the world's many generated lava pools are still settling).
    if (lavaLightingChanged && lavaLightingCooldown <= 0.0f)
    {
        lighting.recomputeLava(world);
        lavaLightingCooldown = LAVA_LIGHTING_COOLDOWN_SECONDS;
    }

    camera.follow(player.center(), dt);

    const ItemStack& held = player.inventory().slot(player.selectedSlot());

    const std::string mode = buildMode
        ? "  -  BUILD: " + std::string(machineInfo(buildType).name)
        : "";

    window.setTitle("Litharia" + mode + "  -  machines: " + std::to_string(machines.count()) +
                    "  -  drops: " + std::to_string(drops.size()) + "  -  holding: " +
                    std::string(held.empty() ? "nothing"
                                             : std::string(itemInfo(held.type).name) + " x" +
                                                   std::to_string(held.count)));
}

void Game::drawMiningHighlight()
{
    if (!player.isMining())
        return;

    const sf::Vector2i target = player.miningTarget();

    const sf::Vector2f corner{static_cast<float>(target.x * TILE_SIZE),
                              static_cast<float>(target.y * TILE_SIZE)};

    // The tile being worked on.
    sf::RectangleShape outline({TILE_SIZE, TILE_SIZE});
    outline.setPosition(corner);
    outline.setFillColor(sf::Color::Transparent);
    outline.setOutlineThickness(-2.0f);
    outline.setOutlineColor(sf::Color(255, 255, 255, 200));

    window.draw(outline);

    // How far through breaking it we are: the tile whitens as it cracks.
    const float progress = player.miningProgress();

    sf::RectangleShape crack({TILE_SIZE, TILE_SIZE});
    crack.setPosition(corner);
    crack.setFillColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(progress * 140.0f)));

    window.draw(crack);
}

void Game::render()
{
    window.clear(skyColor(dayNightClock.daylightFactor()));

    window.setView(camera.view());

    chunks.draw(window, camera.view());

    machineRenderer.draw(window, machines, buildMode);

    drawMiningHighlight();

    // Dropped stacks.
    sf::RectangleShape item({ItemEntity::SIZE, ItemEntity::SIZE});
    item.setOutlineThickness(-1.0f);
    item.setOutlineColor(sf::Color(30, 25, 20));

    for (const ItemEntity& drop : drops)
    {
        item.setPosition(drop.position());
        item.setFillColor(itemColor(drop.stack().type));

        window.draw(item);
    }

    // The player, until there is a sprite for one.
    sf::RectangleShape body({Player::WIDTH, Player::HEIGHT});
    body.setPosition(player.position());
    body.setFillColor(sf::Color(232, 90, 80));
    body.setOutlineThickness(-2.0f);
    body.setOutlineColor(sf::Color(40, 20, 20));

    window.draw(body);

    drawDamagePopups();

    const sf::Vector2i playerTile{static_cast<int>(std::floor(player.center().x / TILE_SIZE)),
                                   static_cast<int>(std::floor(player.center().y / TILE_SIZE))};

    std::vector<std::pair<sf::Vector2i, int>> heldLight;
    const ItemStack& held = player.inventory().slot(player.selectedSlot());
    if (held.type == ItemType::Torch)
        heldLight = lighting.heldTorchLight(world, playerTile);

    // Equipping a Torch widens ambient vision to cover the whole screen -
    // still gated by the same "only through open tiles you've actually dug
    // into" connectivity rule ambientOutline already enforces, just sized to
    // the camera's current view instead of the default 20-tile bubble. This
    // is a Manhattan-distance step cutoff (half-width-in-tiles plus
    // half-height-in-tiles, not the shorter Euclidean diagonal - the
    // diagonal would undercover the screen's corners), so it stays cheap and
    // correct at any window size or zoom: a huge open cavern is still never
    // explored past what's actually on screen.
    int ambientRadius = Lighting::AMBIENT_OUTLINE_RADIUS;
    if (held.type == ItemType::Torch)
    {
        const sf::Vector2f viewSize = camera.view().getSize();
        const int halfWidthTiles = static_cast<int>(std::ceil((viewSize.x * 0.5f) / TILE_SIZE));
        const int halfHeightTiles = static_cast<int>(std::ceil((viewSize.y * 0.5f) / TILE_SIZE));
        ambientRadius = halfWidthTiles + halfHeightTiles + 2;
    }

    const std::vector<std::pair<sf::Vector2i, int>> ambientOutline =
        lighting.ambientOutline(world, playerTile, ambientRadius);

    lightRenderer.draw(window, camera.view(), world, lighting, dayNightClock.daylightFactor(), heldLight,
                        ambientOutline);

    hud.draw(window, player.inventory(), player.selectedSlot());
    hud.drawHealth(window, player.health(), Player::MAX_HEALTH);
    hud.drawDayNightIndicator(window, dayNightClock.daylightFactor());

    const sf::Vector2f mouseScreenPos(sf::Mouse::getPosition(window));
    if (hud.isHealthBarHovered(mouseScreenPos))
        hud.drawHealthTooltip(window, player.health(), Player::MAX_HEALTH);

    if (buildMode)
        hud.drawBuildPalette(window, buildType, player.inventory());

    if (inventoryOpen)
        drawInventoryPanels();

    if (dragging)
        hud.drawDragGhost(window, dragStack, sf::Vector2f(sf::Mouse::getPosition(window)));

    drawMachineTooltip();

    window.display();
}
