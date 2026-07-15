#include "Direction.h"

int dirDX(Direction d)
{
    switch (d)
    {
        case Direction::Left:  return -1;
        case Direction::Right: return  1;
        default:               return  0;
    }
}

int dirDY(Direction d)
{
    switch (d)
    {
        case Direction::Up:   return -1;
        case Direction::Down: return  1;
        default:              return  0;
    }
}

Direction rotateCW(Direction d)
{
    switch (d)
    {
        case Direction::Up:    return Direction::Right;
        case Direction::Right: return Direction::Down;
        case Direction::Down:  return Direction::Left;
        case Direction::Left:  return Direction::Up;
    }
    return Direction::Up;
}
