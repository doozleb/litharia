#pragma once

#include <SFML/Graphics.hpp>

#include <optional>
#include <vector>

#include "../Camera/Camera.h"
#include "../Hud/Hud.h"
#include "../Items/Inventory.h"
#include "../Items/ItemEntity.h"
#include "../Machines/Machines.h"
#include "../Machines/MachineRenderer.h"
#include "../Player/Player.h"
#include "../World/Chunks.h"
#include "../World/TerrainGenerator.h"
#include "../World/World.h"

class Game
{
public:
    Game();

    void run();

private:
    void handleEvents();

    // Simulation. Runs on a fixed timestep so a frame hitch cannot launch the
    // player through the floor.
    void fixedUpdate(float dt);

    void updateDrops(float dt);

    void render();
    void drawMiningHighlight();

    PlayerInput readInput() const;
    sf::Vector2f cursorWorldPosition() const;

    sf::Vector2f findSpawn() const;

    // A mined block becomes a stack on the ground.
    void spawnDrop(const ActionResult& result);

    void tickMachines(float dt);
    void placeMachineAtCursor();
    void removeMachineAtCursor();
    void cycleBuildType(int delta);
    void interactAtCursor();
    void toggleInventory();
    void drawInventoryPanels();
    void beginDrag();
    void endDrag();
    sf::Vector2i cursorTile() const;
    void drawMachineTooltip();

    sf::RenderWindow window;

    World world;
    TerrainGenerator generator;
    ChunkRenderer chunks;
    Camera camera;
    Player player;
    Hud hud;

    std::vector<ItemEntity> drops;

    Machines machines;
    MachineRenderer machineRenderer;

    bool buildMode = false;
    MachineType buildType = MachineType::Belt;
    Direction buildFacing = Direction::Right;

    bool inventoryOpen = false;
    std::optional<sf::Vector2i> openChestTile;

    enum class InventoryPanel { Bag, Chest };

    bool dragging = false;
    ItemStack dragStack;
    InventoryPanel dragSourcePanel = InventoryPanel::Bag;
    int dragSourceSlot = -1;
};
