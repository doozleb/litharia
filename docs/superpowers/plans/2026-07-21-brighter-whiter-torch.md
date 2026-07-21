# Brighter, Whiter Torch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Torch (both placed and held) noticeably brighter and farther-reaching than sky/Lava by giving it its own, higher light-seed level, and shift its rendered tint partway toward white.

**Architecture:** Two independent, small changes. (1) A new `Lighting::TORCH_LIGHT_LEVEL` constant (15, using spare headroom already present in the 4-bit `torch` field - no data-model change) replaces `MAX_LIGHT_LEVEL` at the two places a Torch's own light gets seeded (`recomputeAll` for a placed Torch, `heldTorchLight` for the player's carried one) - sky and Lava keep seeding at the unchanged `MAX_LIGHT_LEVEL` (9), and the render-time brightness divisor stays `MAX_LIGHT_LEVEL` too, which is what makes a Torch both brighter (stays clipped to full brightness over a wider radius) and farther-reaching (takes longer to decay to 0). (2) `TORCH_TINT` in `LightRenderer.cpp` changes from `(255, 200, 110)` to `(255, 228, 183)`, a 50/50 blend toward white.

**Tech Stack:** C++20, SFML 3 (Graphics/Window/System), doctest, CMake + Visual Studio 18 2026 generator (existing project setup - no new dependencies).

## Global Constraints

- `Lighting::TORCH_LIGHT_LEVEL` is exactly `15` - the maximum representable value in the existing 4-bit `torch` field (`LightLevel::torch : 4`), so no change to `LightLevel`'s size or its `static_assert`.
- Only Torch's own seeding changes (`recomputeAll`'s `torchSeeds` loop, and `heldTorchLight`'s seed) - sky's and Lava's seeding, `MAX_LIGHT_LEVEL` itself, the render-time brightness-normalization divisor, the ambient-outline levels/radius, and the wall-penetration search/penalty table are all untouched.
- Held and placed Torch light must stay identical to each other (both use `TORCH_LIGHT_LEVEL`) - `Lighting.h`'s own existing doc comment on `heldTorchLight` already promises this equivalence.
- `TORCH_TINT` becomes `(255, 228, 183)` - `LAVA_TINT`, the sky gradient, and `OUTLINE_TINT` are untouched.
- Every changed source file must build under the existing `cmake --build build --config Debug` invocation, and `Litharia_tests.exe` must pass in full after every task.

---

### Task 1: `Lighting::TORCH_LIGHT_LEVEL` - a brighter, farther-reaching seed level for Torch light

**Files:**
- Modify: `src/World/Lighting.h`
- Modify: `src/World/Lighting.cpp`
- Modify: `tests/test_lighting.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `static constexpr int Lighting::TORCH_LIGHT_LEVEL = 15`. Task 2 (the tint change) doesn't depend on this - the two tasks are independent and can be done in either order.

- [ ] **Step 1: Update the failing/changing tests**

In `tests/test_lighting.cpp`, add this new test case right after the existing `"MAX_LIGHT_LEVEL is 9"` test case (around line 22):

```cpp
TEST_CASE("TORCH_LIGHT_LEVEL is 15")
{
    CHECK(Lighting::TORCH_LIGHT_LEVEL == 15);
}
```

Change the assertions in `"a placed Torch lights the torch channel only, decaying by 1 per step"` from:

```cpp
    CHECK(lighting.torchLight(10, 10) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.torchLight(9, 10) == Lighting::MAX_LIGHT_LEVEL - 1);
```

to:

```cpp
    CHECK(lighting.torchLight(10, 10) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(lighting.torchLight(9, 10) == Lighting::TORCH_LIGHT_LEVEL - 1);
```

Add this new test case right after that same test case:

```cpp
TEST_CASE("a placed Torch reads brighter than MAX_LIGHT_LEVEL, unlike Lava or sky")
{
    World world;
    fillSolid(world);
    world.set(10, 10, BlockType::Air);

    Machines machines;
    machines.place(MachineType::Torch, 10, 10, Direction::Right);

    Lighting lighting;
    lighting.recomputeAll(world, machines);

    CHECK(lighting.torchLight(10, 10) > Lighting::MAX_LIGHT_LEVEL);
    CHECK(lighting.torchLight(10, 10) == Lighting::TORCH_LIGHT_LEVEL);
}
```

Rename `"heldTorchLight lights its source at the max level and decays by 1 per step"` to `"heldTorchLight lights its source at TORCH_LIGHT_LEVEL and decays by 1 per step"`, and change its assertions from:

```cpp
    CHECK(levelAt(10, 10) == Lighting::MAX_LIGHT_LEVEL);
    CHECK(levelAt(9, 10) == Lighting::MAX_LIGHT_LEVEL - 1);
    CHECK(levelAt(8, 10) == Lighting::MAX_LIGHT_LEVEL - 2);
```

to:

```cpp
    CHECK(levelAt(10, 10) == Lighting::TORCH_LIGHT_LEVEL);
    CHECK(levelAt(9, 10) == Lighting::TORCH_LIGHT_LEVEL - 1);
    CHECK(levelAt(8, 10) == Lighting::TORCH_LIGHT_LEVEL - 2);
```

No other test case in this file changes - every Lava-only, sky-only, ambient-outline, and out-of-bounds test keeps asserting against `MAX_LIGHT_LEVEL` exactly as before, since none of them involve Torch light values.

- [ ] **Step 2: Run the tests to verify they fail to build**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAIL - `Lighting::TORCH_LIGHT_LEVEL` is not declared yet.

- [ ] **Step 3: Add `TORCH_LIGHT_LEVEL` and update doc comments in `src/World/Lighting.h`**

Add this constant right after the existing `MAX_LIGHT_LEVEL` declaration:

```cpp
    // How far light travels before going fully dark: a source (a placed
    // Torch, a Lava tile, an open sky column) starts at this level and
    // decays by 1 per orthogonal step, so a tile this many steps away is the
    // last one that still reads as lit at all - one dimmer at each step in
    // between.
    static constexpr int MAX_LIGHT_LEVEL = 9;

    // A Torch's own seed level - deliberately higher than MAX_LIGHT_LEVEL,
    // using headroom the 4-bit `torch` field already has (up to 15), so a
    // Torch is both brighter and farther-reaching than sky or Lava at the
    // same distance: brightness is still normalized against MAX_LIGHT_LEVEL
    // at render time, so a Torch stays fully bright out to
    // (TORCH_LIGHT_LEVEL - MAX_LIGHT_LEVEL) tiles farther than a source
    // seeded at MAX_LIGHT_LEVEL would, and doesn't decay to 0 until this
    // many steps out instead of MAX_LIGHT_LEVEL. Used for both a placed
    // Torch (recomputeAll) and the player's held Torch (heldTorchLight) -
    // the two are deliberately kept identical, per heldTorchLight's own
    // comment below.
    static constexpr int TORCH_LIGHT_LEVEL = 15;
```

Update the `LightLevel` struct's `torch` field comment from:

```cpp
    std::uint16_t torch : 4; // 0-MAX_LIGHT_LEVEL, this tile's Torch exposure
```

to:

```cpp
    std::uint16_t torch : 4; // 0-TORCH_LIGHT_LEVEL, this tile's Torch exposure
```

Update the class-level doc comment from:

```cpp
// Computes and stores per-tile lighting: how exposed to the sky a tile is
// (skyLight), how close it is to a placed Torch (torchLight), and how close
// it is to a Lava tile (lavaLight) - each 0-MAX_LIGHT_LEVEL, decaying by 1
// per orthogonal step, blocked entirely by solid tiles. Torch and Lava are
```

to:

```cpp
// Computes and stores per-tile lighting: how exposed to the sky a tile is
// (skyLight), how close it is to a placed Torch (torchLight), and how close
// it is to a Lava tile (lavaLight) - sky and lava are 0-MAX_LIGHT_LEVEL,
// torch is 0-TORCH_LIGHT_LEVEL (deliberately brighter and farther-reaching
// than the other two sources) - each decaying by 1 per orthogonal step,
// blocked entirely by solid tiles. Torch and Lava are
```

Update the `torchLight` accessor's comment from:

```cpp
    int torchLight(int x, int y) const; // 0-MAX_LIGHT_LEVEL; 0 out of bounds
```

to:

```cpp
    int torchLight(int x, int y) const; // 0-TORCH_LIGHT_LEVEL; 0 out of bounds
```

Update `heldTorchLight`'s doc comment from:

```cpp
    // A single-source flood fill from `source` at MAX_LIGHT_LEVEL - the same
    // brightness and decay/occlusion rule as a placed Torch's own
    // torchLight, but computed fresh every call rather than stored in
    // `levels`. Used for the player's held Torch, which moves with them
    // every frame: naturally bounded to within MAX_LIGHT_LEVEL steps of
    // `source` (the seed starts there and floodFill's decay reaches 0 by
    // then), so this stays cheap enough to call once a frame without forcing
    // a full recompute.
```

to:

```cpp
    // A single-source flood fill from `source` at TORCH_LIGHT_LEVEL - the
    // same brightness and decay/occlusion rule as a placed Torch's own
    // torchLight, but computed fresh every call rather than stored in
    // `levels`. Used for the player's held Torch, which moves with them
    // every frame: naturally bounded to within TORCH_LIGHT_LEVEL steps of
    // `source` (the seed starts there and floodFill's decay reaches 0 by
    // then), so this stays cheap enough to call once a frame without forcing
    // a full recompute.
```

- [ ] **Step 4: Use `TORCH_LIGHT_LEVEL` for Torch seeding in `src/World/Lighting.cpp`**

In `recomputeAll`, change:

```cpp
    for (const Machine& m : machines.all())
        if (m.type == MachineType::Torch)
            torchSeeds.push_back({m.x, m.y, MAX_LIGHT_LEVEL});
```

to:

```cpp
    for (const Machine& m : machines.all())
        if (m.type == MachineType::Torch)
            torchSeeds.push_back({m.x, m.y, TORCH_LIGHT_LEVEL});
```

In `heldTorchLight`, change:

```cpp
std::vector<std::pair<sf::Vector2i, int>> Lighting::heldTorchLight(const World& world,
                                                                    sf::Vector2i source) const
{
    return floodFill(world, {LightSeed{source.x, source.y, MAX_LIGHT_LEVEL}});
}
```

to:

```cpp
std::vector<std::pair<sf::Vector2i, int>> Lighting::heldTorchLight(const World& world,
                                                                    sf::Vector2i source) const
{
    return floodFill(world, {LightSeed{source.x, source.y, TORCH_LIGHT_LEVEL}});
}
```

Sky's and Lava's seeding (`skySeeds.push_back({x, y, MAX_LIGHT_LEVEL})` and `lavaSeeds.push_back({x, y, MAX_LIGHT_LEVEL})`) are untouched.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - every new and changed test passes, and the full existing suite (every Lava-only, sky-only, and ambient-outline test, all still asserting against the unchanged `MAX_LIGHT_LEVEL`) still passes.

- [ ] **Step 6: Commit**

```bash
git add src/World/Lighting.h src/World/Lighting.cpp tests/test_lighting.cpp
git commit -m "feat: give Torch its own brighter, farther-reaching light level"
```

---

### Task 2: Whiter Torch tint

**Files:**
- Modify: `src/World/LightRenderer.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: no new public interface - purely a constant color change. Independent of Task 1 (different file, no shared code path); can be done in either order.

This task has no unit test (`LightRenderer` has no window-free test target, matching the project's existing convention for `ChunkRenderer`/`MachineRenderer`) - verification is a clean executable build plus a manual/visual check (Step 3).

- [ ] **Step 1: Change `TORCH_TINT`**

In `src/World/LightRenderer.cpp`, change:

```cpp
constexpr sf::Color TORCH_TINT(255, 200, 110);
```

to:

```cpp
constexpr sf::Color TORCH_TINT(255, 228, 183);
```

- [ ] **Step 2: Build the executable and run the full test suite**

Run: `cmake --build build --config Debug --target Litharia`
Expected: SUCCESS.

Run: `cmake --build build --config Debug --target Litharia_tests`
Then: `./build/Debug/Litharia_tests.exe`
Expected: PASS - unchanged from Task 1's Step 5 (this file isn't linked into `Litharia_tests`).

- [ ] **Step 3: Manually verify in-game**

Run `Litharia.exe` and confirm: a lit Torch (placed or held) glows noticeably brighter and reaches farther than before this feature, and its color reads as a lighter, warmer-white glow rather than the previous saturated orange-yellow. This is a manual check, not an automated one, matching this file's existing convention.

- [ ] **Step 4: Commit**

```bash
git add src/World/LightRenderer.cpp
git commit -m "tweak: shift the Torch's tint partway toward white"
```
