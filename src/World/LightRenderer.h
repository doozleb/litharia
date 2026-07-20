#pragma once

#include <SFML/Graphics.hpp>

#include <utility>
#include <vector>

#include "Lighting.h"

class World;

// Draws a screen-darkening overlay on top of everything else in the world:
// solid dark where an open tile has no sky or block light reaching it,
// tinted warm where a Torch/Lava tile dominates and cool-toward-white where
// daylight dominates. Drawn with sf::BlendMultiply so it darkens whatever
// was already drawn underneath without needing every entity to know about
// lighting itself.
//
// Solid tiles (unexcavated ground - dirt, stone, ore, anything the player
// hasn't dug into) are skipped entirely, rendering at their normal,
// undarkened color: they are terrain, always visible, not a "space" whose
// visibility the lighting engine is meant to gate. Darkness is reserved for
// actual open air - caves, dug tunnels, the sky - which is the only thing a
// disconnected pocket needs to hide.
//
// Unlike ChunkRenderer, this rebuilds every visible tile fresh every single
// frame rather than caching per-chunk geometry: the day/night clock and the
// player's held-Torch light both change continuously, so there is no
// "clean, skip it" case to cache against - a persistent chunk grid would
// only add bookkeeping with nothing to skip.
class LightRenderer
{
public:
    void draw(sf::RenderTarget& target, const sf::View& view, const World& world, const Lighting& lighting,
              float daylightFactor, const std::vector<std::pair<sf::Vector2i, int>>& heldTorchLight) const;
};
