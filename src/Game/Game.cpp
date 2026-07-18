#include "Game.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "../Core/Constants.h"
#include "../Core/Noise.h"
#include "../Machines/MachineType.h"

namespace
{

constexpr unsigned WINDOW_WIDTH = 1280;
constexpr unsigned WINDOW_HEIGHT = 720;

constexpr std::uint32_t WORLD_SEED = 1337;

// Physics runs at exactly this rate no matter what the display does.
constexpr float FIXED_STEP = 1.0f / 60.0f;

// If the game stalls, simulate at most this much time before giving up and
// dropping the rest, rather than spiralling into an ever-growing catch-up.
constexpr float MAX_FRAME_TIME = 0.25f;

// How long mining down a placed Chest/Crafting Table/Furnace takes - flat,
// no tool requirement, unlike world blocks: you built it, you can always
// take it back down.
constexpr float MINING_FURNITURE_SECONDS = 1.0f;

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
        default:                     return MachineType::None;
    }
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

void Game::placeFurnitureAtCursor(const PlayerInput& input)
{
    if (!input.place)
        return;

    const ItemStack& held = player.inventory().slot(player.selectedSlot());
    const MachineType type = furnitureMachineForItem(held.type);

    if (type == MachineType::None)
        return;

    const sf::Vector2i tile = cursorTile();

    if (!player.inReach(tile.x, tile.y))
        return;

    const MachineInfo& info = machineInfo(type);

    for (int dy = 0; dy < info.height; ++dy)
        for (int dx = 0; dx < info.width; ++dx)
        {
            if (world.isSolid(tile.x + dx, tile.y + dy))
                return;

            const AABB tileBox{{static_cast<float>((tile.x + dx) * TILE_SIZE),
                                static_cast<float>((tile.y + dy) * TILE_SIZE)},
                               {static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)}};

            if (physics::overlaps(tileBox, player.box()))
                return;
        }

    if (machines.place(type, tile.x, tile.y, Direction::Right) == nullptr)
        return;

    player.inventory().removeOne(held.type);
}

void Game::mineFurnitureAtCursor(const PlayerInput& input, float dt)
{
    const sf::Vector2i tile = cursorTile();
    const Machine* target = machines.at(tile.x, tile.y);
    const bool validTarget = input.mine && target != nullptr && isFurniture(target->type)
        && player.inReach(tile.x, tile.y);

    if (!validTarget)
    {
        miningFurniture = false;
        miningFurnitureProgress = 0.0f;
        return;
    }

    if (!miningFurniture || miningFurnitureTarget.x != tile.x || miningFurnitureTarget.y != tile.y)
    {
        miningFurniture = true;
        miningFurnitureTarget = tile;
        miningFurnitureProgress = 0.0f;
    }

    miningFurnitureProgress += dt;
    if (miningFurnitureProgress < MINING_FURNITURE_SECONDS)
        return;

    const MachineType type = target->type;
    const Inventory storage = target->storage;
    if (!machines.remove(tile.x, tile.y))
        return;

    refundMachineItem(type);
    spillInventoryToGround(storage);

    miningFurniture = false;
    miningFurnitureProgress = 0.0f;
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
    if (buildType == MachineType::Belt
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
        return;
    }

    const sf::Vector2i tile = cursorTile();
    const Machine* machine = machines.at(tile.x, tile.y);

    openStorageTile.reset();
    openCraftingTableTile.reset();
    openFurnaceTile.reset();

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
                            craftingRecipeIndex, craftProgress);
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
    const int leftover = bag.add({recipe.output, 1});

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
            if (buildMode)
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
                if (buildType == MachineType::Belt)
                    buildFacing = (buildFacing == Direction::Right) ? Direction::Left : Direction::Right;
                else
                    buildFacing = rotateCW(buildFacing);
            }

            if (key->code == Key::F)
                interactAtCursor();

            if (key->code == Key::E)
                toggleInventory();

            if (key->code == Key::F1) setBuildType(MachineType::BurnerGenerator);
            if (key->code == Key::F3) setBuildType(MachineType::Belt);
            if (key->code == Key::F4) setBuildType(MachineType::Chute);
            if (key->code == Key::F5) setBuildType(MachineType::Smelter);
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
                    const auto craftHit =
                        hud.hitTestCraftButton(screenPos, windowSize, openCraftingTableTile.has_value());

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
    const PlayerInput input = readInput();
    const ActionResult result = player.update(input, world, dt, &machines);

    if (result.broke)
    {
        // The player mutated one or more tiles; the renderer has to be told.
        for (const BrokenTile& tile : result.broken)
            chunks.markDirty(tile.x, tile.y);

        spawnDrop(result);
    }

    if (result.placed)
        chunks.markDirty(result.placedX, result.placedY);

    placeFurnitureAtCursor(input);
    mineFurnitureAtCursor(input, dt);

    updateDrops(dt);
    tickMachines(dt);
    updateCrafting(dt);
    updateSmelting(dt);

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
    window.clear(sf::Color(122, 184, 240));

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

    hud.draw(window, player.inventory(), player.selectedSlot());

    if (buildMode)
        hud.drawBuildPalette(window, buildType, player.inventory());

    if (inventoryOpen)
        drawInventoryPanels();

    if (dragging)
        hud.drawDragGhost(window, dragStack, sf::Vector2f(sf::Mouse::getPosition(window)));

    drawMachineTooltip();

    window.display();
}
