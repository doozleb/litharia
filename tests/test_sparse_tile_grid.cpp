#include "doctest.h"

#include "Core/Constants.h"
#include "World/SparseTileGrid.h"

TEST_CASE("a freshly constructed grid reads 0 everywhere")
{
    SparseTileGrid grid;

    CHECK(grid.at(0, 0) == 0);
    CHECK(grid.at(500, 250) == 0);
}

TEST_CASE("set() makes at() return the value written, and leaves other cells at 0")
{
    SparseTileGrid grid;
    grid.clear();
    grid.set(10, 20, 7);

    CHECK(grid.at(10, 20) == 7);
    CHECK(grid.at(11, 20) == 0);
}

TEST_CASE("set() unconditionally overwrites, even to a lower value")
{
    SparseTileGrid grid;
    grid.clear();
    grid.set(10, 20, 7);
    grid.set(10, 20, 3);

    CHECK(grid.at(10, 20) == 3);
}

TEST_CASE("merge() raises the stored value to the max of everything merged in this round")
{
    SparseTileGrid grid;
    grid.clear();
    grid.merge(10, 20, 3);
    grid.merge(10, 20, 7);
    grid.merge(10, 20, 5);

    CHECK(grid.at(10, 20) == 7);
}

TEST_CASE("clear() invalidates every previously set or merged cell")
{
    SparseTileGrid grid;
    grid.clear();
    grid.set(10, 20, 7);
    grid.merge(30, 40, 5);

    grid.clear();

    CHECK(grid.at(10, 20) == 0);
    CHECK(grid.at(30, 40) == 0);
}

TEST_CASE("a second clear()+set() round does not leak values from the first round")
{
    SparseTileGrid grid;
    grid.clear();
    grid.set(10, 20, 7);

    grid.clear();
    grid.set(30, 40, 9);

    CHECK(grid.at(30, 40) == 9);
    CHECK(grid.at(10, 20) == 0);
}

TEST_CASE("a second clear()+merge() round does not leak values from the first round")
{
    SparseTileGrid grid;
    grid.clear();
    grid.merge(10, 20, 7);

    grid.clear();
    grid.merge(10, 20, 2);

    // Same coordinate as before, but a fresh round: must read as freshly
    // merged (2), not maxed against the stale prior-round value (7).
    CHECK(grid.at(10, 20) == 2);
}

TEST_CASE("at() returns 0 for coordinates outside world bounds")
{
    SparseTileGrid grid;
    grid.clear();
    grid.set(0, 0, 9);

    CHECK(grid.at(-1, 0) == 0);
    CHECK(grid.at(WORLD_WIDTH, 0) == 0);
    CHECK(grid.at(0, -1) == 0);
    CHECK(grid.at(0, WORLD_HEIGHT) == 0);
}
