#pragma once

#include <cstdint>

#include "../Blocks/Blocks.h"

class World;

// A pure function of the seed: the same seed always produces a byte-identical world.
//
// Four passes:
//   1. Surface - fractal noise over x gives a rolling height; grass, then a dirt
//      band, then stone all the way down.
//   2. Caves   - 2D fractal noise crossing a threshold carves air. The threshold
//      tightens near the surface so caves do not shred the landscape.
//   3. Ore     - hashed candidate points inside a depth band grow small blobs, but
//      only ever overwrite stone, so ore never floats in a cave or sits in dirt.
//   4. Trees   - a low-frequency noise channel gives each x position a "forest
//      factor"; columns roll against it to grow an oak tree, spaced far enough
//      apart that no two canopies ever touch.
class TerrainGenerator
{
public:
    // Surface stays inside this band whatever the noise does.
    static constexpr int SURFACE_MIN = 80;
    static constexpr int SURFACE_MAX = 260;

    static constexpr int COPPER_MIN_Y = 200;
    static constexpr int COPPER_MAX_Y = 340;

    static constexpr int IRON_MIN_Y = 320;
    static constexpr int IRON_MAX_Y = 495;

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

    explicit TerrainGenerator(std::uint32_t seed);

    // Passes 1-4.
    void generate(World& world) const;

    // Passes 1-2 only: terrain with no ore in it. The ore pass is defined as
    // "stone becomes ore", and this is the world it is defined against.
    void generateBase(World& world) const;

    int surfaceHeight(int x) const;

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

    void placeTree(World& world, int trunkX, int surface, int height) const;

    std::uint32_t worldSeed;
};
