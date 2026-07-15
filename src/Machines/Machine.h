#pragma once

#include "../Core/Direction.h"
#include "../Items/Items.h"
#include "MachineType.h"

// One placed machine. Plain data: all behaviour lives in Machines. A machine is a
// single tile (multi-tile footprints are a later slice).
struct Machine
{
    MachineType type = MachineType::None;

    int x = 0;
    int y = 0;
    Direction facing = Direction::Right;

    // Processing machines (drill, smelter, generator).
    ItemStack input;
    ItemStack output;
    float progress = 0.0f; // seconds into the current operation

    // Burner generator: seconds of fuel left to burn.
    float fuel = 0.0f;

    // Power, set every tick by Machines::updatePower().
    int network = -1;
    bool powered = false;

    // Transport machines (belt, chute): one carried item and its move timer.
    ItemType carried = ItemType::None;
    float carryTimer = 0.0f; // counts down; the item advances when it reaches 0

    bool empty() const { return type == MachineType::None; }
};
