#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Blocks knows nothing about SFML: the registry is plain data so that World,
// TerrainGenerator and the tests can use it without a window.

// What kind of tool a block needs to be mined, and what an item is if it's a
// tool. Shared between BlockInfo and ItemInfo (Items depends on Blocks, so it
// lives here rather than duplicated in both places).
enum class ToolType : std::uint8_t
{
    None,
    Pickaxe,
    Axe,
};

// How advanced a tool is, independent of what kind it is (Pickaxe vs Axe).
// Ordered: a higher tier can always do everything a lower tier can.
enum class ToolTier : std::uint8_t
{
    Wood,
    Stone,
    Copper,
    Iron,
    Obsidian,
};

// True if a tool of `held` tier can mine a block that requires `required`.
inline bool meetsTier(ToolTier held, ToolTier required)
{
    return static_cast<std::uint8_t>(held) >= static_cast<std::uint8_t>(required);
}

// Indexed by ToolTier. Multiplies mining speed - Wood is nerfed below 1x,
// every tier from Copper up is a bonus above Stone's baseline 1x.
inline constexpr std::array<float, 5> TOOL_TIER_SPEED_MULTIPLIER = {
    0.6f,  // Wood
    1.0f,  // Stone
    1.25f, // Copper
    1.5f,  // Iron
    1.75f, // Obsidian
};

inline float toolTierSpeedMultiplier(ToolTier tier)
{
    return TOOL_TIER_SPEED_MULTIPLIER[static_cast<std::size_t>(tier)];
}

enum class BlockType : std::uint8_t
{
    Air,
    Grass,
    Dirt,
    Stone,
    CopperOre,
    IronOre,
    Coal,
    OakLog,
    OakLeaves,
    Obsidian,

    Water1,
    Water2,
    Water3,
    Water4,
    Water5,
    Water6,
    Water7,
    Water8,

    Lava1,
    Lava2,
    Lava3,
    Lava4,
    Lava5,
    Lava6,
    Lava7,
    Lava8,

    Count
};

struct BlockColor
{
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

struct BlockInfo
{
    std::string_view name;
    BlockColor color;
    bool solid;
    float hardness; // seconds of mining to break

    // What the block leaves behind when mined, named as a block. Items maps this
    // to an ItemType; keeping it a BlockType is what lets Items depend on Blocks
    // and not the other way round.
    BlockType drop;

    // What tool is needed to mine this block at all. A mismatched (or empty)
    // hand makes the block unbreakable, not just slower.
    ToolType requiredTool;

    // The minimum tier of that tool. Meaningless when requiredTool is None.
    // Defaults to Wood - "any tool of the right kind works" - so every
    // existing row keeps compiling unchanged.
    ToolTier requiredTier = ToolTier::Wood;
};

const BlockInfo& blockInfo(BlockType type);

inline bool isSolidBlock(BlockType type)
{
    return blockInfo(type).solid;
}

// True for Water1..Water8.
inline bool isWater(BlockType type)
{
    return type >= BlockType::Water1 && type <= BlockType::Water8;
}

// True for Lava1..Lava8.
inline bool isLava(BlockType type)
{
    return type >= BlockType::Lava1 && type <= BlockType::Lava8;
}

// True for CopperOre, IronOre, and Coal - the game's three mineable ore
// blocks, as opposed to plain Stone/Dirt.
inline bool isOre(BlockType type)
{
    return type == BlockType::CopperOre || type == BlockType::IronOre || type == BlockType::Coal;
}

inline bool isFluid(BlockType type)
{
    return isWater(type) || isLava(type);
}

// 1-8 for a fluid tile (1 = almost empty, 8 = full/source-like), 0 otherwise.
inline int fluidLevel(BlockType type)
{
    if (isWater(type))
        return static_cast<int>(type) - static_cast<int>(BlockType::Water1) + 1;

    if (isLava(type))
        return static_cast<int>(type) - static_cast<int>(BlockType::Lava1) + 1;

    return 0;
}

// level must be 1-8.
inline BlockType waterAtLevel(int level)
{
    return static_cast<BlockType>(static_cast<int>(BlockType::Water1) + level - 1);
}

// level must be 1-8.
inline BlockType lavaAtLevel(int level)
{
    return static_cast<BlockType>(static_cast<int>(BlockType::Lava1) + level - 1);
}

// Air if level <= 0; otherwise the same fluid family as sameTypeAs (water stays
// water, lava stays lava) at the given level.
inline BlockType fluidAtLevel(BlockType sameTypeAs, int level)
{
    if (level <= 0)
        return BlockType::Air;

    return isWater(sameTypeAs) ? waterAtLevel(level) : lavaAtLevel(level);
}
