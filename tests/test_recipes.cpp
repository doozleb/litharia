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
    CHECK(itemInfo(ItemType::CopperDrill).name == "Copper Drill");
    CHECK(itemInfo(ItemType::IronDrill).name == "Iron Drill");
    CHECK(itemInfo(ItemType::ObsidianDrill).name == "Obsidian Drill"); // <-- new line
    CHECK(itemInfo(ItemType::CopperBelt).name == "Copper Belt");
    CHECK(itemInfo(ItemType::IronBelt).name == "Iron Belt");
    CHECK(itemInfo(ItemType::CopperChute).name == "Copper Chute");
    CHECK(itemInfo(ItemType::IronChute).name == "Iron Chute");
    CHECK(itemInfo(ItemType::CopperSmelter).name == "Copper Smelter");
    CHECK(itemInfo(ItemType::IronSmelter).name == "Iron Smelter");
    CHECK(itemInfo(ItemType::Chest).name == "Chest");
    CHECK(itemInfo(ItemType::Furnace).name == "Furnace");
    CHECK(itemInfo(ItemType::ItemAcceptor).name == "Item Acceptor");
}

TEST_CASE("allCraftRecipes exposes every craftable item exactly once")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    CHECK(all.size() == 23);
}

TEST_CASE("the Chest recipe costs 8 oak logs and 2 copper plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::Chest; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::OakLog);
    CHECK(it->ingredients[0].count == 8);
    CHECK(it->ingredients[1].item == ItemType::CopperPlate);
    CHECK(it->ingredients[1].count == 2);
}

TEST_CASE("the Item Acceptor recipe costs 10 stone and 3 copper plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ItemAcceptor; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stone);
    CHECK(it->ingredients[0].count == 10);
    CHECK(it->ingredients[1].item == ItemType::CopperPlate);
    CHECK(it->ingredients[1].count == 3);
    CHECK(it->seconds == doctest::Approx(3.0f));
}

TEST_CASE("the Furnace recipe requires a table and costs 20 stone")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::Furnace; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stone);
    CHECK(it->ingredients[0].count == 20);
    CHECK(it->seconds == doctest::Approx(4.0f));
}

TEST_CASE("allFurnaceRecipes exposes manual smelting for both ores, slower than the automated Smelter")
{
    const std::span<const FurnaceRecipe> all = allFurnaceRecipes();
    REQUIRE(all.size() == 2);

    CHECK(all[0].in == ItemType::CopperOre);
    CHECK(all[0].out == ItemType::CopperPlate);
    CHECK(all[0].seconds == doctest::Approx(5.0f));

    CHECK(all[1].in == ItemType::IronOre);
    CHECK(all[1].out == ItemType::IronPlate);
    CHECK(all[1].seconds == doctest::Approx(7.5f));

    // Manual smelting is deliberately slower than the automated Smelter's own
    // SmeltRecipe timing, so building one is still worth it.
    CHECK(all[0].seconds > smeltRecipeFor(ItemType::CopperOre)->seconds);
    CHECK(all[1].seconds > smeltRecipeFor(ItemType::IronOre)->seconds);
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

TEST_CASE("the Copper Drill recipe costs 4 copper plates and 2 stone, no iron needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperDrill; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::CopperPlate);
    CHECK(it->ingredients[0].count == 4);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(4.0f));
}

TEST_CASE("the Iron Drill recipe costs 4 iron plates and 2 stone, no copper needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronDrill; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::IronPlate);
    CHECK(it->ingredients[0].count == 4);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(4.0f));
}

TEST_CASE("the Copper Belt recipe costs 2 copper plates and 2 stone, no iron needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperBelt; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::CopperPlate);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(1.0f));
}

TEST_CASE("the Iron Belt recipe costs 2 iron plates and 2 stone, no copper needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronBelt; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::IronPlate);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(1.0f));
}

TEST_CASE("the Copper Chute recipe costs 1 copper plate and 2 stone, no iron needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperChute; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::CopperPlate);
    CHECK(it->ingredients[0].count == 1);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(1.0f));
}

TEST_CASE("the Iron Chute recipe costs 1 iron plate and 2 stone, no copper needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronChute; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::IronPlate);
    CHECK(it->ingredients[0].count == 1);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(1.0f));
}

TEST_CASE("the Copper Smelter recipe costs 3 copper plates and 5 stone, no iron needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperSmelter; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::CopperPlate);
    CHECK(it->ingredients[0].count == 3);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 5);
    CHECK(it->seconds == doctest::Approx(4.0f));
}

TEST_CASE("the Iron Smelter recipe costs 3 iron plates and 5 stone, no copper needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronSmelter; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::IronPlate);
    CHECK(it->ingredients[0].count == 3);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 5);
    CHECK(it->seconds == doctest::Approx(4.0f));
}

TEST_CASE("the Stick recipe costs 1 oak log and yields 4 sticks")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::Stick; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::OakLog);
    CHECK(it->ingredients[0].count == 1);
    CHECK(it->outputCount == 4);
}

TEST_CASE("every recipe outputs a positive count, 1 by default")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();

    for (const CraftRecipe& r : all)
        CHECK(r.outputCount > 0);
}

TEST_CASE("the Stone Pickaxe recipe costs 2 sticks and 2 sharp rocks")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::StonePickaxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::SharpRock);
    CHECK(it->ingredients[1].count == 2);
}

TEST_CASE("the Stone Axe recipe costs 2 sticks and 1 sharp rock")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::StoneAxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::SharpRock);
    CHECK(it->ingredients[1].count == 1);
}

TEST_CASE("the Copper Pickaxe recipe costs 2 sticks and 4 copper plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperPickaxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::CopperPlate);
    CHECK(it->ingredients[1].count == 4);
}

TEST_CASE("the Copper Axe recipe costs 2 sticks and 2 copper plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::CopperAxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::CopperPlate);
    CHECK(it->ingredients[1].count == 2);
}

TEST_CASE("the Iron Pickaxe recipe costs 2 sticks and 4 iron plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronPickaxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::IronPlate);
    CHECK(it->ingredients[1].count == 4);
}

TEST_CASE("the Iron Axe recipe costs 2 sticks and 2 iron plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::IronAxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::IronPlate);
    CHECK(it->ingredients[1].count == 2);
}

TEST_CASE("the Obsidian Pickaxe recipe costs 2 sticks and 3 obsidian")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ObsidianPickaxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Obsidian);
    CHECK(it->ingredients[1].count == 3);
}

TEST_CASE("the Obsidian Axe recipe costs 2 sticks and 2 obsidian")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ObsidianAxe; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stick);
    CHECK(it->ingredients[0].count == 2);
    CHECK(it->ingredients[1].item == ItemType::Obsidian);
    CHECK(it->ingredients[1].count == 2);
}

TEST_CASE("the Obsidian Drill recipe costs 4 obsidian and 2 stone, no plate needed")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ObsidianDrill; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Obsidian);
    CHECK(it->ingredients[0].count == 4);
    CHECK(it->ingredients[1].item == ItemType::Stone);
    CHECK(it->ingredients[1].count == 2);
    CHECK(it->seconds == doctest::Approx(4.0f));
}
