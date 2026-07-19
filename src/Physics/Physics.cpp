#include "Physics.h"

#include <algorithm>
#include <cmath>

#include "../Core/Constants.h"
#include "../World/World.h"

namespace
{

// A box flush against a tile edge must not count as overlapping the tile beyond it.
constexpr float EPSILON = 0.001f;

// No substep may carry the box further than half a tile, which is what makes
// tunneling through a one-tile wall impossible regardless of speed.
constexpr float MAX_STEP = TILE_SIZE * 0.5f;

int tileOf(float pixels)
{
    return static_cast<int>(std::floor(pixels / TILE_SIZE));
}

// Solid tiles overlapped by the box, as a tile-index range.
void tileRange(const AABB& box, int& x0, int& x1, int& y0, int& y1)
{
    x0 = tileOf(box.left());
    x1 = tileOf(box.right() - EPSILON);
    y0 = tileOf(box.top());
    y1 = tileOf(box.bottom() - EPSILON);
}

// Returns true if the move was blocked, having pushed the box out of the tile.
bool stepX(AABB& box, float dx, const World& world)
{
    if (dx == 0.0f)
        return false;

    box.position.x += dx;

    int x0, x1, y0, y1;
    tileRange(box, x0, x1, y0, y1);

    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            if (!world.isSolid(x, y))
                continue;

            if (dx > 0.0f)
                box.position.x = static_cast<float>(x * TILE_SIZE) - box.size.x;
            else
                box.position.x = static_cast<float>((x + 1) * TILE_SIZE);

            return true;
        }
    }

    return false;
}

// Returns true if blocked. `fromBelow` reports whether the correction came from
// under the box, which is the only thing that counts as landing.
bool stepY(AABB& box, float dy, const World& world, bool& fromBelow)
{
    fromBelow = false;

    if (dy == 0.0f)
        return false;

    box.position.y += dy;

    int x0, x1, y0, y1;
    tileRange(box, x0, x1, y0, y1);

    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            if (!world.isSolid(x, y))
                continue;

            if (dy > 0.0f)
            {
                box.position.y = static_cast<float>(y * TILE_SIZE) - box.size.y;
                fromBelow = true;
            }
            else
            {
                box.position.y = static_cast<float>((y + 1) * TILE_SIZE);
            }

            return true;
        }
    }

    return false;
}

} // namespace

namespace physics
{

bool overlapsSolid(const AABB& box, const World& world)
{
    int x0, x1, y0, y1;
    tileRange(box, x0, x1, y0, y1);

    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (world.isSolid(x, y))
                return true;

    return false;
}

bool overlapsFluid(const AABB& box, const World& world)
{
    int x0, x1, y0, y1;
    tileRange(box, x0, x1, y0, y1);

    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (isFluid(world.get(x, y)))
                return true;

    return false;
}

bool overlaps(const AABB& a, const AABB& b)
{
    return a.left() < b.right() && a.right() > b.left() && a.top() < b.bottom() &&
           a.bottom() > b.top();
}

CollisionResult moveAndCollide(AABB& box, sf::Vector2f& velocity, const World& world, float dt)
{
    CollisionResult result;

    const sf::Vector2f delta = velocity * dt;

    const float longest = std::max(std::abs(delta.x), std::abs(delta.y));
    const int steps = std::max(1, static_cast<int>(std::ceil(longest / MAX_STEP)));

    const float stepX_ = delta.x / static_cast<float>(steps);
    const float stepY_ = delta.y / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i)
    {
        // One axis at a time: X, push out, zero X; then Y, push out, zero Y.
        if (!result.hitX && stepX(box, stepX_, world))
        {
            result.hitX = true;
            velocity.x = 0.0f;
        }

        if (!result.hitY)
        {
            bool fromBelow = false;

            if (stepY(box, stepY_, world, fromBelow))
            {
                result.hitY = true;
                result.grounded = fromBelow;
                velocity.y = 0.0f;
            }
        }

        // Both axes are done; no point stepping further.
        if (result.hitX && result.hitY)
            break;
    }

    return result;
}

} // namespace physics
