#pragma once

#include <cstdint>
#include <string>

#include "Machine.h"

// What a machine shows inside its tile: nothing, a fuel level, or a work-cycle
// progress. Two kinds so the renderer can color them differently and a player
// never mistakes "waiting on fuel" for "mid-cycle".
enum class MachineBar : std::uint8_t
{
    None,
    Fuel,
    Progress
};

struct MachineStatus
{
    MachineBar bar = MachineBar::None;
    float fraction = 0.0f; // 0..1, meaningful only when bar != MachineBar::None
    std::string reason;    // empty when running normally; explains why otherwise
};

// Pure: no World, no Machines container. Just what this one machine's own
// fields say about its bar right now.
MachineStatus barStatus(const Machine& m);
