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
    {"Obsidian",    { 40,  20,  55}, true,  3.00f, BlockType::Obsidian,  ToolType::Pickaxe, ToolTier::Iron},

    {"Water (Level 1)", {170, 200, 230}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 2)", {151, 187, 229}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 3)", {133, 174, 227}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 4)", {114, 161, 226}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 5)", { 96, 149, 224}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 6)", { 77, 136, 223}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 7)", { 59, 123, 221}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Water (Level 8)", { 40, 110, 220}, false, 0.00f, BlockType::Air, ToolType::None},

    {"Lava (Level 1)",  { 90,  40,  15}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 2)",  {110,  47,  16}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 3)",  {130,  54,  16}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 4)",  {150,  61,  17}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 5)",  {170,  69,  18}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 6)",  {190,  76,  19}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 7)",  {210,  83,  19}, false, 0.00f, BlockType::Air, ToolType::None},
    {"Lava (Level 8)",  {230,  90,  20}, false, 0.00f, BlockType::Air, ToolType::None},
}};

} // namespace

const BlockInfo& blockInfo(BlockType type)
{
    const auto index = static_cast<std::size_t>(type);

    if (index >= registry.size())
        return registry[static_cast<std::size_t>(BlockType::Air)];

    return registry[index];
}
