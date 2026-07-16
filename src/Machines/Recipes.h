#pragma once

#include <array>
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

// One ingredient a hand-craft recipe consumes. count == 0 (with item ==
// ItemType::None) marks an unused slot - every recipe here needs at most 2.
struct CraftIngredient
{
    ItemType item = ItemType::None;
    int count = 0;
};

// One hand-craft recipe: up to 2 ingredients from the bag become one output
// item after `seconds`. requiresCraftingTable is false only for the Crafting
// Table itself - the one recipe reachable with no table placed yet.
struct CraftRecipe
{
    ItemType output;
    std::array<CraftIngredient, 2> ingredients;
    float seconds;
    bool requiresCraftingTable;
};

// Every defined hand-craft recipe.
std::span<const CraftRecipe> allCraftRecipes();
