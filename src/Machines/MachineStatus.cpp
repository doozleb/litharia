#include "MachineStatus.h"

#include <algorithm>

#include "MachineType.h"
#include "Recipes.h"

MachineStatus barStatus(const Machine& m)
{
    MachineStatus status;

    if (m.type == MachineType::BurnerGenerator)
    {
        status.bar = MachineBar::Fuel;
        status.fraction = std::clamp(m.fuel / COAL_BURN_SECONDS, 0.0f, 1.0f);
    }
    else if (m.type == MachineType::Drill)
    {
        status.bar = MachineBar::Progress;
        status.fraction =
            std::clamp(m.progress / machineInfo(MachineType::Drill).actionTime, 0.0f, 1.0f);
    }
    else if (m.type == MachineType::Smelter)
    {
        const SmeltRecipe* recipe = m.input.empty() ? nullptr : smeltRecipeFor(m.input.type);

        if (recipe != nullptr)
        {
            status.bar = MachineBar::Progress;
            status.fraction = std::clamp(m.progress / recipe->seconds, 0.0f, 1.0f);
        }
    }

    return status;
}
