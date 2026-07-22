#pragma once

class World;

// A fluid surface run's horizontal bounds and shared fill height - see
// fluidSurfaceRunAt.
struct FluidSurfaceRun
{
    int left;     // inclusive world x of the run's left end
    int right;    // inclusive world x of the run's right end
    float height; // fill fraction (0..1) - same value fluidSurfaceHeight
                  // would return for any x in [left, right]
};

// Computes the surface run containing (x, y): the maximal contiguous run of
// same-family surface tiles through (x, y), and the volume-average level of
// that run as a fill fraction (0..1) - see fluidSurfaceHeight's own comment
// below for the full rules (submerged tiles, the solid/different-fluid stop
// condition, the scan cap). Returns bounds alongside the value so a caller
// iterating a row left-to-right (see ChunkRenderer::rebuild) can detect
// "still inside the run I already scanned" and skip recomputing it for
// every member tile, instead of paying the scan once per tile the way
// calling fluidSurfaceHeight independently per tile would. A tile that
// isn't itself a run member (non-fluid, submerged, or a run wider than the
// scan cap) returns a zero-width run - left == right == x - so a caller
// never mistakenly treats a non-cacheable result as reusable for a
// neighboring tile.
FluidSurfaceRun fluidSurfaceRunAt(const World& world, int x, int y);

// The fill fraction (0..1) at which to draw the fluid surface tile at (x, y).
// A "surface" tile is a fluid tile with no same-family fluid directly above it.
// The value is the volume-average level of the maximal contiguous run of
// same-family surface tiles through (x, y), so an entire run draws at one flat
// height even though the settled tile levels differ by up to one. The scan is
// bounded; a run longer than the cap falls back to the tile's own level (still
// within one level of flat). Returns 0 for a non-fluid tile.
float fluidSurfaceHeight(const World& world, int x, int y);
