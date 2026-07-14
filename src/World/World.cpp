#include "World.h"


World::World()
{
}


void World::generate()
{
    for(int x = 0; x < 50; x++)
    {
        for(int y = 0; y < 37; y++)
        {
            if(y == 15)
            {
                tiles[x][y].setBlock(BlockType::Grass);
            }

            else if(y > 15 && y < 25)
            {
                tiles[x][y].setBlock(BlockType::Dirt);
            }

            else if(y >= 25)
            {
                tiles[x][y].setBlock(BlockType::Stone);
            }
        }
    }
}



void World::draw(
    sf::RenderWindow& window,
    sf::Vector2f camera
)
{
    for(int x = 0; x < 50; x++)
    {
        for(int y = 0; y < 37; y++)
        {
            tiles[x][y].draw(
                window,
                x,
                y,
                camera
            );
        }
    }
}