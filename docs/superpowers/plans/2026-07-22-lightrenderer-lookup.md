# LightRenderer Lookup Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop `LightRenderer::draw` from rebuilding two `std::unordered_map`s from scratch every rendered frame and doing up to 24 hashed lookups per solid visible tile — the largest per-frame cost flagged by the whole-game optimization audit.

**Architecture:** Extract a new `SparseTileGrid` class (pure logic, no SFML dependency, lives in `Litharia_core` alongside `Lighting`) that answers "what value was written to `(x, y)` this round" via a generation-stamped flat array instead of a hash map — the same trick `Lighting::floodStamp`/`floodBest` already use, one layer up. `LightRenderer` owns two instances (`heldGrid`, `outlineGrid`) as `mutable` members, filled once per `draw()` call from the existing `heldTorchLight`/`ambientOutline` input vectors, replacing `heldMap`/`outlineMap` and every lookup that used them.

**Tech Stack:** C++20, CMake (Visual Studio generator on this machine), doctest for tests, SFML 3 (Graphics/Window/System).

## Global Constraints

- No change to `LightRenderer::draw`'s public signature or `LightRenderer`'s observable behavior — this is an internal-only performance change. `draw()` stays `const`.
- No change to rendered output: brightness, tint, wall-penetration falloff, and the ambient-outline fallback must look identical before and after.
- `SparseTileGrid` must have zero SFML dependency and live in `Litharia_core` (the graphics-free static library both `Litharia` and `Litharia_tests` link against — see `CMakeLists.txt:23-55`), so it can be unit tested without a window/render context, matching this repo's existing core/graphics split (`Lighting` is in `Litharia_core`; `LightRenderer`, which touches `sf::RenderTarget`, is not and never will be).
- `SparseTileGrid::set`/`merge` assume in-world coordinates (callers' responsibility, same as `Lighting`'s internal scratch writes); only `at()` bounds-checks, because the wall-penetration search's offset tiles can land outside `[0, WORLD_WIDTH) x [0, WORLD_HEIGHT)` near world edges.
- Generation counters (`uint32_t`) must not silently misbehave on wraparound — guard with a one-time buffer clear if a counter ever wraps to `0`, same convention as `Lighting::floodGeneration`/`outlineGeneration`.
- `LightRenderer.cpp` is compiled only into the `Litharia` target, not `Litharia_core` or `Litharia_tests` (`CMakeLists.txt:60-69`) — any task touching it must build the `Litharia` target explicitly to catch compile errors; `Litharia_tests` alone will not.

---

### Task 1: `SparseTileGrid` — a generation-stamped lookup grid, pure logic

**Files:**
- Create: `src/World/SparseTileGrid.h`
- Create: `src/World/SparseTileGrid.cpp`
- Modify: `CMakeLists.txt:23-49` (add to `Litharia_core` sources), `CMakeLists.txt:87-111` (add test file)
- Test: `tests/test_sparse_tile_grid.cpp`

**Interfaces:**
- Consumes: `WORLD_WIDTH`, `WORLD_HEIGHT` from `src/Core/Constants.h`.
- Produces (used by Task 2): `class SparseTileGrid` with a default constructor, `void clear()`, `void set(int x, int y, int level)`, `void merge(int x, int y, int level)`, `int at(int x, int y) const`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_sparse_tile_grid.cpp`:

```cpp
#include "doctest.h"

#include "Core/Constants.h"
#include "World/SparseTileGrid.h"

TEST_CASE("a freshly constructed grid reads 0 everywhere")
{
    SparseTileGrid grid;

    CHECK(grid.at(0, 0) == 0);
    CHECK(grid.at(500, 250) == 0);
}

TEST_CASE("set() makes at() return the value written, and leaves other cells at 0")
{
    SparseTileGrid grid;
    grid.clear();
    grid.set(10, 20, 7);

    CHECK(grid.at(10, 20) == 7);
    CHECK(grid.at(11, 20) == 0);
}

TEST_CASE("set() unconditionally overwrites, even to a lower value")
{
    SparseTileGrid grid;
    grid.clear();
    grid.set(10, 20, 7);
    grid.set(10, 20, 3);

    CHECK(grid.at(10, 20) == 3);
}

TEST_CASE("merge() raises the stored value to the max of everything merged in this round")
{
    SparseTileGrid grid;
    grid.clear();
    grid.merge(10, 20, 3);
    grid.merge(10, 20, 7);
    grid.merge(10, 20, 5);

    CHECK(grid.at(10, 20) == 7);
}

TEST_CASE("clear() invalidates every previously set or merged cell")
{
    SparseTileGrid grid;
    grid.clear();
    grid.set(10, 20, 7);
    grid.merge(30, 40, 5);

    grid.clear();

    CHECK(grid.at(10, 20) == 0);
    CHECK(grid.at(30, 40) == 0);
}

TEST_CASE("a second clear()+set() round does not leak values from the first round")
{
    SparseTileGrid grid;
    grid.clear();
    grid.set(10, 20, 7);

    grid.clear();
    grid.set(30, 40, 9);

    CHECK(grid.at(30, 40) == 9);
    CHECK(grid.at(10, 20) == 0);
}

TEST_CASE("a second clear()+merge() round does not leak values from the first round")
{
    SparseTileGrid grid;
    grid.clear();
    grid.merge(10, 20, 7);

    grid.clear();
    grid.merge(10, 20, 2);

    // Same coordinate as before, but a fresh round: must read as freshly
    // merged (2), not maxed against the stale prior-round value (7).
    CHECK(grid.at(10, 20) == 2);
}

TEST_CASE("at() returns 0 for coordinates outside world bounds")
{
    SparseTileGrid grid;
    grid.clear();
    grid.set(0, 0, 9);

    CHECK(grid.at(-1, 0) == 0);
    CHECK(grid.at(WORLD_WIDTH, 0) == 0);
    CHECK(grid.at(0, -1) == 0);
    CHECK(grid.at(0, WORLD_HEIGHT) == 0);
}
```

Register the new test file in `CMakeLists.txt`. In the `add_executable(Litharia_tests ...)` block (`CMakeLists.txt:87-111`), add a line after `tests/test_lighting.cpp`:

```cmake
    tests/test_lighting.cpp
    tests/test_sparse_tile_grid.cpp
)
```

- [ ] **Step 2: Confirm the build fails (the class doesn't exist yet)**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
```
Expected: FAIL — compile error, `SparseTileGrid` (or `World/SparseTileGrid.h`) not found.

- [ ] **Step 3: Create `src/World/SparseTileGrid.h`**

```cpp
#pragma once

#include <cstdint>
#include <vector>

// A world-sized lookup grid answering "what value was written to (x, y)
// this round" without a hash map's per-entry heap allocation and hashing
// cost - the same generation-stamp trick Lighting's own floodStamp/floodBest
// scratch buffers use (see Lighting.h), applied wherever a sparse per-frame
// (x, y) -> value set needs O(1) lookup instead of a fresh
// std::unordered_map rebuilt every call.
//
// clear() is an O(1) counter bump, not an O(world size) fill: a cell only
// reads as "set this round" when its stamp matches the current generation,
// so stale values from a prior round are invisible without ever being
// erased. set()/merge() assume in-world coordinates - callers are
// responsible for bounds-checking before writing, same as Lighting's
// internal scratch writes; only at() bounds-checks, since callers may
// legitimately query coordinates outside the world (e.g. a search that
// steps past a world edge) and expect 0 back rather than undefined
// behavior.
class SparseTileGrid
{
public:
    SparseTileGrid();

    // Starts a fresh round: every previously set()/merge()'d cell reads as
    // unset (0) again until touched again this round.
    void clear();

    // Unconditionally overwrites (x, y)'s value for this round. Use only
    // when the caller guarantees each coordinate is written at most once
    // per round - e.g. Lighting::heldTorchLight's result, which never
    // repeats a tile (see Lighting.h's floodFill comment).
    void set(int x, int y, int level);

    // Raises (x, y)'s value to the max of everything merge()'d into it this
    // round. Use when the same coordinate may be written more than once per
    // round - e.g. Lighting::ambientOutline's result, where a solid border
    // tile can be reached from multiple open neighbours.
    void merge(int x, int y, int level);

    // 0 if (x, y) is outside world bounds or was never set()/merge()'d this
    // round.
    int at(int x, int y) const;

private:
    std::vector<std::uint32_t> stamp; // WORLD_WIDTH * WORLD_HEIGHT
    std::vector<std::int8_t> value;   // WORLD_WIDTH * WORLD_HEIGHT
    std::uint32_t generation = 0;
};
```

- [ ] **Step 4: Create `src/World/SparseTileGrid.cpp`**

```cpp
#include "SparseTileGrid.h"

#include <algorithm>

#include "../Core/Constants.h"

SparseTileGrid::SparseTileGrid()
    : stamp(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
    , value(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
{
}

void SparseTileGrid::clear()
{
    ++generation;
    if (generation == 0)
    {
        std::fill(stamp.begin(), stamp.end(), 0);
        generation = 1;
    }
}

void SparseTileGrid::set(int x, int y, int level)
{
    const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
    stamp[i] = generation;
    value[i] = static_cast<std::int8_t>(level);
}

void SparseTileGrid::merge(int x, int y, int level)
{
    const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
    if (stamp[i] != generation)
    {
        stamp[i] = generation;
        value[i] = static_cast<std::int8_t>(level);
    }
    else
    {
        value[i] = static_cast<std::int8_t>(std::max<int>(value[i], level));
    }
}

int SparseTileGrid::at(int x, int y) const
{
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT)
        return 0;

    const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
    return (stamp[i] == generation) ? value[i] : 0;
}
```

- [ ] **Step 5: Register the new source file with `Litharia_core`**

In `CMakeLists.txt`, inside `add_library(Litharia_core STATIC ...)` (`CMakeLists.txt:23-49`), add a line right after `src/World/Lighting.cpp`:

```cmake
    src/World/Lighting.cpp
    src/World/SparseTileGrid.cpp
```

- [ ] **Step 6: Build and run the full test suite**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe
```
Expected: every test case PASSES — the 8 new `SparseTileGrid` cases plus every pre-existing case (this file has no interaction with any other subsystem, so this doubles as the regression check).

- [ ] **Step 7: Commit**

```bash
git add src/World/SparseTileGrid.h src/World/SparseTileGrid.cpp tests/test_sparse_tile_grid.cpp CMakeLists.txt
git commit -m "perf: add SparseTileGrid, a generation-stamped lookup grid"
```

---

### Task 2: Replace `LightRenderer`'s per-frame hash maps with `SparseTileGrid`

**Files:**
- Modify: `src/World/LightRenderer.h` (full file, 52 lines)
- Modify: `src/World/LightRenderer.cpp` (full file, 243 lines)

**Interfaces:**
- Consumes: `SparseTileGrid` from Task 1 (`clear()`, `set()`, `merge()`, `at()`).
- Produces: `LightRenderer::draw` keeps its exact existing signature — `Game::render` (`src/Game/Game.cpp:1152-1153`) calls it unmodified.

This task has no new automated test of its own: `LightRenderer.cpp` requires an `sf::RenderTarget` and is compiled only into the `Litharia` target (graphics-dependent), which `Litharia_tests` does not link — see Global Constraints. `SparseTileGrid`'s own correctness (Task 1) plus this task's manual verification (Task 3) are the coverage for this change, consistent with `docs/superpowers/specs/2026-07-22-lightrenderer-lookup-design.md`'s Testing section.

- [ ] **Step 1: Replace `src/World/LightRenderer.h`**

```cpp
#pragma once

#include <SFML/Graphics.hpp>

#include <utility>
#include <vector>

#include "Lighting.h"
#include "SparseTileGrid.h"

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

private:
    // Per-frame lookup grids for heldTorchLight/ambientOutline, replacing
    // what used to be two std::unordered_maps rebuilt from scratch every
    // call (see SparseTileGrid's own comment for the generation-stamp
    // mechanism). mutable: draw() stays const - these are implementation-
    // detail caches, not part of LightRenderer's observable state, same
    // convention Lighting uses for its own scratch buffers.
    mutable SparseTileGrid heldGrid;
    mutable SparseTileGrid outlineGrid;
};
```

- [ ] **Step 2: Replace `src/World/LightRenderer.cpp`**

```cpp
#include "LightRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>

#include "../Core/Constants.h"
#include "World.h"

namespace
{

constexpr sf::Color NIGHT_TINT(20, 25, 45);
constexpr sf::Color DAY_TINT(225, 235, 250);
constexpr sf::Color TORCH_TINT(255, 228, 183);
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

// How far real light (sky, Torch, or Lava alike) can be glimpsed through
// solid rock: a solid tile borrows the best `channelValue - distance`
// candidate from every tile within this many orthogonal steps, not just its
// 4 immediate neighbours - a direct generalization of "borrow one step
// dimmer than the brightest neighbour" out to a wider radius, so a wall a
// couple of tiles from a lit cavity glows faintly instead of reading fully
// dark. Distance 1 alone reproduces today's 4-neighbour behaviour exactly.
constexpr int WALL_PENETRATION_DEPTH = 3;

// Per-distance brightness penalty for the wall-penetration search above -
// steeper than the general "-1 per step" light-decay rate used everywhere
// else in this system, so the fade across those 3 tiles actually reads as a
// fade instead of three near-identical shades: distance 1 stays reasonably
// bright, distance 2 reads as "just dark," distance 3 as "very dark" for a
// source at or near Lighting::MAX_LIGHT_LEVEL - a Torch, seeded well above
// that (Lighting::TORCH_LIGHT_LEVEL), still has real brightness budget left
// at distance 3 and glows more than "very dark" there. Indexed by
// distance - 1, since wallPenetrationOffsets only ever produces distances
// 1..WALL_PENETRATION_DEPTH.
constexpr int WALL_PENETRATION_PENALTY[WALL_PENETRATION_DEPTH] = {2, 5, 8};

// Every (dx, dy, distance) offset within Manhattan distance 1..WALL_PENETRATION_DEPTH
// of a tile - a 24-cell diamond (4 tiles at distance 1, 8 at distance 2, 12
// at distance 3). Built once (see the function-local static in draw()); no
// isSolid check on the offset tile itself is needed here - Lighting's BFS
// never assigns a solid tile its own light, so an offset that happens to
// land on solid ground already reads 0 from every channel accessor and can
// never win over an actually-lit open tile.
std::vector<std::tuple<int, int, int>> wallPenetrationOffsets()
{
    std::vector<std::tuple<int, int, int>> offsets;

    for (int dx = -WALL_PENETRATION_DEPTH; dx <= WALL_PENETRATION_DEPTH; ++dx)
    {
        for (int dy = -WALL_PENETRATION_DEPTH; dy <= WALL_PENETRATION_DEPTH; ++dy)
        {
            const int distance = std::abs(dx) + std::abs(dy);
            if (distance >= 1 && distance <= WALL_PENETRATION_DEPTH)
                offsets.push_back({dx, dy, distance});
        }
    }

    return offsets;
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

    // Replaces what used to be two std::unordered_maps rebuilt from scratch
    // every call - see SparseTileGrid's own comment for the generation-
    // stamp mechanism this relies on.
    heldGrid.clear();
    for (const auto& [tile, level] : heldTorchLight)
        heldGrid.set(tile.x, tile.y, level);

    outlineGrid.clear();
    for (const auto& [tile, level] : ambientOutline)
        outlineGrid.merge(tile.x, tile.y, level);

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
                // A solid tile borrows the best (channelValue - distance)
                // candidate from anywhere within WALL_PENETRATION_DEPTH
                // orthogonal steps, per channel - see wallPenetrationOffsets
                // for why no separate isSolid check is needed on the
                // candidate tiles themselves.
                //
                // Torch is the one channel with a "held" equivalent: the
                // player's held Torch (heldGrid) lights open tiles the same
                // way a placed Torch would, but never touches the stored
                // grid, so a lookup that only reads lighting.torchLight
                // would miss it - fold heldGrid into the lookup too, same as
                // the tile-itself case below.
                const auto torchAt = [&lighting, this](int nx, int ny) {
                    return std::max(lighting.torchLight(nx, ny), heldGrid.at(nx, ny));
                };

                static const std::vector<std::tuple<int, int, int>> penetrationOffsets =
                    wallPenetrationOffsets();

                int skyBest = 0;
                int torchBest = 0;
                int lavaBest = 0;

                for (const auto& [dx, dy, distance] : penetrationOffsets)
                {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    const int penalty = WALL_PENETRATION_PENALTY[distance - 1];

                    skyBest = std::max(skyBest, lighting.skyLight(nx, ny) - penalty);
                    torchBest = std::max(torchBest, torchAt(nx, ny) - penalty);
                    lavaBest = std::max(lavaBest, lighting.lavaLight(nx, ny) - penalty);
                }

                // skyBest/torchBest/lavaBest start at 0 and are only ever
                // raised by std::max, so they can never go negative - no
                // separate clamp needed here (unlike the old single-neighbour
                // version, which subtracted 1 *after* taking the max and so
                // needed an explicit std::max(0, ...) guard).
                skyRaw = skyBest;
                torchRaw = torchBest;
                lavaRaw = lavaBest;
            }
            else
            {
                skyRaw = lighting.skyLight(x, y);
                torchRaw = lighting.torchLight(x, y);
                lavaRaw = lighting.lavaLight(x, y);
            }

            torchRaw = std::max(torchRaw, heldGrid.at(x, y));

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
                const int outlineLevel = outlineGrid.at(x, y);
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

Note what changed versus the old file: `#include <unordered_map>` is gone (no longer used); the standalone `tileKey()` helper is gone (audit item #17 — folded in here, since there's no hash key to compute anymore); the `heldMap`/`outlineMap` build block is now `heldGrid.clear()`/`set()` and `outlineGrid.clear()`/`merge()`; `torchAt` reads `heldGrid.at(nx, ny)` instead of a `heldMap.find(...)`; the two standalone lookups (old lines 194 and 227) are now `heldGrid.at(x, y)` and `outlineGrid.at(x, y)`.

- [ ] **Step 3: Build the `Litharia` target (the only target that compiles this file)**

Run:
```bash
cmake --build build --config Debug --target Litharia
```
Expected: builds successfully with no errors or new warnings. This is the step that actually exercises the edited files — `Litharia_tests` does not compile `LightRenderer.cpp` at all (see Global Constraints).

- [ ] **Step 4: Build and run the full test suite to confirm no regressions elsewhere**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe
```
Expected: every test case PASSES, including `test_lighting.cpp` and Task 1's `test_sparse_tile_grid.cpp` — this task doesn't touch `Lighting` or `SparseTileGrid`, so this is a pure regression check.

- [ ] **Step 5: Commit**

```bash
git add src/World/LightRenderer.h src/World/LightRenderer.cpp
git commit -m "perf: replace LightRenderer's per-frame hash maps with SparseTileGrid"
```

---

### Task 3: Manual in-game verification

**Files:** none (no code changes — verification only).

**Interfaces:** none.

- [ ] **Step 1: Build the game in Debug**

Run:
```bash
cmake --build build --config Debug --target Litharia
```
Expected: builds successfully.

- [ ] **Step 2: Run the game and check for rendering regressions**

Run:
```bash
build/Debug/Litharia.exe
```
In-game: mine into an open cavern, place a Torch, and walk around with a Torch both equipped and unequipped. Confirm:
- Torch, Lava, and sky light appear in the same places, same brightness, same falloff as before this change.
- Walls near a lit cavity still glow faintly through rock (the wall-penetration effect), fading over the same ~3-tile depth.
- Walking near unlit rock still shows the faint ambient outline, with ore tiles reading slightly brighter than plain stone.
- No flicker, no stale light lingering after the Torch or player moves away (this is the specific failure mode a broken generation-stamp reset would cause).

- [ ] **Step 3: Check for the improvement**

With a Torch equipped, walk through a large open cavern (the worst case: `ambientOutline`'s radius is sized to the full camera view when a Torch is held, per `Game.cpp:1140-1147`, and a large fraction of visible tiles are solid underground, maximizing the old per-tile hashed-lookup cost). Confirm movement feels smoother than before this change — subjective/manual, since this repo has no frame-time benchmark harness. If still choppy, that points at a different cost from the audit backlog (`docs/superpowers/specs/2026-07-22-optimization-audit-design.md`) worth picking up next — most likely item #2 (chunk rebuild + fluid surface scan) or #3 (`FluidSim::scanRun`).

No commit for this task — it's verification only, nothing to stage.
