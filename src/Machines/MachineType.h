#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "../Blocks/Blocks.h" // for BlockColor
#include "../Items/Items.h"   // for ItemType

// Seconds of generator burn one coal provides.
inline constexpr float COAL_BURN_SECONDS = 20.0f;

// How many tiles straight down a drill scans for ore to eat.
inline constexpr int DRILL_REACH = 4;

// Copper-tier machines run this much slower than their Iron-tier equivalent.
inline constexpr float COPPER_TIER_SLOWDOWN = 1.75f;

// How many item slots a chest holds (2 rows of 10 in the UI).
inline constexpr int CHEST_SLOTS = 20;

// How many item slots an Item Acceptor holds (1 row of 10 in the UI).
inline constexpr int ITEM_ACCEPTOR_SLOTS = 10;

// Ore item types a Drill can mine, in display order.
inline constexpr std::array<ItemType, 3> DRILL_ORES = {
    ItemType::CopperOre, ItemType::IronOre, ItemType::Coal};

enum class MachineType : std::uint8_t
{
    None,
    BurnerGenerator,
    CopperDrill,
    IronDrill,
    CopperBelt,
    IronBelt,
    Chute,
    Smelter,
    Chest,
    CraftingTable,
    Furnace,
    ItemAcceptor,

    Count
};

struct MachineInfo
{
    std::string_view name;
    BlockColor color;

    bool generator; // supplies power to the machines touching it
    bool consumer;  // draws power from a generator touching it
    bool transport; // belt/chute: carries one item toward its facing (chute: down)

    float powerRating; // supply if generator, demand if consumer
    float actionTime;  // drill: seconds per ore; transport: transfer interval

    // Tile footprint starting at the machine's placed (x, y), growing right
    // (width) and down (height). 1x1 for every machine except the Crafting
    // Table (2x1) and the Furnace (2x2).
    int width;
    int height;
};

const MachineInfo& machineInfo(MachineType type);

// The item that represents `type` in the bag - what the build palette counts,
// what placing consumes, what removing refunds. None for MachineType::None.
ItemType itemForMachine(MachineType type);

// True for machines placed/removed like a world block (hotbar-select,
// right-click to place, mine to remove) instead of through the build-mode
// palette: Chest, Crafting Table, Furnace.
bool isFurniture(MachineType type);

// True for CopperDrill or IronDrill - the two speed tiers of the same machine.
bool isDrill(MachineType type);

// True for CopperBelt or IronBelt - the two speed tiers of the same machine.
bool isBelt(MachineType type);

// "Copper Ore, Iron Ore, Coal" - one segment per entry, joined by ", ". An
// empty span yields "".
std::string formatDrillOreList(std::span<const ItemType> ores);
