# Lighting Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop `Lighting::heldTorchLight` and `Lighting::ambientOutline` from allocating and zero-filling full-world-sized (500,000-element) scratch buffers on every rendered frame, which is the dominant cause of the reported lag.

**Architecture:** Replace the per-call fresh scratch vectors in `Lighting::floodFill` (backing `recomputeAll` and `heldTorchLight`) and in `Lighting::ambientOutline`'s own BFS with persistent, generation-stamped scratch buffers owned by `Lighting` and allocated once at construction. A monotonically increasing generation counter replaces the full-buffer clear: a cell only counts as "touched this call" when its stamp equals the current generation, so reset becomes O(1) and per-call cost stays proportional to the actual (small, local) search result instead of world size.

**Tech Stack:** C++20, CMake (Visual Studio generator on this machine), doctest for tests.

## Global Constraints

- No change to any public `Lighting` method's signature or return value (`recomputeAll`, `skyLight`, `torchLight`, `lavaLight`, `heldTorchLight`, `ambientOutline`) — this is an internal-only performance change.
- No change to observable lighting behavior — every existing assertion in `tests/test_lighting.cpp` must keep passing unmodified.
- `Lighting` remains single-threaded; no synchronization needed for the new mutable scratch state (confirmed: no threading anywhere in `src/`).
- Generation counters (`uint32_t`) must not silently misbehave on wraparound — guard with a one-time buffer clear if a counter ever wraps to `0`.

---

### Task 1: Persistent generation-stamped scratch for `floodFill` (backs `recomputeAll` and `heldTorchLight`)

**Files:**
- Modify: `src/World/Lighting.h:44-146` (class `Lighting`)
- Modify: `src/World/Lighting.cpp:12-54` (constructor, `floodFill`)
- Test: `tests/test_lighting.cpp` (append new test cases)

**Interfaces:**
- Consumes: nothing new from outside `Lighting`.
- Produces: `floodFill` keeps its existing signature and behavior (only its internals and its `static`-ness change) — `recomputeAll` (`Lighting.cpp:56-101`) and `heldTorchLight` (`Lighting.cpp:127-131`) call it exactly as they do today, unmodified.

- [ ] **Step 1: Add two characterization tests that exercise `Lighting`'s scratch state being reused across successive calls**

Append to `tests/test_lighting.cpp` (after the last existing `TEST_CASE`, i.e. after line 462):

```cpp
TEST_CASE("floodFill's persistent scratch does not leak stale state between successive recomputeAll calls")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(11, 10, BlockType::Air);

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(10, 10) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(lighting.torchLight(9, 10) == Lighting::TORCH_LIGHT_LEVEL - 1);

    // A second call on the same Lighting instance (reusing its persistent
    // scratch buffers), with the Torch moved to an entirely different,
    // previously-untouched part of the grid.
    world.set(10, 10, BlockType::Stone);
    world.set(9, 10, BlockType::Stone);
    world.set(11, 10, BlockType::Stone);
    world.set(30, 30, BlockType::Air);
    world.set(29, 30, BlockType::Air);

    Machines machines2;
    machines2.place(MachineType::Torch, 30, 30, Direction::Right);

    lighting.recomputeAll(world, machines2);

    CHECK(lighting.torchLight(30, 30) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(lighting.torchLight(29, 30) == Lighting::TORCH_LIGHT_LEVEL - 1);
    // The old Torch's tile is Stone now and was never a light source this
    // call - if stale scratch state from the first call leaked through,
    // this is the value most likely to read wrong.
    CHECK(lighting.torchLight(10, 10) == 0);
}

TEST_CASE("heldTorchLight's persistent scratch does not leak between successive calls at different sources")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);
    world.set(9, 10, BlockType::Air);
    world.set(30, 30, BlockType::Air);
    world.set(29, 30, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    lighting.heldTorchLight(world, {10, 10});
    const auto second = lighting.heldTorchLight(world, {30, 30});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : second)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(30, 30) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(levelAt(29, 30) == Lighting::TORCH_LIGHT_LEVEL - 1);
    // The second call's result must not still contain the first call's
    // source tile.
    CHECK(levelAt(10, 10) == 0);
}
```

- [ ] **Step 2: Run the test suite to confirm these pass against today's (unmodified) implementation**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe -tc="floodFill's persistent scratch does not leak stale state between successive recomputeAll calls,heldTorchLight's persistent scratch does not leak between successive calls at different sources"
```
Expected: both new test cases PASS. This establishes the baseline: today's fresh-buffer-per-call implementation is already correct, so these are characterization tests for the refactor in the next steps, not a red/green bug fix.

- [ ] **Step 3: Add the persistent scratch members to `Lighting.h`**

Replace the `private:` section of `src/World/Lighting.h` (currently lines 125-146):

```cpp
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
    // no second full-grid scan needed to extract it. Serves the world-wide
    // recompute, a single moving source (Lighting::heldTorchLight), and (via
    // its own separate BFS, not this one) the ambient-outline reachability
    // query alike.
    //
    // Uses floodStamp/floodBest/floodGeneration (below) as scratch rather
    // than allocating a fresh full-world "visited" buffer per call: a cell
    // counts as touched this call only when floodStamp[i] == floodGeneration,
    // so resetting between calls is an O(1) counter bump instead of an
    // O(world size) fill - see recomputeAll and heldTorchLight's own
    // comments for why this runs often enough (every frame, for
    // heldTorchLight) that the old per-call allocation mattered.
    std::vector<std::pair<sf::Vector2i, int>> floodFill(const World& world,
                                                          const std::vector<LightSeed>& seeds) const;

    std::vector<LightLevel> levels; // WORLD_WIDTH * WORLD_HEIGHT

    // Persistent scratch for floodFill, sized once at construction rather
    // than reallocated per call. floodBest[i] is only meaningful when
    // floodStamp[i] == floodGeneration; any other stamp value means tile i
    // hasn't been touched during the current call, equivalent to the old
    // per-call buffer's -1 sentinel.
    mutable std::vector<std::uint32_t> floodStamp; // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::vector<std::int8_t> floodBest;     // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::uint32_t floodGeneration = 0;

    // Persistent scratch for ambientOutline's own BFS - added in a later
    // step of this same change, same generation-stamp trick, kept separate
    // from floodFill's scratch above since the two searches store different
    // payloads (best light level vs. BFS step count).
    mutable std::vector<std::uint32_t> outlineStamp; // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::vector<std::int16_t> outlineStep;   // WORLD_WIDTH * WORLD_HEIGHT
    mutable std::uint32_t outlineGeneration = 0;
};
```

(This step adds the `outlineStamp`/`outlineStep`/`outlineGeneration` members now too, even though `ambientOutline` itself isn't refactored until Task 2, so the constructor only needs one edit across both tasks. They sit unused — but correctly sized and zero-initialized — until Task 2 wires them up.)

- [ ] **Step 4: Update the constructor and reimplement `floodFill` in `Lighting.cpp`**

Replace `src/World/Lighting.cpp` lines 12-54 (the constructor and `floodFill`) with:

```cpp
Lighting::Lighting()
    : levels(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, LightLevel{0, 0, 0})
    , floodStamp(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
    , floodBest(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
    , outlineStamp(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
    , outlineStep(static_cast<std::size_t>(WORLD_WIDTH) * WORLD_HEIGHT, 0)
{
}

std::vector<std::pair<sf::Vector2i, int>> Lighting::floodFill(const World& world,
                                                                const std::vector<LightSeed>& seeds) const
{
    // See floodStamp/floodBest's declaration in Lighting.h for why this is
    // a counter bump instead of a std::fill over the whole grid.
    ++floodGeneration;
    if (floodGeneration == 0)
    {
        std::fill(floodStamp.begin(), floodStamp.end(), 0);
        floodGeneration = 1;
    }

    std::vector<LightSeed> queue;

    auto tryVisit = [&](int x, int y, int level)
    {
        if (level <= 0 || !world.inBounds(x, y) || world.isSolid(x, y))
            return;

        const std::size_t i = static_cast<std::size_t>(y) * WORLD_WIDTH + x;
        const int currentBest = (floodStamp[i] == floodGeneration) ? floodBest[i] : -1;
        if (level <= currentBest)
            return;

        floodStamp[i] = floodGeneration;
        floodBest[i] = static_cast<std::int8_t>(level);
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
```

Everything else in `Lighting.cpp` (`recomputeAll`, `skyLight`, `torchLight`, `lavaLight`, `heldTorchLight`, `ambientOutline`) stays as-is for this task — `recomputeAll` and `heldTorchLight` already call `floodFill(world, ...)` unqualified from within member functions, so they need no changes to keep compiling and working against the new non-static, scratch-backed version.

- [ ] **Step 5: Build and run the full test suite**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe
```
Expected: all test cases PASS, including every existing `test_lighting.cpp` case and the two added in Step 1. If anything fails, the bug is almost certainly in the stamp/generation comparison logic in `tryVisit` — check that `floodStamp[i] == floodGeneration` (not `>=` or similar) and that both `floodStamp[i]` and `floodBest[i]` are written together on every accepted visit.

- [ ] **Step 6: Commit**

```bash
git add src/World/Lighting.h src/World/Lighting.cpp tests/test_lighting.cpp
git commit -m "perf: reuse persistent scratch buffers in floodFill instead of allocating per call"
```

---

### Task 2: Persistent generation-stamped scratch for `ambientOutline`

**Files:**
- Modify: `src/World/Lighting.cpp:133-199` (`ambientOutline`)
- Test: `tests/test_lighting.cpp` (append new test case)

**Interfaces:**
- Consumes: `outlineStamp`, `outlineStep`, `outlineGeneration` members added to `Lighting.h` in Task 1, Step 3 (already present in the header by the time this task starts).
- Produces: `ambientOutline` keeps its existing signature and return value (`std::vector<std::pair<sf::Vector2i, int>>`, same semantics) — `Game::render` (`Game.cpp:1149-1150`) calls it exactly as today, unmodified.

- [ ] **Step 1: Add a characterization test for `ambientOutline` scratch reuse across calls**

Append to `tests/test_lighting.cpp` (after the two test cases added in Task 1):

```cpp
TEST_CASE("ambientOutline's persistent scratch does not leak between successive calls at different player positions")
{
    World world;
    fillSolid(world);
    for (int x = 10; x <= 15; ++x)
        world.set(x, 10, BlockType::Air);
    for (int x = 30; x <= 35; ++x)
        world.set(x, 30, BlockType::Air);

    Machines machines;
    Lighting lighting;
    lighting.recomputeAll(world, machines);

    lighting.ambientOutline(world, {10, 10});
    const auto second = lighting.ambientOutline(world, {30, 30});

    auto levelAt = [&](int x, int y) -> int
    {
        for (const auto& [tile, level] : second)
            if (tile.x == x && tile.y == y)
                return level;
        return 0;
    };

    CHECK(levelAt(35, 30) == Lighting::AMBIENT_OUTLINE_LEVEL);
    // The two corridors aren't connected, so nothing from the first query's
    // region should appear in the second call's result.
    CHECK(levelAt(15, 10) == 0);
    CHECK(levelAt(10, 10) == 0);
}
```

- [ ] **Step 2: Run the test suite to confirm this passes against today's (unmodified) `ambientOutline`**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe -tc="ambientOutline's persistent scratch does not leak between successive calls at different player positions"
```
Expected: PASS. Baseline confirmed before refactoring.

- [ ] **Step 3: Reimplement `ambientOutline` in `Lighting.cpp` using the persistent scratch buffers**

Replace `src/World/Lighting.cpp` lines 133-199 (the full body of `ambientOutline`) with:

```cpp
std::vector<std::pair<sf::Vector2i, int>> Lighting::ambientOutline(const World& world, sf::Vector2i playerTile,
                                                                    int radius) const
{
    // A reachability BFS through open tiles only, bounded by step count
    // rather than a decaying value (every reached tile gets the same flat
    // level) - reuses the same "visit each tile at most once, solid tiles
    // are walls" shape as floodFill, but floodFill's early-exit is keyed on
    // a *level* reaching 0, which doesn't fit "same value everywhere, cut
    // off by distance" - so this is its own small BFS instead of a floodFill
    // call. Uses outlineStamp/outlineStep (Lighting.h) as scratch, same
    // generation-stamp trick as floodFill's floodStamp/floodBest - see that
    // declaration's comment for why.
    if (!world.inBounds(playerTile.x, playerTile.y) || world.isSolid(playerTile.x, playerTile.y))
        return {};

    ++outlineGeneration;
    if (outlineGeneration == 0)
    {
        std::fill(outlineStamp.begin(), outlineStamp.end(), 0);
        outlineGeneration = 1;
    }

    std::vector<sf::Vector2i> queue{playerTile};

    const std::size_t playerIndex = static_cast<std::size_t>(playerTile.y) * WORLD_WIDTH + playerTile.x;
    outlineStamp[playerIndex] = outlineGeneration;
    outlineStep[playerIndex] = 0;

    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const sf::Vector2i tile = queue[head];
        const std::size_t tileIndex = static_cast<std::size_t>(tile.y) * WORLD_WIDTH + tile.x;
        const int steps = outlineStep[tileIndex];

        if (steps >= radius)
            continue;

        const sf::Vector2i neighbors[4] = {
            {tile.x - 1, tile.y}, {tile.x + 1, tile.y}, {tile.x, tile.y - 1}, {tile.x, tile.y + 1}};

        for (const sf::Vector2i& n : neighbors)
        {
            if (!world.inBounds(n.x, n.y) || world.isSolid(n.x, n.y))
                continue;

            const std::size_t i = static_cast<std::size_t>(n.y) * WORLD_WIDTH + n.x;
            if (outlineStamp[i] == outlineGeneration)
                continue;

            outlineStamp[i] = outlineGeneration;
            outlineStep[i] = static_cast<std::int16_t>(steps + 1);
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

- [ ] **Step 4: Build and run the full test suite**

Run:
```bash
cmake --build build --config Debug --target Litharia_tests
build/Debug/Litharia_tests.exe
```
Expected: all test cases PASS, including every existing `ambientOutline`-related case (radius default/custom, ore vs. plain brightness, disconnected-pocket exclusion, out-of-bounds) and the one added in Step 1.

- [ ] **Step 5: Commit**

```bash
git add src/World/Lighting.cpp tests/test_lighting.cpp
git commit -m "perf: reuse persistent scratch buffers in ambientOutline instead of allocating per call"
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
Expected: builds successfully with no new warnings from `Lighting.cpp`/`Lighting.h`.

- [ ] **Step 2: Run the game and check for correctness regressions**

Run:
```bash
build/Debug/Litharia.exe
```
In-game: mine into an open cavern, place a Torch, walk around with it both equipped and unequipped. Confirm:
- Torch, Lava, and sky light still appear in the same places, same brightness, same falloff as before this change (nothing about *what* lights up should have changed, only how it's computed internally).
- Walking near unlit rock still shows the faint ambient outline (and ore tiles still read slightly brighter than plain stone).

- [ ] **Step 3: Check for the reported lag**

With a Torch equipped, walk through a large open cavern (the worst case: `ambientOutline`'s radius is sized to the full camera view when a Torch is held, per `Game.cpp:1140-1147`). Confirm movement feels smoother than before this change — this is a subjective/manual check since this repo has no frame-time benchmark harness; if it's still choppy, that points at a second cost (e.g. `LightRenderer::draw`'s per-visible-tile work, called out as out-of-scope in the design spec) worth investigating separately.

No commit for this task — it's verification only, nothing to stage.
