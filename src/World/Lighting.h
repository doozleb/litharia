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
    std::uint16_t torch : 4; // 0-MAX_LIGHT_LEVEL, this tile's Torch exposure
    std::uint16_t lava : 4;  // 0-MAX_LIGHT_LEVEL, this tile's Lava exposure
};
static_assert(sizeof(LightLevel) == 2, "Two bytes per tile: a 1000x500 world stays 1 MB.");

// Computes and stores per-tile lighting: how exposed to the sky a tile is
// (skyLight), how close it is to a placed Torch (torchLight), and how close
// it is to a Lava tile (lavaLight) - each 0-MAX_LIGHT_LEVEL, decaying by 1
// per orthogonal step, blocked entirely by solid tiles. Torch and Lava are
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
    int torchLight(int x, int y) const; // 0-MAX_LIGHT_LEVEL; 0 out of bounds
    int lavaLight(int x, int y) const;  // 0-MAX_LIGHT_LEVEL; 0 out of bounds

    // A single-source flood fill from `source` at MAX_LIGHT_LEVEL - the same
    // brightness and decay/occlusion rule as a placed Torch's own
    // torchLight, but computed fresh every call rather than stored in
    // `levels`. Used for the player's held Torch, which moves with them
    // every frame: naturally bounded to within MAX_LIGHT_LEVEL steps of
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
    // tiles up to AMBIENT_OUTLINE_RADIUS steps (so a sealed pocket with no
    // path back to the player gets nothing, exactly like real light) -
    // every open tile reached this way, and every solid tile bordering one,
    // is included in the result at AMBIENT_OUTLINE_LEVEL (AMBIENT_OUTLINE_ORE_LEVEL
    // if the solid tile is ore). Unlike real light, this value does not
    // decay with distance inside the radius - it's a flat floor, not a
    // gradient.
    std::vector<std::pair<sf::Vector2i, int>> ambientOutline(const World& world,
                                                              sf::Vector2i playerTile) const;

private:
    struct LightSeed
    {
        int x;
        int y;
        int level;
    };

    // Multi-source BFS: every seed starts queued at its own level; each step,
    // the 4 orthogonal neighbours get level-1 wherever that beats what they
    // already have, stopping at level 0 or a solid tile (World::isSolid).
    // Since decay is always exactly 1 per step, a tile is only ever improved
    // once, so the BFS visit order alone is already the full sparse result -
    // no second full-grid scan needed to extract it. Static (no `this`) so it
    // can serve the world-wide recompute, a single moving source
    // (Lighting::heldTorchLight), and the ambient-outline reachability query
    // alike.
    static std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world,
                                                                const std::vector<LightSeed>& seeds);

    std::vector<LightLevel> levels; // WORLD_WIDTH * WORLD_HEIGHT
};
