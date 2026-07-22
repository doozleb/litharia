#include "FluidSurface.h"

#include "../Blocks/Blocks.h"
#include "../Core/Constants.h"
#include "World.h"

namespace
{

// Cap on how far a single surface run is scanned, so one rebuild of a very wide
// body stays cheap. Beyond it, a tile just uses its own level.
constexpr int MAX_RUN_SCAN = 128;

} // namespace

FluidSurfaceRun fluidSurfaceRunAt(const World& world, int x, int y)
{
    const BlockType here = world.get(x, y);

    if (!isFluid(here))
        return {x, x, 0.0f};

    const bool water = isWater(here);

    // A run member is a same-family fluid tile that is itself a surface tile
    // (no same-family fluid directly above it).
    auto isRunMember = [&](int cx) {
        const BlockType t = world.get(cx, y);
        const bool sameFamily = water ? isWater(t) : isLava(t);
        if (!sameFamily)
            return false;

        const BlockType above = world.get(cx, y - 1);
        const bool submerged = water ? isWater(above) : isLava(above);
        return !submerged;
    };

    if (!isRunMember(x))
        return {x, x, fluidLevel(here) / 8.0f};

    int xL = x;
    int xR = x;
    int scanned = 1;

    while (xL - 1 >= 0 && scanned < MAX_RUN_SCAN && isRunMember(xL - 1))
    {
        --xL;
        ++scanned;
    }
    while (xR + 1 < WORLD_WIDTH && scanned < MAX_RUN_SCAN && isRunMember(xR + 1))
    {
        ++xR;
        ++scanned;
    }

    if (scanned >= MAX_RUN_SCAN)
        return {x, x, fluidLevel(here) / 8.0f};

    int total = 0;
    for (int cx = xL; cx <= xR; ++cx)
        total += fluidLevel(world.get(cx, y));

    const int n = xR - xL + 1;
    return {xL, xR, (static_cast<float>(total) / n) / 8.0f};
}

float fluidSurfaceHeight(const World& world, int x, int y)
{
    return fluidSurfaceRunAt(world, x, y).height;
}
