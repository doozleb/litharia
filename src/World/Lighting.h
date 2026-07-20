#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <SFML/System/Vector2.hpp>

class World;
class Machines;

// One tile's current light, split into two independent channels - see
// Lighting's own comment for why two, and why this lives in its own grid
// rather than on Tile.
struct LightLevel
{
    std::uint8_t sky : 4;   // 0-8, this tile's sunlight exposure
    std::uint8_t block : 4; // 0-8, this tile's torch/lava exposure
};
static_assert(sizeof(LightLevel) == 1, "One byte per tile: a 1000x500 world stays 500 KB.");

// Computes and stores per-tile lighting: how exposed to the sky a tile is
// (skyLight) and how close it is to a Torch or Lava tile (blockLight), each
// 0-8 and decaying by 1 per orthogonal step, blocked entirely by solid
// tiles. A separate grid from World/Tile - light values change on every
// block/Torch edit (and skyLight's *effective* brightness changes every
// tick, scaled by the day/night clock - see DayNightClock), which doesn't
// belong on Tile any more than a fluid's flow state would.
//
// recomputeAll() rebuilds the whole grid from scratch rather than patching
// just the changed region: correctly patching only a local region after a
// light SOURCE disappears (a Torch mined, a wall sealing off a lit shaft)
// needs a "clear then re-flood from any surviving neighbour" pass, not a
// simple re-seed - full recompute sidesteps that complexity entirely and is
// still cheap, since it only runs on a block/Torch edit (a rare, player-
// paced event), never once a tick.
class Lighting
{
public:
    Lighting();

    // Clears every tile's sky/block level to 0, then floods block light
    // from every Lava tile and every placed Torch, and sky light from every
    // column open to the world's top edge. Call whenever a block is mined
    // or placed, or a Torch is placed or removed.
    void recomputeAll(const World& world, const Machines& machines);

    int skyLight(int x, int y) const;   // 0-8; 0 out of bounds
    int blockLight(int x, int y) const; // 0-8; 0 out of bounds

    // A single-source flood fill from `source` at level 8 - the same
    // brightness and decay/occlusion rule as a placed Torch's own
    // blockLight, but computed fresh every call rather than stored in
    // `levels`. Used for the player's held Torch, which moves with them
    // every frame: naturally bounded to within 8 steps of `source` (the seed
    // starts at level 8 and floodFill's decay reaches 0 by then), so this
    // stays cheap enough to call once a frame without forcing a full
    // recompute.
    std::vector<std::pair<sf::Vector2i, int>> heldTorchLight(const World& world,
                                                              sf::Vector2i source) const;

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
    // can serve both the world-wide recompute and a single moving source
    // (Lighting::heldTorchLight, added later) alike.
    static std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world,
                                                                const std::vector<LightSeed>& seeds);

    std::vector<LightLevel> levels; // WORLD_WIDTH * WORLD_HEIGHT
};
