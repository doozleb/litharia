#pragma once

#include <vector>

#include "../Core/Constants.h"
#include "../Tile/Tile.h"

// Tile storage and nothing else. No SFML, no drawing, no generation: those are
// Chunks' and TerrainGenerator's jobs. That split is what makes this testable.
class World
{
public:
    World();

    bool inBounds(int x, int y) const;

    // Out of bounds reads return Air; out of bounds writes are ignored. The world
    // therefore has no undefined behavior at its edges, only open sky.
    BlockType get(int x, int y) const;
    void set(int x, int y, BlockType type);

    // The decoration layer: independent of get()/set(), never affects
    // isSolid() or fluid flow. Same out-of-bounds convention as get()/set().
    BlockType getDecoration(int x, int y) const;
    void setDecoration(int x, int y, BlockType type);

    bool isSolid(int x, int y) const;

    void fill(BlockType type);

private:
    std::size_t index(int x, int y) const;

    std::vector<Tile> tiles;
};
