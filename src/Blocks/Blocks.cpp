#include "Blocks.h"

#include <array>

namespace
{

// Indexed by BlockType. Order must match the enum.
constexpr std::array<BlockInfo, static_cast<std::size_t>(BlockType::Count)> registry = {{
    //  name          color              solid  hardness  drop
    {"Air",         {  0,   0,   0}, false, 0.00f, BlockType::Air},
    {"Grass",       { 86, 176,  74}, true,  0.35f, BlockType::Dirt},
    {"Dirt",        {134,  89,  52}, true,  0.35f, BlockType::Dirt},
    {"Stone",       {112, 112, 118}, true,  0.90f, BlockType::Stone},
    {"Copper Ore",  {201, 116,  56}, true,  1.40f, BlockType::CopperOre},
    {"Iron Ore",    {166, 174, 190}, true,  2.00f, BlockType::IronOre},
    {"Coal",        { 44,  44,  50}, true,  1.10f, BlockType::Coal},
}};

} // namespace

const BlockInfo& blockInfo(BlockType type)
{
    const auto index = static_cast<std::size_t>(type);

    if (index >= registry.size())
        return registry[static_cast<std::size_t>(BlockType::Air)];

    return registry[index];
}
