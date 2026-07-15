#include "doctest.h"

#include "Core/Direction.h"

TEST_CASE("direction deltas point the right way")
{
    CHECK(dirDX(Direction::Left)  == -1);
    CHECK(dirDX(Direction::Right) ==  1);
    CHECK(dirDY(Direction::Up)    == -1);
    CHECK(dirDY(Direction::Down)  ==  1);

    // The other axis is zero.
    CHECK(dirDY(Direction::Left)  == 0);
    CHECK(dirDX(Direction::Up)    == 0);
}

TEST_CASE("rotateCW cycles through all four and wraps")
{
    CHECK(rotateCW(Direction::Up)    == Direction::Right);
    CHECK(rotateCW(Direction::Right) == Direction::Down);
    CHECK(rotateCW(Direction::Down)  == Direction::Left);
    CHECK(rotateCW(Direction::Left)  == Direction::Up);
}
