#pragma once

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
};

const BlockInfo& blockInfo(BlockType type);

inline bool isSolidBlock(BlockType type)
{
    return blockInfo(type).solid;
}
