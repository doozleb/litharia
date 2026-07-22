#include "Chunks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../Blocks/Blocks.h"
#include "../Core/Constants.h"
#include "FluidSurface.h"
#include "World.h"

namespace
{

constexpr int CHUNK_PIXELS = CHUNK_SIZE * TILE_SIZE;

// Fluid renders a bit see-through rather than fully opaque, both as a look in
// its own right and so a decoration (tree log/leaves) underneath a flooded
// tile is still visible through it.
constexpr std::uint8_t FLUID_ALPHA = 200;

int chunksAcross(int tiles)
{
    return (tiles + CHUNK_SIZE - 1) / CHUNK_SIZE;
}

sf::Color toColor(BlockColor c, std::uint8_t alpha = 255)
{
    return sf::Color(c.r, c.g, c.b, alpha);
}

// Two triangles per tile: SFML 3 has no quad primitive.
void appendQuad(sf::VertexArray& vertices, float left, float top, float right, float bottom,
                 sf::Color color)
{
    vertices.append({{left, top}, color});
    vertices.append({{right, top}, color});
    vertices.append({{right, bottom}, color});

    vertices.append({{left, top}, color});
    vertices.append({{right, bottom}, color});
    vertices.append({{left, bottom}, color});
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
        // One cached fluid surface run per row, reused across every tile
        // that falls within it - see FluidSurfaceRun's own comment for why:
        // fluidSurfaceRunAt's scan is only paid once per run instead of
        // once per member tile. right < left means "nothing cached yet",
        // which is guaranteed to miss on the first candidate tile of the
        // row (x >= startX >= 0 > -1 == cachedRun.right).
        FluidSurfaceRun cachedRun{0, -1, 0.0f};

        for (int x = startX; x < endX; ++x)
        {
            const BlockType type = world.get(x, y);
            const BlockType decoration = world.getDecoration(x, y);

            const float left = static_cast<float>(x * TILE_SIZE);
            const float right = left + TILE_SIZE;
            const float bottom = static_cast<float>((y + 1) * TILE_SIZE);
            const float top = static_cast<float>(y * TILE_SIZE);

            // Decoration draws first (full tile, opaque) so terrain drawn
            // after it - in particular translucent fluid - blends on top.
            if (decoration != BlockType::Air)
                appendQuad(chunk.vertices, left, top, right, bottom, toColor(blockInfo(decoration).color));

            if (type == BlockType::Air)
                continue;

            float fluidTop = top;

            // A fluid surface tile - one with no fluid directly above it - is
            // drawn only as full as its level: liquid fills the tile from the
            // bottom up, so a level-1 tile is a 1/8-height sliver and a level-8
            // tile fills the whole block. Submerged fluid (fluid above it) stays
            // full, so only the very top of a pool shows a partial surface.
            if (isFluid(type) && !isFluid(world.get(x, y - 1)))
            {
                // A run is a maximal contiguous stretch, and this loop visits
                // x in strictly increasing order, so a miss here can only
                // mean "new run" (or a lone/capped tile) - never a stale
                // partial overlap with the previous cached run.
                if (x < cachedRun.left || x > cachedRun.right)
                    cachedRun = fluidSurfaceRunAt(world, x, y);

                const float fillHeight = TILE_SIZE * cachedRun.height;
                fluidTop = bottom - fillHeight;
            }

            const sf::Color color =
                toColor(blockInfo(type).color, isFluid(type) ? FLUID_ALPHA : std::uint8_t{255});

            appendQuad(chunk.vertices, left, fluidTop, right, bottom, color);
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
