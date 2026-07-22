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

TEST_CASE("MAX_LIGHT_LEVEL is 9")
{
    CHECK(Lighting::MAX_LIGHT_LEVEL == 9);
}

TEST_CASE("TORCH_LIGHT_LEVEL is 15")
{
    CHECK(Lighting::TORCH_LIGHT_LEVEL == 15);
}

TEST_CASE("a lone Lava tile lights itself on the lava channel only, decaying by 1 per step")
{
    World world;
    fillSolid(world);
    world.set(9, 10, BlockType::Air);
    world.set(11, 10, BlockType::Air);
    world.set(10, 10, BlockType::Lava8);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.lavaLight(10, 10) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.lavaLight(9, 10) == Lighting::MAX_LIGHT_LEVEL - 1);
    CHECK(lighting.lavaLight(11, 10) == Lighting::MAX_LIGHT_LEVEL - 1);

    CHECK(lighting.torchLight(10, 10) == 0);
}

TEST_CASE("lava light is blocked entirely by a solid tile")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Lava8);
    // (11, 10) stays Stone: solid, so light cannot pass through or into it.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.lavaLight(11, 10) == 0);
}

TEST_CASE("a placed Torch lights the torch channel only, decaying by 1 per step")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(10, 10) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(lighting.torchLight(9, 10) == Lighting::TORCH_LIGHT_LEVEL - 1);

    CHECK(lighting.lavaLight(10, 10) == 0);
}

TEST_CASE("a placed Torch reads brighter than MAX_LIGHT_LEVEL, unlike Lava or sky")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(10, 10) > Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.torchLight(10, 10) == Lighting::TORCH_LIGHT_LEVEL);
}

TEST_CASE("torch light is blocked entirely by a solid tile")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    // (11, 10) stays Stone: solid, unreachable.

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(11, 10) == 0);
}

TEST_CASE("a tile lit by both a Torch and Lava reads a nonzero level on each independent channel")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(11, 10, BlockType::Air);
    world.set(11, 11, BlockType::Lava8);

    Machines machines;
    machines.place(MachineType::Torch, 9, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(10, 10) > 0);
    CHECK(lighting.lavaLight(10, 10) > 0);
}

TEST_CASE("an open vertical shaft stays sky-lit at the max level at every depth")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 50; ++y)
        world.set(10, y, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 0) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.skyLight(10, 25) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.skyLight(10, 50) == Lighting::MAX_LIGHT_LEVEL);
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

    CHECK(lighting.skyLight(10, 20) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.skyLight(11, 20) == Lighting::MAX_LIGHT_LEVEL - 1);
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
    // this column and wrongly seed this tile directly at the max level,
    // since it has no path back to the open shaft above and no other
    // reachable source, so anything other than 0 here proves the scan is
    // over-seeding.
    world.set(10, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 4) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.skyLight(10, 5) == 0);  // solid: never lit
    CHECK(lighting.skyLight(10, 10) == 0); // isolated pocket: no seed, no path
}

TEST_CASE("torchLight, lavaLight, and skyLight are 0 out of bounds")
{
    World world;
    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(-1, 0) == 0);
    CHECK(lighting.torchLight(WORLD_WIDTH, 0) == 0);
    CHECK(lighting.lavaLight(-1, 0) == 0);
    CHECK(lighting.lavaLight(WORLD_WIDTH, 0) == 0);
    CHECK(lighting.skyLight(0, -1) == 0);
    CHECK(lighting.skyLight(0, WORLD_HEIGHT) == 0);
}

TEST_CASE("heldTorchLight lights its source at TORCH_LIGHT_LEVEL and decays by 1 per step")
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

    CHECK(levelAt(10, 10) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(levelAt(9, 10) == Lighting::TORCH_LIGHT_LEVEL - 1);
    CHECK(levelAt(8, 10) == Lighting::TORCH_LIGHT_LEVEL - 2);
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
    CHECK(lighting.torchLight(10, 10) == 0);
    CHECK(lighting.torchLight(9, 10) == 0);
}

TEST_CASE("ambientOutline marks a solid tile bordering the player's reachable open space")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    // (11, 10) stays Stone: the wall immediately beside the player.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(11, 10) == Lighting::AMBIENT_OUTLINE_LEVEL);
}

TEST_CASE("ambientOutline marks an ore tile brighter than a plain stone tile at the same distance")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(10, 9, BlockType::Air);
    world.set(11, 9, BlockType::CopperOre);
    // (8, 10) stays Stone - a plain neighbor at the same one-step distance.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(11, 9) == Lighting::AMBIENT_OUTLINE_ORE_LEVEL);
    CHECK(levelAt(8, 10) == Lighting::AMBIENT_OUTLINE_LEVEL);
    CHECK(Lighting::AMBIENT_OUTLINE_ORE_LEVEL > Lighting::AMBIENT_OUTLINE_LEVEL);
}

TEST_CASE("ambientOutline reveals open tiles within reach even with no light, but not disconnected ones")
{
    World world;
    fillSolid(world);
    for (int x = 10; x <= 15; ++x)
        world.set(x, 10, BlockType::Air);
    // A sealed pocket, walled off on every side - no path back to the player.
    world.set(20, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(15, 10) == Lighting::AMBIENT_OUTLINE_LEVEL);
    CHECK(levelAt(20, 10) == 0);
}

TEST_CASE("ambientOutline does not reach past AMBIENT_OUTLINE_RADIUS steps from the player")
{
    World world;
    fillSolid(world);
    for (int x = 10; x <= 10 + Lighting::AMBIENT_OUTLINE_RADIUS + 5; ++x)
        world.set(x, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(10 + Lighting::AMBIENT_OUTLINE_RADIUS - 1, 10) == Lighting::AMBIENT_OUTLINE_LEVEL);
    CHECK(levelAt(10 + Lighting::AMBIENT_OUTLINE_RADIUS + 5, 10) == 0);
}

TEST_CASE("ambientOutline never writes to the stored grid")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    lighting.ambientOutline(world, {10, 10});

    CHECK(lighting.torchLight(10, 10) == 0);
    CHECK(lighting.lavaLight(10, 10) == 0);
    CHECK(lighting.skyLight(10, 10) == 0);
}

TEST_CASE("ambientOutline accepts a custom radius that reaches further than the default")
{
    World world;
    fillSolid(world);
    for (int x = 10; x <= 10 + Lighting::AMBIENT_OUTLINE_RADIUS + 10; ++x)
        world.set(x, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10}, Lighting::AMBIENT_OUTLINE_RADIUS + 10);

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(10 + Lighting::AMBIENT_OUTLINE_RADIUS + 9, 10) == Lighting::AMBIENT_OUTLINE_LEVEL);
}

TEST_CASE("ambientOutline still defaults to AMBIENT_OUTLINE_RADIUS when no radius argument is given")
{
    World world;
    fillSolid(world);
    for (int x = 10; x <= 10 + Lighting::AMBIENT_OUTLINE_RADIUS + 5; ++x)
        world.set(x, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10}); // no third argument

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(10 + Lighting::AMBIENT_OUTLINE_RADIUS + 5, 10) == 0); // still capped at the default
}

TEST_CASE("floodFill's persistent scratch does not leak stale state between successive recomputeAll calls")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(11, 10, BlockType::Air);

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(10, 10) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(lighting.torchLight(9, 10) == Lighting::TORCH_LIGHT_LEVEL - 1);

    // A second call on the same Lighting instance (reusing its persistent
    // scratch buffers), with the Torch moved to an entirely different,
    // previously-untouched part of the grid.
    world.set(10, 10, BlockType::Stone);
    world.set(9, 10, BlockType::Stone);
    world.set(11, 10, BlockType::Stone);
    world.set(30, 30, BlockType::Air);
    world.set(29, 30, BlockType::Air);

    Machines machines2;
    machines2.place(MachineType::Torch, 30, 30, Direction::Right);

    lighting.recomputeAll(world, machines2);

    CHECK(lighting.torchLight(30, 30) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(lighting.torchLight(29, 30) == Lighting::TORCH_LIGHT_LEVEL - 1);
    // The old Torch's tile is Stone now and was never a light source this
    // call - if stale scratch state from the first call leaked through,
    // this is the value most likely to read wrong.
    CHECK(lighting.torchLight(10, 10) == 0);
}

TEST_CASE("heldTorchLight's persistent scratch does not leak between successive calls at different sources")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(30, 30, BlockType::Air);
    world.set(29, 30, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    lighting.heldTorchLight(world, {10, 10});
    const auto second = lighting.heldTorchLight(world, {30, 30});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : second)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(30, 30) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(levelAt(29, 30) == Lighting::TORCH_LIGHT_LEVEL - 1);
    // The second call's result must not still contain the first call's
    // source tile.
    CHECK(levelAt(10, 10) == 0);
}
