#pragma once

#include <cstdint>

// A cardinal direction on the tile grid. Kept free of SFML so it can live in the
// core library and the test binary.
enum class Direction : std::uint8_t { Up, Down, Left, Right };

int dirDX(Direction d);
int dirDY(Direction d);

// The next direction clockwise. Used to rotate a machine before placing it.
Direction rotateCW(Direction d);
