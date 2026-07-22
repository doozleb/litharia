#pragma once

#include <cstdint>
#include <vector>

// A world-sized lookup grid answering "what value was written to (x, y)
// this round" without a hash map's per-entry heap allocation and hashing
// cost - the same generation-stamp trick Lighting's own floodStamp/floodBest
// scratch buffers use (see Lighting.h), applied wherever a sparse per-frame
// (x, y) -> value set needs O(1) lookup instead of a fresh
// std::unordered_map rebuilt every call.
//
// clear() is an O(1) counter bump, not an O(world size) fill: a cell only
// reads as "set this round" when its stamp matches the current generation,
// so stale values from a prior round are invisible without ever being
// erased. set()/merge() assume in-world coordinates - callers are
// responsible for bounds-checking before writing, same as Lighting's
// internal scratch writes; only at() bounds-checks, since callers may
// legitimately query coordinates outside the world (e.g. a search that
// steps past a world edge) and expect 0 back rather than undefined
// behavior.
class SparseTileGrid
{
public:
    SparseTileGrid();

    // Starts a fresh round: every previously set()/merge()'d cell reads as
    // unset (0) again until touched again this round.
    void clear();

    // Unconditionally overwrites (x, y)'s value for this round. Use only
    // when the caller guarantees each coordinate is written at most once
    // per round - e.g. Lighting::heldTorchLight's result, which never
    // repeats a tile (see Lighting.h's floodFill comment).
    void set(int x, int y, int level);

    // Raises (x, y)'s value to the max of everything merge()'d into it this
    // round. Use when the same coordinate may be written more than once per
    // round - e.g. Lighting::ambientOutline's result, where a solid border
    // tile can be reached from multiple open neighbours.
    void merge(int x, int y, int level);

    // 0 if (x, y) is outside world bounds or was never set()/merge()'d this
    // round.
    int at(int x, int y) const;

private:
    std::vector<std::uint32_t> stamp; // WORLD_WIDTH * WORLD_HEIGHT
    std::vector<std::int8_t> value;   // WORLD_WIDTH * WORLD_HEIGHT
    std::uint32_t generation = 0;
};
