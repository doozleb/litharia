#pragma once

#include <SFML/Graphics.hpp>
#include "../World/World.h"


class Game
{
public:

    Game();

    void run();


private:

    void update();
    void render();


    sf::RenderWindow window;


    World world;


    sf::Vector2f cameraPosition;
};