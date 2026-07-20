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
#include "../World/DayNightClock.h"
#include "../World/FluidSim.h"
#include "../World/Lighting.h"
#include "../World/LightRenderer.h"
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
    bool placeFurnitureAtCursor(const PlayerInput& input);
    bool mineFurnitureAtCursor(const PlayerInput& input, float dt);
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
    void scrollCraftPanel(int delta);
    void startSmelt(int recipeIndex);
    void updateSmelting(float dt);
    sf::Vector2i cursorTile() const;
    void drawMachineTooltip();

    // A floating "-N" that rises and fades near the player when they take
    // damage. Position is world-space (it needs to drift with the world, not
    // the screen) so it is drawn among the other world-space draws in
    // render(), before the HUD's screen-space calls.
    struct DamagePopup
    {
        sf::Text text;
        sf::Vector2f worldPos;
        float age = 0.0f;
    };

    void spawnDamagePopup(int amount);
    void updateDamagePopups(float dt);
    void drawDamagePopups();

    sf::RenderWindow window;

    World world;
    TerrainGenerator generator;
    ChunkRenderer chunks;
    Camera camera;
    Player player;
    Hud hud;

    std::vector<ItemEntity> drops;
    std::vector<DamagePopup> damagePopups;

    float sharpRockRespawnTimer = 0.0f;
    int sharpRockSpawnCounter = 0;

    Machines machines;
    MachineRenderer machineRenderer;
    FluidSim fluids;
    DayNightClock dayNightClock;
    Lighting lighting;
    LightRenderer lightRenderer;

    // Rate-limits the lava-movement lighting recompute (see fixedUpdate):
    // Lighting::recomputeAll is a full-world flood fill, expensive enough
    // that re-running it on every single tick a lava tile changes - which is
    // most ticks while any of the world's many generated lava pools are
    // still settling - visibly stalls the game. Counts down each tick;
    // a lava-triggered recompute only fires once it reaches 0, then resets.
    float lavaLightingCooldown = 0.0f;

    bool buildMode = false;
    MachineType buildType = MachineType::CopperBelt;
    Direction buildFacing = Direction::Right;

    bool inventoryOpen = false;
    std::optional<sf::Vector2i> openStorageTile;
    std::optional<sf::Vector2i> openCraftingTableTile;
    std::optional<sf::Vector2i> openFurnaceTile;

    // How many rows scrolled into the craft panel's recipe list (see
    // Hud::CRAFT_PANEL_VISIBLE_ROWS) - reset whenever the inventory closes,
    // clamped on every scroll-wheel tick against Hud::craftRecipeCount.
    int craftPanelScroll = 0;

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
