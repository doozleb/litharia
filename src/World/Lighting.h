#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <SFML/System/Vector2.hpp>

class World;
class Machines;

// One tile's current light, split into three independent channels - see
// Lighting's own comment for why, and why this lives in its own grid rather
// than on Tile.
struct LightLevel
{
    std::uint16_t sky : 4;   // 0-MAX_LIGHT_LEVEL, this tile's sunlight exposure
    std::uint16_t torch : 4; // 0-TORCH_LIGHT_LEVEL, this tile's Torch exposure
    std::uint16_t lava : 4;  // 0-MAX_LIGHT_LEVEL, this tile's Lava exposure
};
static_assert(sizeof(LightLevel) == 2, "Two bytes per tile: a 1000x500 world stays 1 MB.");

// Computes and stores per-tile lighting: how exposed to the sky a tile is
// (skyLight), how close it is to a placed Torch (torchLight), and how close
// it is to a Lava tile (lavaLight) - sky and lava are 0-MAX_LIGHT_LEVEL,
// torch is 0-TORCH_LIGHT_LEVEL (deliberately brighter and farther-reaching
// than the other two sources) - each decaying by 1 per orthogonal step,
// blocked entirely by solid tiles. Torch and Lava are
// separate channels (not merged into one "block light") purely so
// LightRenderer can tint them differently - a lava pool and a lit Torch
// should not look identical. A separate grid from World/Tile - light values
// change on every block/Torch edit (and skyLight's *effective* brightness
// changes every tick, scaled by the day/night clock - see DayNightClock),
// which doesn't belong on Tile any more than a fluid's flow state would.
//
// recomputeAll() rebuilds the whole grid from scratch rather than patching
// just the changed region: correctly patching only a local region after a
// light SOURCE disappears (a Torch mined, a wall sealing off a lit shaft)
// needs a "clear then re-flood from any surviving neighbour" pass, not a
// simple re-seed - full recompute sidesteps that complexity entirely and is
// still cheap, since it only runs on a block/Torch edit (a rare, player-
// paced event) or rate-limited lava movement, never once a tick
// unconditionally.
class Lighting
{
public:
    // How far light travels before going fully dark: a source (a placed
    // Torch, a Lava tile, an open sky column) starts at this level and
    // decays by 1 per orthogonal step, so a tile this many steps away is the
    // last one that still reads as lit at all - one dimmer at each step in
    // between.
    static constexpr int MAX_LIGHT_LEVEL = 9;

    // A Torch's own seed level - deliberately higher than MAX_LIGHT_LEVEL,
    // using headroom the 4-bit `torch` field already has (up to 15), so a
    // Torch is both brighter and farther-reaching than sky or Lava at the
    // same distance: brightness is still normalized against MAX_LIGHT_LEVEL
    // at render time, so a Torch stays fully bright out to
    // (TORCH_LIGHT_LEVEL - MAX_LIGHT_LEVEL) tiles farther than a source
    // seeded at MAX_LIGHT_LEVEL would, and doesn't decay to 0 until this
    // many steps out instead of MAX_LIGHT_LEVEL. Used for both a placed
    // Torch (recomputeAll) and the player's held Torch (heldTorchLight) -
    // the two are deliberately kept identical, per heldTorchLight's own
    // comment below.
    static constexpr int TORCH_LIGHT_LEVEL = 15;

    // How far the player's own "eyes adjusting to the dark" ambient
    // visibility reaches - much further than MAX_LIGHT_LEVEL, since it's not
    // real light, just enough to make out shapes and ore nearby. See
    // ambientOutline().
    static constexpr int AMBIENT_OUTLINE_RADIUS = 20;

    // Flat (non-decaying) brightness ambientOutline() assigns to a plain
    // solid tile or reachable open tile within range - deliberately small
    // relative to MAX_LIGHT_LEVEL so it reads as "barely lightens," not as
    // real light.
    static constexpr int AMBIENT_OUTLINE_LEVEL = 1;

    // Same as AMBIENT_OUTLINE_LEVEL, but for a solid tile that is itself an
    // ore (see isOre) - slightly brighter so ore reads as "there's something
    // here worth digging" without revealing which ore or how much.
    static constexpr int AMBIENT_OUTLINE_ORE_LEVEL = 2;

    Lighting();

    // Clears every tile's sky/torch/lava level to 0, then floods sky light
    // from every column open to the world's top edge, torch light from every
    // placed Torch, and lava light from every Lava tile. Call whenever a
    // block is mined or placed, or a Torch is placed or removed.
    void recomputeAll(const World& world, const Machines& machines);

    int skyLight(int x, int y) const;   // 0-MAX_LIGHT_LEVEL; 0 out of bounds
    int torchLight(int x, int y) const; // 0-TORCH_LIGHT_LEVEL; 0 out of bounds
    int lavaLight(int x, int y) const;  // 0-MAX_LIGHT_LEVEL; 0 out of bounds

    // A single-source flood fill from `source` at TORCH_LIGHT_LEVEL - the
    // same brightness and decay/occlusion rule as a placed Torch's own
    // torchLight, but computed fresh every call rather than stored in
    // `levels`. Used for the player's held Torch, which moves with them
    // every frame: naturally bounded to within TORCH_LIGHT_LEVEL steps of
    // `source` (the seed starts there and floodFill's decay reaches 0 by
    // then), so this stays cheap enough to call once a frame without forcing
    // a full recompute.
    std::vector<std::pair<sf::Vector2i, int>> heldTorchLight(const World& world,
                                                              sf::Vector2i source) const;

    // A short-range, uncolored visibility floor around the player's current
    // tile, recomputed fresh every frame (never stored, never forcing a
    // recompute) - the "you can make out shapes and ore nearby even with no
    // light" mechanic that replaces solid ground's old always-fully-visible
    // behavior. A bounded BFS from `playerTile`, traveling only through open
    // tiles up to `radius` steps (so a sealed pocket with no path back to
    // the player gets nothing, exactly like real light) - every open tile
    // reached this way, and every solid tile bordering one, is included in
    // the result at AMBIENT_OUTLINE_LEVEL (AMBIENT_OUTLINE_ORE_LEVEL if the
    // solid tile is ore). Unlike real light, this value does not decay with
    // distance inside the radius - it's a flat floor, not a gradient.
    // `radius` defaults to AMBIENT_OUTLINE_RADIUS (the normal "eyes adjusted
    // to the dark" case); callers pass a larger value to widen the covered
    // area - Game::render does this when a Torch is equipped, sizing it to
    // the camera's current view instead of the fixed default.
    std::vector<std::pair<sf::Vector2i, int>> ambientOutline(const World& world, sf::Vector2i playerTile,
                                                              int radius = AMBIENT_OUTLINE_RADIUS) const;

private:
    struct LightSeed
    {
        int x;
        int y;
        int level;
    };

    // One candidate in floodFill's shared frontier: the accumulated
    // distance to reach (x, y) via the path that produced this entry, not
    // necessarily its final (true minimum) distance - standard
    // lazy-deletion Dijkstra, where a tile can appear more than once and
    // only its first (smallest-distance) pop is authoritative.
    struct FloodEntry
    {
        float distance;
        int x;
        int y;
    };

    // Min-heap by distance: smallest distance pops first.
    struct FloodEntryGreater
    {
        bool operator()(const FloodEntry& a, const FloodEntry& b) const
        {
            return a.distance > b.distance;
        }
    };

    // A single shared multi-source Dijkstra across every seed in `seeds`:
    // every seed starts queued at distance 0, and each step relaxes the 4
    // orthogonal (weight 1) and 4 diagonal (weight sqrt(2), corner-cutting
    // guarded - see the .cpp) neighbours of whichever tile the frontier's
    // smallest-distance entry names, stopping once a tile's resulting level
    // (seedLevel - distance, floored) would be 0. Since Dijkstra finalizes
    // each tile exactly once, at its true minimum distance, the first time
    // it's popped, a tile is only ever improved once - the popped visit
    // order is already the full sparse result, same invariant the old
    // per-seed version relied on. Serves the world-wide recompute and a
    // single moving source (Lighting::heldTorchLight) alike - see
    // docs/superpowers/specs/2026-07-22-floodfill-shared-search-design.md
    // for the full design and its one deliberate behavior change: falloff
    // is exactly circular only along the 8 principal directions, very
    // slightly octagon-ish between them - a standard property of
    // 8-connected weighted-grid distance, present even with no obstacles
    // at all.
    //
    // Precondition (unenforced, true of every current caller): every seed
    // in one call shares the same seed.level.
    //
    // Uses floodStamp/floodBest/floodGeneration and floodHeap (below) as
    // scratch rather than allocating fresh buffers per call - see
    // floodStamp's own comment for why.
    std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world,
                                                          const std::vector<LightSeed>& seeds) const;

    std::vector<LightLevel> levels; // WORLD_WIDTH * WORLD_HEIGHT

    // Persistent scratch for floodFill, sized once at construction rather
    // than reallocated per call. floodBest[i] is only meaningful when
    // floodStamp[i] == floodGeneration; any other stamp value means tile i
    // hasn't been touched during the current call, equivalent to the old
    // per-call buffer's -1 sentinel.
    mutable std::vector<std::uint32_t> floodStamp; // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::vector<std::int8_t> floodBest;     // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::uint32_t floodGeneration = 0;

    // Persistent scratch for floodFill's shared frontier - a binary heap
    // (std::push_heap/pop_heap, ordered by FloodEntryGreater) over this
    // vector, reused across calls via clear() so its allocated capacity
    // survives between calls instead of being discarded and regrown.
    mutable std::vector<FloodEntry> floodHeap;

    // Persistent scratch for ambientOutline's own BFS - added in a later
    // step of this same change, same generation-stamp trick, kept separate
    // from floodFill's scratch above since the two searches store different
    // payloads (best light level vs. BFS step count).
    mutable std::vector<std::uint32_t> outlineStamp; // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::vector<std::int16_t> outlineStep;   // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::uint32_t outlineGeneration = 0;
};
