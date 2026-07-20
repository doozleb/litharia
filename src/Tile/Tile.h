#pragma once

#include "../Blocks/Blocks.h"

// A tile is data; drawing it is the chunk renderer's job. `decoration` is an
// independent overlay slot for things the player already walks straight
// through (tree logs/leaves) - kept separate from `type` so fluid can flow
// through a decorated tile without having to overwrite (and so destroy) it.
struct Tile
{
    BlockType type = BlockType::Air;
    BlockType decoration = BlockType::Air;
};

static_assert(sizeof(Tile) == 2, "A tile must stay two bytes: a 1000x500 world is 1 MB.");
