#pragma once

#include <SFML/System/Vector2.hpp>

#include <vector>

#include "../Blocks/Blocks.h"

class World;

// Ticks Water/Lava tiles: falling, spreading, and reacting into Obsidian where
// they touch. Owns its own active-tile queue so a 1000x500 world never needs a
// full-grid scan - only tiles that changed (or are adjacent to a change) tick.
//
// Falling and spreading are exactly conservative (nothing is created or
// destroyed by flow alone) - the only thing that ever reduces a pool's total
// fluid is the Obsidian reaction.
class FluidSim
{
public:
    // Seconds between simulation steps - independent of the 60Hz physics step,
    // so flow reads as a visible process rather than an instant teleport.
    static constexpr float TICK_INTERVAL = 0.1f;

    // Marks a tile as needing to be checked on the next step. Call this
    // whenever a fluid tile is placed (world generation, world load).
    void activate(int x, int y);

    // Activates (x, y) and its 4 neighbors. Call this whenever a tile change
    // (mining, placing) might newly expose or block a nearby fluid tile.
    void activateAround(int x, int y);

    // A one-time full-world scan that activates every existing fluid tile.
    // Used once, right after world generation, since generation writes fluid
    // tiles directly into World without going through activate().
    void activateAll(const World& world);

    // Advances the internal timer by dt; if a full TICK_INTERVAL has
    // accumulated, runs exactly one simulation step and appends every tile
    // position that changed to `changedTiles` (so the caller can mark chunks
    // dirty), the same convention as Machines::tick's `mined` out-param.
    void tick(World& world, float dt, std::vector<sf::Vector2i>& changedTiles);

private:
    void step(World& world, std::vector<sf::Vector2i>& changedTiles);

    // Each returns true if it made a change (and the caller should not also
    // try the next rule on the same tile this tick).
    bool reactAt(World& world, int x, int y, std::vector<sf::Vector2i>& changed);
    bool fallAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);
    bool spreadAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);

    std::vector<sf::Vector2i> active;
    float timer = 0.0f;
};
