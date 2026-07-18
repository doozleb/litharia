#include "Items.h"

#include <array>

namespace
{

// Indexed by ItemType. Order must match the enum.
constexpr std::array<ItemInfo, static_cast<std::size_t>(ItemType::Count)> registry = {{
    //  name                 maxStack  placeBlock            toolType           iconColor
    {"Nothing",           0,  BlockType::Air,       ToolType::None,    {  0,   0,   0}},
    {"Dirt",             99,  BlockType::Dirt,      ToolType::None,    {134,  89,  52}},
    {"Stone",            99,  BlockType::Stone,     ToolType::None,    {112, 112, 118}},
    {"Copper Ore",       99,  BlockType::CopperOre, ToolType::None,    {201, 116,  56}},
    {"Iron Ore",         99,  BlockType::IronOre,   ToolType::None,    {166, 174, 190}},
    {"Coal",             99,  BlockType::Coal,      ToolType::None,    { 44,  44,  50}},
    {"Copper Plate",     99,  BlockType::Air,       ToolType::None,    {224, 150,  90}},
    {"Iron Plate",       99,  BlockType::Air,       ToolType::None,    {205, 210, 218}},
    {"Pickaxe",           1,  BlockType::Air,       ToolType::Pickaxe, {170, 170, 180}},
    {"Axe",               1,  BlockType::Air,       ToolType::Axe,     {160,  90,  50}},
    {"Oak Log",          99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
    {"Crafting Table",   10,  BlockType::Air,       ToolType::None,    {120,  80,  40}},
    {"Burner Generator", 10,  BlockType::Air,       ToolType::None,    {190, 120,  60}},
    {"Copper Drill",      10,  BlockType::Air,       ToolType::None,    {176, 133, 108}},
    {"Iron Drill",        10,  BlockType::Air,       ToolType::None,    {158, 162, 175}},
    {"Copper Belt",       50,  BlockType::Air,       ToolType::None,    {146, 103,  78}},
    {"Iron Belt",         50,  BlockType::Air,       ToolType::None,    {128, 132, 145}},
    {"Copper Chute",      50,  BlockType::Air,       ToolType::None,    {136,  93,  68}},
    {"Iron Chute",        50,  BlockType::Air,       ToolType::None,    {118, 122, 135}},
    {"Smelter",          10,  BlockType::Air,       ToolType::None,    {200,  90,  70}},
    {"Chest",            10,  BlockType::Air,       ToolType::None,    {140,  95,  50}},
    {"Furnace",          10,  BlockType::Air,       ToolType::None,    {110, 110, 115}},
    {"Item Acceptor",    10,  BlockType::Air,       ToolType::None,    { 80, 140, 190}},
}};

} // namespace

const ItemInfo& itemInfo(ItemType type)
{
    const auto index = static_cast<std::size_t>(type);

    if (index >= registry.size())
        return registry[static_cast<std::size_t>(ItemType::None)];

    return registry[index];
}

ItemType itemForBlock(BlockType block)
{
    // Grass drops dirt, so route through the registry's drop rather than mapping the
    // mined block straight across.
    const BlockType dropped = blockInfo(block).drop;

    switch (dropped)
    {
        case BlockType::Dirt:      return ItemType::Dirt;
        case BlockType::Stone:     return ItemType::Stone;
        case BlockType::CopperOre: return ItemType::CopperOre;
        case BlockType::IronOre:   return ItemType::IronOre;
        case BlockType::Coal:      return ItemType::Coal;
        case BlockType::OakLog:    return ItemType::OakLog;

        default: return ItemType::None;
    }
}
