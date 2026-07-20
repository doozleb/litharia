#include "doctest.h"

#include "Core/Constants.h"
#include "Core/Direction.h"
#include "Machines/Machines.h"
#include "World/Lighting.h"
#include "World/World.h"

namespace
{

void fillSolid(World& world)
{
    world.fill(BlockType::Stone);
}

} // namespace

TEST_CASE("a lone Lava tile lights itself and decays by 1 per orthogonal step")
{
    World world;
    fillSolid(world);
    world.set(9, 10, BlockType::Air);
    world.set(11, 10, BlockType::Air);
    world.set(10, 10, BlockType::Lava8);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.blockLight(10, 10) == 8);
    CHECK(lighting.blockLight(9, 10) == 7);
    CHECK(lighting.blockLight(11, 10) == 7);
}

TEST_CASE("block light is blocked entirely by a solid tile")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Lava8);
    // (11, 10) stays Stone: solid, so light cannot pass through or into it.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.blockLight(11, 10) == 0);
}

TEST_CASE("a placed Torch is a level-8 block light source")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.blockLight(10, 10) == 8);
    CHECK(lighting.blockLight(9, 10) == 7);
}

TEST_CASE("an open vertical shaft stays sky-lit at level 8 at every depth")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 50; ++y)
        world.set(10, y, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 0) == 8);
    CHECK(lighting.skyLight(10, 25) == 8);
    CHECK(lighting.skyLight(10, 50) == 8);
}

TEST_CASE("sky light decays by 1 per step spreading sideways from an open shaft")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 20; ++y)
        world.set(10, y, BlockType::Air);
    world.set(11, 20, BlockType::Air); // one step sideways off the shaft, same depth

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 20) == 8);
    CHECK(lighting.skyLight(11, 20) == 7);
}

TEST_CASE("a cave with no path to an open shaft reads sky 0, even directly beside a lit one")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 20; ++y)
        world.set(10, y, BlockType::Air);
    // A sealed pocket, walled off on every side by Stone - no orthogonal
    // path back to the shaft exists (the tile between them stays solid).
    world.set(12, 20, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(12, 20) == 0);
}

TEST_CASE("a column blocked from the surface gets no direct sky seed of its own")
{
    World world;
    fillSolid(world);
    // Open only near the very top; solid resumes at y=5 and stays solid the
    // rest of the way down, with no connection to any other open tile.
    world.set(10, 0, BlockType::Air);
    world.set(10, 1, BlockType::Air);
    world.set(10, 2, BlockType::Air);
    world.set(10, 3, BlockType::Air);
    world.set(10, 4, BlockType::Air);
    // (10, 5) onward stays Stone.

    // An isolated open tile further down the same column, walled off on
    // every side - this is what actually distinguishes a correct top-down
    // scan that stops (break) at the first solid tile from a buggy one that
    // skips past it (continue): a `continue` bug would keep scanning down
    // this column and wrongly seed this tile directly at level 8, since it
    // has no path back to the open shaft above and no other reachable
    // source, so anything other than 0 here proves the scan is over-seeding.
    world.set(10, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 4) == 8);
    CHECK(lighting.skyLight(10, 5) == 0);  // solid: never lit
    CHECK(lighting.skyLight(10, 10) == 0); // isolated pocket: no seed, no path
}

TEST_CASE("blockLight and skyLight are 0 out of bounds")
{
    World world;
    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.blockLight(-1, 0) == 0);
    CHECK(lighting.blockLight(WORLD_WIDTH, 0) == 0);
    CHECK(lighting.skyLight(0, -1) == 0);
    CHECK(lighting.skyLight(0, WORLD_HEIGHT) == 0);
}

TEST_CASE("heldTorchLight lights its source at level 8 and decays by 1 per step")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(8, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.heldTorchLight(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(10, 10) == 8);
    CHECK(levelAt(9, 10) == 7);
    CHECK(levelAt(8, 10) == 6);
}

TEST_CASE("heldTorchLight is blocked by solid tiles, same as a placed Torch")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    // (11, 10) stays Stone: solid, unreachable.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.heldTorchLight(world, {10, 10});

    for (const auto& [tile, level] : result)
    {
        if (tile.x == 11 && tile.y == 10)
            CHECK(false);
    }
}

TEST_CASE("heldTorchLight never writes to the stored grid")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    lighting.heldTorchLight(world, {10, 10});

    // No Torch or Lava anywhere in this world, so the stored grid must still
    // read 0 - heldTorchLight is a pure query, not a mutation.
    CHECK(lighting.blockLight(10, 10) == 0);
    CHECK(lighting.blockLight(9, 10) == 0);
}
