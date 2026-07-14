#include "doctest.h"

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "World/World.h"

TEST_CASE("get/set round-trips inside the world")
{
    World world;

    world.set(10, 20, BlockType::Stone);
    world.set(0, 0, BlockType::Grass);
    world.set(WORLD_WIDTH - 1, WORLD_HEIGHT - 1, BlockType::CopperOre);

    CHECK(world.get(10, 20) == BlockType::Stone);
    CHECK(world.get(0, 0) == BlockType::Grass);
    CHECK(world.get(WORLD_WIDTH - 1, WORLD_HEIGHT - 1) == BlockType::CopperOre);
}

TEST_CASE("a fresh world is entirely air")
{
    const World world;

    CHECK(world.get(0, 0) == BlockType::Air);
    CHECK(world.get(500, 250) == BlockType::Air);
    CHECK(world.get(WORLD_WIDTH - 1, WORLD_HEIGHT - 1) == BlockType::Air);
}

TEST_CASE("out-of-bounds reads return Air rather than misbehaving")
{
    World world;
    world.set(0, 0, BlockType::Stone);

    CHECK(world.get(-1, 0) == BlockType::Air);
    CHECK(world.get(0, -1) == BlockType::Air);
    CHECK(world.get(WORLD_WIDTH, 0) == BlockType::Air);
    CHECK(world.get(0, WORLD_HEIGHT) == BlockType::Air);
    CHECK(world.get(-99999, 99999) == BlockType::Air);
}

TEST_CASE("out-of-bounds writes are ignored, not clamped into a real tile")
{
    World world;

    world.set(-1, 5, BlockType::Stone);
    world.set(WORLD_WIDTH, 5, BlockType::Stone);
    world.set(5, -1, BlockType::Stone);
    world.set(5, WORLD_HEIGHT, BlockType::Stone);

    // Nothing leaked into the edges of the real grid.
    CHECK(world.get(0, 5) == BlockType::Air);
    CHECK(world.get(WORLD_WIDTH - 1, 5) == BlockType::Air);
    CHECK(world.get(5, 0) == BlockType::Air);
    CHECK(world.get(5, WORLD_HEIGHT - 1) == BlockType::Air);
}

TEST_CASE("isSolid agrees with the block registry")
{
    World world;

    world.set(1, 1, BlockType::Air);
    world.set(2, 1, BlockType::Grass);
    world.set(3, 1, BlockType::Dirt);
    world.set(4, 1, BlockType::Stone);
    world.set(5, 1, BlockType::CopperOre);
    world.set(6, 1, BlockType::IronOre);

    CHECK(world.isSolid(1, 1) == false);
    CHECK(world.isSolid(2, 1) == true);
    CHECK(world.isSolid(3, 1) == true);
    CHECK(world.isSolid(4, 1) == true);
    CHECK(world.isSolid(5, 1) == true);
    CHECK(world.isSolid(6, 1) == true);

    for (int i = 0; i < static_cast<int>(BlockType::Count); ++i)
    {
        const auto type = static_cast<BlockType>(i);
        world.set(0, 0, type);
        CHECK(world.isSolid(0, 0) == blockInfo(type).solid);
    }
}

TEST_CASE("out of bounds is never solid, so the world has open edges")
{
    const World world;

    CHECK(world.isSolid(-1, 10) == false);
    CHECK(world.isSolid(WORLD_WIDTH, 10) == false);
    CHECK(world.isSolid(10, WORLD_HEIGHT) == false);
}

TEST_CASE("inBounds marks exactly the real grid")
{
    const World world;

    CHECK(world.inBounds(0, 0));
    CHECK(world.inBounds(WORLD_WIDTH - 1, WORLD_HEIGHT - 1));

    CHECK_FALSE(world.inBounds(-1, 0));
    CHECK_FALSE(world.inBounds(0, -1));
    CHECK_FALSE(world.inBounds(WORLD_WIDTH, 0));
    CHECK_FALSE(world.inBounds(0, WORLD_HEIGHT));
}

TEST_CASE("the block registry is fully populated")
{
    for (int i = 0; i < static_cast<int>(BlockType::Count); ++i)
    {
        const BlockInfo& info = blockInfo(static_cast<BlockType>(i));

        CHECK_FALSE(info.name.empty());
        CHECK(info.hardness >= 0.0f);
    }

    CHECK(blockInfo(BlockType::Air).solid == false);
    CHECK(blockInfo(BlockType::Air).hardness == 0.0f);

    // Ore is meant to be slower to mine than the stone it sits in.
    CHECK(blockInfo(BlockType::CopperOre).hardness > blockInfo(BlockType::Stone).hardness);
    CHECK(blockInfo(BlockType::IronOre).hardness > blockInfo(BlockType::CopperOre).hardness);
}
