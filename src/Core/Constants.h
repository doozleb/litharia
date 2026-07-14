#pragma once

// The world is finite and fully resident in memory. Chunks batch rendering and
// cull offscreen tiles; they are not a streaming mechanism.

inline constexpr int TILE_SIZE = 16;

inline constexpr int WORLD_WIDTH = 1000;
inline constexpr int WORLD_HEIGHT = 500;

inline constexpr int CHUNK_SIZE = 32;
