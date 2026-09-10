# Litharia

A 2D sandbox engine written from scratch in C++20 — no game framework doing the
hard parts. Fluid simulation, propagating light, procedural terrain, machines
and a factory chain, all built on a simulation core that runs headless so it can
actually be tested.

```
290 commits · 22 core translation units · 26 test files
simulation core links no graphics code at all
```

---

## What's in it

| System | What it does |
|---|---|
| **Fluid simulation** | Cellular fluid with surface tracking — water settles, flows, and finds its level across chunk boundaries |
| **Lighting** | Light propagates through the tile grid and falls off with distance, driven by a day/night clock |
| **World storage** | Sparse chunked tile grid, so an effectively unbounded world only pays for what exists |
| **Terrain** | Procedural generation with caves, layered strata and ore distribution |
| **Physics** | Tile-aware collision, gravity and knockback for the player and entities |
| **Machines** | Placeable machines with a recipe system, power, and item transport between them |
| **Items** | Inventory, dropped item entities, pickup, mining and placing |
| **Enemies** | Spawning tied to light level and depth, chase AI, contact damage |

## The bit that matters

**The simulation core links no graphics code.**

`Litharia_core` is a static library containing 22 translation units — world,
fluids, lighting, terrain, physics, machines, items, enemies — and it depends on
SFML *System* only. No window, no renderer, no GPU.

That is not an aesthetic choice. It means the entire world can be stepped inside
a test, with no window and no frame loop:

```cpp
TEST_CASE("isWater/isLava/isFluid correctly classify every fluid level, "
          "and nothing else")
{
    for (int level = 1; level <= 8; ++level)
    {
        CHECK(isWater(waterAtLevel(level)));
        CHECK_FALSE(isLava(waterAtLevel(level)));
        CHECK(isFluid(waterAtLevel(level)));
    }

    CHECK_FALSE(isFluid(BlockType::Air));
    CHECK_FALSE(isWater(BlockType::Lava8));
}
```

The suite runs in about a second rather than by playing the game, and the hard
bugs it has caught are exactly the ones that are miserable to reproduce by hand:

- fluid not staying **conservative at the world border**
- lighting **not recomputing when lava moves**, only on block and torch edits
- a **held torch not lighting solid walls** the way a placed one does
- **hill caves not anchoring to hills**, leaving trees floating over entrances

If you take one thing from this repository, take that: **separate the thing that
simulates from the thing that draws, before you need to.**

## Building

Requires **CMake 3.20+**, a **C++20** compiler, and **SFML 3**.

SFML is expected at `libs/SFML` (gitignored — fetch it separately):

```bash
cmake -B build -S .
cmake --build build --config Release
```

Two targets are produced:

```
Litharia         the game       (Graphics + Window + System)
Litharia_tests   the test suite (System only, no window)
```

On MSVC the build passes `/FS`, because a broadly-included header changing can
otherwise race several `CL.EXE` processes against one `.pdb` and fail with
C1041.

## Tests

```bash
./build/Litharia_tests
```

26 test files covering fluids, lighting, terrain, physics, mining, placing,
inventory, pickup, machines, power, recipes, processing, extraction, transport,
enemies, spawning, the day/night clock and the sparse tile grid.

They run headless, so they work in CI and finish fast enough to run on every
change.

## Layout

```
src/World/      world, chunks, fluids, lighting, terrain, day/night
src/Blocks/     block definitions
src/Physics/    collision and movement
src/Machines/   machines, recipes, power, transport
src/Items/      inventory, item entities
src/Enemies/    spawning and AI
src/Player/     player state and input handling
src/Camera/     view and follow behaviour
src/Hud/        HUD and layout
src/Game/       the loop that ties it together
src/Tile/       tile representation
src/Core/       constants, direction, noise
tests/          26 headless test files
docs/           specs
plans/          implementation plans
```

## How it is built

Every feature starts as a written spec, becomes a plan broken into reviewable
tasks, and is implemented test-first — the failing test written and *observed
failing* before any implementation exists. Specs and plans are committed
alongside the code in `docs/` and `plans/`.

Much of it is written with AI coding agents under that discipline. Doing this on
C++ rather than on web code is the subject of
**[doozleb.com](https://doozleb.com)**, and the related project is
[FourShades](https://github.com/doozleb/FourShades) — a Game Boy emulator built
the same way and scored against public test ROMs.

## Status

Actively developed, and not finished. It is a working sandbox you can play, not
a polished game.
