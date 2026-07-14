#pragma once

#include <cstdint>

class World;

// A pure function of the seed: the same seed always produces a byte-identical
// world. Step 2 replaces the placeholder bands below with noise, caves and ore
// veins; the interface does not change.
class TerrainGenerator
{
public:
    explicit TerrainGenerator(std::uint32_t seed);

    void generate(World& world) const;

    std::uint32_t seed() const { return worldSeed; }

private:
    std::uint32_t worldSeed;
};
