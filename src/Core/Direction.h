#pragma once

#include <cstdint>

// A cardinal direction on the tile grid. Kept free of SFML so it can live in the
// core library and the test binary.
enum class Direction : std::uint8_t { Up, Down, Left, Right };

int dirDX(Direction d);
int dirDY(Direction d);

// The next direction clockwise. Used to rotate a machine before placing it.
Direction rotateCW(Direction d);

// The reverse of a direction: Up<->Down, Left<->Right. Used to turn "which way
// an item is travelling" into "which side of the tile it arrives at".
Direction oppositeDirection(Direction d);
