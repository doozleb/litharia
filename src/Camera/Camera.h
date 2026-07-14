#pragma once

#include <SFML/Graphics.hpp>

// Follows a target with smoothing and clamps to the world edges, so the view never
// scrolls off the world into empty space.
class Camera
{
public:
    explicit Camera(sf::Vector2f viewSize);

    void setViewSize(sf::Vector2f viewSize);

    void snapTo(sf::Vector2f target);
    void follow(sf::Vector2f target, float dt);

    // Move the view directly. Temporary: step 3 hands the camera to the player.
    void pan(sf::Vector2f delta);

    const sf::View& view() const { return sfView; }
    sf::Vector2f center() const { return sfView.getCenter(); }

private:
    void clampToWorld();

    sf::View sfView;
};
