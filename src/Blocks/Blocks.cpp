#include "Blocks.h"

#include <array>

namespace
{

// Indexed by BlockType. Order must match the enum.
constexpr std::array<BlockInfo, static_cast<std::size_t>(BlockType::Count)> registry = {{
    //  name          color              solid  hardness  drop                  requiredTool
    {"Air",         {  0,   0,   0}, false, 0.00f, BlockType::Air,       ToolType::None},
    {"Grass",       { 86, 176,  74}, true,  0.35f, BlockType::Dirt,      ToolType::Pickaxe},
    {"Dirt",        {134,  89,  52}, true,  0.35f, BlockType::Dirt,      ToolType::Pickaxe},
    {"Stone",       {112, 112, 118}, true,  0.90f, BlockType::Stone,     ToolType::Pickaxe},
    {"Copper Ore",  {201, 116,  56}, true,  1.40f, BlockType::CopperOre, ToolType::Pickaxe, ToolTier::Stone},
    {"Iron Ore",    {166, 174, 190}, true,  2.00f, BlockType::IronOre,   ToolType::Pickaxe, ToolTier::Copper},
    {"Coal",        { 44,  44,  50}, true,  1.10f, BlockType::Coal,      ToolType::Pickaxe},
    {"Oak Log",     {101,  67,  33}, false, 0.60f, BlockType::OakLog,    ToolType::Axe},
    {"Oak Leaves",  { 60, 140,  50}, false, 0.15f, BlockType::Air,       ToolType::Axe},
}};

} // namespace

const BlockInfo& blockInfo(BlockType type)
{
    const auto index = static_cast<std::size_t>(type);

    if (index >= registry.size())
        return registry[static_cast<std::size_t>(BlockType::Air)];

    return registry[index];
}
