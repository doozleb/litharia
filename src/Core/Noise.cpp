#include "Noise.h"

#include <cmath>

namespace
{

float smoothstep(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

int floorToInt(float v)
{
    return static_cast<int>(std::floor(v));
}

} // namespace

namespace noise
{

std::uint32_t hash(int x, int y, std::uint32_t seed)
{
    std::uint32_t h = seed;

    h ^= static_cast<std::uint32_t>(x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<std::uint32_t>(y) * 0xC2B2AE35u;
    h = (h << 17) | (h >> 15);

    h *= 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;

    return h;
}

float hashFloat(int x, int y, std::uint32_t seed)
{
    return static_cast<float>(hash(x, y, seed)) / 4294967296.0f;
}

float value1D(float x, std::uint32_t seed)
{
    const int x0 = floorToInt(x);
    const float t = smoothstep(x - static_cast<float>(x0));

    return lerp(hashFloat(x0, 0, seed), hashFloat(x0 + 1, 0, seed), t);
}

float value2D(float x, float y, std::uint32_t seed)
{
    const int x0 = floorToInt(x);
    const int y0 = floorToInt(y);

    const float tx = smoothstep(x - static_cast<float>(x0));
    const float ty = smoothstep(y - static_cast<float>(y0));

    const float top = lerp(hashFloat(x0, y0, seed), hashFloat(x0 + 1, y0, seed), tx);
    const float bottom = lerp(hashFloat(x0, y0 + 1, seed), hashFloat(x0 + 1, y0 + 1, seed), tx);

    return lerp(top, bottom, ty);
}

float fbm1D(float x, std::uint32_t seed, int octaves)
{
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; ++i)
    {
        // Each octave gets its own seed so the layers are independent, not shifted
        // copies of the same lattice.
        sum += amplitude * value1D(x * frequency, seed + static_cast<std::uint32_t>(i) * 7919u);

        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return (total > 0.0f) ? sum / total : 0.0f;
}

float fbm2D(float x, float y, std::uint32_t seed, int octaves)
{
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; ++i)
    {
        sum += amplitude * value2D(x * frequency,
                                   y * frequency,
                                   seed + static_cast<std::uint32_t>(i) * 7919u);

        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return (total > 0.0f) ? sum / total : 0.0f;
}

} // namespace noise
