#include "Chunks.h"

#include <algorithm>
#include <cmath>

#include "../Blocks/Blocks.h"
#include "../Core/Constants.h"
#include "World.h"

namespace
{

constexpr int CHUNK_PIXELS = CHUNK_SIZE * TILE_SIZE;

int chunksAcross(int tiles)
{
    return (tiles + CHUNK_SIZE - 1) / CHUNK_SIZE;
}

sf::Color toColor(BlockColor c)
{
    return sf::Color(c.r, c.g, c.b);
}

} // namespace

ChunkRenderer::ChunkRenderer(const World& world)
    : world(world)
    , chunksX(chunksAcross(WORLD_WIDTH))
    , chunksY(chunksAcross(WORLD_HEIGHT))
    , chunks(static_cast<std::size_t>(chunksAcross(WORLD_WIDTH)) * chunksAcross(WORLD_HEIGHT))
{
}

void ChunkRenderer::markDirty(int tileX, int tileY)
{
    const int cx = tileX / CHUNK_SIZE;
    const int cy = tileY / CHUNK_SIZE;

    if (tileX < 0 || tileY < 0 || cx >= chunksX || cy >= chunksY)
        return;

    chunks[static_cast<std::size_t>(cy) * chunksX + cx].dirty = true;
}

void ChunkRenderer::markAllDirty()
{
    for (Chunk& chunk : chunks)
        chunk.dirty = true;
}

void ChunkRenderer::rebuild(Chunk& chunk, int chunkX, int chunkY) const
{
    chunk.vertices.clear();

    const int startX = chunkX * CHUNK_SIZE;
    const int startY = chunkY * CHUNK_SIZE;

    const int endX = std::min(startX + CHUNK_SIZE, WORLD_WIDTH);
    const int endY = std::min(startY + CHUNK_SIZE, WORLD_HEIGHT);

    for (int y = startY; y < endY; ++y)
    {
        for (int x = startX; x < endX; ++x)
        {
            const BlockType type = world.get(x, y);

            if (type == BlockType::Air)
                continue;

            const sf::Color color = toColor(blockInfo(type).color);

            const float left = static_cast<float>(x * TILE_SIZE);
            const float right = left + TILE_SIZE;
            const float bottom = static_cast<float>((y + 1) * TILE_SIZE);
            float top = static_cast<float>(y * TILE_SIZE);

            // A fluid surface tile - one with no fluid directly above it - is
            // drawn only as full as its level: liquid fills the tile from the
            // bottom up, so a level-1 tile is a 1/8-height sliver and a level-8
            // tile fills the whole block. Submerged fluid (fluid above it) stays
            // full, so only the very top of a pool shows a partial surface.
            if (isFluid(type) && !isFluid(world.get(x, y - 1)))
            {
                const float fillHeight = TILE_SIZE * (fluidLevel(type) / 8.0f);
                top = bottom - fillHeight;
            }

            // Two triangles per tile: SFML 3 has no quad primitive.
            chunk.vertices.append({{left, top}, color});
            chunk.vertices.append({{right, top}, color});
            chunk.vertices.append({{right, bottom}, color});

            chunk.vertices.append({{left, top}, color});
            chunk.vertices.append({{right, bottom}, color});
            chunk.vertices.append({{left, bottom}, color});
        }
    }

    chunk.dirty = false;
}

void ChunkRenderer::draw(sf::RenderTarget& target, const sf::View& view)
{
    const sf::Vector2f center = view.getCenter();
    const sf::Vector2f size = view.getSize();

    const float viewLeft = center.x - size.x * 0.5f;
    const float viewTop = center.y - size.y * 0.5f;
    const float viewRight = center.x + size.x * 0.5f;
    const float viewBottom = center.y + size.y * 0.5f;

    // One chunk of slack on each side so a chunk part-way onscreen is never dropped.
    const int firstX = std::max(0, static_cast<int>(std::floor(viewLeft / CHUNK_PIXELS)));
    const int firstY = std::max(0, static_cast<int>(std::floor(viewTop / CHUNK_PIXELS)));
    const int lastX = std::min(chunksX - 1, static_cast<int>(std::floor(viewRight / CHUNK_PIXELS)));
    const int lastY = std::min(chunksY - 1, static_cast<int>(std::floor(viewBottom / CHUNK_PIXELS)));

    drawnChunks = 0;

    for (int cy = firstY; cy <= lastY; ++cy)
    {
        for (int cx = firstX; cx <= lastX; ++cx)
        {
            Chunk& chunk = chunks[static_cast<std::size_t>(cy) * chunksX + cx];

            if (chunk.dirty)
                rebuild(chunk, cx, cy);

            if (chunk.vertices.getVertexCount() == 0)
                continue;

            target.draw(chunk.vertices);
            ++drawnChunks;
        }
    }
}
