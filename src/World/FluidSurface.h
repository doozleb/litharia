#pragma once

class World;

// The fill fraction (0..1) at which to draw the fluid surface tile at (x, y).
// A "surface" tile is a fluid tile with no same-family fluid directly above it.
// The value is the volume-average level of the maximal contiguous run of
// same-family surface tiles through (x, y), so an entire run draws at one flat
// height even though the settled tile levels differ by up to one. The scan is
// bounded; a run longer than the cap falls back to the tile's own level (still
// within one level of flat). Returns 0 for a non-fluid tile.
float fluidSurfaceHeight(const World& world, int x, int y);
