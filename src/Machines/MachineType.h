#pragma once

#include <cstdint>
#include <string_view>

#include "../Blocks/Blocks.h" // for BlockColor

// Seconds of generator burn one coal provides.
inline constexpr float COAL_BURN_SECONDS = 20.0f;

// How many tiles straight down a drill scans for ore to eat.
inline constexpr int DRILL_REACH = 4;

enum class MachineType : std::uint8_t
{
    None,
    BurnerGenerator,
    Drill,
    Belt,
    Chute,
    Smelter,

    Count
};

struct MachineInfo
{
    std::string_view name;
    BlockColor color;

    bool generator; // supplies power to its network
    bool consumer;  // draws power from its network
    bool transport; // belt/chute: carries one item toward its facing (chute: down)

    float powerRating; // supply if generator, demand if consumer
    float actionTime;  // drill: seconds per ore; transport: transfer interval
};

const MachineInfo& machineInfo(MachineType type);
