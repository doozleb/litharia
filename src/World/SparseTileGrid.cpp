#include "SparseTileGrid.h"

#include <algorithm>

#include "../Core/Constants.h"

SparseTileGrid::SparseTileGrid()
    : stamp(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
    , value(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
{
}

void SparseTileGrid::clear()
{
    ++generation;
    if (generation == 0)
    {
        std::fill(stamp.begin(), stamp.end(), 0);
        generation = 1;
    }
}

void SparseTileGrid::set(int x, int y, int level)
{
    const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
    stamp[i] = generation;
    value[i] = static_cast<std::int8_t>(level);
}

void SparseTileGrid::merge(int x, int y, int level)
{
    const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
    if (stamp[i] != generation)
    {
        stamp[i] = generation;
        value[i] = static_cast<std::int8_t>(level);
    }
    else
    {
        value[i] = static_cast<std::int8_t>(std::max<int>(value[i], level));
    }
}

int SparseTileGrid::at(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
    return (stamp[i] == generation) ? value[i] : 0;
}
