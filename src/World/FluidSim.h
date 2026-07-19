#pragma once

#include <SFML/System/Vector2.hpp>

#include <vector>

#include "../Blocks/Blocks.h"

class World;

// Ticks Water/Lava tiles so a body of liquid settles as flat as it can: every
// tile moves down first, filling the space below as much as it fits, then
// levels out sideways with its lower neighbour - so a pool fills a hollow,
// overflows the lip, and evens out to a flat surface. Lava is more viscous and
// only flows on every LAVA_MOVE_INTERVAL-th step, so it creeps to level far
// slower than water. Where lava meets water it reacts into Obsidian.
//
// Owns its own active-tile queue so a 1000x500 world never needs a full-grid
// scan - only tiles that changed (or are adjacent to a change) tick, and a
// tile with nowhere left to move goes quiescent.
//
// Falling and levelling are exactly conservative (nothing is created or
// destroyed by flow alone, and no fluid is lost off the world's edges) - the
// only thing that ever reduces a pool's total fluid is the Obsidian reaction.
class FluidSim
{
public:
    // Seconds between simulation steps - independent of the 60Hz physics step,
    // so flow reads as a visible process rather than an instant teleport.
    static constexpr float TICK_INTERVAL = 0.1f;

    // Lava flows on one in every this-many steps; water flows every step. This
    // is the whole of "lava moves slower than water".
    static constexpr int LAVA_MOVE_INTERVAL = 3;

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
    bool equalizeAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);

    // True if the tile at (x, y) has anywhere to fall or level to. Used to let
    // a throttled (off-step) lava tile stay pending only while it still has a
    // move left, so settled lava goes quiescent like everything else.
    bool canMove(const World& world, int x, int y, BlockType type) const;

    std::vector<sf::Vector2i> active;
    float timer = 0.0f;
    int stepCount = 0;
};
