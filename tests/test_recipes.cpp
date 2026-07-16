#include "doctest.h"

#include "Machines/Recipes.h"

TEST_CASE("ores smelt into their plates")
{
    const SmeltRecipe* copper = smeltRecipeFor(ItemType::CopperOre);
    REQUIRE(copper != nullptr);
    CHECK(copper->out == ItemType::CopperPlate);
    CHECK(copper->seconds > 0.0f);

    const SmeltRecipe* iron = smeltRecipeFor(ItemType::IronOre);
    REQUIRE(iron != nullptr);
    CHECK(iron->out == ItemType::IronPlate);
}

TEST_CASE("things that do not smelt return null")
{
    CHECK(smeltRecipeFor(ItemType::Stone) == nullptr);
    CHECK(smeltRecipeFor(ItemType::CopperPlate) == nullptr);
    CHECK(smeltRecipeFor(ItemType::None) == nullptr);
}

TEST_CASE("allSmeltRecipes exposes both defined recipes")
{
    const std::span<const SmeltRecipe> all = allSmeltRecipes();

    REQUIRE(all.size() == 2);
    CHECK(all[0].in == ItemType::CopperOre);
    CHECK(all[1].in == ItemType::IronOre);
}

TEST_CASE("formatSmeltRecipeList joins every recipe as \"In -> Out\"")
{
    CHECK(formatSmeltRecipeList(allSmeltRecipes()) ==
          "Copper Ore -> Copper Plate, Iron Ore -> Iron Plate");
}

TEST_CASE("formatSmeltRecipeList on an empty span yields an empty string")
{
    CHECK(formatSmeltRecipeList({}) == "");
}
