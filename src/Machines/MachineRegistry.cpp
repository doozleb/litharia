#include "MachineType.h"

#include <array>

namespace
{

constexpr std::array<MachineInfo, static_cast<std::size_t>(MachineType::Count)> registry = {{
    //  name                 color            gen    con    trans  power  action  width
    {"None",              {  0,   0,   0}, false, false, false, 0.0f,  0.0f,  1},
    {"Burner Generator",  {190, 120,  60}, true,  false, false, 10.0f, 0.0f,  1},
    {"Drill",             {150, 150, 160}, false, true,  false, 5.0f,  3.0f,  1},
    {"Belt",              { 90,  90, 100}, false, false, true,  0.0f,  0.5f,  1},
    {"Chute",             { 70,  70,  80}, false, false, true,  0.0f,  0.5f,  1},
    {"Smelter",           {200,  90,  70}, false, true,  false, 5.0f,  0.0f,  1},
    {"Chest",             {140,  95,  50}, false, false, false, 0.0f,  0.0f,  1},
    {"Crafting Table",    {120,  80,  40}, false, false, false, 0.0f,  0.0f,  2},
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
        case MachineType::Drill:           return ItemType::Drill;
        case MachineType::Belt:            return ItemType::Belt;
        case MachineType::Chute:           return ItemType::Chute;
        case MachineType::Smelter:         return ItemType::Smelter;
        case MachineType::Chest:           return ItemType::Chest;
        case MachineType::CraftingTable:   return ItemType::CraftingTable;
        default:                           return ItemType::None;
    }
}
