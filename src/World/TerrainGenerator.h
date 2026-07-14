#pragma once

#include <cstdint>

#include "../Blocks/Blocks.h"

class World;

// A pure function of the seed: the same seed always produces a byte-identical world.
//
// Three passes:
//   1. Surface - fractal noise over x gives a rolling height; grass, then a dirt
//      band, then stone all the way down.
//   2. Caves   - 2D fractal noise crossing a threshold carves air. The threshold
//      tightens near the surface so caves do not shred the landscape.
//   3. Ore     - hashed candidate points inside a depth band grow small blobs, but
//      only ever overwrite stone, so ore never floats in a cave or sits in dirt.
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

    explicit TerrainGenerator(std::uint32_t seed);

    // Passes 1-3.
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

    void growVein(World& world,
                  int centerX,
                  int centerY,
                  float radius,
                  BlockType ore,
                  int minY,
                  int maxY) const;

    std::uint32_t worldSeed;
};
