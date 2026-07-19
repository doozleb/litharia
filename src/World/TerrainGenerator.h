#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "../Blocks/Blocks.h"

class World;

enum class PoolKind
{
    Lake,
    WaterPool,
    LavaPool,
};

// Where a single fluid pool was placed, and which kind it is - returned by
// scatterFluids so tests (and, later, nothing else - Game reads the world
// directly) can verify counts and placement without re-deriving them.
struct FluidPoolSpawn
{
    int x;
    int y;
    PoolKind kind;
};

// A pure function of the seed: the same seed always produces a byte-identical world.
//
// Six passes:
//   1. Surface    - fractal noise over x gives a rolling height; grass, then a dirt
//      band, then stone all the way down.
//   2. Caves      - 2D fractal noise crossing a threshold carves air. The threshold
//      tightens near the surface so caves do not shred the landscape.
//   3. Hill caves - 4 hand-placed cave systems, each a trunk (random walk down to
//      the iron layer) plus a handful of dead-end branches, anchored on the
//      most elevated column near a fixed offset from spawn.
//   4. Ore        - hashed candidate points inside a depth band grow small blobs, but
//      only ever overwrite stone, so ore never floats in a cave or sits in dirt.
//   5. Trees      - a low-frequency noise channel gives each x position a "forest
//      factor"; columns roll against it to grow an oak tree, spaced far enough
//      apart that no two canopies ever touch.
//   6. Fluids     - surface lakes, underground water pools, and lava pools are
//      carved and filled as source-level blobs, lava biased toward the bottom
//      of its band. Runs between Ore and Trees in generate() so lakes still
//      block tree placement via Trees' own Grass check, and pools don't fight
//      ore veins.
class TerrainGenerator
{
public:
    // Surface stays inside this band whatever the noise does.
    static constexpr int SURFACE_MIN = 80;
    static constexpr int SURFACE_MAX = 260;

    static constexpr int COPPER_MIN_Y = 200;
    static constexpr int COPPER_MAX_Y = 340;

    // A much rarer deep band: copper is normally shallow, but a lucky dig
    // can still turn up copper this far down.
    static constexpr int COPPER_DEEP_MIN_Y = 341;
    static constexpr int COPPER_DEEP_MAX_Y = 495;

    static constexpr int IRON_MIN_Y = 320;
    static constexpr int IRON_MAX_Y = 495;

    // A much rarer shallow band: iron is normally deep, but a lucky dig can
    // still turn up iron this high.
    static constexpr int IRON_SHALLOW_MIN_Y = 90;
    static constexpr int IRON_SHALLOW_MAX_Y = 319;

    static constexpr int COAL_MIN_Y = 180;
    static constexpr int COAL_MAX_Y = 300;

    static constexpr int TREE_MIN_HEIGHT = 4;
    static constexpr int TREE_MAX_HEIGHT = 6;

    // Minimum distance between two trunks. Each canopy is 3 tiles wide
    // (trunk-1..trunk+1), so two trunks 4 apart have their nearest leaves at
    // trunk+1 and trunk+3 - a gap at trunk+2 that keeps them from ever being
    // 4-connected. That gap is what keeps the break-cascade's flood-fill
    // from ever bleeding into a neighbor tree.
    static constexpr int TREE_MIN_SPACING = 4;

    // Underground water pools stay above this - safely below the highest a
    // surface lake's basin could possibly reach (SURFACE_MAX + the largest
    // pool radius, with margin).
    static constexpr int WATER_POOL_MIN_Y = 280;
    static constexpr int WATER_POOL_MAX_Y = 320;

    // Lava pools live below the iron layer, down near the world's floor.
    static constexpr int LAVA_MIN_Y = 350;
    static constexpr int LAVA_MAX_Y = 495;

    static constexpr int SURFACE_LAKE_COUNT = 5;
    static constexpr int WATER_POOL_COUNT = 10;
    static constexpr int LAVA_POOL_COUNT = 15;

    // A finite early-game resource: exactly this many Sharp Rock spawn
    // points at world generation. Game tops it back up at runtime via
    // randomSurfaceSpot - see SHARP_ROCK_RESPAWN_INTERVAL in Game.h.
    static constexpr int SHARP_ROCK_COUNT = 9;

    // Starting search radius for findHillPeak - it expands from here (see
    // findHillPeak's own comment) when the terrain's real peak lies further
    // out than this.
    static constexpr int HILL_SEARCH_RADIUS = 30;

    // The 4 hill caves sit at spawnX +/- these offsets: 2 near, 2 far.
    static constexpr int SPECIAL_CAVE_NEAR_OFFSET = 100; // ~7-12s run from spawn
    static constexpr int SPECIAL_CAVE_FAR_OFFSET = 350;  // ~25-40s run from spawn

    explicit TerrainGenerator(std::uint32_t seed);

    // Passes 1-6.
    void generate(World& world) const;

    // Passes 1-2 only: terrain with no ore in it. The ore pass is defined as
    // "stone becomes ore", and this is the world it is defined against.
    void generateBase(World& world) const;

    // Pass 3: 4 hand-placed, hill-anchored cave systems (a trunk down to the
    // iron layer, plus a handful of dead-end branches), layered onto
    // generateBase's output. Public, like generateBase, so tests can
    // isolate exactly what this pass adds.
    void carveSpecialCaves(World& world) const;

    int surfaceHeight(int x) const;

    // SHARP_ROCK_COUNT random surface spots, hashed from the world seed
    // alone - deterministic like every other pass. Used once at world
    // generation.
    std::vector<std::pair<int, int>> scatterSharpRocks(const World& world) const;

    // Pass 6 (called from generate()): carves and fills SURFACE_LAKE_COUNT
    // surface lakes, WATER_POOL_COUNT underground water pools, and
    // LAVA_POOL_COUNT lava pools (biased toward the bottom of its band).
    // Returns what it placed, in placement order - public, like
    // scatterSharpRocks, so tests can verify counts/bands directly.
    std::vector<FluidPoolSpawn> scatterFluids(World& world) const;

    // One more random surface spot, hashed from the world seed and `salt` -
    // vary `salt` per call (e.g. an incrementing counter) to get a
    // different spot each time. Used by Game's runtime respawn timer, since
    // that is not a generation pass and needs a fresh pick on demand.
    std::pair<int, int> randomSurfaceSpot(const World& world, std::uint32_t salt) const;

    // The most elevated column near targetX (smaller surfaceHeight = higher
    // ground). Starts searching a window of HILL_SEARCH_RADIUS and, if the
    // best point found sits right on that window's edge - a sign the real
    // peak lies further out, not that the window's edge genuinely is the
    // peak - doubles the window and tries again, up to maxRadius. Ties
    // break toward the first x found. Public, like surfaceHeight, so it can
    // be tested directly.
    int findHillPeak(int targetX, int maxRadius) const;

    std::uint32_t seed() const { return worldSeed; }

private:
    void generateSurface(World& world) const;
    void carveCaves(World& world) const;
    void scatterOre(World& world) const;
    void scatterTrees(World& world) const;

    void growVein(World& world,
                  int centerX,
                  int centerY,
                  float radius,
                  BlockType ore,
                  int minY,
                  int maxY) const;

    // An irregular underground pocket of `fluid`: fills tiles whose 2D-noise
    // value beats their distance from the centre, so the core is solid but the
    // rim ravels into a random clutter rather than a clean outline. Clipped to
    // [minY, maxY]. Like every pool it displaces whatever terrain is there
    // (unlike growVein, which only replaces Stone). `salt` decorrelates the
    // noise field between pools.
    void growPoolNoise(World& world,
                       int centerX,
                       int centerY,
                       float radius,
                       BlockType fluid,
                       int minY,
                       int maxY,
                       std::uint32_t salt) const;

    // A surface lake: a flat-topped basin, deepest at its centre column and
    // tapering smoothly to nothing at its edges (a parabolic bowl). `topY` is
    // the flat water level; each column fills from there (or its own surface,
    // whichever is lower, so water never floats above ground) down to the
    // column's depth.
    void growLakeBasin(World& world,
                       int centerX,
                       int topY,
                       float radius) const;

    void placeTree(World& world, int trunkX, int surface, int height) const;

    void carveTunnelPoint(World& world, int cx, int cy, float radius) const;

    std::vector<std::pair<int, int>> carveTrunk(World& world,
                                                 int caveIndex,
                                                 int startX,
                                                 int startY) const;

    void carveBranch(World& world,
                      int caveIndex,
                      int branchIndex,
                      int startX,
                      int startY) const;

    std::uint32_t worldSeed;
};
