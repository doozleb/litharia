#pragma once

#include <SFML/System/Vector2.hpp>

#include <cstdint>
#include <vector>

#include "../Blocks/Blocks.h"

class World;

// Ticks Water/Lava tiles so a body of liquid settles flat. Each tile moves down
// first (filling the space below as much as fits); a resting row then sloshes
// one unit per step from its highest cell to its lowest, so the surface visibly
// settles rather than snapping flat in a single tick, until it is flat within
// one level; water widens gradually into open space beside it and spills over
// ledges. Lava is more viscous and only flows on every LAVA_MOVE_INTERVAL-th
// step, so it creeps to level far slower than water. Where lava meets water it
// reacts into Obsidian.
//
// Owns its own active-tile queue so a 1000x500 world never needs a full-grid
// scan - only tiles that changed (or are adjacent to a change) tick, and a
// tile with nowhere left to move goes quiescent.
//
// Falling, levelling, spilling and the world edges are all exactly conservative
// (no fluid is lost off the edges, and no fluid is created or destroyed by
// levelling a resting run flat). The Obsidian reaction is the only thing that
// deliberately consumes fluid.
class FluidSim
{
public:
    // Seconds between simulation steps - independent of the 60Hz physics step,
    // so flow reads as a visible process rather than an instant teleport. Tuned
    // to 4x the original 0.1s: the settle rule moves one fluid unit per tile
    // per step, so tick rate is what "speed" means here - ticking 4x more often
    // settles a pool 4x faster in real time without changing the conservative,
    // convergent move logic at all (CPU cost is trivial either way; a step
    // averages well under a millisecond).
    static constexpr float TICK_INTERVAL = 0.025f;

    // Lava flows on one in every this-many steps; water flows every step. This
    // is the whole of "lava moves slower than water".
    static constexpr int LAVA_MOVE_INTERVAL = 3;

    FluidSim();

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
    // Conservative horizontal levelling. Only reached when fallAt already tried
    // and failed for this tile this tick - "rests" means the tile below is not
    // open in-bounds air, not that the tile has no room at all (same-fluid-
    // below-with-room also counts as resting, and can still be picked up by
    // fallAt on a later tick once that room appears). Finds the highest and
    // lowest cell across the whole contiguous resting run of same-fluid tiles
    // and, while they differ by two or more, sloshes exactly one unit per step
    // from the highest to the lowest - a visible, gradual settle rather than a
    // one-tick snap - until the run is flat within one level; if the run is
    // already flat, widens one step into open, resting air beside it instead.
    // Every move relocates exactly one unit (or an exact split when widening),
    // so no fluid is ever created or lost; fallAt and equalizeAt are each
    // individually conservative, so running fall-then-equalize in that order
    // keeps the whole step conservative without their domains needing to be
    // strictly disjoint.
    bool equalizeAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);
    bool cascadeAt(World& world, int x, int y, BlockType type, std::vector<sf::Vector2i>& changed);

    // True if the tile at (x, y) has anywhere to fall, level, spread or spill.
    // Used to let a throttled (off-step) lava tile stay pending only while it
    // still has a move left, so settled lava goes quiescent like everything else.
    bool canMove(const World& world, int x, int y, BlockType type) const;

    // The maximal contiguous run of resting same-fluid tiles at a row, and its
    // highest and lowest cells. A tile "rests" when it cannot fall (its below
    // is not open in-bounds air). Used by both equalizeAt (to slosh a unit from
    // maxX to minX) and canMove (to decide a throttled tile still has a move).
    struct RunScan
    {
        int xL;
        int xR;
        int maxX;
        int minX;
        int maxLevel;
        int minLevel;
    };

    // True if (cx, y) is a resting tile of the same fluid family as `type`.
    bool tileRests(const World& world, int cx, int y, BlockType type) const;

    // Precondition: tileRests(world, x, y, type) is true.
    RunScan scanRun(const World& world, int x, int y, BlockType type) const;

    // The pending queue, plus a per-tile flag marking which tiles are already
    // in it. Without the flag, activateAround would push the same tile many
    // times per step (each fluid tile is a neighbour of up to four others), and
    // every duplicate re-runs the full rule chain - the queue grew to ~40x the
    // real fluid-tile count. The flag keeps `active` a true set: at most one
    // entry per tile.
    std::vector<sf::Vector2i> active;
    std::vector<std::uint8_t> pending; // size WORLD_WIDTH*WORLD_HEIGHT, 1 = queued
    float timer = 0.0f;
    int stepCount = 0;
};
