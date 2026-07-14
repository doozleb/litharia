#include "Camera.h"

#include <algorithm>
#include <cmath>

#include "../Core/Constants.h"

namespace
{

constexpr float WORLD_PIXEL_WIDTH = static_cast<float>(WORLD_WIDTH * TILE_SIZE);
constexpr float WORLD_PIXEL_HEIGHT = static_cast<float>(WORLD_HEIGHT * TILE_SIZE);

// Fraction of the remaining distance covered per second. Frame-rate independent.
constexpr float SMOOTHING = 8.0f;

} // namespace

Camera::Camera(sf::Vector2f viewSize)
    : sfView(sf::FloatRect({0.0f, 0.0f}, viewSize))
{
    clampToWorld();
}

void Camera::setViewSize(sf::Vector2f viewSize)
{
    sfView.setSize(viewSize);
    clampToWorld();
}

void Camera::snapTo(sf::Vector2f target)
{
    sfView.setCenter(target);
    clampToWorld();
}

void Camera::follow(sf::Vector2f target, float dt)
{
    const sf::Vector2f current = sfView.getCenter();

    // Exponential smoothing: the same easing regardless of frame rate.
    const float t = 1.0f - std::exp(-SMOOTHING * dt);

    sfView.setCenter(current + (target - current) * t);
    clampToWorld();
}

void Camera::pan(sf::Vector2f delta)
{
    sfView.setCenter(sfView.getCenter() + delta);
    clampToWorld();
}

void Camera::clampToWorld()
{
    const sf::Vector2f size = sfView.getSize();
    sf::Vector2f center = sfView.getCenter();

    const float halfW = size.x * 0.5f;
    const float halfH = size.y * 0.5f;

    // If the view is wider than the world, centre it rather than clamping to a
    // range that runs backwards.
    if (size.x >= WORLD_PIXEL_WIDTH)
        center.x = WORLD_PIXEL_WIDTH * 0.5f;
    else
        center.x = std::clamp(center.x, halfW, WORLD_PIXEL_WIDTH - halfW);

    if (size.y >= WORLD_PIXEL_HEIGHT)
        center.y = WORLD_PIXEL_HEIGHT * 0.5f;
    else
        center.y = std::clamp(center.y, halfH, WORLD_PIXEL_HEIGHT - halfH);

    sfView.setCenter(center);
}
