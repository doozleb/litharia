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
    CopperPlate,
    IronPlate,

    Count
};

struct ItemInfo
{
    std::string_view name;
    int maxStack;

    // The block this item places, or Air if it is not placeable.
    BlockType placeBlock;
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
