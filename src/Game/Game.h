#pragma once

#include <SFML/Graphics.hpp>

#include <vector>

#include "../Camera/Camera.h"
#include "../Items/ItemEntity.h"
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

    void render();
    void drawMiningHighlight();

    PlayerInput readInput() const;
    sf::Vector2f cursorWorldPosition() const;

    sf::Vector2f findSpawn() const;

    // A mined block becomes a stack on the ground.
    void spawnDrop(const MineResult& result);

    sf::RenderWindow window;

    World world;
    TerrainGenerator generator;
    ChunkRenderer chunks;
    Camera camera;
    Player player;

    std::vector<ItemEntity> drops;
};
