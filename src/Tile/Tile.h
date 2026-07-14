#pragma once

#include <SFML/Graphics.hpp>
#include "../Blocks/Blocks.h"

class Tile
{
public:

    Tile();

    void setBlock(BlockType block);

    BlockType getBlock();

    void draw(
        sf::RenderWindow& window,
        int x,
        int y,
        sf::Vector2f camera
    );


private:

    BlockType type;

    sf::RectangleShape shape;
};