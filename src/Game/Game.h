#pragma once

#include <SFML/Graphics.hpp>

#include "../Camera/Camera.h"
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
    void update(float dt);
    void render();

    sf::RenderWindow window;

    World world;
    TerrainGenerator generator;
    ChunkRenderer chunks;
    Camera camera;
};
