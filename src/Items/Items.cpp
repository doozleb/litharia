#include "Items.h"

#include <array>

namespace
{

// Indexed by ItemType. Order must match the enum.
constexpr std::array<ItemInfo, static_cast<std::size_t>(ItemType::Count)> registry = {{
    //  name          maxStack  placeBlock
    {"Nothing",      0,  BlockType::Air},
    {"Dirt",        99,  BlockType::Dirt},
    {"Stone",       99,  BlockType::Stone},
    {"Copper Ore",  99,  BlockType::CopperOre},
    {"Iron Ore",    99,  BlockType::IronOre},
}};

} // namespace

const ItemInfo& itemInfo(ItemType type)
{
    const auto index = static_cast<std::size_t>(type);

    if (index >= registry.size())
        return registry[static_cast<std::size_t>(ItemType::None)];

    return registry[index];
}

ItemType itemForBlock(BlockType block)
{
    // Grass drops dirt, so route through the registry's drop rather than mapping the
    // mined block straight across.
    const BlockType dropped = blockInfo(block).drop;

    switch (dropped)
    {
        case BlockType::Dirt:      return ItemType::Dirt;
        case BlockType::Stone:     return ItemType::Stone;
        case BlockType::CopperOre: return ItemType::CopperOre;
        case BlockType::IronOre:   return ItemType::IronOre;

        default: return ItemType::None;
    }
}
