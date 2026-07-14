#pragma once

#include <SFML/Graphics.hpp>
#include "../Tile/Tile.h"


class World
{
public:

    World();


    void generate();


    void draw(
        sf::RenderWindow& window,
        sf::Vector2f camera
    );


private:

    Tile tiles[50][37];
};