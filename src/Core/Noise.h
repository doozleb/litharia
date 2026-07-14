#pragma once

#include <cstdint>

// Seeded, stateless, hash-based value noise. Every value is a pure function of its
// coordinates and the seed, which is what makes the generator reproducible.
namespace noise
{

std::uint32_t hash(int x, int y, std::uint32_t seed);

// Uniform in [0, 1).
float hashFloat(int x, int y, std::uint32_t seed);

// Smoothly interpolated lattice noise in [0, 1].
float value1D(float x, std::uint32_t seed);
float value2D(float x, float y, std::uint32_t seed);

// Fractal Brownian motion: octaves of value noise, each half the amplitude and
// twice the frequency of the last. Normalised back into [0, 1].
float fbm1D(float x, std::uint32_t seed, int octaves);
float fbm2D(float x, float y, std::uint32_t seed, int octaves);

} // namespace noise
