#include "doctest.h"

#include "Blocks/Blocks.h"

TEST_CASE("isWater/isLava/isFluid correctly classify every fluid level, and nothing else")
{
    for (int level = 1; level <= 8; ++level)
    {
        CHECK(isWater(waterAtLevel(level)));
        CHECK_FALSE(isLava(waterAtLevel(level)));
        CHECK(isFluid(waterAtLevel(level)));

        CHECK(isLava(lavaAtLevel(level)));
        CHECK_FALSE(isWater(lavaAtLevel(level)));
        CHECK(isFluid(lavaAtLevel(level)));
    }

    CHECK_FALSE(isFluid(BlockType::Air));
    CHECK_FALSE(isFluid(BlockType::Stone));
    CHECK_FALSE(isFluid(BlockType::Obsidian));
    CHECK_FALSE(isWater(BlockType::Lava8));
    CHECK_FALSE(isLava(BlockType::Water8));
}

TEST_CASE("fluidLevel round-trips with waterAtLevel/lavaAtLevel, and is 0 for non-fluid blocks")
{
    for (int level = 1; level <= 8; ++level)
    {
        CHECK(fluidLevel(waterAtLevel(level)) == level);
        CHECK(fluidLevel(lavaAtLevel(level)) == level);
    }

    CHECK(fluidLevel(BlockType::Air) == 0);
    CHECK(fluidLevel(BlockType::Stone) == 0);
    CHECK(fluidLevel(BlockType::Obsidian) == 0);
}

TEST_CASE("fluidAtLevel clamps to Air at or below zero, and matches the source's fluid family")
{
    CHECK(fluidAtLevel(BlockType::Water8, 0) == BlockType::Air);
    CHECK(fluidAtLevel(BlockType::Water8, -1) == BlockType::Air);
    CHECK(fluidAtLevel(BlockType::Water8, 5) == BlockType::Water5);
    CHECK(fluidAtLevel(BlockType::Water1, 5) == BlockType::Water5);
    CHECK(fluidAtLevel(BlockType::Lava3, 5) == BlockType::Lava5);
    CHECK(fluidAtLevel(BlockType::Lava8, 1) == BlockType::Lava1);
}

TEST_CASE("every fluid block is non-solid, unmineable, and drops nothing")
{
    for (int level = 1; level <= 8; ++level)
    {
        const BlockInfo& water = blockInfo(waterAtLevel(level));
        CHECK_FALSE(water.solid);
        CHECK(water.requiredTool == ToolType::None);
        CHECK(water.drop == BlockType::Air);
        CHECK_FALSE(water.name.empty());

        const BlockInfo& lava = blockInfo(lavaAtLevel(level));
        CHECK_FALSE(lava.solid);
        CHECK(lava.requiredTool == ToolType::None);
        CHECK(lava.drop == BlockType::Air);
        CHECK_FALSE(lava.name.empty());
    }
}
