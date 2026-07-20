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

TEST_CASE("skyLight is 0 everywhere before the sky pass exists")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 10) == 0);
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
