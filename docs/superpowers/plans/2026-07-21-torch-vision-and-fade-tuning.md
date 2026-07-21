# Torch Vision and Fade Tuning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When a Torch is equipped, widen the existing ambient-vision floor from a fixed 20-tile bubble to cover the whole visible screen (still gated by the same open-tiles-only connectivity rule); and make the 3-tile wall-penetration fade (from the immediately-prior feature) actually read as a fade instead of three near-identical shades.

**Architecture:** Two independent, small changes. (1) `Lighting::ambientOutline` gains a `radius` parameter (defaulting to the existing `AMBIENT_OUTLINE_RADIUS` constant, so every existing call site and test keeps compiling unchanged), and `Game::render` computes a screen-covering radius from the camera's current view size whenever a Torch is equipped. (2) `LightRenderer`'s wall-penetration search swaps its flat `-distance` brightness penalty for a steeper, per-distance lookup table. Neither change touches `Lighting`'s stored grid, `recomputeAll`, or its recompute triggers.

**Tech Stack:** C++20, SFML 3 (Graphics/Window/System), doctest, CMake + Visual Studio 18 2026 generator (existing project setup - no new dependencies).

## Global Constraints

- `Lighting::ambientOutline`'s BFS cutoff (`steps >= radius` in `src/World/Lighting.cpp`) becomes a parameter, not a hardcoded use of `AMBIENT_OUTLINE_RADIUS` - defaulted to `AMBIENT_OUTLINE_RADIUS` so no existing caller or test needs to change.
- The connectivity rule itself is untouched: `ambientOutline` still only reaches tiles connected to the player through open tiles - a sealed pocket with no dug path still gets nothing, regardless of radius.
- The screen-covering radius in `Game::render` must be computed from `camera.view().getSize()` (a Manhattan-distance sum of half-width-in-tiles + half-height-in-tiles, not the Euclidean diagonal - the diagonal would undercover the screen's corners), with a small rounding margin, only when the currently-selected hotbar item is a Torch (`held.type == ItemType::Torch`, the same check `heldTorchLight` already uses) - otherwise `Lighting::AMBIENT_OUTLINE_RADIUS` is used exactly as today.
- The wall-penetration penalty table (`{2, 5, 8}` for distances 1/2/3) only applies inside `LightRenderer`'s wall-penetration search - the general `Lighting` decay rate (how far light spreads through open space) is untouched.
- Neither task adds a test to `LightRenderer.cpp`/`Game.cpp` (SFML Graphics/Window code, no window-free test target, matching the project's existing convention for `ChunkRenderer`/`MachineRenderer`). `Lighting`'s own change (Task 1's `radius` parameter) DOES get tests - it's a window-free, already-tested core-lib class.
- Every changed source file must build under the existing `cmake --build build --config Debug` invocation, and `Litharia_tests.exe` must pass in full after every task.

---

### Task 1: `ambientOutline` gains a radius parameter, wired to the camera's view size when a Torch is equipped

**Files:**
- Modify: `src/World/Lighting.h`
- Modify: `src/World/Lighting.cpp`
- Modify: `tests/test_lighting.cpp`
- Modify: `src/Game/Game.cpp`

**Interfaces:**
- Consumes: `Camera::view()` (`src/Camera/Camera.h`, already used at this exact call site for `lightRenderer.draw`), `TILE_SIZE` (`Core/Constants.h`).
- Produces: `Lighting::ambientOutline(const World&, sf::Vector2i playerTile, int radius = AMBIENT_OUTLINE_RADIUS) const` - the same return shape as before, now caller-configurable. Nothing later depends on new interfaces from this task; it's the last task that touches `Lighting` in this plan.

- [ ] **Step 1: Write the failing tests**

Add these two test cases to the end of `tests/test_lighting.cpp`:

```cpp
TEST_CASE("ambientOutline accepts a custom radius that reaches further than the default")
{
    World world;
    fillSolid(world);
    for (int x = 10; x <= 10 + Lighting::AMBIENT_OUTLINE_RADIUS + 10; ++x)
        world.set(x, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10}, Lighting::AMBIENT_OUTLINE_RADIUS + 10);

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(10 + Lighting::AMBIENT_OUTLINE_RADIUS + 9, 10) == Lighting::AMBIENT_OUTLINE_LEVEL);
}

TEST_CASE("ambientOutline still defaults to AMBIENT_OUTLINE_RADIUS when no radius argument is given")
{
    World world;
    fillSolid(world);
    for (int x = 10; x <= 10 + Lighting::AMBIENT_OUTLINE_RADIUS + 5; ++x)
        world.set(x, 10, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    const auto result = lighting.ambientOutline(world, {10, 10}); // no third argument

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : result)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(10 + Lighting::AMBIENT_OUTLINE_RADIUS + 5, 10) == 0); // still capped at the default
}
```

- [ ] **Step 2: Run the tests to verify they fail to build**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `ambientOutline` does not yet accept a third argument.

- [ ] **Step 3: Add the `radius` parameter in `src/World/Lighting.h`**

Replace the `ambientOutline` declaration (currently reading `sf::Vector2i playerTile) const;` on its own continuation line) with:

```cpp
    // A short-range, uncolored visibility floor around the player's current
    // tile, recomputed fresh every frame (never stored, never forcing a
    // recompute) - the "you can make out shapes and ore nearby even with no
    // light" mechanic that replaces solid ground's old always-fully-visible
    // behavior. A bounded BFS from `playerTile`, traveling only through open
    // tiles up to `radius` steps (so a sealed pocket with no path back to
    // the player gets nothing, exactly like real light) - every open tile
    // reached this way, and every solid tile bordering one, is included in
    // the result at AMBIENT_OUTLINE_LEVEL (AMBIENT_OUTLINE_ORE_LEVEL if the
    // solid tile is ore). Unlike real light, this value does not decay with
    // distance inside the radius - it's a flat floor, not a gradient.
    // `radius` defaults to AMBIENT_OUTLINE_RADIUS (the normal "eyes adjusted
    // to the dark" case); callers pass a larger value to widen the covered
    // area - Game::render does this when a Torch is equipped, sizing it to
    // the camera's current view instead of the fixed default.
    std::vector<std::pair<sf::Vector2i, int>> ambientOutline(const World& world, sf::Vector2i playerTile,
                                                              int radius = AMBIENT_OUTLINE_RADIUS) const;
```

- [ ] **Step 4: Use the parameter instead of the constant in `src/World/Lighting.cpp`**

In `Lighting::ambientOutline`, change the signature line:

```cpp
std::vector<std::pair<sf::Vector2i, int>> Lighting::ambientOutline(const World& world,
                                                                    sf::Vector2i playerTile) const
```

to:

```cpp
std::vector<std::pair<sf::Vector2i, int>> Lighting::ambientOutline(const World& world, sf::Vector2i playerTile,
                                                                    int radius) const
```

(A default argument is only ever written once, on the declaration in the header - repeating it here would be a compile error.)

Then, inside the function body, change:

```cpp
        if (steps >= AMBIENT_OUTLINE_RADIUS)
            continue;
```

to:

```cpp
        if (steps >= radius)
            continue;
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - both new tests pass, and the full existing suite (including every other `ambientOutline` test, which calls it with the old 2-argument form and now gets the default radius) still passes unchanged.

- [ ] **Step 6: Wire the camera-sized radius into `Game::render`**

In `src/Game/Game.cpp`, find this existing block (currently around line 1123-1134):

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

Replace it with:

```cpp
    const sf::Vector2i playerTile{static_cast<int>(std::floor(player.center().x / TILE_SIZE)),
                                   static_cast<int>(std::floor(player.center().y / TILE_SIZE))};

    std::vector<std::pair<sf::Vector2i, int>> heldLight;
    const ItemStack& held = player.inventory().slot(player.selectedSlot());
    if (held.type == ItemType::Torch)
        heldLight = lighting.heldTorchLight(world, playerTile);

    // Equipping a Torch widens ambient vision to cover the whole screen -
    // still gated by the same "only through open tiles you've actually dug
    // into" connectivity rule ambientOutline already enforces, just sized to
    // the camera's current view instead of the default 20-tile bubble. This
    // is a Manhattan-distance step cutoff (half-width-in-tiles plus
    // half-height-in-tiles, not the shorter Euclidean diagonal - the
    // diagonal would undercover the screen's corners), so it stays cheap and
    // correct at any window size or zoom: a huge open cavern is still never
    // explored past what's actually on screen.
    int ambientRadius = Lighting::AMBIENT_OUTLINE_RADIUS;
    if (held.type == ItemType::Torch)
    {
        const sf::Vector2f viewSize = camera.view().getSize();
        const int halfWidthTiles = static_cast<int>(std::ceil((viewSize.x * 0.5f) / TILE_SIZE));
        const int halfHeightTiles = static_cast<int>(std::ceil((viewSize.y * 0.5f) / TILE_SIZE));
        ambientRadius = halfWidthTiles + halfHeightTiles + 2;
    }

    const std::vector<std::pair<sf::Vector2i, int>> ambientOutline =
        lighting.ambientOutline(world, playerTile, ambientRadius);

    lightRenderer.draw(window, camera.view(), world, lighting, dayNightClock.daylightFactor(), heldLight,
                        ambientOutline);
```

`std::ceil` requires `<cmath>`, already included in this file (it already calls `std::floor` two lines above this block).

- [ ] **Step 7: Build the executable and run the full test suite**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - unchanged from Step 5 (nothing in `Game.cpp` is linked into `Litharia_tests`).

Run: `cmake --build build --config Debug --target Litharia`
Expected: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add src/World/Lighting.h src/World/Lighting.cpp tests/test_lighting.cpp src/Game/Game.cpp
git commit -m "feat: widen ambient vision to the whole screen when a Torch is equipped"
```

---

### Task 2: Steeper, front-loaded wall-penetration fade

**Files:**
- Modify: `src/World/LightRenderer.cpp`

**Interfaces:**
- Consumes: `WALL_PENETRATION_DEPTH`, `wallPenetrationOffsets()` (both already in this file, unchanged).
- Produces: no new public interface - purely an internal brightness-curve change inside `LightRenderer::draw`. Independent of Task 1 (different file, no shared code path); can be done in either order.

This task has no unit test (see Global Constraints) - verification is a clean executable build plus a manual/visual check (Step 3).

- [ ] **Step 1: Add the penalty table**

In `src/World/LightRenderer.cpp`, add this right after the existing `WALL_PENETRATION_DEPTH` constant (in the anonymous namespace, before `wallPenetrationOffsets()`):

```cpp
// Per-distance brightness penalty for the wall-penetration search above -
// steeper than the general "-1 per step" light-decay rate used everywhere
// else in this system, so the fade across those 3 tiles actually reads as a
// fade instead of three near-identical shades: distance 1 stays reasonably
// bright, distance 2 reads as "just dark," distance 3 as "very dark" (only
// a source at or near Lighting::MAX_LIGHT_LEVEL has any brightness budget
// left by then). Indexed by distance - 1, since wallPenetrationOffsets only
// ever produces distances 1..WALL_PENETRATION_DEPTH.
constexpr int WALL_PENETRATION_PENALTY[WALL_PENETRATION_DEPTH] = {2, 5, 8};
```

- [ ] **Step 2: Use the penalty table in the search loop**

Replace this loop inside the solid-tile branch of `LightRenderer::draw`:

```cpp
                for (const auto& [dx, dy, distance] : penetrationOffsets)
                {
                    const int nx = x + dx;
                    const int ny = y + dy;

                    skyBest = std::max(skyBest, lighting.skyLight(nx, ny) - distance);
                    torchBest = std::max(torchBest, torchAt(nx, ny) - distance);
                    lavaBest = std::max(lavaBest, lighting.lavaLight(nx, ny) - distance);
                }
```

with:

```cpp
                for (const auto& [dx, dy, distance] : penetrationOffsets)
                {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    const int penalty = WALL_PENETRATION_PENALTY[distance - 1];

                    skyBest = std::max(skyBest, lighting.skyLight(nx, ny) - penalty);
                    torchBest = std::max(torchBest, torchAt(nx, ny) - penalty);
                    lavaBest = std::max(lavaBest, lighting.lavaLight(nx, ny) - penalty);
                }
```

- [ ] **Step 3: Build and manually verify**

Run: `cmake --build build --config Debug --target Litharia`
Expected: SUCCESS.

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - unchanged (this file isn't linked into `Litharia_tests`).

Manually run `Litharia.exe` and confirm: a wall 1 tile from a bright light source looks about the same as before this task; 2 tiles in is noticeably dimmer ("just dark"); 3 tiles in is only barely visible for a bright source ("very dark"), fully black for a dimmer one; 4+ tiles in is unaffected. This is a manual check, not an automated one, matching this file's existing convention.

- [ ] **Step 4: Commit**

```bash
git add src/World/LightRenderer.cpp
git commit -m "tweak: make the 3-tile wall-penetration fade steeper and front-loaded"
```
