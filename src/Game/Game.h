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
#include "../Machines/Recipes.h"
#include "../Player/Player.h"
#include "../World/Chunks.h"
#include "../World/FluidSim.h"
#include "../World/TerrainGenerator.h"
#include "../World/World.h"

class Game
{
public:
    static constexpr float SHARP_ROCK_RESPAWN_INTERVAL = 300.0f; // 5 minutes

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
    void spawnSharpRocks();
    void respawnSharpRocksIfNeeded(float dt);

    void tickMachines(float dt);
    void placeMachineAtCursor();
    void placeFurnitureAtCursor(const PlayerInput& input);
    void mineFurnitureAtCursor(const PlayerInput& input, float dt);
    void refundMachineItem(MachineType type);
    void removeMachineAtCursor();
    void dropAtPlayer(ItemStack stack);
    void spillInventoryToGround(const Inventory& inventory);
    void cycleBuildType(int delta);
    void setBuildType(MachineType type);
    void interactAtCursor();
    void toggleInventory();
    void drawInventoryPanels();
    void beginDrag();
    void endDrag();
    void depositAllToStorage();
    void collectAllFromStorage();
    void startCraft(int recipeIndex);
    void updateCrafting(float dt);
    void startSmelt(int recipeIndex);
    void updateSmelting(float dt);
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

    float sharpRockRespawnTimer = 0.0f;
    int sharpRockSpawnCounter = 0;

    Machines machines;
    MachineRenderer machineRenderer;
    FluidSim fluids;

    bool buildMode = false;
    MachineType buildType = MachineType::CopperBelt;
    Direction buildFacing = Direction::Right;

    bool inventoryOpen = false;
    std::optional<sf::Vector2i> openStorageTile;
    std::optional<sf::Vector2i> openCraftingTableTile;
    std::optional<sf::Vector2i> openFurnaceTile;

    bool smelting = false;
    int smeltingRecipeIndex = -1;
    float smeltProgress = 0.0f;

    bool miningFurniture = false;
    sf::Vector2i miningFurnitureTarget{0, 0};
    float miningFurnitureProgress = 0.0f;

    bool crafting = false;
    int craftingRecipeIndex = -1;
    float craftProgress = 0.0f;

    enum class InventoryPanel { Bag, Storage };

    bool dragging = false;
    ItemStack dragStack;
    InventoryPanel dragSourcePanel = InventoryPanel::Bag;
    int dragSourceSlot = -1;
};
