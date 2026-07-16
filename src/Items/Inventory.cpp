#include "Inventory.h"

#include <algorithm>

namespace
{

const ItemStack emptyStack{};

} // namespace

Inventory::Inventory(int slotCount)
    : slots(static_cast<std::size_t>(std::max(0, slotCount)))
{
}

int Inventory::add(ItemStack stack, int firstSlot)
{
    if (stack.empty())
        return 0;

    int remaining = stack.count;
    const int max = itemInfo(stack.type).maxStack;
    const std::size_t start = static_cast<std::size_t>(std::clamp(firstSlot, 0, slotCount()));

    // Top up matching stacks first, so the bag fills densely rather than opening a
    // new slot for every pickup.
    for (std::size_t i = start; i < slots.size(); ++i)
    {
        if (remaining <= 0)
            break;

        ItemStack& existing = slots[i];

        if (existing.type != stack.type || existing.count >= max)
            continue;

        const int room = max - existing.count;
        const int moved = std::min(room, remaining);

        existing.count += moved;
        remaining -= moved;
    }

    // Only then open empty slots.
    for (std::size_t i = start; i < slots.size(); ++i)
    {
        if (remaining <= 0)
            break;

        ItemStack& existing = slots[i];

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

bool Inventory::removeOne(ItemType type)
{
    for (std::size_t i = 0; i < slots.size(); ++i)
        if (slots[i].type == type)
            return removeOne(static_cast<int>(i));

    return false;
}

ItemStack Inventory::take(int index)
{
    if (!validSlot(index))
        return {};

    ItemStack& existing = slots[static_cast<std::size_t>(index)];
    const ItemStack taken = existing;
    existing = ItemStack{};
    return taken;
}

ItemStack Inventory::exchange(int index, ItemStack incoming)
{
    if (!validSlot(index))
        return incoming;

    if (incoming.empty())
        return {};

    ItemStack& existing = slots[static_cast<std::size_t>(index)];

    if (existing.empty())
    {
        existing = incoming;
        return {};
    }

    if (existing.type != incoming.type)
    {
        const ItemStack displaced = existing;
        existing = incoming;
        return displaced;
    }

    // Same item: merge as far as it fits, hand back the remainder.
    const int max = itemInfo(existing.type).maxStack;
    const int room = max - existing.count;
    const int moved = std::min(room, incoming.count);

    existing.count += moved;
    const int remaining = incoming.count - moved;

    return remaining > 0 ? ItemStack{incoming.type, remaining} : ItemStack{};
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
