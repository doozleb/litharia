#pragma once

#include <span>
#include <string>

#include "../Items/Items.h"

// One smelter recipe: an input item becomes an output item after a fixed time.
struct SmeltRecipe
{
    ItemType in;
    ItemType out;
    float seconds;
};

// The recipe whose input is `in`, or nullptr if that item does not smelt.
const SmeltRecipe* smeltRecipeFor(ItemType in);

// Every defined smelter recipe, e.g. for a tooltip's "accepts" listing.
std::span<const SmeltRecipe> allSmeltRecipes();

// "Copper Ore -> Copper Plate, Iron Ore -> Iron Plate" - one segment per
// recipe, joined by ", ". An empty span yields "".
std::string formatSmeltRecipeList(std::span<const SmeltRecipe> list);
