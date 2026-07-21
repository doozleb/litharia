# Wall Light Penetration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a solid tile borrow real light (sky, Torch, or Lava alike) from up to 3 tiles away instead of just its 4 immediate neighbours, dimming 1 level per tile of distance - so a wall a couple of tiles from a lit cavity (or hiding an ore seam) glows faintly instead of reading fully dark, without changing anything about how far light actually *propagates* for gameplay purposes.

**Architecture:** A pure rendering-time change confined entirely to `LightRenderer::draw` (`src/World/LightRenderer.cpp`) - the current 4-neighbour "borrow one step dimmer" lookup becomes a bounded 24-cell diamond search (every tile within Manhattan distance 1-3), taking the best `channelValue - distance` candidate per channel. `Lighting`'s stored grid, its recompute triggers, and every one of its existing tests are untouched.

**Tech Stack:** C++20, SFML 3 (Graphics), CMake + Visual Studio 18 2026 generator (existing project setup - no new dependencies).

## Global Constraints

- This change touches only `src/World/LightRenderer.cpp` - `Lighting` (`src/World/Lighting.h/.cpp`), its recompute triggers in `Game.cpp`, and `tests/test_lighting.cpp` are not modified. A solid tile must continue to read exactly 0 from `Lighting::skyLight`/`torchLight`/`lavaLight` directly (unchanged invariant) - this plan only changes what `LightRenderer` chooses to paint on top of a solid tile it already draws.
- Penetration depth is fixed at 3 tiles (Manhattan distance), decaying by exactly 1 per tile of distance - matching the existing decay rate used everywhere else in this system.
- All three channels (sky, Torch, Lava) get this treatment identically - no channel-specific carve-out.
- `LightRenderer` has no unit test today (SFML Graphics code, matches the project's existing convention for `ChunkRenderer`/`MachineRenderer`) - this plan doesn't add one. Verification is a clean build of the `Litharia` executable target.
- Every changed source file must build under the existing `cmake --build build --config Debug` invocation, and `Litharia_tests.exe` must pass in full (unaffected by this change, since it never links `LightRenderer`).

---

### Task 1: Extend the solid-tile borrow rule from 1 neighbour to a 3-tile diamond

**Files:**
- Modify: `src/World/LightRenderer.cpp` (only the solid-tile branch of `draw`, plus one new file-local helper)

**Interfaces:**
- Consumes: `Lighting::skyLight`/`torchLight`/`lavaLight` (unchanged signatures), `World::isSolid` (unchanged), the existing `heldMap` local built earlier in `draw` (unchanged).
- Produces: no new public interface - `LightRenderer::draw`'s signature is unchanged, this task only changes its internal solid-tile brightness computation. Nothing later depends on new interfaces from this task; it is a self-contained visual change.

This task has no unit test (see Global Constraints) - verification is the executable building cleanly and a manual/visual check (Step 4).

- [ ] **Step 1: Read the current file to confirm nothing has shifted**

Read `src/World/LightRenderer.cpp` in full and confirm the solid-tile branch still matches this shape (it was last touched by the "fix: make held-torch light reach solid walls like a placed Torch does" commit):

```cpp
            if (world.isSolid(x, y))
            {
                // A solid tile borrows one step dimmer than its brightest
                // open neighbour, per channel - Lighting's BFS never
                // assigns a solid tile its own light, so a neighbour that's
                // itself solid always reads 0 here already, no separate
                // isSolid check needed on the neighbours themselves.
                //
                // Torch is the one channel with a "held" equivalent: the
                // player's held Torch (heldMap) lights open tiles the same
                // way a placed Torch would, but never touches the stored
                // grid, so a neighbour lookup that only reads
                // lighting.torchLight would miss it - fold heldMap into the
                // neighbour lookup too, same as the tile-itself case below.
                const auto torchAt = [&lighting, &heldMap](int nx, int ny) {
                    int level = lighting.torchLight(nx, ny);
                    const auto it = heldMap.find(tileKey(nx, ny));
                    if (it != heldMap.end())
                        level = std::max(level, it->second);
                    return level;
                };

                const int skyN = std::max({lighting.skyLight(x - 1, y), lighting.skyLight(x + 1, y),
                                            lighting.skyLight(x, y - 1), lighting.skyLight(x, y + 1)});
                const int torchN = std::max({torchAt(x - 1, y), torchAt(x + 1, y),
                                              torchAt(x, y - 1), torchAt(x, y + 1)});
                const int lavaN = std::max({lighting.lavaLight(x - 1, y), lighting.lavaLight(x + 1, y),
                                             lighting.lavaLight(x, y - 1), lighting.lavaLight(x, y + 1)});

                skyRaw = std::max(0, skyN - 1);
                torchRaw = std::max(0, torchN - 1);
                lavaRaw = std::max(0, lavaN - 1);
            }
```

If the file differs meaningfully from this (beyond trivial whitespace), stop and report NEEDS_CONTEXT rather than guessing which version is current.

- [ ] **Step 2: Add the offset table and replace the solid-tile branch**

In `src/World/LightRenderer.cpp`, add this to the anonymous namespace at the top of the file, right after the `appendQuad` function (before the closing `} // namespace`):

```cpp
// How far real light (sky, Torch, or Lava alike) can be glimpsed through
// solid rock: a solid tile borrows the best `channelValue - distance`
// candidate from every tile within this many orthogonal steps, not just its
// 4 immediate neighbours - a direct generalization of "borrow one step
// dimmer than the brightest neighbour" out to a wider radius, so a wall a
// couple of tiles from a lit cavity glows faintly instead of reading fully
// dark. Distance 1 alone reproduces today's 4-neighbour behaviour exactly.
constexpr int WALL_PENETRATION_DEPTH = 3;

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
```

Add `#include <tuple>` to the existing include block at the top of the file (alongside `<algorithm>`, `<cmath>`, `<cstdint>`, `<unordered_map>`).

Then replace the solid-tile branch shown in Step 1 with:

```cpp
            if (world.isSolid(x, y))
            {
                // A solid tile borrows the best (channelValue - distance)
                // candidate from anywhere within WALL_PENETRATION_DEPTH
                // orthogonal steps, per channel - see wallPenetrationOffsets
                // for why no separate isSolid check is needed on the
                // candidate tiles themselves.
                //
                // Torch is the one channel with a "held" equivalent: the
                // player's held Torch (heldMap) lights open tiles the same
                // way a placed Torch would, but never touches the stored
                // grid, so a lookup that only reads lighting.torchLight
                // would miss it - fold heldMap into the lookup too, same as
                // the tile-itself case below.
                const auto torchAt = [&lighting, &heldMap](int nx, int ny) {
                    int level = lighting.torchLight(nx, ny);
                    const auto it = heldMap.find(tileKey(nx, ny));
                    if (it != heldMap.end())
                        level = std::max(level, it->second);
                    return level;
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

                    skyBest = std::max(skyBest, lighting.skyLight(nx, ny) - distance);
                    torchBest = std::max(torchBest, torchAt(nx, ny) - distance);
                    lavaBest = std::max(lavaBest, lighting.lavaLight(nx, ny) - distance);
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
```

- [ ] **Step 3: Build the executable target**

Run: `"/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia`
Expected: SUCCESS - no compile errors. (`std::tuple`/structured bindings on a `std::tuple` require the `<tuple>` include added in Step 2; everything else used - `std::vector`, `std::abs`, `std::max` - is already available via this file's existing includes.)

Run: `"/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug --target Litharia_tests`
Then: `C:\litharia\build\Debug\Litharia_tests.exe`
Expected: PASS, unchanged from before this task - `Litharia_tests` never links `LightRenderer.cpp`, so this confirms only that nothing else was accidentally broken.

- [ ] **Step 4: Manually verify in-game**

Run `C:\litharia\build\Debug\Litharia.exe` and confirm:
- A wall 1 tile from a lit Torch, Lava pool, or sunlit shaft looks the same as it did before this change (distance-1 behaviour is unchanged by construction).
- A wall 2-3 tiles from one of those now reads as a faint, progressively dimmer glow instead of solid black - including being able to make out that a tile 1-2 layers behind a wall is ore (a different colour from plain stone/dirt) rather than only ever seeing the wall's own material.
- A wall 4+ tiles from the nearest real light source is unaffected - it still falls back to the ambient-outline floor or full black exactly as before this task.
- A sunlit surface tile's glow into the ground beneath it behaves the same way (sky counts identically to Torch/Lava, per this feature's scope).

This is a manual check, not an automated one, matching this file's existing convention (no unit test exists for `LightRenderer`).

- [ ] **Step 5: Commit**

```bash
git add src/World/LightRenderer.cpp
git commit -m "feat: let real light glimpse up to 3 tiles into solid rock, not just 1"
```
