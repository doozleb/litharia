# Tools and Trees Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Pickaxe and an Axe as real items, gate mining behind holding the right one, and add fellable oak trees that drop logs.

**Architecture:** A new `ToolType` enum threads through the existing `BlockInfo`/`ItemInfo` data tables (`requiredTool` and `toolType` fields). `Player::mine()` gains a tool-match check and, for tree blocks, a small flood-fill that fells everything connected above the tile that broke. `ActionResult` moves from a single broken-tile record to a vector so the fell-cascade can report multiple tiles in one action. `TerrainGenerator` gains a noise-driven tree-scatter pass, following the same techniques (`fbm1D`, hashed rolls) it already uses for hills and ore veins.

**Tech Stack:** C++20, SFML 3, doctest, CMake + Visual Studio generator (existing project stack — no new dependencies).

## Global Constraints

- Build: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` (the VS-bundled `cmake.exe` at `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` is not on `PATH` in this shell — add it, or invoke the full path).
- Test binary: `C:\Litharia\build\Debug\Litharia_tests.exe`. Baseline before this work: **166 test cases, 166 passed, 0 failed**. Every task below must keep the full suite green.
- Follow the existing data-table style in `Blocks.cpp`/`Items.cpp`: aggregate-initialized `std::array`s indexed by the enum, one row per type, comment header naming the columns.
- Spec: `docs/superpowers/specs/2026-07-16-tools-and-trees-design.md`.

---

## Task 1: `ToolType` and the block registry

**Files:**
- Modify: `src/Blocks/Blocks.h`
- Modify: `src/Blocks/Blocks.cpp`
- Test: `tests/test_mining.cpp`

**Interfaces:**
- Produces: `enum class ToolType : std::uint8_t { None, Pickaxe, Axe };` (in `Blocks.h`, used by both `BlockInfo` here and `ItemInfo` in Task 2). `BlockInfo::requiredTool` (`ToolType`). `BlockType::OakLog`, `BlockType::OakLeaves`.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_mining.cpp` (anywhere among the other `TEST_CASE`s, e.g. right after "the item registry maps mined blocks to items"):

```cpp
TEST_CASE("every terrain block requires a pickaxe, and both tree blocks require an axe")
{
    CHECK(blockInfo(BlockType::Grass).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::Dirt).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::Stone).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::CopperOre).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::IronOre).requiredTool == ToolType::Pickaxe);
    CHECK(blockInfo(BlockType::Coal).requiredTool == ToolType::Pickaxe);

    CHECK(blockInfo(BlockType::OakLog).requiredTool == ToolType::Axe);
    CHECK(blockInfo(BlockType::OakLeaves).requiredTool == ToolType::Axe);

    // Neither tree block is solid: the whole tree is non-collidable.
    CHECK_FALSE(blockInfo(BlockType::OakLog).solid);
    CHECK_FALSE(blockInfo(BlockType::OakLeaves).solid);

    // Leaves drop nothing; the log drops itself.
    CHECK(blockInfo(BlockType::OakLeaves).drop == BlockType::Air);
    CHECK(blockInfo(BlockType::OakLog).drop == BlockType::OakLog);
}
```

This will fail to compile (`BlockType::OakLog`, `ToolType`, `requiredTool` do not exist yet) — that is the expected failing state for this step.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: **compile error** — `'OakLog' is not a member of 'BlockType'` (or similar for `ToolType`/`requiredTool`).

- [ ] **Step 3: Add `ToolType`, `requiredTool`, and the two new block types**

In `src/Blocks/Blocks.h`, add the enum right after the includes, before `BlockType`:

```cpp
// What kind of tool a block needs to be mined, and what an item is if it's a
// tool. Shared between BlockInfo and ItemInfo (Items depends on Blocks, so it
// lives here rather than duplicated in both places).
enum class ToolType : std::uint8_t
{
    None,
    Pickaxe,
    Axe,
};
```

Extend `BlockType` (add before `Count`):

```cpp
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
```

Add the field to `BlockInfo`:

```cpp
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
```

In `src/Blocks/Blocks.cpp`, update the registry (every existing row gains a trailing `requiredTool`, and two rows are added):

```cpp
constexpr std::array<BlockInfo, static_cast<std::size_t>(BlockType::Count)> registry = {{
    //  name          color              solid  hardness  drop                  requiredTool
    {"Air",         {  0,   0,   0}, false, 0.00f, BlockType::Air,       ToolType::None},
    {"Grass",       { 86, 176,  74}, true,  0.35f, BlockType::Dirt,      ToolType::Pickaxe},
    {"Dirt",        {134,  89,  52}, true,  0.35f, BlockType::Dirt,      ToolType::Pickaxe},
    {"Stone",       {112, 112, 118}, true,  0.90f, BlockType::Stone,     ToolType::Pickaxe},
    {"Copper Ore",  {201, 116,  56}, true,  1.40f, BlockType::CopperOre, ToolType::Pickaxe},
    {"Iron Ore",    {166, 174, 190}, true,  2.00f, BlockType::IronOre,   ToolType::Pickaxe},
    {"Coal",        { 44,  44,  50}, true,  1.10f, BlockType::Coal,      ToolType::Pickaxe},
    {"Oak Log",     {101,  67,  33}, false, 0.60f, BlockType::OakLog,    ToolType::Axe},
    {"Oak Leaves",  { 60, 140,  50}, false, 0.15f, BlockType::Air,       ToolType::Axe},
}};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: builds clean; `test cases: 167 | 167 passed | 0 failed` (166 baseline + 1 new).

- [ ] **Step 5: Commit**

```bash
git add src/Blocks/Blocks.h src/Blocks/Blocks.cpp tests/test_mining.cpp
git commit -m "feat: add ToolType and oak log/leaves blocks"
```

---

## Task 2: Items — Pickaxe, Axe, Oak Log, and real icon colors

**Files:**
- Modify: `src/Items/Items.h`
- Modify: `src/Items/Items.cpp`
- Modify: `src/Game/Game.cpp` (~line 32-35)
- Modify: `src/Hud/Hud.cpp` (~line 41-44)
- Modify: `src/Machines/MachineRenderer.cpp` (~line 20-29)
- Test: `tests/test_mining.cpp`

**Interfaces:**
- Consumes: `ToolType` from Task 1 (`Blocks.h`).
- Produces: `ItemType::Pickaxe`, `ItemType::Axe`, `ItemType::OakLog`. `ItemInfo::toolType` (`ToolType`), `ItemInfo::iconColor` (`BlockColor`). `itemForBlock(BlockType::OakLog) == ItemType::OakLog`.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_mining.cpp`:

```cpp
TEST_CASE("pickaxe, axe, and oak log are correctly typed items")
{
    CHECK(itemInfo(ItemType::Pickaxe).toolType == ToolType::Pickaxe);
    CHECK(itemInfo(ItemType::Pickaxe).maxStack == 1);

    CHECK(itemInfo(ItemType::Axe).toolType == ToolType::Axe);
    CHECK(itemInfo(ItemType::Axe).maxStack == 1);

    CHECK(itemInfo(ItemType::OakLog).toolType == ToolType::None);

    CHECK(itemForBlock(BlockType::OakLog) == ItemType::OakLog);
    CHECK(itemForBlock(BlockType::OakLeaves) == ItemType::None);

    // Every non-tool item still reports no tool type.
    CHECK(itemInfo(ItemType::Dirt).toolType == ToolType::None);
}

TEST_CASE("every item has a real icon color, even non-placeable ones")
{
    // Index 0 is "Nothing" - never rendered, skip it.
    for (int i = 1; i < static_cast<int>(ItemType::Count); ++i)
    {
        const ItemInfo& info = itemInfo(static_cast<ItemType>(i));
        const bool allBlack = info.iconColor.r == 0 && info.iconColor.g == 0 && info.iconColor.b == 0;

        CHECK_FALSE(allBlack);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: compile error (`ItemType::Pickaxe` etc. do not exist, `ItemInfo` has no `toolType`/`iconColor`).

- [ ] **Step 3: Extend `ItemType` and `ItemInfo`**

In `src/Items/Items.h`, add before `Count`:

```cpp
enum class ItemType : std::uint8_t
{
    None,
    Dirt,
    Stone,
    CopperOre,
    IronOre,
    Coal,
    CopperPlate,
    IronPlate,
    Pickaxe,
    Axe,
    OakLog,

    Count
};
```

Update `ItemInfo`:

```cpp
struct ItemInfo
{
    std::string_view name;
    int maxStack;

    // The block this item places, or Air if it is not placeable.
    BlockType placeBlock;

    // None for everything except the two tools.
    ToolType toolType;

    // What this item looks like in the hotbar/bag and on the ground. Independent
    // of placeBlock: a non-placeable item (a plate, a tool, a log) still needs a
    // color of its own to render as anything but a black square.
    BlockColor iconColor;
};
```

- [ ] **Step 4: Update the item registry and `itemForBlock`**

In `src/Items/Items.cpp`:

```cpp
constexpr std::array<ItemInfo, static_cast<std::size_t>(ItemType::Count)> registry = {{
    //  name           maxStack  placeBlock            toolType           iconColor
    {"Nothing",       0,  BlockType::Air,       ToolType::None,    {  0,   0,   0}},
    {"Dirt",         99,  BlockType::Dirt,      ToolType::None,    {134,  89,  52}},
    {"Stone",        99,  BlockType::Stone,     ToolType::None,    {112, 112, 118}},
    {"Copper Ore",   99,  BlockType::CopperOre, ToolType::None,    {201, 116,  56}},
    {"Iron Ore",     99,  BlockType::IronOre,   ToolType::None,    {166, 174, 190}},
    {"Coal",         99,  BlockType::Coal,      ToolType::None,    { 44,  44,  50}},
    {"Copper Plate", 99,  BlockType::Air,       ToolType::None,    {224, 150,  90}},
    {"Iron Plate",   99,  BlockType::Air,       ToolType::None,    {205, 210, 218}},
    {"Pickaxe",       1,  BlockType::Air,       ToolType::Pickaxe, {170, 170, 180}},
    {"Axe",           1,  BlockType::Air,       ToolType::Axe,     {160,  90,  50}},
    {"Oak Log",      99,  BlockType::Air,       ToolType::None,    {101,  67,  33}},
}};
```

Add the new case to `itemForBlock`'s switch:

```cpp
    switch (dropped)
    {
        case BlockType::Dirt:      return ItemType::Dirt;
        case BlockType::Stone:     return ItemType::Stone;
        case BlockType::CopperOre: return ItemType::CopperOre;
        case BlockType::IronOre:   return ItemType::IronOre;
        case BlockType::Coal:      return ItemType::Coal;
        case BlockType::OakLog:    return ItemType::OakLog;

        default: return ItemType::None;
    }
```

(`OakLeaves` drops `Air`, which already falls through to the `default: None` branch — no case needed.)

- [ ] **Step 5: Point the three `itemColor()` helpers at `iconColor`**

In `src/Game/Game.cpp`, replace:

```cpp
// The block an item would place, which is also what it looks like on the ground.
sf::Color itemColor(ItemType type)
{
    return toColor(blockInfo(itemInfo(type).placeBlock).color);
}
```

with:

```cpp
// An item's own color, independent of what it places (most non-placeable
// items, like tools or logs, don't place anything at all).
sf::Color itemColor(ItemType type)
{
    return toColor(itemInfo(type).iconColor);
}
```

In `src/Hud/Hud.cpp`, replace:

```cpp
// The block an item places is also what it looks like in the slot.
sf::Color itemColor(ItemType type)
{
    return toColor(blockInfo(itemInfo(type).placeBlock).color);
}
```

with:

```cpp
sf::Color itemColor(ItemType type)
{
    return toColor(itemInfo(type).iconColor);
}
```

In `src/Machines/MachineRenderer.cpp`, replace:

```cpp
// The color of the item riding a machine, matching how drops look on the ground.
sf::Color itemColor(ItemType type)
{
    const BlockType block = itemInfo(type).placeBlock;

    // Plates are not placeable; give them a bright refined tint.
    if (block == BlockType::Air)
        return sf::Color(220, 220, 235);

    return toColor(blockInfo(block).color);
}
```

with:

```cpp
// The color of the item riding a machine, matching how drops look on the ground.
sf::Color itemColor(ItemType type)
{
    return toColor(itemInfo(type).iconColor);
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: builds clean; `test cases: 169 | 169 passed | 0 failed`.

- [ ] **Step 7: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Game/Game.cpp src/Hud/Hud.cpp src/Machines/MachineRenderer.cpp tests/test_mining.cpp
git commit -m "feat: add pickaxe, axe, and oak log items; give every item a real icon color"
```

---

## Task 3: Player spawns holding a pickaxe and an axe

**Files:**
- Modify: `src/Player/Player.cpp` (constructor, ~line 45-48)
- Test: `tests/test_mining.cpp`

**Interfaces:**
- Consumes: `ItemType::Pickaxe`/`Axe` (Task 2), `Inventory::exchange(int, ItemStack)` (existing).
- Produces: player hotbar slot 0 = Pickaxe, slot 1 = Axe, from construction.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_mining.cpp`:

```cpp
TEST_CASE("the player spawns already holding a pickaxe and an axe")
{
    World world;
    buildFloor(world, 30);
    Player player = standingAt(world, 10.0f, 30);

    CHECK(player.inventory().slot(0).type == ItemType::Pickaxe);
    CHECK(player.inventory().slot(0).count == 1);
    CHECK(player.inventory().slot(1).type == ItemType::Axe);
    CHECK(player.inventory().slot(1).count == 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: this test **fails at runtime** (slot 0/1 are empty, `type == ItemType::None`), everything else still passes.

- [ ] **Step 3: Give starting tools in the constructor**

In `src/Player/Player.cpp`, replace:

```cpp
Player::Player(sf::Vector2f topLeft)
    : body{topLeft, {WIDTH, HEIGHT}}
{
}
```

with:

```cpp
Player::Player(sf::Vector2f topLeft)
    : body{topLeft, {WIDTH, HEIGHT}}
{
    // There is no crafting system yet, so the player starts equipped rather
    // than unable to mine anything at all.
    bag.exchange(0, {ItemType::Pickaxe, 1});
    bag.exchange(1, {ItemType::Axe, 1});
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 170 | 170 passed | 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/Player/Player.cpp tests/test_mining.cpp
git commit -m "feat: give the player a starting pickaxe and axe"
```

---

## Task 4: `ActionResult` reports every broken tile, not just one

This is a mechanical refactor: mining still only ever breaks one tile at this
point in the plan, it is just now reported through a one-element vector. No
behavior changes; every existing assertion is rewritten to the new shape.

**Files:**
- Modify: `src/Player/Player.h` (`ActionResult`, ~line 30-44)
- Modify: `src/Player/Player.cpp` (`mine()`, ~line 156-165)
- Modify: `src/Game/Game.h` (no signature change, listed for context only)
- Modify: `src/Game/Game.cpp` (`spawnDrop`, ~line 95-113; `fixedUpdate`, ~line 513-519)
- Test: `tests/test_mining.cpp` (~line 89-97)
- Test: `tests/test_pickup.cpp` (~line 166-177)
- Test: `tests/test_placing.cpp` (~line 268-277)

**Interfaces:**
- Produces: `struct BrokenTile { BlockType block; int x; int y; };` (file scope in `Player.h`). `ActionResult::broken` (`std::vector<BrokenTile>`), replacing `brokenBlock`/`brokenX`/`brokenY`.

- [ ] **Step 1: Update the three existing tests to the new shape (this is the "failing test" step — they fail to compile until Step 3)**

In `tests/test_mining.cpp`, in `TEST_CASE("holding mine breaks a block after its hardness, and it drops itself")`, replace:

```cpp
    REQUIRE(result.broke);

    CHECK(elapsed >= hardness);
    CHECK(elapsed < hardness + 0.05f);

    CHECK(result.brokenBlock == BlockType::Stone);
    CHECK(result.brokenX == 12);
    CHECK(result.brokenY == 29);

    // The tile really is gone.
    CHECK(world.get(12, 29) == BlockType::Air);

    // And it yields the right item.
    CHECK(itemForBlock(result.brokenBlock) == ItemType::Stone);
```

with:

```cpp
    REQUIRE(result.broke);
    REQUIRE(result.broken.size() == 1);

    CHECK(elapsed >= hardness);
    CHECK(elapsed < hardness + 0.05f);

    CHECK(result.broken[0].block == BlockType::Stone);
    CHECK(result.broken[0].x == 12);
    CHECK(result.broken[0].y == 29);

    // The tile really is gone.
    CHECK(world.get(12, 29) == BlockType::Air);

    // And it yields the right item.
    CHECK(itemForBlock(result.broken[0].block) == ItemType::Stone);
```

In `tests/test_pickup.cpp`, in `TEST_CASE("mining and picking up puts the block in the bag")`, replace:

```cpp
    // The drop the game would spawn.
    ItemEntity drop({itemForBlock(result.brokenBlock), 1},
                    {static_cast<float>(result.brokenX * TILE_SIZE),
                     static_cast<float>(result.brokenY * TILE_SIZE)},
                    {0.0f, 0.0f});
```

with:

```cpp
    // The drop the game would spawn.
    ItemEntity drop({itemForBlock(result.broken[0].block), 1},
                    {static_cast<float>(result.broken[0].x * TILE_SIZE),
                     static_cast<float>(result.broken[0].y * TILE_SIZE)},
                    {0.0f, 0.0f});
```

In `tests/test_placing.cpp`, in `TEST_CASE("mine it, pick it up, place it back: the loop closes")`, replace:

```cpp
    // Pick it up (the game does this via the item entity; the effect is the same).
    player.inventory().add({itemForBlock(mined.brokenBlock), 1});
```

with:

```cpp
    // Pick it up (the game does this via the item entity; the effect is the same).
    player.inventory().add({itemForBlock(mined.broken[0].block), 1});
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: compile errors — `ActionResult` (still the old shape) has no member `broken`.

- [ ] **Step 3: Change `ActionResult` in `Player.h`**

Add `#include <vector>` to the top of `src/Player/Player.h`'s include block (alongside the existing `SFML/System/Vector2.hpp` include).

Replace:

```cpp
// What the player's actions did to the world this tick. The player mutates the
// world, but spawning drops and rebuilding chunks is Game's business, so what
// happened is handed back rather than acted on here.
struct ActionResult
{
    bool broke = false;
    BlockType brokenBlock = BlockType::Air;
    int brokenX = 0;
    int brokenY = 0;

    bool placed = false;
    int placedX = 0;
    int placedY = 0;
};
```

with:

```cpp
// One tile a mining action turned to air, and what it was before that.
struct BrokenTile
{
    BlockType block;
    int x;
    int y;
};

// What the player's actions did to the world this tick. The player mutates the
// world, but spawning drops and rebuilding chunks is Game's business, so what
// happened is handed back rather than acted on here.
//
// broken is usually one tile, but felling a tree reports every log/leaf tile
// the cascade took down in the same action.
struct ActionResult
{
    bool broke = false;
    std::vector<BrokenTile> broken;

    bool placed = false;
    int placedX = 0;
    int placedY = 0;
};
```

- [ ] **Step 4: Update `Player::mine()` in `Player.cpp`**

Replace the end of `mine()`:

```cpp
    // Broken.
    world.set(tileX, tileY, BlockType::Air);

    mining = false;
    progress = 0.0f;

    result.broke = true;
    result.brokenBlock = block;
    result.brokenX = tileX;
    result.brokenY = tileY;
}
```

with:

```cpp
    // Broken.
    world.set(tileX, tileY, BlockType::Air);

    mining = false;
    progress = 0.0f;

    result.broke = true;
    result.broken.push_back({block, tileX, tileY});
}
```

- [ ] **Step 5: Update `Game.cpp`'s `spawnDrop` and `fixedUpdate`**

Replace:

```cpp
void Game::spawnDrop(const ActionResult& result)
{
    const ItemType type = itemForBlock(result.brokenBlock);

    if (type == ItemType::None)
        return;

    // Pop out of the ground with a small hashed kick, so a row of drops does not
    // land in a perfectly straight line.
    const float roll = noise::hashFloat(result.brokenX, result.brokenY, WORLD_SEED);

    const sf::Vector2f velocity{(roll - 0.5f) * 90.0f, -140.0f};

    // Centred in the tile it came from.
    const sf::Vector2f position{result.brokenX * TILE_SIZE + (TILE_SIZE - ItemEntity::SIZE) * 0.5f,
                                result.brokenY * TILE_SIZE + (TILE_SIZE - ItemEntity::SIZE) * 0.5f};

    drops.emplace_back(ItemStack{type, 1}, position, velocity);
}
```

with:

```cpp
void Game::spawnDrop(const ActionResult& result)
{
    for (const BrokenTile& tile : result.broken)
    {
        const ItemType type = itemForBlock(tile.block);

        if (type == ItemType::None)
            continue;

        // Pop out of the ground with a small hashed kick, so a row of drops does
        // not land in a perfectly straight line.
        const float roll = noise::hashFloat(tile.x, tile.y, WORLD_SEED);

        const sf::Vector2f velocity{(roll - 0.5f) * 90.0f, -140.0f};

        // Centred in the tile it came from.
        const sf::Vector2f position{tile.x * TILE_SIZE + (TILE_SIZE - ItemEntity::SIZE) * 0.5f,
                                    tile.y * TILE_SIZE + (TILE_SIZE - ItemEntity::SIZE) * 0.5f};

        drops.emplace_back(ItemStack{type, 1}, position, velocity);
    }
}
```

In `fixedUpdate`, replace:

```cpp
    if (result.broke)
    {
        // The player mutated the tile; the renderer has to be told about it.
        chunks.markDirty(result.brokenX, result.brokenY);

        spawnDrop(result);
    }
```

with:

```cpp
    if (result.broke)
    {
        // The player mutated one or more tiles; the renderer has to be told.
        for (const BrokenTile& tile : result.broken)
            chunks.markDirty(tile.x, tile.y);

        spawnDrop(result);
    }
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: builds clean; `test cases: 170 | 170 passed | 0 failed` (same count as Task 3 — this task changes shape, not coverage).

- [ ] **Step 7: Commit**

```bash
git add src/Player/Player.h src/Player/Player.cpp src/Game/Game.cpp tests/test_mining.cpp tests/test_pickup.cpp tests/test_placing.cpp
git commit -m "refactor: report every broken tile from an action, not just one"
```

---

## Task 5: Tool gating — the wrong tool (or no tool) can't mine at all

**Files:**
- Modify: `src/Player/Player.cpp` (`mine()`, early-reset branch, ~line 130-139)
- Test: `tests/test_mining.cpp`

**Interfaces:**
- Consumes: `ItemInfo::toolType` (Task 2), `BlockInfo::requiredTool` (Task 1), `Inventory::slot(int)` (existing).

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_mining.cpp`:

```cpp
TEST_CASE("the wrong tool cannot break a block at all")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::Stone);

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(1); // Axe, not a Pickaxe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    for (int i = 0; i < 300; ++i)
    {
        const ActionResult result = player.update(input, world, STEP);
        REQUIRE_FALSE(result.broke);
    }

    CHECK(world.get(12, 29) == BlockType::Stone);
    CHECK_FALSE(player.isMining());
}

TEST_CASE("a pickaxe cannot fell a tree")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::OakLog);

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(0); // Pickaxe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    for (int i = 0; i < 300; ++i)
    {
        const ActionResult result = player.update(input, world, STEP);
        REQUIRE_FALSE(result.broke);
    }

    CHECK(world.get(12, 29) == BlockType::OakLog);
}

TEST_CASE("an empty hand cannot mine anything")
{
    World world;
    buildFloor(world, 30);
    world.set(12, 29, BlockType::Dirt);

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(2); // an empty hotbar slot

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29);

    for (int i = 0; i < 300; ++i)
    {
        const ActionResult result = player.update(input, world, STEP);
        REQUIRE_FALSE(result.broke);
    }

    CHECK(world.get(12, 29) == BlockType::Dirt);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: builds clean, but the three new cases **fail** — today every tool can mine every block, so `result.broke` becomes true and the `REQUIRE_FALSE` inside the loop aborts that test case.

- [ ] **Step 3: Add the tool check to `Player::mine()`**

In `src/Player/Player.cpp`, replace:

```cpp
void Player::mine(const PlayerInput& input, World& world, ActionResult& result, float dt)
{
    const int tileX = tileOf(input.cursor.x);
    const int tileY = tileOf(input.cursor.y);

    const BlockType block = world.get(tileX, tileY);

    // Not holding the button, nothing solid under the cursor, or out of arm's
    // reach: no progress, and any progress already made is thrown away.
    if (!input.mine || block == BlockType::Air || !inReach(tileX, tileY))
    {
        mining = false;
        progress = 0.0f;
        return;
    }
```

with:

```cpp
void Player::mine(const PlayerInput& input, World& world, ActionResult& result, float dt)
{
    const int tileX = tileOf(input.cursor.x);
    const int tileY = tileOf(input.cursor.y);

    const BlockType block = world.get(tileX, tileY);

    const ToolType heldTool = itemInfo(bag.slot(selected).type).toolType;
    const bool wrongTool = block != BlockType::Air && blockInfo(block).requiredTool != heldTool;

    // Not holding the button, nothing solid under the cursor, out of arm's
    // reach, or the wrong tool (including no tool at all) in hand: no
    // progress, and any progress already made is thrown away. A block cannot
    // be chipped away by hand or the wrong tool - it simply does not break.
    if (!input.mine || block == BlockType::Air || !inReach(tileX, tileY) || wrongTool)
    {
        mining = false;
        progress = 0.0f;
        return;
    }
```

`Player.cpp` does not yet include `Items.h` directly (it gets `ItemStack`/`Inventory` transitively through `Player.h`, but `itemInfo()` is declared in `Items.h`) — add `#include "../Items/Items.h"` to the top of `src/Player/Player.cpp`'s include block.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 173 | 173 passed | 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/Player/Player.cpp tests/test_mining.cpp
git commit -m "feat: gate mining behind holding the right tool"
```

---

## Task 6: Felling a tree — the break-cascade

**Files:**
- Modify: `src/Player/Player.cpp` (`mine()`, ~line 156-165; new anonymous-namespace helpers)
- Test: `tests/test_mining.cpp`

**Interfaces:**
- Consumes: `BrokenTile` (Task 4), `BlockType::OakLog`/`OakLeaves` (Task 1).
- Produces: `collectTreeBreak(World&, int, int, std::vector<BrokenTile>&)` (file-local to `Player.cpp` — not exposed, but named here so its behavior is unambiguous for testing purposes: it's exercised only indirectly, through `Player::mine()`).

- [ ] **Step 1: Write the failing tests**

Add a tree-building helper and four test cases to `tests/test_mining.cpp`. Put the helper in the existing anonymous namespace at the top of the file, alongside `buildFloor`/`standingAt`/`cursorOn`:

```cpp
// A trunk of `height` oak logs standing on the grass row at `groundY`, with a
// fixed 6-tile canopy above it - same shape TerrainGenerator will place.
void buildTree(World& world, int trunkX, int groundY, int height)
{
    for (int i = 1; i <= height; ++i)
        world.set(trunkX, groundY - i, BlockType::OakLog);

    const int topY = groundY - height;

    world.set(trunkX - 1, topY, BlockType::OakLeaves);
    world.set(trunkX + 1, topY, BlockType::OakLeaves);

    for (int dx = -1; dx <= 1; ++dx)
        world.set(trunkX + dx, topY - 1, BlockType::OakLeaves);

    world.set(trunkX, topY - 2, BlockType::OakLeaves);
}
```

Then the test cases:

```cpp
TEST_CASE("breaking the bottom log fells the whole tree and drops every log")
{
    World world;
    buildFloor(world, 30);
    buildTree(world, 12, 30, 5); // trunk rows 25..29, canopy above row 25

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(1); // Axe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29); // the bottom log

    ActionResult result;

    for (int i = 0; i < 200 && !result.broke; ++i)
        result = player.update(input, world, STEP);

    REQUIRE(result.broke);

    int logCount = 0;
    for (const BrokenTile& tile : result.broken)
        if (tile.block == BlockType::OakLog)
            ++logCount;

    CHECK(logCount == 5);
    CHECK(result.broken.size() == 5 + 6); // 5 logs + the 6-tile canopy

    // The whole column, trunk and canopy, is gone.
    for (int y = 20; y < 30; ++y)
        CHECK(world.get(12, y) == BlockType::Air);
}

TEST_CASE("breaking a log partway up a tree only fells what's above the cut")
{
    World world;
    buildFloor(world, 30);
    buildTree(world, 12, 30, 5); // trunk rows 25..29 (25 = top, 29 = bottom)

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(1); // Axe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 27); // third log from the bottom

    ActionResult result;

    for (int i = 0; i < 200 && !result.broke; ++i)
        result = player.update(input, world, STEP);

    REQUIRE(result.broke);

    int logCount = 0;
    for (const BrokenTile& tile : result.broken)
        if (tile.block == BlockType::OakLog)
            ++logCount;

    // Rows 25, 26, 27 come down (3 logs); the canopy comes with them.
    CHECK(logCount == 3);
    CHECK(result.broken.size() == 3 + 6);

    // The untouched lower trunk survives.
    CHECK(world.get(12, 28) == BlockType::OakLog);
    CHECK(world.get(12, 29) == BlockType::OakLog);
}

TEST_CASE("leaves never drop an item, whether broken directly or as part of a cascade")
{
    World world;
    buildFloor(world, 30);
    buildTree(world, 12, 30, 5); // canopy apex sits at row 25 - 2 = 23

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(1); // Axe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 23); // the lone apex leaf, isolated from the rest

    ActionResult result;

    for (int i = 0; i < 200 && !result.broke; ++i)
        result = player.update(input, world, STEP);

    REQUIRE(result.broke);
    REQUIRE(result.broken.size() == 1);
    CHECK(result.broken[0].block == BlockType::OakLeaves);
    CHECK(itemForBlock(result.broken[0].block) == ItemType::None);
}

TEST_CASE("a felled tree's logs are the only thing an axe drops")
{
    World world;
    buildFloor(world, 30);
    buildTree(world, 12, 30, 4);

    Player player = standingAt(world, 10.0f, 30);
    player.setSelectedSlot(1); // Axe

    PlayerInput input;
    input.mine = true;
    input.cursor = cursorOn(12, 29); // bottom log

    ActionResult result;

    for (int i = 0; i < 200 && !result.broke; ++i)
        result = player.update(input, world, STEP);

    REQUIRE(result.broke);

    for (const BrokenTile& tile : result.broken)
    {
        const ItemType dropped = itemForBlock(tile.block);
        CHECK((dropped == ItemType::OakLog || dropped == ItemType::None));
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: builds clean (Task 1's `OakLog`/`OakLeaves` and Task 4's `BrokenTile` already exist), but these four cases **fail** — today `mine()` only ever clears the single targeted tile, so `result.broken.size() == 1` when the tests expect a multi-tile cascade.

- [ ] **Step 3: Implement the flood-fill cascade**

In `src/Player/Player.cpp`, add to the anonymous namespace (near the other small free functions like `tileOf`/`tileBox`):

```cpp
bool isTreePart(BlockType type)
{
    return type == BlockType::OakLog || type == BlockType::OakLeaves;
}

// Flood-fills from the tile that was just broken through 4-connected
// OakLog/OakLeaves neighbors, moving sideways or up but never down. That
// asymmetry is what makes cutting a trunk partway up only take the top half:
// the fill can never step back below the tile that started it, so the
// untouched trunk beneath the cut is never reached.
void collectTreeBreak(World& world, int startX, int startY, std::vector<BrokenTile>& out)
{
    std::vector<sf::Vector2i> stack{sf::Vector2i{startX, startY}};

    while (!stack.empty())
    {
        const sf::Vector2i pos = stack.back();
        stack.pop_back();

        const BlockType type = world.get(pos.x, pos.y);

        if (!isTreePart(type))
            continue;

        // Clearing immediately doubles as the visited marker: a neighbor
        // reached a second time from another direction is already Air, so it
        // fails the isTreePart check above and is skipped rather than
        // reprocessed or double-counted.
        world.set(pos.x, pos.y, BlockType::Air);
        out.push_back({type, pos.x, pos.y});

        stack.push_back(sf::Vector2i{pos.x - 1, pos.y});
        stack.push_back(sf::Vector2i{pos.x + 1, pos.y});
        stack.push_back(sf::Vector2i{pos.x, pos.y - 1});
    }
}
```

Then replace the "Broken." section of `mine()`:

```cpp
    // Broken.
    world.set(tileX, tileY, BlockType::Air);

    mining = false;
    progress = 0.0f;

    result.broke = true;
    result.broken.push_back({block, tileX, tileY});
}
```

with:

```cpp
    // Broken.
    if (isTreePart(block))
        collectTreeBreak(world, tileX, tileY, result.broken);
    else
    {
        world.set(tileX, tileY, BlockType::Air);
        result.broken.push_back({block, tileX, tileY});
    }

    mining = false;
    progress = 0.0f;

    result.broke = true;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 177 | 177 passed | 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/Player/Player.cpp tests/test_mining.cpp
git commit -m "feat: felling a tree brings down everything connected above the cut"
```

---

## Task 7: Generate trees in the terrain

**Files:**
- Modify: `src/World/TerrainGenerator.h`
- Modify: `src/World/TerrainGenerator.cpp`
- Test: `tests/test_terrain.cpp`

**Interfaces:**
- Consumes: `noise::fbm1D`, `noise::hashFloat` (existing, `Core/Noise.h`), `BlockType::OakLog`/`OakLeaves` (Task 1).
- Produces: `TerrainGenerator::scatterTrees(World&) const` (private, called from `generate()`). `TerrainGenerator::TREE_MIN_HEIGHT`, `TREE_MAX_HEIGHT`, `TREE_MIN_SPACING` (public constants, so tests can assert against them without duplicating literals).

- [ ] **Step 1: Update the one existing test that assumes nothing but air sits above the surface**

`tests/test_terrain.cpp` uses `std::min`/`std::max` today without an explicit
`<algorithm>` include (it comes in transitively); the new test in Step 2 below
uses `std::min_element`/`std::max_element`, so add the include explicitly
rather than relying on that. At the top of `tests/test_terrain.cpp`, replace:

```cpp
#include "doctest.h"

#include <vector>
```

with:

```cpp
#include "doctest.h"

#include <algorithm>
#include <vector>
```

In `TEST_CASE("every column has a surface, inside the world bounds")`, replace:

```cpp
        // The surface tile itself is grass, and there is open air directly above it.
        CHECK(world.get(x, surface) == BlockType::Grass);
        CHECK(world.get(x, surface - 1) == BlockType::Air);
```

with:

```cpp
        // The surface tile itself is grass. Directly above it is open air,
        // unless a tree's bottom log has grown there instead.
        CHECK(world.get(x, surface) == BlockType::Grass);

        const BlockType above = world.get(x, surface - 1);
        CHECK((above == BlockType::Air || above == BlockType::OakLog));
```

- [ ] **Step 2: Write the new failing test**

Add to `tests/test_terrain.cpp`:

```cpp
TEST_CASE("trees stand on the surface, stay within height bounds, and never crowd a neighbor")
{
    World world;
    const TerrainGenerator generator(2026);
    generator.generate(world);

    std::vector<int> trunkColumns;

    for (int x = 1; x < WORLD_WIDTH - 1; ++x)
    {
        const int surface = generator.surfaceHeight(x);

        if (world.get(x, surface - 1) != BlockType::OakLog)
            continue;

        trunkColumns.push_back(x);

        // Walk up the trunk counting logs until it runs out.
        int height = 0;
        int y = surface - 1;

        while (world.get(x, y) == BlockType::OakLog)
        {
            ++height;
            --y;
        }

        CHECK(height >= TerrainGenerator::TREE_MIN_HEIGHT);
        CHECK(height <= TerrainGenerator::TREE_MAX_HEIGHT);
    }

    // The pass actually grew a meaningful number of trees.
    REQUIRE(trunkColumns.size() > 10);

    // No two trunks close enough for their canopies to touch.
    for (std::size_t i = 1; i < trunkColumns.size(); ++i)
        CHECK(trunkColumns[i] - trunkColumns[i - 1] >= TerrainGenerator::TREE_MIN_SPACING);
}

TEST_CASE("forest density blends across the world rather than switching on and off")
{
    // Same seed as the density noise itself: what matters is that some wide
    // stretches of the world have many more trees than others, evidence the
    // low-frequency forest-factor channel is doing something rather than
    // every column rolling independently at a flat rate.
    World world;
    const TerrainGenerator generator(4040);
    generator.generate(world);

    auto treeCountIn = [&](int fromX, int toX) {
        int count = 0;

        for (int x = fromX; x < toX; ++x)
        {
            const int surface = generator.surfaceHeight(x);
            if (world.get(x, surface - 1) == BlockType::OakLog)
                ++count;
        }

        return count;
    };

    std::vector<int> bandCounts;
    constexpr int BAND_WIDTH = 100;

    for (int start = 0; start + BAND_WIDTH <= WORLD_WIDTH; start += BAND_WIDTH)
        bandCounts.push_back(treeCountIn(start, start + BAND_WIDTH));

    const int lowest = *std::min_element(bandCounts.begin(), bandCounts.end());
    const int highest = *std::max_element(bandCounts.begin(), bandCounts.end());

    // A flat per-column chance would make every 100-wide band come out close
    // to the same count; blended forest patches should not.
    CHECK(highest > lowest);
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: compile error — `TerrainGenerator::TREE_MIN_HEIGHT` etc. do not exist yet.

- [ ] **Step 4: Add the tree pass**

In `src/World/TerrainGenerator.h`, add public constants (alongside `COAL_MIN_Y`/`COAL_MAX_Y`):

```cpp
    static constexpr int TREE_MIN_HEIGHT = 4;
    static constexpr int TREE_MAX_HEIGHT = 6;

    // Minimum distance between two trunks. Each canopy is 3 tiles wide
    // (trunk-1..trunk+1); at this spacing the widest two canopies can ever
    // get is one tile apart, so they can never touch - which is what keeps
    // the break-cascade's flood-fill from ever bleeding into a neighbor tree.
    static constexpr int TREE_MIN_SPACING = 3;
```

Add the private method declaration, alongside `scatterOre`:

```cpp
    void scatterTrees(World& world) const;

    void placeTree(World& world, int trunkX, int surface, int height) const;
```

Update the class's doc comment to mention the fourth pass:

```cpp
// Four passes:
//   1. Surface - fractal noise over x gives a rolling height; grass, then a dirt
//      band, then stone all the way down.
//   2. Caves   - 2D fractal noise crossing a threshold carves air. The threshold
//      tightens near the surface so caves do not shred the landscape.
//   3. Ore     - hashed candidate points inside a depth band grow small blobs, but
//      only ever overwrite stone, so ore never floats in a cave or sits in dirt.
//   4. Trees   - a low-frequency noise channel gives each x position a "forest
//      factor"; columns roll against it to grow an oak tree, spaced far enough
//      apart that no two canopies ever touch.
```

In `src/World/TerrainGenerator.cpp`, add the new tuning constants to the anonymous namespace (alongside the ore constants):

```cpp
// --- Pass 4: trees ------------------------------------------------------------
// A wider wavelength than the surface noise, so forested and bare stretches
// span many tens of tiles rather than flickering column to column.
constexpr float FOREST_FREQUENCY = 0.006f;
constexpr int FOREST_OCTAVES = 3;

// The forest factor in [0, 1] is remapped into this density range before each
// column rolls against it - so even a "bare" stretch occasionally grows a
// tree, and a "forest" stretch is dense but still gated by TREE_MIN_SPACING.
constexpr float FOREST_DENSITY_MIN = 0.05f;
constexpr float FOREST_DENSITY_MAX = 0.6f;

constexpr std::uint32_t SALT_FOREST = 0x6000u;
constexpr std::uint32_t SALT_TREE = 0x7000u;
```

Update `generate()`:

```cpp
void TerrainGenerator::generate(World& world) const
{
    generateBase(world);
    scatterOre(world);
    scatterTrees(world);
}
```

Add the two new methods (near `scatterOre`/`growVein`):

```cpp
void TerrainGenerator::placeTree(World& world, int trunkX, int surface, int height) const
{
    for (int i = 1; i <= height; ++i)
        world.set(trunkX, surface - i, BlockType::OakLog);

    const int topY = surface - height;

    // Flanking the top log.
    world.set(trunkX - 1, topY, BlockType::OakLeaves);
    world.set(trunkX + 1, topY, BlockType::OakLeaves);

    // The 3-wide row above that.
    for (int dx = -1; dx <= 1; ++dx)
        world.set(trunkX + dx, topY - 1, BlockType::OakLeaves);

    // The single apex tile on top.
    world.set(trunkX, topY - 2, BlockType::OakLeaves);
}

void TerrainGenerator::scatterTrees(World& world) const
{
    // Far enough back that the very first eligible column can still place a
    // tree instead of being rejected for "too close to the last one".
    int lastTrunkX = -TREE_MIN_SPACING;

    // Columns 0 and WORLD_WIDTH - 1 are skipped: a canopy needs a tile on
    // each side of its trunk, and one at the world edge would not have it.
    for (int x = 1; x < WORLD_WIDTH - 1; ++x)
    {
        if (x - lastTrunkX < TREE_MIN_SPACING)
            continue;

        const float forestFactor = noise::fbm1D(static_cast<float>(x) * FOREST_FREQUENCY,
                                                 worldSeed + SALT_FOREST,
                                                 FOREST_OCTAVES);

        const float density = lerp(FOREST_DENSITY_MIN, FOREST_DENSITY_MAX, forestFactor);

        if (noise::hashFloat(x, 0, worldSeed + SALT_TREE) >= density)
            continue;

        const float heightRoll = noise::hashFloat(x, 0, worldSeed + SALT_TREE + 1u);
        const int height =
            TREE_MIN_HEIGHT + static_cast<int>(heightRoll * (TREE_MAX_HEIGHT - TREE_MIN_HEIGHT + 1));

        placeTree(world, x, surfaceHeight(x), height);

        lastTrunkX = x;
    }
}
```

(`lerp` already exists as a private free function in this file's anonymous namespace, used by `caveThreshold` — no need to add it again.)

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 179 | 179 passed | 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/World/TerrainGenerator.h src/World/TerrainGenerator.cpp tests/test_terrain.cpp
git commit -m "feat: scatter oak trees across the surface"
```

---

## Task 8: Manual verification in the running game

This feature has a real interactive surface (holding tools, mining gated by
tool, chopping down a tree) that the automated suite exercises through
`Player`/`World` directly but never through actual input or rendering. Confirm
it end-to-end before calling this done.

**Files:** none (verification only).

- [ ] **Step 1: Build and run the game**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia` then launch `C:\Litharia\build\Debug\Litharia.exe`.

- [ ] **Step 2: Confirm starting tools**

Check the hotbar: slot 1 shows a Pickaxe icon, slot 2 shows an Axe icon (both
with their new, non-black colors).

- [ ] **Step 3: Confirm tool gating**

With the Pickaxe selected, walk up to a tree and try to mine it: it should not
break no matter how long the mouse button is held, and no mining-progress
highlight should ever appear on it. With the Axe selected, mine ordinary
ground (grass/dirt/stone): same result, unbreakable.

- [ ] **Step 4: Confirm felling a tree**

Switch to the Axe. Find a generated tree and mine its bottom log: the entire
trunk and canopy should vanish at once, and a burst of Oak Log item drops
(one per log in the trunk, not one per canopy tile) should pop out and be
collectible into the bag. Try felling a second tree partway up the trunk
instead of at the bottom: only the upper portion (and the canopy) should come
down, leaving the lower trunk logs standing.

- [ ] **Step 5: Confirm the world looks reasonable**

Fly/walk across a wide stretch of the surface and confirm trees appear in
uneven, blended clusters rather than a perfectly even grid or a single flat
density - some stretches noticeably more wooded than others.

- [ ] **Step 6: Report back**

Summarize what was checked and any visual/gameplay issues noticed (e.g. tree
colors being hard to distinguish from grass, canopy shape looking odd) so
they can be triaged as follow-up polish rather than blocking this plan.

---

## Self-Review Notes

- **Spec coverage:** `ToolType`/gating (Task 1, 5), tree shape + cascade (Task 6, 7), starting inventory (Task 3), icon-color fix (Task 2), noise-blended density + spacing (Task 7) all have a task. `ActionResult` plumbing called out in the spec is Task 4.
- **Type consistency:** `BrokenTile{block, x, y}` is used identically in Tasks 4, 6, and in the `Game.cpp` call sites - checked against each task's code blocks.
- **Test count is illustrative, not gospel:** each task's "Expected" test count assumes the previous tasks' counts landed exactly as written. If an earlier task's test count differs (e.g. a task was split further), trust the suite's pass/fail status over the literal number.
