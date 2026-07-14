#pragma once

#include <SFML/Graphics.hpp>

#include "../Camera/Camera.h"
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

    sf::Vector2f findSpawn() const;

    sf::RenderWindow window;

    World world;
    TerrainGenerator generator;
    ChunkRenderer chunks;
    Camera camera;
    Player player;
};
