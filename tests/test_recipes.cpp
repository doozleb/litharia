#include "doctest.h"

#include <algorithm>
#include <ostream>

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

TEST_CASE("every placeable machine has a matching craftable item")
{
    CHECK(itemInfo(ItemType::CraftingTable).name == "Crafting Table");
    CHECK(itemInfo(ItemType::BurnerGenerator).name == "Burner Generator");
    CHECK(itemInfo(ItemType::Drill).name == "Drill");
    CHECK(itemInfo(ItemType::Belt).name == "Belt");
    CHECK(itemInfo(ItemType::Chute).name == "Chute");
    CHECK(itemInfo(ItemType::Smelter).name == "Smelter");
    CHECK(itemInfo(ItemType::Chest).name == "Chest");
}

TEST_CASE("allCraftRecipes exposes every craftable item exactly once")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    CHECK(all.size() == 7);
}

TEST_CASE("the Crafting Table recipe costs 15 oak logs and needs no table")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CraftingTable; });

    REQUIRE(it != all.end());
    CHECK_FALSE(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::OakLog);
    CHECK(it->ingredients[0].count == 15);
}

TEST_CASE("every recipe but the Crafting Table requires a placed table")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();

    for (const CraftRecipe& r : all)
        if (r.output != ItemType::CraftingTable)
            CHECK(r.requiresCraftingTable);
}

TEST_CASE("every recipe costs a positive amount of at least one ingredient")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();

    for (const CraftRecipe& r : all)
    {
        CHECK(r.ingredients[0].item != ItemType::None);
        CHECK(r.ingredients[0].count > 0);
        CHECK(r.seconds > 0.0f);
    }
}
