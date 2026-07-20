#pragma once

#include <cstdint>
#include <string_view>

#include "../Blocks/Blocks.h"

// Items depends on Blocks, never the other way round. The block registry names its
// drop as a BlockType; this is where that becomes an item.

enum class ItemType : std::uint8_t
{
    None,
    Dirt,
    Stone,
    CopperOre,
    IronOre,
    Coal,
    Obsidian,
    CopperPlate,
    IronPlate,
    WoodPickaxe,
    WoodAxe,
    StonePickaxe,
    StoneAxe,
    CopperPickaxe,
    CopperAxe,
    IronPickaxe,
    IronAxe,
    ObsidianPickaxe,
    ObsidianAxe,
    OakLog,
    Stick,
    SharpRock,
    CraftingTable,
    BurnerGenerator,
    CopperDrill,
    IronDrill,
    ObsidianDrill,
    CopperBelt,
    IronBelt,
    CopperChute,
    IronChute,
    CopperSmelter,
    IronSmelter,
    Chest,
    Furnace,
    ItemAcceptor,

    Count
};

struct ItemInfo
{
    std::string_view name;
    int maxStack;

    // The block this item places, or Air if it is not placeable.
    BlockType placeBlock;

    // None for everything except tools.
    ToolType toolType;

    // What this item looks like in the hotbar/bag and on the ground. Independent
    // of placeBlock: a non-placeable item (a plate, a tool, a log) still needs a
    // color of its own to render as anything but a black square.
    BlockColor iconColor;

    // How advanced a tool item is. Meaningless when toolType is None.
    ToolTier tier = ToolTier::Wood;
};

const ItemInfo& itemInfo(ItemType type);

// What a mined block turns into. Air yields None.
ItemType itemForBlock(BlockType block);

struct ItemStack
{
    ItemType type = ItemType::None;
    int count = 0;

    bool empty() const { return type == ItemType::None || count <= 0; }

    int maxStack() const { return itemInfo(type).maxStack; }
};
