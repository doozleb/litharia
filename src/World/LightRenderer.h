#pragma once

#include <SFML/Graphics.hpp>

#include <utility>
#include <vector>

#include "Lighting.h"

class World;

// Draws a color overlay on top of everything else in the world, applied with
// sf::BlendMultiply so it darkens/tints whatever was already drawn
// underneath without needing every entity to know about lighting itself.
// Every visible tile - solid or open - gets a brightness and a tint:
//
// - An open tile's brightness/tint comes straight from its own stored
//   sky/torch/lava levels (plus the player's held-Torch light, folded into
//   the torch channel).
// - A solid tile has no stored light of its own (Lighting's BFS never
//   visits solid tiles), so it borrows one step dimmer than the brightest
//   value among its 4 orthogonal open neighbours, per channel - this is
//   what makes a torch-lit tunnel's walls actually look lit, instead of
//   either pitch black (the pre-existing bug) or always full-bright
//   regardless of nearby light (the bug this class fixes).
// - If neither of the above puts any real light on a tile, it falls back to
//   the ambient-outline floor (see Lighting::ambientOutline): a flat, dim,
//   uncolored "you can make out shapes and ore nearby" brightness. Real
//   light always wins over the ambient floor when both apply.
// - A tile with neither real light nor ambient-outline coverage renders
//   fully black - hidden, exactly like a disconnected cave already read
//   before this class existed, now correctly extended to solid ground too.
//
// Tints: lava is a hot red-orange, torch a warmer yellow (distinct from
// lava, unlike before), sky a cool-to-white gradient driven by the day/night
// clock. Where more than one real channel is lit, the tint is a weighted
// blend of all three rather than a single "whichever's highest wins"
// switch.
//
// Rebuilds every visible tile fresh every single frame rather than caching
// per-chunk geometry, like ChunkRenderer does: the day/night clock, the
// player's held-Torch light, and the player's own position (ambientOutline)
// all change continuously, so there is no "clean, skip it" case to cache
// against.
class LightRenderer
{
public:
    void draw(sf::RenderTarget& target, const sf::View& view, const World& world, const Lighting& lighting,
              float daylightFactor, const std::vector<std::pair<sf::Vector2i, int>>& heldTorchLight,
              const std::vector<std::pair<sf::Vector2i, int>>& ambientOutline) const;
};
