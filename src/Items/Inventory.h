#pragma once

#include <array>

#include "Items.h"

// A fixed bag of slots. The first HOTBAR_SIZE of them are the hotbar.
class Inventory
{
public:
    static constexpr int SIZE = 40;
    static constexpr int HOTBAR_SIZE = 10;

    // Tops up existing matching stacks before opening a fresh slot, and respects
    // each item's max stack size.
    //
    // Returns the LEFTOVER count - what would not fit. Nothing is ever silently
    // destroyed by a full bag; the caller is responsible for the remainder, which
    // is what lets a dropped item stay on the ground and keep trying.
    int add(ItemStack stack);

    // Decrements a stack, clearing the slot when it hits zero. False if the slot
    // was already empty.
    bool removeOne(int slot);

    const ItemStack& slot(int index) const;

    bool isEmpty() const;

    // How many of an item type the bag holds in total.
    int count(ItemType type) const;

private:
    static bool validSlot(int index) { return index >= 0 && index < SIZE; }

    std::array<ItemStack, SIZE> slots{};
};
