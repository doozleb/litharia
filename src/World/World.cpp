#include "World.h"

World::World()
    : tiles(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT)
{
}

std::size_t World::index(int x, int y) const
{
    return static_cast<std::size_t>(y) * WORLD_WIDTH + static_cast<std::size_t>(x);
}

bool World::inBounds(int x, int y) const
{
    return x >= 0 && x < WORLD_WIDTH && y >= 0 && y < WORLD_HEIGHT;
}

BlockType World::get(int x, int y) const
{
    if (!inBounds(x, y))
        return BlockType::Air;

    return tiles[index(x, y)].type;
}

void World::set(int x, int y, BlockType type)
{
    if (!inBounds(x, y))
        return;

    tiles[index(x, y)].type = type;
}

BlockType World::getDecoration(int x, int y) const
{
    if (!inBounds(x, y))
        return BlockType::Air;

    return tiles[index(x, y)].decoration;
}

void World::setDecoration(int x, int y, BlockType type)
{
    if (!inBounds(x, y))
        return;

    tiles[index(x, y)].decoration = type;
}

bool World::isSolid(int x, int y) const
{
    return isSolidBlock(get(x, y));
}

void World::fill(BlockType type)
{
    for (Tile& tile : tiles)
        tile.type = type;
}
