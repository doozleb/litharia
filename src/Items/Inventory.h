#pragma once

#include <vector>

#include "Items.h"

// A bag of item-stack slots. A player's bag treats the first HOTBAR_SIZE slots
// as the hotbar; a chest uses every slot the same way, with no hotbar concept
// at all.
class Inventory
{
public:
    static constexpr int SIZE = 40;
    static constexpr int HOTBAR_SIZE = 10;

    // slotCount defaults to a player-sized bag. A chest constructs a smaller one
    // explicitly, e.g. Inventory(20).
    explicit Inventory(int slotCount = SIZE);

    // Tops up existing matching stacks before opening a fresh slot, and respects
    // each item's max stack size. Only considers slots >= `firstSlot` (default
    // 0, the whole inventory) - lets a caller keep an earlier range (e.g. the
    // player's hotbar) untouched, such as Collect All only ever filling the
    // bag panel.
    //
    // Returns the LEFTOVER count - what would not fit. Nothing is ever silently
    // destroyed by a full bag; the caller is responsible for the remainder, which
    // is what lets a dropped item stay on the ground and keep trying.
    int add(ItemStack stack, int firstSlot = 0);

    // Decrements a stack, clearing the slot when it hits zero. False if the slot
    // was already empty.
    bool removeOne(int slot);

    // Removes one unit of `type` from the first slot holding it, clearing that
    // slot if it reaches zero. False and no-op if the bag holds none of it -
    // the by-type counterpart to the by-slot removeOne(int) above.
    bool removeOne(ItemType type);

    // Empties a slot outright and returns whatever was in it (an empty stack if
    // the slot already was). Used to lift a stack off the grid, e.g. dragging.
    ItemStack take(int index);

    // Puts `incoming` into `index`: merges onto a matching stack as far as it
    // fits, or swaps wholesale if the slot holds something else. Returns
    // whatever doesn't end up in the slot - a merge remainder, or the stack
    // that got swapped out - so nothing dragged is ever destroyed. An
    // out-of-range index bounces `incoming` straight back unchanged.
    ItemStack exchange(int index, ItemStack incoming);

    const ItemStack& slot(int index) const;

    bool isEmpty() const;

    // How many of an item type the bag holds in total.
    int count(ItemType type) const;

    int slotCount() const { return static_cast<int>(slots.size()); }

private:
    bool validSlot(int index) const
    {
        return index >= 0 && index < static_cast<int>(slots.size());
    }

    std::vector<ItemStack> slots;
};
