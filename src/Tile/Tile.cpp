#include "Tile.h"


Tile::Tile()
{
    type = BlockType::Air;

    shape.setSize({14,14});
}


void Tile::setBlock(BlockType block)
{
    type = block;


    switch(type)
    {
        case BlockType::Grass:
            shape.setFillColor(sf::Color(80,200,80));
            break;


        case BlockType::Dirt:
            shape.setFillColor(sf::Color(130,80,40));
            break;


        case BlockType::Stone:
            shape.setFillColor(sf::Color(100,100,100));
            break;


        case BlockType::Air:
            shape.setFillColor(sf::Color::Transparent);
            break;
    }
}


BlockType Tile::getBlock()
{
    return type;
}


void Tile::draw(
    sf::RenderWindow& window,
    int x,
    int y,
    sf::Vector2f camera
)
{
    float screenX = x * 16 - camera.x;
    float screenY = y * 16 - camera.y;


    if(type == BlockType::Air)
        return;


    sf::RectangleShape border;

    border.setSize({16,16});
    border.setPosition({
        screenX,
        screenY
    });

    border.setFillColor(sf::Color::Black);

    window.draw(border);


    shape.setPosition({
        screenX + 1,
        screenY + 1
    });


    window.draw(shape);
}