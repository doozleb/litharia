#include "Recipes.h"

#include <array>

namespace
{

constexpr std::array<SmeltRecipe, 2> recipes = {{
    {ItemType::CopperOre, ItemType::CopperPlate, 2.0f},
    {ItemType::IronOre,   ItemType::IronPlate,   3.5f},
}};

constexpr std::array<CraftRecipe, 7> craftRecipes = {{
    {ItemType::CraftingTable,   {{{ItemType::OakLog, 15}, {}}},                          3.0f, false},
    {ItemType::Chest,           {{{ItemType::OakLog, 8}, {}}},                           2.0f, true},
    {ItemType::Belt,            {{{ItemType::IronPlate, 1}, {ItemType::CopperPlate, 1}}}, 1.0f, true},
    {ItemType::Chute,           {{{ItemType::Stone, 2}, {}}},                            1.0f, true},
    {ItemType::BurnerGenerator, {{{ItemType::Stone, 5}, {ItemType::IronPlate, 2}}},       3.0f, true},
    {ItemType::Drill,           {{{ItemType::IronPlate, 5}, {ItemType::CopperPlate, 2}}}, 4.0f, true},
    {ItemType::Smelter,         {{{ItemType::Stone, 5}, {ItemType::CopperPlate, 3}}},     4.0f, true},
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
