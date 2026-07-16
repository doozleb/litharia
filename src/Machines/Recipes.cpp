#include "Recipes.h"

#include <array>

namespace
{

constexpr std::array<SmeltRecipe, 2> recipes = {{
    {ItemType::CopperOre, ItemType::CopperPlate, 2.0f},
    {ItemType::IronOre,   ItemType::IronPlate,   3.5f},
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
