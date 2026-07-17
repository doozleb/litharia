# Hill Caves and Ore Rebalancing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 4 hand-placed, hill-anchored branching cave systems (2 near spawn, 2 far) that reach iron depth, plus a rare "wrong-zone" ore band so iron is occasionally findable shallow and copper occasionally findable deep.

**Architecture:** Three independent additions to `TerrainGenerator`: (1) two new low-density ore bands reusing the existing `scatterOre` mechanism, (2) a new public `findHillPeak` helper that locates the most elevated column near a target x, (3) a new `carveSpecialCaves` pass (trunk random-walk + branch dead-ends, both pure functions of the world seed) wired into `generate()` after the existing noise-cave pass.

**Tech Stack:** C++20, `noise::hashFloat` (existing seeded hash utility) for all randomness — no new RNG type, no SFML (this file is core, tested via doctest in `Litharia_tests`).

## Global Constraints

- Everywhere outside the 4 special caves, cave generation is unchanged — no change to `carveCaves`, `CAVE_FREQUENCY`, or either `CAVE_THRESHOLD_*` constant.
- `IRON_MIN_Y` (320) and `IRON_MAX_Y` (495) and `COPPER_MIN_Y` (200) and `COPPER_MAX_Y` (340) do not move — they keep meaning exactly what they mean today; new rare bands are separate constants.
- Rare bands: `IRON_SHALLOW_MIN_Y..IRON_SHALLOW_MAX_Y` = 90..319, `COPPER_DEEP_MIN_Y..COPPER_DEEP_MAX_Y` = 341..495 — adjacent to their common band with no gap and no overlap.
- Rare-band density = common-band density / 5 for each ore (`IRON_DENSITY / 5.0f`, `COPPER_DENSITY / 5.0f`).
- Cave targets: `spawnX = WORLD_WIDTH / 2`; offsets `SPECIAL_CAVE_NEAR_OFFSET = 100`, `SPECIAL_CAVE_FAR_OFFSET = 350`; 4 targets = `spawnX - 350, spawnX - 100, spawnX + 100, spawnX + 350`.
- Hill anchoring: `HILL_SEARCH_RADIUS = 30` tiles either side of a target; the entrance is the x with the smallest `surfaceHeight()` in that window (smaller y = higher elevation); ties break to the first x found.
- Trunk carving stops the first step its y reaches `IRON_MIN_Y` — "down to iron layer minimum."
- Branches: 3-6 per cave, 15-40 steps long, spawned from random points on the trunk's own path, one level deep only (no branches of branches).
- All randomness is `noise::hashFloat(x, y, seed)` — a pure function of the world seed, matching this file's existing determinism guarantee (same seed → byte-identical world).
- Coal's band and density are untouched.

---

### Task 1: Rare ore bands (shallow iron, deep copper)

**Files:**
- Modify: `src/World/TerrainGenerator.h`
- Modify: `src/World/TerrainGenerator.cpp`
- Test: `tests/test_terrain.cpp`

**Interfaces:**
- Consumes: nothing new — reuses the existing `scatterOre`/`growVein`/`Ore` struct mechanism unchanged.
- Produces: `TerrainGenerator::IRON_SHALLOW_MIN_Y`, `IRON_SHALLOW_MAX_Y`, `COPPER_DEEP_MIN_Y`, `COPPER_DEEP_MAX_Y` (public `static constexpr int`), used by Task 3's test to reason about ore vs. cave interaction only incidentally — no other task depends on these directly.

- [ ] **Step 1: Write the failing tests**

Open `tests/test_terrain.cpp`. Replace the existing `TEST_CASE("each ore stays inside its own depth band")` (it must widen to the new union range, otherwise it will fail once rare-band ore exists) with:

```cpp
TEST_CASE("each ore stays inside its own depth band, common or rare")
{
    World world;
    TerrainGenerator(555).generate(world);

    int copper = 0;
    int iron = 0;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
    {
        for (int x = 0; x < WORLD_WIDTH; ++x)
        {
            const BlockType type = world.get(x, y);

            if (type == BlockType::CopperOre)
            {
                ++copper;
                REQUIRE(y >= TerrainGenerator::COPPER_MIN_Y);
                REQUIRE(y <= TerrainGenerator::COPPER_DEEP_MAX_Y);
            }
            else if (type == BlockType::IronOre)
            {
                ++iron;
                REQUIRE(y >= TerrainGenerator::IRON_SHALLOW_MIN_Y);
                REQUIRE(y <= TerrainGenerator::IRON_MAX_Y);
            }
        }
    }

    // Both ores exist, and copper is the commoner shallow one.
    CHECK(copper > 100);
    CHECK(iron > 100);
}
```

Then add a new test case right after it:

```cpp
TEST_CASE("iron rarely appears shallow, copper rarely appears deep, but each stays rare")
{
    World world;
    TerrainGenerator(2025).generate(world);

    int shallowIron = 0;
    int commonIron = 0;
    int deepCopper = 0;
    int commonCopper = 0;

    for (int y = 0; y < WORLD_HEIGHT; ++y)
    {
        for (int x = 0; x < WORLD_WIDTH; ++x)
        {
            const BlockType type = world.get(x, y);

            if (type == BlockType::IronOre)
            {
                if (y <= TerrainGenerator::IRON_SHALLOW_MAX_Y)
                    ++shallowIron;
                else
                    ++commonIron;
            }
            else if (type == BlockType::CopperOre)
            {
                if (y >= TerrainGenerator::COPPER_DEEP_MIN_Y)
                    ++deepCopper;
                else
                    ++commonCopper;
            }
        }
    }

    // The rare bands must exist at all...
    CHECK(shallowIron > 0);
    CHECK(deepCopper > 0);

    // ...but stay clearly rarer than the common band they're paired with.
    CHECK(shallowIron < commonIron / 2);
    CHECK(deepCopper < commonCopper / 2);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:
```
"C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build C:/Litharia/build --config Debug --target Litharia_tests
```

Expected: **build fails** — `TerrainGenerator::IRON_SHALLOW_MIN_Y` (and the other 3 new constants) don't exist yet. That compile failure is the "red" of this step; there is no runnable binary until Step 3 adds the constants.

- [ ] **Step 3: Add the new constants**

In `src/World/TerrainGenerator.h`, find:

```cpp
    static constexpr int COPPER_MIN_Y = 200;
    static constexpr int COPPER_MAX_Y = 340;

    static constexpr int IRON_MIN_Y = 320;
    static constexpr int IRON_MAX_Y = 495;
```

Replace with:

```cpp
    static constexpr int COPPER_MIN_Y = 200;
    static constexpr int COPPER_MAX_Y = 340;

    // A much rarer deep band: copper is normally shallow, but a lucky dig
    // can still turn up copper this far down.
    static constexpr int COPPER_DEEP_MIN_Y = 341;
    static constexpr int COPPER_DEEP_MAX_Y = 495;

    static constexpr int IRON_MIN_Y = 320;
    static constexpr int IRON_MAX_Y = 495;

    // A much rarer shallow band: iron is normally deep, but a lucky dig can
    // still turn up iron this high.
    static constexpr int IRON_SHALLOW_MIN_Y = 90;
    static constexpr int IRON_SHALLOW_MAX_Y = 319;
```

- [ ] **Step 4: Run tests to verify the new constants compile but the rarity test still fails**

Run the same build command as Step 2.

Expected: **build succeeds**, then running `C:/Litharia/build/Debug/Litharia_tests.exe` shows `"each ore stays inside its own depth band, common or rare"` passing (the widened bound already matches today's ore, since no rare-band ore exists yet) but `"iron rarely appears shallow, copper rarely appears deep, but each stays rare"` **failing** on `CHECK(shallowIron > 0)` and `CHECK(deepCopper > 0)` — there is no rare-band ore yet.

- [ ] **Step 5: Add the salts, densities, and scatterOre entries**

In `src/World/TerrainGenerator.cpp`, find:

```cpp
constexpr float COPPER_DENSITY = 0.34f;
constexpr float IRON_DENSITY = 0.24f;
constexpr float COAL_DENSITY = 0.30f;
```

Replace with:

```cpp
constexpr float COPPER_DENSITY = 0.34f;
constexpr float IRON_DENSITY = 0.24f;
constexpr float COAL_DENSITY = 0.30f;

// Rare bands roll at a fifth of their ore's normal density - a real find,
// not a routine one.
constexpr float COPPER_DEEP_DENSITY = COPPER_DENSITY / 5.0f;
constexpr float IRON_SHALLOW_DENSITY = IRON_DENSITY / 5.0f;
```

Then find:

```cpp
constexpr std::uint32_t SALT_SURFACE = 0x1000u;
constexpr std::uint32_t SALT_CAVE = 0x2000u;
constexpr std::uint32_t SALT_COPPER = 0x3000u;
constexpr std::uint32_t SALT_IRON = 0x4000u;
constexpr std::uint32_t SALT_COAL = 0x5000u;
```

Replace with:

```cpp
constexpr std::uint32_t SALT_SURFACE = 0x1000u;
constexpr std::uint32_t SALT_CAVE = 0x2000u;
constexpr std::uint32_t SALT_COPPER = 0x3000u;
constexpr std::uint32_t SALT_COPPER_DEEP = 0x3001u;
constexpr std::uint32_t SALT_IRON = 0x4000u;
constexpr std::uint32_t SALT_IRON_SHALLOW = 0x4001u;
constexpr std::uint32_t SALT_COAL = 0x5000u;
```

Then find `scatterOre`'s `ores[]` array:

```cpp
    const Ore ores[] = {
        {BlockType::CopperOre, COPPER_MIN_Y, COPPER_MAX_Y, COPPER_DENSITY, SALT_COPPER},
        {BlockType::IronOre, IRON_MIN_Y, IRON_MAX_Y, IRON_DENSITY, SALT_IRON},
        {BlockType::Coal, COAL_MIN_Y, COAL_MAX_Y, COAL_DENSITY, SALT_COAL},
    };
```

Replace with:

```cpp
    const Ore ores[] = {
        {BlockType::CopperOre, COPPER_MIN_Y, COPPER_MAX_Y, COPPER_DENSITY, SALT_COPPER},
        {BlockType::CopperOre, COPPER_DEEP_MIN_Y, COPPER_DEEP_MAX_Y, COPPER_DEEP_DENSITY, SALT_COPPER_DEEP},
        {BlockType::IronOre, IRON_MIN_Y, IRON_MAX_Y, IRON_DENSITY, SALT_IRON},
        {BlockType::IronOre, IRON_SHALLOW_MIN_Y, IRON_SHALLOW_MAX_Y, IRON_SHALLOW_DENSITY, SALT_IRON_SHALLOW},
        {BlockType::Coal, COAL_MIN_Y, COAL_MAX_Y, COAL_DENSITY, SALT_COAL},
    };
```

- [ ] **Step 6: Run tests to verify they pass**

Run the Step 2 build command, then `C:/Litharia/build/Debug/Litharia_tests.exe`.

Expected: all tests pass, including both tests from Step 1. If `shallowIron < commonIron / 2` or `deepCopper < commonCopper / 2` fails (i.e. the rare band turned out not rare enough), double check `COPPER_DEEP_DENSITY`/`IRON_SHALLOW_DENSITY` really are `/ 5.0f` of their common density and that `scatterOre`'s loop bounds (`ore.minY / VEIN_CELL` to `ore.maxY / VEIN_CELL`) are using the new bands, not the old ones, for the new entries.

- [ ] **Step 7: Commit**

```bash
git add src/World/TerrainGenerator.h src/World/TerrainGenerator.cpp tests/test_terrain.cpp
git commit -m "feat: iron rarely spawns shallow, copper rarely spawns deep"
```

---

### Task 2: Hill peak finder

**Files:**
- Modify: `src/World/TerrainGenerator.h`
- Modify: `src/World/TerrainGenerator.cpp`
- Test: `tests/test_terrain.cpp`

**Interfaces:**
- Consumes: `TerrainGenerator::surfaceHeight(int x) const` (existing, public).
- Produces: `int TerrainGenerator::findHillPeak(int targetX) const` (public) and `static constexpr int HILL_SEARCH_RADIUS = 30` (public) — Task 3's `carveSpecialCaves` calls `findHillPeak` directly to anchor each cave's entrance.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_terrain.cpp` (anywhere after the existing `#include`s, alongside the other `TEST_CASE`s):

```cpp
TEST_CASE("findHillPeak returns the most elevated column in its search window")
{
    const TerrainGenerator generator(2026);

    // These 4 x positions mirror where the hill caves will actually be
    // placed in the next task (spawnX +/- 100 and +/- 350) - hardcoded here
    // since the SPECIAL_CAVE_*_OFFSET constants don't exist until then.
    const int spawnX = WORLD_WIDTH / 2;
    const int targets[] = {spawnX - 350, spawnX - 100, spawnX + 100, spawnX + 350};

    for (int target : targets)
    {
        const int peak = generator.findHillPeak(target);

        const int lo = target - TerrainGenerator::HILL_SEARCH_RADIUS;
        const int hi = target + TerrainGenerator::HILL_SEARCH_RADIUS;

        for (int x = lo; x <= hi; ++x)
            CHECK(generator.surfaceHeight(peak) <= generator.surfaceHeight(x));
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```
"C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build C:/Litharia/build --config Debug --target Litharia_tests
```

Expected: **build fails** — `findHillPeak` and `HILL_SEARCH_RADIUS` don't exist yet.

- [ ] **Step 3: Declare and implement findHillPeak**

In `src/World/TerrainGenerator.h`, find:

```cpp
    static constexpr int TREE_MIN_SPACING = 4;

    explicit TerrainGenerator(std::uint32_t seed);
```

Replace with:

```cpp
    static constexpr int TREE_MIN_SPACING = 4;

    // How far either side of a target x to search for the most elevated
    // column when anchoring a hill cave.
    static constexpr int HILL_SEARCH_RADIUS = 30;

    explicit TerrainGenerator(std::uint32_t seed);
```

Then find:

```cpp
    int surfaceHeight(int x) const;
```

Replace with:

```cpp
    int surfaceHeight(int x) const;

    // The most elevated column within HILL_SEARCH_RADIUS of targetX (smaller
    // surfaceHeight = higher ground). Ties break toward the first x found.
    // Public, like surfaceHeight, so it can be tested directly.
    int findHillPeak(int targetX) const;
```

In `src/World/TerrainGenerator.cpp`, find:

```cpp
int TerrainGenerator::surfaceHeight(int x) const
{
    const float n = noise::fbm1D(static_cast<float>(x) * SURFACE_FREQUENCY,
                                 worldSeed + SALT_SURFACE,
                                 SURFACE_OCTAVES);

    // n is in [0, 1]; centre it so the hills swing both ways around the base.
    const float height = SURFACE_BASE + (n - 0.5f) * 2.0f * SURFACE_AMPLITUDE;

    // Clamped, so no amount of extreme noise can push the surface out of the world.
    return std::clamp(static_cast<int>(std::lround(height)), SURFACE_MIN, SURFACE_MAX);
}
```

Add directly after it:

```cpp
int TerrainGenerator::findHillPeak(int targetX) const
{
    const int lo = std::max(0, targetX - HILL_SEARCH_RADIUS);
    const int hi = std::min(WORLD_WIDTH - 1, targetX + HILL_SEARCH_RADIUS);

    int bestX = lo;
    int bestHeight = surfaceHeight(lo);

    for (int x = lo + 1; x <= hi; ++x)
    {
        const int height = surfaceHeight(x);
        if (height < bestHeight)
        {
            bestHeight = height;
            bestX = x;
        }
    }

    return bestX;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run the Step 2 build command, then `C:/Litharia/build/Debug/Litharia_tests.exe`.

Expected: all tests pass, including `"findHillPeak returns the most elevated column in its search window"`.

- [ ] **Step 5: Commit**

```bash
git add src/World/TerrainGenerator.h src/World/TerrainGenerator.cpp tests/test_terrain.cpp
git commit -m "feat: add findHillPeak to locate a hill's most elevated column"
```

---

### Task 3: Hill caves — trunk and branches

**Files:**
- Modify: `src/World/TerrainGenerator.h`
- Modify: `src/World/TerrainGenerator.cpp`
- Test: `tests/test_terrain.cpp`

**Interfaces:**
- Consumes: `findHillPeak(int) const`, `HILL_SEARCH_RADIUS`, `surfaceHeight(int) const`, `IRON_MIN_Y` (all from Task 2 / existing code); `noise::hashFloat(int, int, std::uint32_t)` (existing).
- Produces: `void TerrainGenerator::carveSpecialCaves(World& world) const` (public — called from `generate()`, and callable standalone by tests to isolate this pass's effect the same way `generateBase` isolates the ore pass); `static constexpr int SPECIAL_CAVE_NEAR_OFFSET = 100` and `SPECIAL_CAVE_FAR_OFFSET = 350` (public, used by this task's own test).

- [ ] **Step 1: Write the failing test**

Add to `tests/test_terrain.cpp`:

```cpp
TEST_CASE("each hill cave's trunk reaches down to at least the iron layer")
{
    World world;
    const TerrainGenerator generator(2026);
    generator.generateBase(world);
    generator.carveSpecialCaves(world);

    const int spawnX = WORLD_WIDTH / 2;
    const int targets[] = {
        spawnX - TerrainGenerator::SPECIAL_CAVE_FAR_OFFSET,
        spawnX - TerrainGenerator::SPECIAL_CAVE_NEAR_OFFSET,
        spawnX + TerrainGenerator::SPECIAL_CAVE_NEAR_OFFSET,
        spawnX + TerrainGenerator::SPECIAL_CAVE_FAR_OFFSET,
    };

    for (int target : targets)
    {
        bool reachedIronDepth = false;

        for (int x = target - TerrainGenerator::HILL_SEARCH_RADIUS;
             x <= target + TerrainGenerator::HILL_SEARCH_RADIUS && !reachedIronDepth;
             ++x)
        {
            for (int y = TerrainGenerator::IRON_MIN_Y; y < WORLD_HEIGHT; ++y)
            {
                if (world.get(x, y) == BlockType::Air)
                {
                    reachedIronDepth = true;
                    break;
                }
            }
        }

        CHECK(reachedIronDepth);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```
"C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build C:/Litharia/build --config Debug --target Litharia_tests
```

Expected: **build fails** — `carveSpecialCaves`, `SPECIAL_CAVE_NEAR_OFFSET`, and `SPECIAL_CAVE_FAR_OFFSET` don't exist yet.

- [ ] **Step 3: Declare the new members**

In `src/World/TerrainGenerator.h`, find:

```cpp
    // How far either side of a target x to search for the most elevated
    // column when anchoring a hill cave.
    static constexpr int HILL_SEARCH_RADIUS = 30;
```

Replace with:

```cpp
    // How far either side of a target x to search for the most elevated
    // column when anchoring a hill cave.
    static constexpr int HILL_SEARCH_RADIUS = 30;

    // The 4 hill caves sit at spawnX +/- these offsets: 2 near, 2 far.
    static constexpr int SPECIAL_CAVE_NEAR_OFFSET = 100; // ~7-12s run from spawn
    static constexpr int SPECIAL_CAVE_FAR_OFFSET = 350;  // ~25-40s run from spawn
```

Find:

```cpp
    // Passes 1-4.
    void generate(World& world) const;

    // Passes 1-2 only: terrain with no ore in it. The ore pass is defined as
    // "stone becomes ore", and this is the world it is defined against.
    void generateBase(World& world) const;
```

Replace with:

```cpp
    // Passes 1-5.
    void generate(World& world) const;

    // Passes 1-2 only: terrain with no ore in it. The ore pass is defined as
    // "stone becomes ore", and this is the world it is defined against.
    void generateBase(World& world) const;

    // Pass 3: 4 hand-placed, hill-anchored cave systems (a trunk down to the
    // iron layer, plus a handful of dead-end branches), layered onto
    // generateBase's output. Public, like generateBase, so tests can
    // isolate exactly what this pass adds.
    void carveSpecialCaves(World& world) const;
```

Find the private section:

```cpp
private:
    void generateSurface(World& world) const;
    void carveCaves(World& world) const;
    void scatterOre(World& world) const;
    void scatterTrees(World& world) const;

    void growVein(World& world,
                  int centerX,
                  int centerY,
                  float radius,
                  BlockType ore,
                  int minY,
                  int maxY) const;

    void placeTree(World& world, int trunkX, int surface, int height) const;

    std::uint32_t worldSeed;
```

Replace with:

```cpp
private:
    void generateSurface(World& world) const;
    void carveCaves(World& world) const;
    void scatterOre(World& world) const;
    void scatterTrees(World& world) const;

    void growVein(World& world,
                  int centerX,
                  int centerY,
                  float radius,
                  BlockType ore,
                  int minY,
                  int maxY) const;

    void placeTree(World& world, int trunkX, int surface, int height) const;

    void carveTunnelPoint(World& world, int cx, int cy, float radius) const;

    std::vector<std::pair<int, int>> carveTrunk(World& world,
                                                 int caveIndex,
                                                 int startX,
                                                 int startY) const;

    void carveBranch(World& world,
                      int caveIndex,
                      int branchIndex,
                      int startX,
                      int startY) const;

    std::uint32_t worldSeed;
```

At the top of `src/World/TerrainGenerator.h`, find:

```cpp
#pragma once

#include <cstdint>

#include "../Blocks/Blocks.h"
```

Replace with:

```cpp
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "../Blocks/Blocks.h"
```

Also update the class-level comment at the top (find the `// Four passes:` block) to reflect the new pass count:

```cpp
// A pure function of the seed: the same seed always produces a byte-identical world.
//
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

Replace with:

```cpp
// A pure function of the seed: the same seed always produces a byte-identical world.
//
// Five passes:
//   1. Surface    - fractal noise over x gives a rolling height; grass, then a dirt
//      band, then stone all the way down.
//   2. Caves      - 2D fractal noise crossing a threshold carves air. The threshold
//      tightens near the surface so caves do not shred the landscape.
//   3. Hill caves - 4 hand-placed cave systems, each a trunk (random walk down to
//      the iron layer) plus a handful of dead-end branches, anchored on the
//      most elevated column near a fixed offset from spawn.
//   4. Ore        - hashed candidate points inside a depth band grow small blobs, but
//      only ever overwrite stone, so ore never floats in a cave or sits in dirt.
//   5. Trees      - a low-frequency noise channel gives each x position a "forest
//      factor"; columns roll against it to grow an oak tree, spaced far enough
//      apart that no two canopies ever touch.
```

- [ ] **Step 4: Wire carveSpecialCaves into generate()**

In `src/World/TerrainGenerator.cpp`, find:

```cpp
void TerrainGenerator::generate(World& world) const
{
    generateBase(world);
    scatterOre(world);
    scatterTrees(world);
}
```

Replace with:

```cpp
void TerrainGenerator::generate(World& world) const
{
    generateBase(world);
    carveSpecialCaves(world);
    scatterOre(world);
    scatterTrees(world);
}
```

- [ ] **Step 5: Add the pass-local constants**

Find:

```cpp
constexpr int CAVE_MIN_DEPTH = 4;   // no caves in the top few tiles of ground
constexpr int CAVE_FADE_DEPTH = 45; // fully deep-threshold by this depth

// --- Pass 3: ore -------------------------------------------------------------
```

Replace with:

```cpp
constexpr int CAVE_MIN_DEPTH = 4;   // no caves in the top few tiles of ground
constexpr int CAVE_FADE_DEPTH = 45; // fully deep-threshold by this depth

// --- Pass 3: hill caves --------------------------------------------------------
constexpr float SPECIAL_CAVE_RADIUS = 2.5f;

constexpr int TRUNK_MAX_STEPS = 3000; // loop-safety cap; the downward bias
                                       // means this is never expected to bind

constexpr int BRANCH_MIN_COUNT = 3;
constexpr int BRANCH_MAX_COUNT = 6;
constexpr int BRANCH_MIN_STEPS = 15;
constexpr int BRANCH_MAX_STEPS = 40;

constexpr std::uint32_t SALT_SPECIAL_CAVE = 0x8000u;

// --- Pass 4: ore -------------------------------------------------------------
```

Also find:

```cpp
// --- Pass 4: trees ------------------------------------------------------------
```

Replace with:

```cpp
// --- Pass 5: trees ------------------------------------------------------------
```

so the pass numbering stays consistent throughout the file.

- [ ] **Step 6: Implement carveTunnelPoint, carveTrunk, carveBranch, and carveSpecialCaves**

In `src/World/TerrainGenerator.cpp`, find `TerrainGenerator::growVein`'s closing brace (the end of that function, right before `void TerrainGenerator::scatterOre`) and insert the following new functions between them:

```cpp
void TerrainGenerator::carveTunnelPoint(World& world, int cx, int cy, float radius) const
{
    const int reach = static_cast<int>(std::ceil(radius));
    const float radiusSquared = radius * radius;

    for (int dy = -reach; dy <= reach; ++dy)
    {
        for (int dx = -reach; dx <= reach; ++dx)
        {
            if (static_cast<float>(dx * dx + dy * dy) > radiusSquared)
                continue;

            const int x = cx + dx;
            const int y = cy + dy;

            // A tunnel opens through solid ground only - it never punches
            // into a cave that's already open (nothing to do there) and
            // there is no ore yet at this pass.
            const BlockType current = world.get(x, y);
            if (current != BlockType::Stone && current != BlockType::Dirt)
                continue;

            world.set(x, y, BlockType::Air);
        }
    }
}

std::vector<std::pair<int, int>> TerrainGenerator::carveTrunk(World& world,
                                                               int caveIndex,
                                                               int startX,
                                                               int startY) const
{
    std::vector<std::pair<int, int>> path;

    int x = startX;
    int y = startY;

    const std::uint32_t seed =
        worldSeed + SALT_SPECIAL_CAVE + static_cast<std::uint32_t>(caveIndex) * 997u;

    for (int step = 0; step < TRUNK_MAX_STEPS; ++step)
    {
        carveTunnelPoint(world, x, y, SPECIAL_CAVE_RADIUS);
        path.push_back({x, y});

        if (y >= IRON_MIN_Y)
            break;

        // Mostly down, sometimes flat, rarely back up - a trunk that
        // reliably descends but doesn't fall in a straight line.
        const float dyRoll = noise::hashFloat(step, 0, seed);
        y += (dyRoll < 0.65f) ? 1 : (dyRoll < 0.85f ? 0 : -1);

        const float dxRoll = noise::hashFloat(step, 1, seed);
        x += (dxRoll < 1.0f / 3.0f) ? -1 : (dxRoll < 2.0f / 3.0f ? 0 : 1);
    }

    return path;
}

void TerrainGenerator::carveBranch(World& world,
                                    int caveIndex,
                                    int branchIndex,
                                    int startX,
                                    int startY) const
{
    const std::uint32_t seed = worldSeed + SALT_SPECIAL_CAVE +
                                static_cast<std::uint32_t>(caveIndex) * 997u +
                                static_cast<std::uint32_t>(branchIndex) * 131u;

    const float lengthRoll = noise::hashFloat(branchIndex, 2, seed);
    const int length =
        BRANCH_MIN_STEPS + static_cast<int>(lengthRoll * (BRANCH_MAX_STEPS - BRANCH_MIN_STEPS + 1));

    int x = startX;
    int y = startY;

    for (int step = 0; step < length; ++step)
    {
        carveTunnelPoint(world, x, y, SPECIAL_CAVE_RADIUS);

        // No downward bias here - a branch wanders freely and simply stops
        // when its length runs out. That stop is the dead end.
        const float dyRoll = noise::hashFloat(step, 3, seed);
        y += (dyRoll < 1.0f / 3.0f) ? -1 : (dyRoll < 2.0f / 3.0f ? 0 : 1);

        const float dxRoll = noise::hashFloat(step, 4, seed);
        x += (dxRoll < 1.0f / 3.0f) ? -1 : (dxRoll < 2.0f / 3.0f ? 0 : 1);
    }
}

void TerrainGenerator::carveSpecialCaves(World& world) const
{
    const int spawnX = WORLD_WIDTH / 2;
    const int targets[4] = {
        spawnX - SPECIAL_CAVE_FAR_OFFSET,
        spawnX - SPECIAL_CAVE_NEAR_OFFSET,
        spawnX + SPECIAL_CAVE_NEAR_OFFSET,
        spawnX + SPECIAL_CAVE_FAR_OFFSET,
    };

    for (int caveIndex = 0; caveIndex < 4; ++caveIndex)
    {
        const int peakX = findHillPeak(targets[caveIndex]);
        const int startY = surfaceHeight(peakX) + 2;

        const std::vector<std::pair<int, int>> trunkPath =
            carveTrunk(world, caveIndex, peakX, startY);

        const std::uint32_t caveSeed =
            worldSeed + SALT_SPECIAL_CAVE + static_cast<std::uint32_t>(caveIndex) * 997u;

        const float countRoll = noise::hashFloat(caveIndex, 5, caveSeed);
        const int branchCount =
            BRANCH_MIN_COUNT + static_cast<int>(countRoll * (BRANCH_MAX_COUNT - BRANCH_MIN_COUNT + 1));

        for (int branchIndex = 0; branchIndex < branchCount; ++branchIndex)
        {
            const float pickRoll = noise::hashFloat(branchIndex, 6, caveSeed);
            const std::size_t rawIndex =
                static_cast<std::size_t>(pickRoll * static_cast<float>(trunkPath.size()));
            const std::size_t pathIndex = std::min(rawIndex, trunkPath.size() - 1);

            const auto [branchX, branchY] = trunkPath[pathIndex];
            carveBranch(world, caveIndex, branchIndex, branchX, branchY);
        }
    }
}
```

- [ ] **Step 7: Run test to verify it passes**

Run:
```
"C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build C:/Litharia/build --config Debug --target Litharia_tests
```

Then run `C:/Litharia/build/Debug/Litharia_tests.exe`.

Expected: all tests pass, including `"each hill cave's trunk reaches down to at least the iron layer"`. If it fails on one target, double check `targets[]`'s ordering in `carveSpecialCaves` matches the test's (`far-left, near-left, near-right, far-right`), and that `carveTrunk`'s loop actually breaks once `y >= IRON_MIN_Y` rather than only checking at the very end.

- [ ] **Step 8: Build and run the game executable**

Run:
```
"C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build C:/Litharia/build --config Debug --target Litharia
```

Expected: build succeeds with no errors. (Do not launch or play the game as part of this step — building is enough to confirm the executable links; a human will check the caves visually afterward.)

- [ ] **Step 9: Commit**

```bash
git add src/World/TerrainGenerator.h src/World/TerrainGenerator.cpp tests/test_terrain.cpp
git commit -m "feat: carve 4 hill-anchored branching cave systems down to the iron layer"
```

---

## Manual verification (for the human, not part of any task)

`TerrainGenerator.cpp` is fully covered by the automated tests above, but the *shape* of the caves — do they actually look right, sit visibly on hills, feel like a short vs. long walk — is a visual/gameplay judgment call that testing can't make. Once all 3 tasks are done, worth checking in-game:

- Walk left and right from spawn about 100 tiles each way: a hill with a cave mouth should appear within a short walk on both sides.
- Walk further out (roughly 350 tiles either direction): a second, farther cave should appear on each side.
- Each cave should visibly sit on a hill (elevated terrain), not a flat stretch or valley.
- Descending each cave should feel winding, with side passages that dead-end rather than one straight shaft, and should reach iron-bearing stone.
- Digging around near the surface should occasionally turn up a lone iron ore; digging deep should occasionally turn up a lone copper ore — both should feel like a rare find, not routine.
