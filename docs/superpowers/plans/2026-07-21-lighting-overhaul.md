# Lighting Overhaul Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix "you can see through walls" (solid terrain currently always renders at full brightness regardless of light) and "everything's one shade of grey" (Torch/Lava share one tint, falloff was crushed to 3 tiles) by splitting `block` light into independent `torch`/`lava` channels, extending real light to 9 tiles, adding a real-light "borrow brightness from your brightest open neighbor" rule so walls near light sources actually look lit, and adding a new always-on, short-range, uncolored "ambient outline" visibility floor around the player so nearby terrain/ore reads as a dim silhouette even with no light nearby - closing the "solid ground is always fully visible" hole without reintroducing the old "solid ground renders pitch black" bug.

**Architecture:** `Lighting` (window-free, `Litharia_core`) gains a third BFS-computed channel and a new per-frame single-source BFS query (`ambientOutline`, same cost class as the existing `heldTorchLight`). `LightRenderer` (SFML Graphics, `Litharia` executable only) is rewritten to compute brightness/tint for every visible tile - solid or open - instead of skipping solid tiles, blending three tint colors proportionally instead of picking one winner. `Game` threads one new per-frame value (`ambientOutline`) into the existing `render()` call to `LightRenderer::draw`. No change to when `recomputeAll` runs (still block/Torch-edit and rate-limited lava-movement triggered) and no change to `DayNightClock`, the Torch item/recipe, or `ChunkRenderer`.

**Tech Stack:** C++20, SFML 3 (Graphics/Window/System), doctest, CMake + Visual Studio 18 2026 generator (existing project setup - no new dependencies).

## Global Constraints

- `Lighting`, `DayNightClock`, and every core-lib file may only depend on SFML System (or nothing) - no SFML Graphics/Window include, since they compile into `Litharia_core` alongside `FluidSim`/`Machines`/`Player`. `LightRenderer` (SFML Graphics) compiles only into the `Litharia` executable target and has no unit tests, matching `ChunkRenderer`/`MachineRenderer`.
- `LightLevel` stays a hard-capped bitfield struct with a `static_assert` on its size, same discipline as `Tile.h` - now `uint16_t`-backed (2 bytes/tile) since three 4-bit channels no longer fit in one byte.
- Light levels are 0-`Lighting::MAX_LIGHT_LEVEL` (now 9) on every channel, decaying by exactly 1 per orthogonal step, blocked entirely by a solid tile (`World::isSolid`).
- The stored lighting grid (`Lighting::recomputeAll`) is only ever recomputed on a block mined/placed, a Torch placed/removed, or rate-limited lava movement - never once a tick unconditionally. `ambientOutline` (like `heldTorchLight`) is a per-frame rendering-only query that never touches or forces a rebuild of the stored grid.
- No persistent "explored tile" memory: every visibility decision (real light or ambient outline) is recomputed from the player's *current* position and the *current* light state every frame. A tile that goes dark again renders exactly as if never seen.
- Every new/changed source file must build under the existing `cmake --build build --config Debug` invocation, and `Litharia_tests.exe` must pass in full after every task.

---

### Task 1: `isOre` block-classification helper

**Files:**
- Modify: `src/Blocks/Blocks.h`
- Test: `tests/test_fluids.cpp`

**Interfaces:**
- Produces: `bool isOre(BlockType type)` - Task 3 (`Lighting::ambientOutline`) uses this to give ore a brighter outline than plain Stone/Dirt.

- [ ] **Step 1: Write the failing test**

In `tests/test_fluids.cpp`, add a new test case right after the existing `"isWater/isLava/isFluid correctly classify every fluid level, and nothing else"` test case:

```cpp
TEST_CASE("isOre is true for exactly CopperOre, IronOre, and Coal")
{
    CHECK(isOre(BlockType::CopperOre));
    CHECK(isOre(BlockType::IronOre));
    CHECK(isOre(BlockType::Coal));

    CHECK_FALSE(isOre(BlockType::Air));
    CHECK_FALSE(isOre(BlockType::Grass));
    CHECK_FALSE(isOre(BlockType::Dirt));
    CHECK_FALSE(isOre(BlockType::Stone));
    CHECK_FALSE(isOre(BlockType::OakLog));
    CHECK_FALSE(isOre(BlockType::OakLeaves));
    CHECK_FALSE(isOre(BlockType::Obsidian));
    CHECK_FALSE(isOre(BlockType::Water8));
    CHECK_FALSE(isOre(BlockType::Lava8));
}
```

- [ ] **Step 2: Run the test to verify it fails to build**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `isOre` is not declared.

- [ ] **Step 3: Add `isOre` to `src/Blocks/Blocks.h`**

Add this function right after `isLava` (after line 133, before `isFluid`):

```cpp
// True for CopperOre, IronOre, and Coal - the game's three mineable ore
// blocks, as opposed to plain Stone/Dirt.
inline bool isOre(BlockType type)
{
    return type == BlockType::CopperOre || type == BlockType::IronOre || type == BlockType::Coal;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - the new `isOre` test passes, and the full existing suite still passes.

- [ ] **Step 5: Commit**

```bash
git add src/Blocks/Blocks.h tests/test_fluids.cpp
git commit -m "feat: add isOre block-classification helper"
```

---

### Task 2: Lighting - split `block` into independent `torch`/`lava` channels, extend falloff to 9

**Files:**
- Modify: `src/World/Lighting.h`
- Modify: `src/World/Lighting.cpp`
- Modify: `src/World/LightRenderer.cpp` (only the two `blockLight` call sites, renamed - no behavior change yet, that's Task 4)
- Modify: `tests/test_lighting.cpp`

**Interfaces:**
- Consumes: same as before (`World::inBounds/isSolid/get`, `isLava`, `Machines::all()`, `MachineType::Torch`).
- Produces: `int Lighting::torchLight(int,int) const`, `int Lighting::lavaLight(int,int) const` (replacing `blockLight`); `Lighting::MAX_LIGHT_LEVEL` becomes `9`. Task 3 (`ambientOutline`) and Task 4 (`LightRenderer`) both depend on these two new accessors existing.

- [ ] **Step 1: Write the failing tests**

Replace the entire contents of `tests/test_lighting.cpp` with the following (every `blockLight` call becomes `torchLight` or `lavaLight` depending on what actually lit that tile in the test, and `MAX_LIGHT_LEVEL` is asserted to be 9; test bodies are otherwise unchanged from the current file):

```cpp
#include "doctest.h"

#include "Core/Constants.h"
#include "Core/Direction.h"
#include "Machines/Machines.h"
#include "World/Lighting.h"
#include "World/World.h"

namespace
{

void fillSolid(World& world)
{
    world.fill(BlockType::Stone);
}

} // namespace

TEST_CASE("MAX_LIGHT_LEVEL is 9")
{
    CHECK(Lighting::MAX_LIGHT_LEVEL == 9);
}

TEST_CASE("a lone Lava tile lights itself on the lava channel only, decaying by 1 per step")
{
    World world;
    fillSolid(world);
    world.set(9, 10, BlockType::Air);
    world.set(11, 10, BlockType::Air);
    world.set(10, 10, BlockType::Lava8);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.lavaLight(10, 10) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.lavaLight(9, 10) == Lighting::MAX_LIGHT_LEVEL - 1);
    CHECK(lighting.lavaLight(11, 10) == Lighting::MAX_LIGHT_LEVEL - 1);

    CHECK(lighting.torchLight(10, 10) == 0);
}

TEST_CASE("lava light is blocked entirely by a solid tile")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Lava8);
    // (11, 10) stays Stone: solid, so light cannot pass through or into it.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.lavaLight(11, 10) == 0);
}

TEST_CASE("a placed Torch lights the torch channel only, decaying by 1 per step")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(10, 10) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.torchLight(9, 10) == Lighting::MAX_LIGHT_LEVEL - 1);

    CHECK(lighting.lavaLight(10, 10) == 0);
}

TEST_CASE("torch light is blocked entirely by a solid tile")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    // (11, 10) stays Stone: solid, unreachable.

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(11, 10) == 0);
}

TEST_CASE("a tile lit by both a Torch and Lava reads a nonzero level on each independent channel")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(11, 10, BlockType::Air);
    world.set(11, 11, BlockType::Lava8);

    Machines machines;
    machines.place(MachineType::Torch, 9, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(10, 10) > 0);
    CHECK(lighting.lavaLight(10, 10) > 0);
}

TEST_CASE("an open vertical shaft stays sky-lit at the max level at every depth")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 50; ++y)
        world.set(10, y, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 0) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.skyLight(10, 25) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.skyLight(10, 50) == Lighting::MAX_LIGHT_LEVEL);
}

TEST_CASE("sky light decays by 1 per step spreading sideways from an open shaft")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 20; ++y)
        world.set(10, y, BlockType::Air);
    world.set(11, 20, BlockType::Air); // one step sideways off the shaft, same depth

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 20) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.skyLight(11, 20) == Lighting::MAX_LIGHT_LEVEL - 1);
}

TEST_CASE("a cave with no path to an open shaft reads sky 0, even directly beside a lit one")
{
    World world;
    fillSolid(world);
    for (int y = 0; y <= 20; ++y)
        world.set(10, y, BlockType::Air);
    // A sealed pocket, walled off on every side by Stone - no orthogonal
    // path back to the shaft exists (the tile between them stays solid).
    world.set(12, 20, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(12, 20) == 0);
}

TEST_CASE("a column blocked from the surface gets no direct sky seed of its own")
{
    World world;
    fillSolid(world);
    // Open only near the very top; solid resumes at y=5 and stays solid the
    // rest of the way down, with no connection to any other open tile.
    world.set(10, 0, BlockType::Air);
    world.set(10, 1, BlockType::Air);
    world.set(10, 2, BlockType::Air);
    world.set(10, 3, BlockType::Air);
    world.set(10, 4, BlockType::Air);
    // (10, 5) onward stays Stone.

    // An isolated open tile further down the same column, walled off on
    // every side - this is what actually distinguishes a correct top-down
    // scan that stops (break) at the first solid tile from a buggy one that
    // skips past it (continue): a `continue` bug would keep scanning down
    // this column and wrongly seed this tile directly at the max level,
    // since it has no path back to the open shaft above and no other
    // reachable source, so anything other than 0 here proves the scan is
    // over-seeding.
    world.set(10, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.skyLight(10, 4) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.skyLight(10, 5) == 0);  // solid: never lit
    CHECK(lighting.skyLight(10, 10) == 0); // isolated pocket: no seed, no path
}

TEST_CASE("torchLight, lavaLight, and skyLight are 0 out of bounds")
{
    World world;
    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(-1, 0) == 0);
    CHECK(lighting.torchLight(WORLD_WIDTH, 0) == 0);
    CHECK(lighting.lavaLight(-1, 0) == 0);
    CHECK(lighting.lavaLight(WORLD_WIDTH, 0) == 0);
    CHECK(lighting.skyLight(0, -1) == 0);
    CHECK(lighting.skyLight(0, WORLD_HEIGHT) == 0);
}

TEST_CASE("heldTorchLight lights its source at the max level and decays by 1 per step")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(8, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.heldTorchLight(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(10, 10) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(levelAt(9, 10) == Lighting::MAX_LIGHT_LEVEL - 1);
    CHECK(levelAt(8, 10) == Lighting::MAX_LIGHT_LEVEL - 2);
}

TEST_CASE("heldTorchLight is blocked by solid tiles, same as a placed Torch")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    // (11, 10) stays Stone: solid, unreachable.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.heldTorchLight(world, {10, 10});

    for (const auto& [tile, level] : result)
    {
        if (tile.x == 11 && tile.y == 10)
            CHECK(false);
    }
}

TEST_CASE("heldTorchLight never writes to the stored grid")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    lighting.heldTorchLight(world, {10, 10});

    // No Torch or Lava anywhere in this world, so the stored grid must still
    // read 0 - heldTorchLight is a pure query, not a mutation.
    CHECK(lighting.torchLight(10, 10) == 0);
    CHECK(lighting.torchLight(9, 10) == 0);
}
```

- [ ] **Step 2: Run the tests to verify they fail to build**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `torchLight`/`lavaLight` are not declared, `MAX_LIGHT_LEVEL` is still 3.

- [ ] **Step 3: Rewrite `src/World/Lighting.h`**

Replace the file in full:

```cpp
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <SFML/System/Vector2.hpp>

class World;
class Machines;

// One tile's current light, split into three independent channels - see
// Lighting's own comment for why, and why this lives in its own grid rather
// than on Tile.
struct LightLevel
{
    std::uint16_t sky : 4;   // 0-MAX_LIGHT_LEVEL, this tile's sunlight exposure
    std::uint16_t torch : 4; // 0-MAX_LIGHT_LEVEL, this tile's Torch exposure
    std::uint16_t lava : 4;  // 0-MAX_LIGHT_LEVEL, this tile's Lava exposure
};
static_assert(sizeof(LightLevel) == 2, "Two bytes per tile: a 1000x500 world stays 1 MB.");

// Computes and stores per-tile lighting: how exposed to the sky a tile is
// (skyLight), how close it is to a placed Torch (torchLight), and how close
// it is to a Lava tile (lavaLight) - each 0-MAX_LIGHT_LEVEL, decaying by 1
// per orthogonal step, blocked entirely by solid tiles. Torch and Lava are
// separate channels (not merged into one "block light") purely so
// LightRenderer can tint them differently - a lava pool and a lit Torch
// should not look identical. A separate grid from World/Tile - light values
// change on every block/Torch edit (and skyLight's *effective* brightness
// changes every tick, scaled by the day/night clock - see DayNightClock),
// which doesn't belong on Tile any more than a fluid's flow state would.
//
// recomputeAll() rebuilds the whole grid from scratch rather than patching
// just the changed region: correctly patching only a local region after a
// light SOURCE disappears (a Torch mined, a wall sealing off a lit shaft)
// needs a "clear then re-flood from any surviving neighbour" pass, not a
// simple re-seed - full recompute sidesteps that complexity entirely and is
// still cheap, since it only runs on a block/Torch edit (a rare, player-
// paced event) or rate-limited lava movement, never once a tick
// unconditionally.
class Lighting
{
public:
    // How far light travels before going fully dark: a source (a placed
    // Torch, a Lava tile, an open sky column) starts at this level and
    // decays by 1 per orthogonal step, so a tile this many steps away is the
    // last one that still reads as lit at all - one dimmer at each step in
    // between.
    static constexpr int MAX_LIGHT_LEVEL = 9;

    Lighting();

    // Clears every tile's sky/torch/lava level to 0, then floods sky light
    // from every column open to the world's top edge, torch light from every
    // placed Torch, and lava light from every Lava tile. Call whenever a
    // block is mined or placed, or a Torch is placed or removed.
    void recomputeAll(const World& world, const Machines& machines);

    int skyLight(int x, int y) const;   // 0-MAX_LIGHT_LEVEL; 0 out of bounds
    int torchLight(int x, int y) const; // 0-MAX_LIGHT_LEVEL; 0 out of bounds
    int lavaLight(int x, int y) const;  // 0-MAX_LIGHT_LEVEL; 0 out of bounds

    // A single-source flood fill from `source` at MAX_LIGHT_LEVEL - the same
    // brightness and decay/occlusion rule as a placed Torch's own
    // torchLight, but computed fresh every call rather than stored in
    // `levels`. Used for the player's held Torch, which moves with them
    // every frame: naturally bounded to within MAX_LIGHT_LEVEL steps of
    // `source` (the seed starts there and floodFill's decay reaches 0 by
    // then), so this stays cheap enough to call once a frame without forcing
    // a full recompute.
    std::vector<std::pair<sf::Vector2i, int>> heldTorchLight(const World& world,
                                                              sf::Vector2i source) const;

private:
    struct LightSeed
    {
        int x;
        int y;
        int level;
    };

    // Multi-source BFS: every seed starts queued at its own level; each step,
    // the 4 orthogonal neighbours get level-1 wherever that beats what they
    // already have, stopping at level 0 or a solid tile (World::isSolid).
    // Since decay is always exactly 1 per step, a tile is only ever improved
    // once, so the BFS visit order alone is already the full sparse result -
    // no second full-grid scan needed to extract it. Static (no `this`) so it
    // can serve the world-wide recompute, a single moving source
    // (Lighting::heldTorchLight), and the ambient-outline reachability query
    // alike.
    static std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world,
                                                                const std::vector<LightSeed>& seeds);

    std::vector<LightLevel> levels; // WORLD_WIDTH * WORLD_HEIGHT
};
```

- [ ] **Step 4: Rewrite `src/World/Lighting.cpp`**

Replace the file in full:

```cpp
#include "Lighting.h"

#include <algorithm>

#include "../Blocks/Blocks.h"
#include "../Core/Constants.h"
#include "../Machines/Machine.h"
#include "../Machines/MachineType.h"
#include "../Machines/Machines.h"
#include "World.h"

Lighting::Lighting()
    : levels(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, LightLevel{0, 0, 0})
{
}

std::vector<std::pair<sf::Vector2i, int>> Lighting::floodFill(const World& world,
                                                                const std::vector<LightSeed>& seeds)
{
    std::vector<std::int8_t> best(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, -1);
    std::vector<LightSeed> queue;

    auto tryVisit = [&](int x, int y, int level)
    {
        if (level <= 0 || !world.inBounds(x, y) || world.isSolid(x, y))
            return;

        const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
        if (level <= best[i])
            return;

        best[i] = static_cast<std::int8_t>(level);
        queue.push_back({x, y, level});
    };

    for (const LightSeed& seed : seeds)
        tryVisit(seed.x, seed.y, seed.level);

    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const LightSeed e = queue[head];
        tryVisit(e.x - 1, e.y, e.level - 1);
        tryVisit(e.x + 1, e.y, e.level - 1);
        tryVisit(e.x, e.y - 1, e.level - 1);
        tryVisit(e.x, e.y + 1, e.level - 1);
    }

    std::vector<std::pair<sf::Vector2i, int>> result;
    result.reserve(queue.size());
    for (const LightSeed& e : queue)
        result.push_back({{e.x, e.y}, e.level});

    return result;
}

void Lighting::recomputeAll(const World& world, const Machines& machines)
{
    std::vector<LightSeed> skySeeds;

    for (int x = 0; x < WORLD_WIDTH; ++x)
    {
        for (int y = 0; y < WORLD_HEIGHT; ++y)
        {
            if (world.isSolid(x, y))
                break;

            skySeeds.push_back({x, y, MAX_LIGHT_LEVEL});
        }
    }

    std::vector<LightSeed> torchSeeds;

    for (const Machine& m : machines.all())
        if (m.type == MachineType::Torch)
            torchSeeds.push_back({m.x, m.y, MAX_LIGHT_LEVEL});

    std::vector<LightSeed> lavaSeeds;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int x = 0; x < WORLD_WIDTH; ++x)
            if (isLava(world.get(x, y)))
                lavaSeeds.push_back({x, y, MAX_LIGHT_LEVEL});

    const auto skyResult = floodFill(world, skySeeds);
    const auto torchResult = floodFill(world, torchSeeds);
    const auto lavaResult = floodFill(world, lavaSeeds);

    std::fill(levels.begin(), levels.end(), LightLevel{0, 0, 0});

    for (const auto& [tile, level] : skyResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].sky =
            static_cast<std::uint16_t>(level);

    for (const auto& [tile, level] : torchResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].torch =
            static_cast<std::uint16_t>(level);

    for (const auto& [tile, level] : lavaResult)
        levels[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x].lava =
            static_cast<std::uint16_t>(level);
}

int Lighting::skyLight(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    return levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].sky;
}

int Lighting::torchLight(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    return levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].torch;
}

int Lighting::lavaLight(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    return levels[static_cast<std::size_t>(y) * WORLD_WIDTH + x].lava;
}

std::vector<std::pair<sf::Vector2i, int>> Lighting::heldTorchLight(const World& world,
                                                                    sf::Vector2i source) const
{
    return floodFill(world, {LightSeed{source.x, source.y, MAX_LIGHT_LEVEL}});
}
```

- [ ] **Step 5: Fix the two `blockLight` call sites in `src/World/LightRenderer.cpp`**

This is a mechanical rename only - `LightRenderer`'s actual behavior change (solid tiles, 3-way blending) is Task 4. For now, just keep it compiling: change

```cpp
            int blockEffective = lighting.blockLight(x, y);
```

to

```cpp
            int blockEffective = lighting.torchLight(x, y) + lighting.lavaLight(x, y);
```

(A temporary sum just to keep this file compiling and behaviorally equivalent-ish until Task 4 replaces this whole function body - it is not the final blending logic.)

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - every `Lighting` test passes, and the full existing suite still passes.

Run: `cmake --build build --config Debug --target Litharia`
Expected: SUCCESS - `LightRenderer.cpp`'s temporary fix compiles.

- [ ] **Step 7: Commit**

```bash
git add src/World/Lighting.h src/World/Lighting.cpp src/World/LightRenderer.cpp tests/test_lighting.cpp
git commit -m "feat: split Lighting's block channel into independent torch/lava channels, extend falloff to 9"
```

---

### Task 3: `Lighting::ambientOutline` - short-range, uncolored visibility around the player

**Files:**
- Modify: `src/World/Lighting.h`
- Modify: `src/World/Lighting.cpp`
- Modify: `tests/test_lighting.cpp`

**Interfaces:**
- Consumes: `isOre` (Task 1), same `World`/`Machines` dependencies as `recomputeAll`.
- Produces: `std::vector<std::pair<sf::Vector2i,int>> Lighting::ambientOutline(const World&, sf::Vector2i playerTile) const` - Task 4 calls this every frame from `Game::render` and consumes its result in `LightRenderer` the same way it already consumes `heldTorchLight`'s.

- [ ] **Step 1: Write the failing tests**

Add these test cases to the end of `tests/test_lighting.cpp`:

```cpp
TEST_CASE("ambientOutline marks a solid tile bordering the player's reachable open space")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    // (11, 10) stays Stone: the wall immediately beside the player.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(11, 10) == Lighting::AMBIENT_OUTLINE_LEVEL);
}

TEST_CASE("ambientOutline marks an ore tile brighter than a plain stone tile at the same distance")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(10, 9, BlockType::Air);
    world.set(11, 9, BlockType::CopperOre);
    // (8, 10) stays Stone - a plain neighbor at the same one-step distance.

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(11, 9) == Lighting::AMBIENT_OUTLINE_ORE_LEVEL);
    CHECK(levelAt(8, 10) == Lighting::AMBIENT_OUTLINE_LEVEL);
    CHECK(Lighting::AMBIENT_OUTLINE_ORE_LEVEL > Lighting::AMBIENT_OUTLINE_LEVEL);
}

TEST_CASE("ambientOutline reveals open tiles within reach even with no light, but not disconnected ones")
{
    World world;
    fillSolid(world);
    for (int x = 10; x <= 15; ++x)
        world.set(x, 10, BlockType::Air);
    // A sealed pocket, walled off on every side - no path back to the player.
    world.set(20, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(15, 10) == Lighting::AMBIENT_OUTLINE_LEVEL);
    CHECK(levelAt(20, 10) == 0);
}

TEST_CASE("ambientOutline does not reach past AMBIENT_OUTLINE_RADIUS steps from the player")
{
    World world;
    fillSolid(world);
    for (int x = 10; x <= 10 + Lighting::AMBIENT_OUTLINE_RADIUS + 5; ++x)
        world.set(x, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(10 + Lighting::AMBIENT_OUTLINE_RADIUS - 1, 10) == Lighting::AMBIENT_OUTLINE_LEVEL);
    CHECK(levelAt(10 + Lighting::AMBIENT_OUTLINE_RADIUS + 5, 10) == 0);
}

TEST_CASE("ambientOutline never writes to the stored grid")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    lighting.ambientOutline(world, {10, 10});

    CHECK(lighting.torchLight(10, 10) == 0);
    CHECK(lighting.lavaLight(10, 10) == 0);
    CHECK(lighting.skyLight(10, 10) == 0);
}
```

- [ ] **Step 2: Run the tests to verify they fail to build**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `Lighting::ambientOutline`, `Lighting::AMBIENT_OUTLINE_LEVEL`, `Lighting::AMBIENT_OUTLINE_ORE_LEVEL`, and `Lighting::AMBIENT_OUTLINE_RADIUS` are not declared.

- [ ] **Step 3: Add the ambient-outline constants and method declaration to `src/World/Lighting.h`**

Add these three `static constexpr int` members to the `public:` section, right after `MAX_LIGHT_LEVEL`:

```cpp
    // How far the player's own "eyes adjusting to the dark" ambient
    // visibility reaches - much further than MAX_LIGHT_LEVEL, since it's not
    // real light, just enough to make out shapes and ore nearby. See
    // ambientOutline().
    static constexpr int AMBIENT_OUTLINE_RADIUS = 20;

    // Flat (non-decaying) brightness ambientOutline() assigns to a plain
    // solid tile or reachable open tile within range - deliberately small
    // relative to MAX_LIGHT_LEVEL so it reads as "barely lightens," not as
    // real light.
    static constexpr int AMBIENT_OUTLINE_LEVEL = 1;

    // Same as AMBIENT_OUTLINE_LEVEL, but for a solid tile that is itself an
    // ore (see isOre) - slightly brighter so ore reads as "there's something
    // here worth digging" without revealing which ore or how much.
    static constexpr int AMBIENT_OUTLINE_ORE_LEVEL = 2;
```

Add this method declaration right after `heldTorchLight`'s declaration:

```cpp
    // A short-range, uncolored visibility floor around the player's current
    // tile, recomputed fresh every frame (never stored, never forcing a
    // recompute) - the "you can make out shapes and ore nearby even with no
    // light" mechanic that replaces solid ground's old always-fully-visible
    // behavior. A bounded BFS from `playerTile`, traveling only through open
    // tiles up to AMBIENT_OUTLINE_RADIUS steps (so a sealed pocket with no
    // path back to the player gets nothing, exactly like real light) -
    // every open tile reached this way, and every solid tile bordering one,
    // is included in the result at AMBIENT_OUTLINE_LEVEL (AMBIENT_OUTLINE_ORE_LEVEL
    // if the solid tile is ore). Unlike real light, this value does not
    // decay with distance inside the radius - it's a flat floor, not a
    // gradient.
    std::vector<std::pair<sf::Vector2i, int>> ambientOutline(const World& world,
                                                              sf::Vector2i playerTile) const;
```

- [ ] **Step 4: Implement `ambientOutline` in `src/World/Lighting.cpp`**

Add this at the end of the file:

```cpp
std::vector<std::pair<sf::Vector2i, int>> Lighting::ambientOutline(const World& world,
                                                                    sf::Vector2i playerTile) const
{
    // A reachability BFS through open tiles only, bounded by step count
    // rather than a decaying value (every reached tile gets the same flat
    // level) - reuses the same "visit each tile at most once, solid tiles
    // are walls" shape as floodFill, but floodFill's early-exit is keyed on
    // a *level* reaching 0, which doesn't fit "same value everywhere, cut
    // off by distance" - so this is its own small BFS instead of a floodFill
    // call.
    if (!world.inBounds(playerTile.x, playerTile.y) || world.isSolid(playerTile.x, playerTile.y))
        return {};

    std::vector<std::int8_t> visited(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0);
    std::vector<int> stepOf(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0);
    std::vector<sf::Vector2i> queue{playerTile};

    visited[static_cast<std::size_t>(playerTile.y) * WORLD_WIDTH + playerTile.x] = 1;

    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const sf::Vector2i tile = queue[head];
        const int steps = stepOf[static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x];

        if (steps >= AMBIENT_OUTLINE_RADIUS)
            continue;

        const sf::Vector2i neighbors[4] = {
            {tile.x - 1, tile.y}, {tile.x + 1, tile.y}, {tile.x, tile.y - 1}, {tile.x, tile.y + 1}};

        for (const sf::Vector2i& n : neighbors)
        {
            if (!world.inBounds(n.x, n.y) || world.isSolid(n.x, n.y))
                continue;

            const std::size_t i = static_cast<std::size_t>(n.y) * WORLD_WIDTH + n.x;
            if (visited[i])
                continue;

            visited[i] = 1;
            stepOf[i] = steps + 1;
            queue.push_back(n);
        }
    }

    std::vector<std::pair<sf::Vector2i, int>> result;
    result.reserve(queue.size() * 5);

    for (const sf::Vector2i& tile : queue)
    {
        result.push_back({tile, AMBIENT_OUTLINE_LEVEL});

        const sf::Vector2i neighbors[4] = {
            {tile.x - 1, tile.y}, {tile.x + 1, tile.y}, {tile.x, tile.y - 1}, {tile.x, tile.y + 1}};

        for (const sf::Vector2i& n : neighbors)
        {
            if (!world.inBounds(n.x, n.y) || !world.isSolid(n.x, n.y))
                continue;

            const int level = isOre(world.get(n.x, n.y)) ? AMBIENT_OUTLINE_ORE_LEVEL : AMBIENT_OUTLINE_LEVEL;
            result.push_back({n, level});
        }
    }

    return result;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - every `ambientOutline` test passes, and the full existing suite still passes.

Note: `ambientOutline`'s result can list the same solid tile more than once (once per open neighbor that borders it) - that's fine, `LightRenderer` (Task 4) folds duplicates into a hashmap keyed by tile, taking the max level per tile, same pattern it already uses for `heldTorchLight`.

- [ ] **Step 6: Commit**

```bash
git add src/World/Lighting.h src/World/Lighting.cpp tests/test_lighting.cpp
git commit -m "feat: add Lighting::ambientOutline for short-range visibility around the player"
```

---

### Task 4: `LightRenderer` - light solid tiles, blend three tints, apply the ambient-outline floor, and wire it into `Game::render`

This task's two parts (rewriting `LightRenderer` and updating its sole call site in `Game::render`) are one task, not two: `LightRenderer::draw`'s signature changes, and it already has exactly one caller - splitting the rewrite from the call-site update would leave the `Litharia` executable target broken (won't link) at the commit boundary between them, which is a bad bisection point. Both change together, in one commit, with both targets green at the end.

**Files:**
- Modify: `src/World/LightRenderer.h`
- Modify: `src/World/LightRenderer.cpp`
- Modify: `src/Game/Game.cpp`

**Interfaces:**
- Consumes: `Lighting::skyLight/torchLight/lavaLight` (Task 2), `Lighting::MAX_LIGHT_LEVEL` (Task 2), the `heldTorchLight` result shape (unchanged), `Lighting::ambientOutline` (Task 3).
- Produces: the fully wired feature - a running `Litharia` shows colored torch/lava light, walls lit near a light source, a dim ambient outline around the player elsewhere, and full darkness/hiding everywhere else. Nothing later depends on new interfaces from this task; it is the integration point.

`LightRenderer` itself has no unit test, matching the project's existing convention for SFML Graphics-only renderer classes (`ChunkRenderer`, `MachineRenderer`) - verified by the executable building, and manually by running the game (Step 4 below). The `Game::render` change has no isolated unit test either (SFML Graphics/Window code, no window-free test target) - verification is that `Litharia_tests` still passes in full (nothing here changes core-lib behavior) plus the same manual check.

- [ ] **Step 1: Update the `draw` declaration in `src/World/LightRenderer.h`**

Replace the file in full:

```cpp
#pragma once

#include <SFML/Graphics.hpp>

#include <utility>
#include <vector>

#include "Lighting.h"

class World;

// Draws a color overlay on top of everything else in the world, applied with
// sf::BlendMultiply so it darkens/tints whatever was already drawn
// underneath without needing every entity to know about lighting itself.
// Every visible tile - solid or open - gets a brightness and a tint:
//
// - An open tile's brightness/tint comes straight from its own stored
//   sky/torch/lava levels (plus the player's held-Torch light, folded into
//   the torch channel).
// - A solid tile has no stored light of its own (Lighting's BFS never
//   visits solid tiles), so it borrows one step dimmer than the brightest
//   value among its 4 orthogonal open neighbours, per channel - this is
//   what makes a torch-lit tunnel's walls actually look lit, instead of
//   either pitch black (the pre-existing bug) or always full-bright
//   regardless of nearby light (the bug this class fixes).
// - If neither of the above puts any real light on a tile, it falls back to
//   the ambient-outline floor (see Lighting::ambientOutline): a flat, dim,
//   uncolored "you can make out shapes and ore nearby" brightness. Real
//   light always wins over the ambient floor when both apply.
// - A tile with neither real light nor ambient-outline coverage renders
//   fully black - hidden, exactly like a disconnected cave already read
//   before this class existed, now correctly extended to solid ground too.
//
// Tints: lava is a hot red-orange, torch a warmer yellow (distinct from
// lava, unlike before), sky a cool-to-white gradient driven by the day/night
// clock. Where more than one real channel is lit, the tint is a weighted
// blend of all three rather than a single "whichever's highest wins"
// switch.
//
// Rebuilds every visible tile fresh every single frame rather than caching
// per-chunk geometry, like ChunkRenderer does: the day/night clock, the
// player's held-Torch light, and the player's own position (ambientOutline)
// all change continuously, so there is no "clean, skip it" case to cache
// against.
class LightRenderer
{
public:
    void draw(sf::RenderTarget& target, const sf::View& view, const World& world, const Lighting& lighting,
              float daylightFactor, const std::vector<std::pair<sf::Vector2i, int>>& heldTorchLight,
              const std::vector<std::pair<sf::Vector2i, int>>& ambientOutline) const;
};
```

- [ ] **Step 2: Rewrite `src/World/LightRenderer.cpp`**

Replace the file in full:

```cpp
#include "LightRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "../Core/Constants.h"
#include "World.h"

namespace
{

constexpr sf::Color NIGHT_TINT(20, 25, 45);
constexpr sf::Color DAY_TINT(225, 235, 250);
constexpr sf::Color TORCH_TINT(255, 200, 110);
constexpr sf::Color LAVA_TINT(255, 90, 40);
constexpr sf::Color OUTLINE_TINT(90, 90, 100);

sf::Color lerp(sf::Color a, sf::Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return sf::Color(
        static_cast<std::uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<std::uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<std::uint8_t>(a.b + (b.b - a.b) * t));
}

std::int64_t tileKey(int x, int y)
{
    return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::uint32_t>(y);
}

// Two triangles per tile: SFML 3 has no quad primitive. Matches Chunks.cpp's
// own appendQuad - small enough that each file keeping its own copy reads
// more clearly than sharing a one-off header for it.
void appendQuad(sf::VertexArray& vertices, float left, float top, float right, float bottom,
                sf::Color color)
{
    vertices.append({{left, top}, color});
    vertices.append({{right, top}, color});
    vertices.append({{right, bottom}, color});

    vertices.append({{left, top}, color});
    vertices.append({{right, bottom}, color});
    vertices.append({{left, bottom}, color});
}

} // namespace

void LightRenderer::draw(sf::RenderTarget& target, const sf::View& view, const World& world,
                          const Lighting& lighting, float daylightFactor,
                          const std::vector<std::pair<sf::Vector2i, int>>& heldTorchLight,
                          const std::vector<std::pair<sf::Vector2i, int>>& ambientOutline) const
{
    const sf::Vector2f center = view.getCenter();
    const sf::Vector2f size = view.getSize();

    const float viewLeft = center.x - size.x * 0.5f;
    const float viewTop = center.y - size.y * 0.5f;
    const float viewRight = center.x + size.x * 0.5f;
    const float viewBottom = center.y + size.y * 0.5f;

    const int firstX = std::max(0, static_cast<int>(std::floor(viewLeft / TILE_SIZE)) - 1);
    const int firstY = std::max(0, static_cast<int>(std::floor(viewTop / TILE_SIZE)) - 1);
    const int lastX = std::min(WORLD_WIDTH - 1, static_cast<int>(std::floor(viewRight / TILE_SIZE)) + 1);
    const int lastY = std::min(WORLD_HEIGHT - 1, static_cast<int>(std::floor(viewBottom / TILE_SIZE)) + 1);

    std::unordered_map<std::int64_t, int> heldMap;
    for (const auto& [tile, level] : heldTorchLight)
        heldMap[tileKey(tile.x, tile.y)] = level;

    std::unordered_map<std::int64_t, int> outlineMap;
    for (const auto& [tile, level] : ambientOutline)
    {
        int& slot = outlineMap[tileKey(tile.x, tile.y)];
        slot = std::max(slot, level);
    }

    const sf::Color skyTint = lerp(NIGHT_TINT, DAY_TINT, daylightFactor);

    sf::VertexArray vertices(sf::PrimitiveType::Triangles);

    for (int y = firstY; y <= lastY; ++y)
    {
        for (int x = firstX; x <= lastX; ++x)
        {
            int skyRaw;
            int torchRaw;
            int lavaRaw;

            if (world.isSolid(x, y))
            {
                // A solid tile borrows one step dimmer than its brightest
                // open neighbour, per channel - Lighting's BFS never
                // assigns a solid tile its own light, so a neighbour that's
                // itself solid always reads 0 here already, no separate
                // isSolid check needed on the neighbours themselves.
                const int skyN = std::max({lighting.skyLight(x - 1, y), lighting.skyLight(x + 1, y),
                                            lighting.skyLight(x, y - 1), lighting.skyLight(x, y + 1)});
                const int torchN = std::max({lighting.torchLight(x - 1, y), lighting.torchLight(x + 1, y),
                                              lighting.torchLight(x, y - 1), lighting.torchLight(x, y + 1)});
                const int lavaN = std::max({lighting.lavaLight(x - 1, y), lighting.lavaLight(x + 1, y),
                                             lighting.lavaLight(x, y - 1), lighting.lavaLight(x, y + 1)});

                skyRaw = std::max(0, skyN - 1);
                torchRaw = std::max(0, torchN - 1);
                lavaRaw = std::max(0, lavaN - 1);
            }
            else
            {
                skyRaw = lighting.skyLight(x, y);
                torchRaw = lighting.torchLight(x, y);
                lavaRaw = lighting.lavaLight(x, y);
            }

            const auto heldIt = heldMap.find(tileKey(x, y));
            if (heldIt != heldMap.end())
                torchRaw = std::max(torchRaw, heldIt->second);

            const float skyEffective = static_cast<float>(skyRaw) * daylightFactor;
            const float torchEffective = static_cast<float>(torchRaw);
            const float lavaEffective = static_cast<float>(lavaRaw);

            const float total = skyEffective + torchEffective + lavaEffective;

            sf::Color overlay;

            if (total > 0.0f)
            {
                const float brightness = std::clamp(
                    std::max({skyEffective, torchEffective, lavaEffective}) / Lighting::MAX_LIGHT_LEVEL,
                    0.0f, 1.0f);

                const sf::Color blended(
                    static_cast<std::uint8_t>(
                        (skyTint.r * skyEffective + TORCH_TINT.r * torchEffective + LAVA_TINT.r * lavaEffective) /
                        total),
                    static_cast<std::uint8_t>(
                        (skyTint.g * skyEffective + TORCH_TINT.g * torchEffective + LAVA_TINT.g * lavaEffective) /
                        total),
                    static_cast<std::uint8_t>(
                        (skyTint.b * skyEffective + TORCH_TINT.b * torchEffective + LAVA_TINT.b * lavaEffective) /
                        total));

                overlay = lerp(sf::Color::Black, blended, brightness);
            }
            else
            {
                const auto outlineIt = outlineMap.find(tileKey(x, y));
                const int outlineLevel = outlineIt != outlineMap.end() ? outlineIt->second : 0;
                const float brightness =
                    std::clamp(static_cast<float>(outlineLevel) / Lighting::MAX_LIGHT_LEVEL, 0.0f, 1.0f);

                overlay = lerp(sf::Color::Black, OUTLINE_TINT, brightness);
            }

            const float left = static_cast<float>(x * TILE_SIZE);
            const float top = static_cast<float>(y * TILE_SIZE);
            appendQuad(vertices, left, top, left + TILE_SIZE, top + TILE_SIZE, overlay);
        }
    }

    target.draw(vertices, sf::BlendMultiply);
}
```

- [ ] **Step 3: Compute `playerTile` once and pass `ambientOutline` into the `draw` call**

In `src/Game/Game.cpp`, find `Game::render`'s existing block (currently around line 1123-1133):

```cpp
    std::vector<std::pair<sf::Vector2i, int>> heldLight;
    const ItemStack& held = player.inventory().slot(player.selectedSlot());
    if (held.type == ItemType::Torch)
    {
        const sf::Vector2i playerTile{
            static_cast<int>(std::floor(player.center().x / TILE_SIZE)),
            static_cast<int>(std::floor(player.center().y / TILE_SIZE))};
        heldLight = lighting.heldTorchLight(world, playerTile);
    }

    lightRenderer.draw(window, camera.view(), world, lighting, dayNightClock.daylightFactor(), heldLight);
```

Replace it with:

```cpp
    const sf::Vector2i playerTile{static_cast<int>(std::floor(player.center().x / TILE_SIZE)),
                                   static_cast<int>(std::floor(player.center().y / TILE_SIZE))};

    std::vector<std::pair<sf::Vector2i, int>> heldLight;
    const ItemStack& held = player.inventory().slot(player.selectedSlot());
    if (held.type == ItemType::Torch)
        heldLight = lighting.heldTorchLight(world, playerTile);

    const std::vector<std::pair<sf::Vector2i, int>> ambientOutline = lighting.ambientOutline(world, playerTile);

    lightRenderer.draw(window, camera.view(), world, lighting, dayNightClock.daylightFactor(), heldLight,
                        ambientOutline);
```

- [ ] **Step 4: Build both targets and run the full test suite**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - the full suite (every existing test plus every test from Tasks 1-3) passes unchanged; nothing in this task touches core-lib behavior.

Run: `cmake --build build --config Debug --target Litharia`
Expected: SUCCESS - the game builds with the full lighting overhaul wired in.

- [ ] **Step 5: Manually verify in-game**

Run `./build/Debug/Litharia.exe` and confirm:
- A lit Torch or Lava pool colors nearby walls (not just open tiles) in its own tint - lava reads redder than a Torch's yellow.
- Walking a few tiles away from any light source, terrain (walls, ore) is still faintly visible as a dim grey outline, not pitch black and not fully bright.
- Walking further still (or looking toward a sealed, never-mined pocket), terrain goes fully black/hidden - it is no longer possible to see ore veins or cave shapes far outside the player's reach.
- An unconnected cave (no dug path to it) stays fully hidden even when it happens to be close by, exactly as before this overhaul.

This is a manual check, not an automated one, per the project's existing convention for `LightRenderer`/`ChunkRenderer` (no window-free test target for rendered visuals).

- [ ] **Step 6: Commit**

```bash
git add src/World/LightRenderer.h src/World/LightRenderer.cpp src/Game/Game.cpp
git commit -m "feat: light solid tiles, blend torch/lava/sky tints, and wire the ambient-outline floor into Game::render"
```
