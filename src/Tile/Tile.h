#pragma once

#include "../Blocks/Blocks.h"

// One byte. A tile is data; drawing it is the chunk renderer's job.
struct Tile
{
    BlockType type = BlockType::Air;
};

static_assert(sizeof(Tile) == 1, "A tile must stay one byte: a 1000x500 world is 500 KB.");
