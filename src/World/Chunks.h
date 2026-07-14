#pragma once

#include <SFML/Graphics.hpp>

#include <vector>

class World;

// Bakes each 32x32 tile chunk into one vertex array, rebuilds a chunk only when a
// block inside it changes, and draws only the chunks the view can see.
class ChunkRenderer
{
public:
    explicit ChunkRenderer(const World& world);

    // Call after any block change so the owning chunk is rebuilt before the next draw.
    void markDirty(int tileX, int tileY);
    void markAllDirty();

    void draw(sf::RenderTarget& target, const sf::View& view);

    // Chunks actually submitted last frame - makes the culling observable.
    int lastDrawnChunks() const { return drawnChunks; }
    int chunkCount() const { return chunksX * chunksY; }

private:
    struct Chunk
    {
        sf::VertexArray vertices{sf::PrimitiveType::Triangles};
        bool dirty = true;
    };

    void rebuild(Chunk& chunk, int chunkX, int chunkY) const;

    const World& world;

    int chunksX;
    int chunksY;

    std::vector<Chunk> chunks;

    int drawnChunks = 0;
};
