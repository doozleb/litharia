#pragma once

#include <cstdint>
#include "../Core/Direction.h"
#include "../Items/Inventory.h"
#include "../Items/Items.h"
#include "MachineType.h"

// One placed machine. Plain data: all behaviour lives in Machines. A machine
// occupies machineInfo(type).width x machineInfo(type).height tiles starting
// at (x, y), growing right and down - 1x1 for everything except the
// Crafting Table (2x1) and the Furnace (2x2).
struct Machine
{
    MachineType type = MachineType::None;

    int x = 0;
    int y = 0;
    Direction facing = Direction::Right;

    // `facing` is reserved for input (the side that should face an ore vein or
    // a feeder belt) and is never an output target. outputCursor tracks which of
    // the OTHER 3 sides delivery tries next: seeded just past `facing` when
    // placed, then advanced past whichever side a send last succeeded on, so
    // several belts around a machine take turns rather than one starving the
    // rest.
    Direction outputCursor = Direction::Right;

    // Ascending order of placement, stamped by Machines::place(). The power
    // solve visits consumers in this order, so a machine already running never
    // loses power to one built later. It cannot be read off the machines
    // vector: remove() swap-and-pops, which scrambles that order.
    std::uint32_t placedSeq = 0;

    // Processing machines (drill, smelter, generator).
    ItemStack input;
    ItemStack output;
    float progress = 0.0f; // seconds into the current operation

    // Burner generator: seconds of fuel left to burn.
    float fuel = 0.0f;

    // Power, set every tick by Machines::updatePower().
    bool powered = false;   // consumer: is my demand met by an adjacent generator?
    bool supplying = false; // generator: did I hand any power out this tick?

    // Transport machines (belt, chute): one carried item and its move timer.
    ItemType carried = ItemType::None;
    float carryTimer = 0.0f; // counts down; the item advances when it reaches 0

    // Chest: a bank of slots. 0 slots (the default) for every other machine
    // type - Machines::place() sizes this to CHEST_SLOTS only for a Chest.
    Inventory storage{0};

    bool empty() const { return type == MachineType::None; }
};
