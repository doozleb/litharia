#include "MachineType.h"

#include <array>

namespace
{

// Indexed by MachineType. Order must match the enum.
constexpr std::array<MachineInfo, static_cast<std::size_t>(MachineType::Count)> registry = {{
    //  name                 color            gen    con    trans  power  action  width height
    {"None",              {  0,   0,   0}, false, false, false, 0.0f,  0.0f,  1, 1},
    {"Burner Generator",  {190, 120,  60}, true,  false, false, 10.0f, 0.0f,  1, 1},
    {"Copper Drill",      {176, 133, 108}, false, true,  false, 5.0f,  3.0f * COPPER_TIER_SLOWDOWN, 1, 1},
    {"Iron Drill",        {158, 162, 175}, false, true,  false, 5.0f,  3.0f,                          1, 1},
    {"Obsidian Drill",    { 90,  70, 100}, false, true,  false, 5.0f,  3.0f * OBSIDIAN_TIER_SPEEDUP,   1, 1},
    {"Copper Belt",       {146, 103,  78}, false, false, true,  0.0f,  0.5f * COPPER_TIER_SLOWDOWN, 1, 1},
    {"Iron Belt",         {128, 132, 145}, false, false, true,  0.0f,  0.5f,                          1, 1},
    {"Obsidian Belt",     { 80,  60,  95}, false, false, true,  0.0f,  0.5f * OBSIDIAN_TIER_SPEEDUP,   1, 1},
    {"Copper Chute",      {136,  93,  68}, false, false, true,  0.0f,  0.5f * COPPER_TIER_SLOWDOWN, 1, 1},
    {"Iron Chute",        {118, 122, 135}, false, false, true,  0.0f,  0.5f,                         1, 1},
    {"Copper Smelter",    {201, 103,  63}, false, true,  false, 5.0f,  0.0f, 1, 1, COPPER_TIER_SLOWDOWN},
    {"Iron Smelter",      {183, 132, 130}, false, true,  false, 5.0f,  0.0f, 1, 1, 1.0f},
    {"Chest",             {140,  95,  50}, false, false, false, 0.0f,  0.0f,  1, 1},
    {"Crafting Table",    {120,  80,  40}, false, false, false, 0.0f,  0.0f,  2, 1},
    {"Furnace",           {110, 110, 115}, false, false, false, 0.0f,  0.0f,  2, 2},
    {"Item Acceptor",     { 80, 140, 190}, false, false, false, 0.0f,  0.0f,  1, 1},
}};

} // namespace

const MachineInfo& machineInfo(MachineType type)
{
    const auto index = static_cast<std::size_t>(type);

    if (index >= registry.size())
        return registry[static_cast<std::size_t>(MachineType::None)];

    return registry[index];
}

std::string formatDrillOreList(std::span<const ItemType> ores)
{
    std::string result;

    for (std::size_t i = 0; i < ores.size(); ++i)
    {
        if (i > 0)
            result += ", ";

        result += std::string(itemInfo(ores[i]).name);
    }

    return result;
}

ItemType itemForMachine(MachineType type)
{
    switch (type)
    {
        case MachineType::BurnerGenerator: return ItemType::BurnerGenerator;
        case MachineType::CopperDrill:     return ItemType::CopperDrill;
        case MachineType::IronDrill:       return ItemType::IronDrill;
        case MachineType::ObsidianDrill:   return ItemType::ObsidianDrill;
        case MachineType::CopperBelt:      return ItemType::CopperBelt;
        case MachineType::IronBelt:        return ItemType::IronBelt;
        case MachineType::ObsidianBelt:    return ItemType::ObsidianBelt;
        case MachineType::CopperChute:     return ItemType::CopperChute;
        case MachineType::IronChute:       return ItemType::IronChute;
        case MachineType::CopperSmelter:   return ItemType::CopperSmelter;
        case MachineType::IronSmelter:     return ItemType::IronSmelter;
        case MachineType::Chest:           return ItemType::Chest;
        case MachineType::CraftingTable:   return ItemType::CraftingTable;
        case MachineType::Furnace:         return ItemType::Furnace;
        case MachineType::ItemAcceptor:    return ItemType::ItemAcceptor;
        default:                           return ItemType::None;
    }
}

bool isFurniture(MachineType type)
{
    return type == MachineType::Chest || type == MachineType::CraftingTable
        || type == MachineType::Furnace;
}

bool isDrill(MachineType type)
{
    return type == MachineType::CopperDrill || type == MachineType::IronDrill
        || type == MachineType::ObsidianDrill;
}

bool isBelt(MachineType type)
{
    return type == MachineType::CopperBelt || type == MachineType::IronBelt
        || type == MachineType::ObsidianBelt;
}

bool isChute(MachineType type)
{
    return type == MachineType::CopperChute || type == MachineType::IronChute;
}

bool isSmelter(MachineType type)
{
    return type == MachineType::CopperSmelter || type == MachineType::IronSmelter;
}
