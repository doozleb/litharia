#include "MachineType.h"

#include <array>

namespace
{

// Indexed by MachineType. Order must match the enum.
constexpr std::array<MachineInfo, static_cast<std::size_t>(MachineType::Count)> registry = {{
    //  name                 color            gen    con    trans  power  action
    {"None",              {  0,   0,   0}, false, false, false, 0.0f,  0.0f},
    {"Burner Generator",  {190, 120,  60}, true,  false, false, 10.0f, 0.0f},
    {"Drill",             {150, 150, 160}, false, true,  false, 5.0f,  3.0f},
    {"Belt",              { 90,  90, 100}, false, false, true,  0.0f,  0.5f},
    {"Chute",             { 70,  70,  80}, false, false, true,  0.0f,  0.5f},
    {"Smelter",           {200,  90,  70}, false, true,  false, 5.0f,  0.0f},
}};

} // namespace

const MachineInfo& machineInfo(MachineType type)
{
    const auto index = static_cast<std::size_t>(type);

    if (index >= registry.size())
        return registry[static_cast<std::size_t>(MachineType::None)];

    return registry[index];
}
