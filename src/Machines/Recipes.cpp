#include "Recipes.h"

#include <array>

namespace
{

constexpr std::array<SmeltRecipe, 2> recipes = {{
    {ItemType::CopperOre, ItemType::CopperPlate, 2.0f},
    {ItemType::IronOre,   ItemType::IronPlate,   3.5f},
}};

constexpr std::array<CraftRecipe, 13> craftRecipes = {{
    {ItemType::CraftingTable,   {{{ItemType::OakLog, 15}, {}}},                          3.0f, false},
    {ItemType::Chest,           {{{ItemType::OakLog, 8}, {ItemType::CopperPlate, 2}}},    2.0f, true},
    {ItemType::CopperBelt, {{{ItemType::CopperPlate, 2}, {ItemType::Stone, 2}}}, 1.0f, true},
    {ItemType::IronBelt,   {{{ItemType::IronPlate, 2}, {ItemType::Stone, 2}}},   1.0f, true},
    {ItemType::CopperChute, {{{ItemType::CopperPlate, 1}, {ItemType::Stone, 2}}}, 1.0f, true},
    {ItemType::IronChute,   {{{ItemType::IronPlate, 1}, {ItemType::Stone, 2}}},    1.0f, true},
    {ItemType::BurnerGenerator, {{{ItemType::Stone, 5}, {ItemType::IronPlate, 2}}},       3.0f, true},
    {ItemType::CopperDrill, {{{ItemType::CopperPlate, 4}, {ItemType::Stone, 2}}}, 4.0f, true},
    {ItemType::IronDrill,   {{{ItemType::IronPlate, 4}, {ItemType::Stone, 2}}},   4.0f, true},
    {ItemType::CopperSmelter, {{{ItemType::CopperPlate, 3}, {ItemType::Stone, 5}}}, 4.0f, true},
    {ItemType::IronSmelter,   {{{ItemType::IronPlate, 3}, {ItemType::Stone, 5}}},   4.0f, true},
    {ItemType::Furnace,         {{{ItemType::Stone, 20}, {}}},                           4.0f, true},
    {ItemType::ItemAcceptor,    {{{ItemType::Stone, 10}, {ItemType::CopperPlate, 3}}},    3.0f, true},
}};

constexpr std::array<FurnaceRecipe, 2> furnaceRecipes = {{
    {ItemType::CopperOre, ItemType::CopperPlate, 5.0f},
    {ItemType::IronOre,   ItemType::IronPlate,   7.5f},
}};

} // namespace

const SmeltRecipe* smeltRecipeFor(ItemType in)
{
    for (const SmeltRecipe& r : recipes)
        if (r.in == in)
            return &r;

    return nullptr;
}

std::span<const SmeltRecipe> allSmeltRecipes()
{
    return recipes;
}

std::span<const CraftRecipe> allCraftRecipes()
{
    return craftRecipes;
}

std::span<const FurnaceRecipe> allFurnaceRecipes()
{
    return furnaceRecipes;
}

std::string formatSmeltRecipeList(std::span<const SmeltRecipe> list)
{
    std::string result;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (i > 0)
            result += ", ";

        result += std::string(itemInfo(list[i].in).name) + " -> " +
                   std::string(itemInfo(list[i].out).name);
    }

    return result;
}
