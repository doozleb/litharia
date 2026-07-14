#include "Inventory.h"

#include <algorithm>

namespace
{

const ItemStack emptyStack{};

} // namespace

int Inventory::add(ItemStack stack)
{
    if (stack.empty())
        return 0;

    int remaining = stack.count;
    const int max = itemInfo(stack.type).maxStack;

    // Top up matching stacks first, so the bag fills densely rather than opening a
    // new slot for every pickup.
    for (ItemStack& existing : slots)
    {
        if (remaining <= 0)
            break;

        if (existing.type != stack.type || existing.count >= max)
            continue;

        const int room = max - existing.count;
        const int moved = std::min(room, remaining);

        existing.count += moved;
        remaining -= moved;
    }

    // Only then open empty slots.
    for (ItemStack& existing : slots)
    {
        if (remaining <= 0)
            break;

        if (!existing.empty())
            continue;

        const int moved = std::min(max, remaining);

        existing.type = stack.type;
        existing.count = moved;

        remaining -= moved;
    }

    // Whatever is left over goes back to the caller rather than into the void.
    return remaining;
}

bool Inventory::removeOne(int slot)
{
    if (!validSlot(slot))
        return false;

    ItemStack& existing = slots[static_cast<std::size_t>(slot)];

    if (existing.empty())
        return false;

    --existing.count;

    if (existing.count <= 0)
        existing = ItemStack{};

    return true;
}

const ItemStack& Inventory::slot(int index) const
{
    if (!validSlot(index))
        return emptyStack;

    return slots[static_cast<std::size_t>(index)];
}

bool Inventory::isEmpty() const
{
    for (const ItemStack& existing : slots)
        if (!existing.empty())
            return false;

    return true;
}

int Inventory::count(ItemType type) const
{
    int total = 0;

    for (const ItemStack& existing : slots)
        if (existing.type == type)
            total += existing.count;

    return total;
}
