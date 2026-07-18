#include "doctest.h"

#include "Machines/Machine.h"
#include "Machines/MachineStatus.h"
#include "Machines/MachineType.h"
#include "Machines/Recipes.h"

TEST_CASE("an unfuelled generator shows an empty fuel bar")
{
    Machine m;
    m.type = MachineType::BurnerGenerator;
    m.fuel = 0.0f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Fuel);
    CHECK(status.fraction == doctest::Approx(0.0f));
}

TEST_CASE("a generator mid-burn shows a proportional fuel bar")
{
    Machine m;
    m.type = MachineType::BurnerGenerator;
    m.fuel = COAL_BURN_SECONDS * 0.5f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Fuel);
    CHECK(status.fraction == doctest::Approx(0.5f));
}

TEST_CASE("fuel fraction never exceeds 1 even if fuel overshoots capacity")
{
    Machine m;
    m.type = MachineType::BurnerGenerator;
    m.fuel = COAL_BURN_SECONDS * 2.0f; // should not happen in practice

    CHECK(barStatus(m).fraction == doctest::Approx(1.0f));
}

TEST_CASE("a drill mid-mining shows proportional progress")
{
    Machine m;
    m.type = MachineType::IronDrill;
    m.progress = machineInfo(MachineType::IronDrill).actionTime * 0.25f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Progress);
    CHECK(status.fraction == doctest::Approx(0.25f));
}

TEST_CASE("a smelter with no input shows no bar at all")
{
    Machine m;
    m.type = MachineType::IronSmelter; // input left empty

    CHECK(barStatus(m).bar == MachineBar::None);
}

TEST_CASE("a smelter mid-smelt shows progress against its recipe's time")
{
    Machine m;
    m.type = MachineType::IronSmelter;
    m.input = {ItemType::CopperOre, 1};

    const SmeltRecipe* recipe = smeltRecipeFor(ItemType::CopperOre);
    REQUIRE(recipe != nullptr);
    m.progress = recipe->seconds * 0.5f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Progress);
    CHECK(status.fraction == doctest::Approx(0.5f));
}

TEST_CASE("a Copper Smelter mid-smelt shows progress against recipe seconds times its own multiplier")
{
    Machine m;
    m.type = MachineType::CopperSmelter;
    m.input = {ItemType::CopperOre, 1};

    const SmeltRecipe* recipe = smeltRecipeFor(ItemType::CopperOre);
    REQUIRE(recipe != nullptr);
    const float fullTime = recipe->seconds * machineInfo(MachineType::CopperSmelter).speedMultiplier;
    m.progress = fullTime * 0.5f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Progress);
    CHECK(status.fraction == doctest::Approx(0.5f));
}

TEST_CASE("a belt never shows a bar")
{
    Machine m;
    m.type = MachineType::IronBelt;

    CHECK(barStatus(m).bar == MachineBar::None);
}

TEST_CASE("a Copper Drill mid-mining shows progress against its own (slower) actionTime")
{
    Machine m;
    m.type = MachineType::CopperDrill;
    m.progress = machineInfo(MachineType::CopperDrill).actionTime * 0.25f;

    const MachineStatus status = barStatus(m);

    CHECK(status.bar == MachineBar::Progress);
    CHECK(status.fraction == doctest::Approx(0.25f));
}

TEST_CASE("a copper belt never shows a bar")
{
    Machine m;
    m.type = MachineType::CopperBelt;

    CHECK(barStatus(m).bar == MachineBar::None);
}
