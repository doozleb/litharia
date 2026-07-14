# Litharia — Base Vertical Slice Design

**Date:** 2026-07-14
**Status:** Approved
**Scope:** Terrain generation, player movement, mining, dropped items, inventory.

## Goal

Litharia is a 2D side-view factory game in the Terraria mold. This design covers only
the foundation: a generated world you can walk through, dig into, and carry things out
of. No machines, no conveyors, no crafting. Those come later and are explicitly out of
scope here.

The foundation must not paint us into a corner, so two decisions are made now with the
factory game in mind:

- The generator seeds **ore veins**, because ores are the raw input to every machine we
  will ever add.
- Mined blocks become **item entities on the ground**, because machine output will need
  somewhere to go.

## Current state

The repository has the folder skeleton but almost no code. `Game` opens a window and
runs a loop, `World` fills a fixed 50x37 array with flat bands of grass/dirt/stone, and
`Tile` draws itself as two `sf::RectangleShape`s. `Player`, `Camera`, `Chunks`,
`Blocks.cpp`, and `TerrainGenerator::generate()` are empty. The camera exists as a
`sf::Vector2f` pinned to `{0,0}`.

## Architecture

### The central decision: simulation does not know about rendering

`Tile` currently owns an `sf::RectangleShape` and knows how to draw itself. That single
coupling is why the current code cannot be tested without a window and why it cannot
scale past one screen. It is undone first.

`Tile` becomes plain data: one `BlockType`, one byte. A 1000x500 world is then 500 KB,
held as a flat `std::vector<Tile>` indexed `y * WORLD_WIDTH + x`. Rendering moves to a
separate chunk renderer that reads the world but never mutates it.

The result is that `World`, `TerrainGenerator`, `Inventory`, `Physics`, and the item
entities are pure logic with no window dependency, and can be tested in one second
instead of by playing the game.

### Module boundaries

| Module | Responsibility | Depends on |
|---|---|---|
| `Blocks` | `BlockType` enum + block registry (name, color, solid, hardness, drop) | — |
| `Items` | `ItemType` enum + item registry, `ItemStack`, `Inventory` | `Blocks` |
| `Tile` | Plain data: one `BlockType` | `Blocks` |
| `Noise` | Seeded hash-based value noise + fbm | — |
| `World` | Tile storage, bounds-safe accessors, `isSolid` | `Tile` |
| `TerrainGenerator` | Fills a `World` deterministically from a seed | `World`, `Noise` |
| `Physics` | `moveAndCollide(box, velocity, world)` | `World` |
| `Player` | AABB, input, movement, mining, placing | `Physics`, `World`, `Items` |
| `ItemEntity` | Dropped stack with gravity and magnet pickup | `Physics`, `Items` |
| `Chunks` | Vertex-array chunk renderer with dirty flags and culling | `World`, SFML |
| `Camera` | `sf::View` wrapper, follows player, clamps to world | SFML |
| `Hud` | Hotbar rendering | `Items`, SFML |
| `Game` | Owns everything, fixed-timestep loop | all |

Only `Chunks`, `Camera`, `Hud`, and `Game` touch SFML's Graphics/Window modules. The
rest use at most `sf::Vector2f` from SFML System, which needs no window and links into
the test binary freely.

### World constants

```
TILE_SIZE    = 16 px
WORLD_WIDTH  = 1000 tiles
WORLD_HEIGHT = 500 tiles
CHUNK_SIZE   = 32 tiles
```

The world is finite and fully resident in memory. Chunks exist only to batch rendering
and cull offscreen tiles — there is no streaming, no chunk load/unload, and no
generate-on-demand. This is Terraria's actual model and it removes an entire class of
complexity we do not need.

## Terrain generation

`TerrainGenerator(uint32_t seed)` exposes `generate(World&)` and runs three passes.
All randomness derives from the seed via a hash function, so the generator is a pure
function: the same seed always produces a byte-identical world. This is the property
the tests hang off.

**Pass 1 — Surface.** Fractal value noise over `x` produces a rolling surface height,
clamped to stay well inside the world bounds. Grass sits on the surface tile, a dirt
band runs roughly 8 tiles beneath it, and stone fills everything below.

**Pass 2 — Caves.** 2D fractal noise; where it crosses a threshold the tile is carved to
air. The threshold tightens near the surface so caves do not shred the landscape into
holes.

**Pass 3 — Ore veins.** For each ore type, hashed candidate points are scattered within
a depth band, and each grows a small blob. **A vein tile is only written where the
existing tile is stone.** This is what keeps ore from floating inside caves or embedding
in the dirt band. Copper spawns in the shallow band, iron deeper.

Blocks in scope: `Air`, `Grass`, `Dirt`, `Stone`, `CopperOre`, `IronOre`.

## Player, physics, and mining

The player is an axis-aligned box of **30 x 46 px** (~1.9 x 2.9 tiles). It reads as the
intended 2-wide, 3-tall body, but is deliberately a hair under 2 full tiles: a box
exactly `2 * TILE_SIZE` wide cannot reliably pass through a 2-tile gap once
floating-point rounding enters the picture, and would wedge in its own corridors.

Movement is horizontal acceleration with ground friction, gravity, and a jump gated on
being grounded.

Collision resolves **one axis at a time**: apply X velocity, push out of any overlapping
solid tile and zero X velocity; then the same for Y, setting a `grounded` flag when the
correction came from below. This is the standard approach, it will not tunnel at our
speeds, and it lives in `Physics` as a free function over `(box, velocity, world)` —
because dropped items need exactly the same behavior. One implementation, two callers.

**Mining** is hold-to-break. Left-click a tile within a ~5 tile reach; progress
accumulates against that block's hardness from the registry; on completion the tile is
set to air and its drop spawns as an item entity. Releasing or targeting a different
tile resets progress.

**Placing** is right-click: the selected hotbar item, if it maps to a block, is written
into an air tile — provided the resulting block would not overlap the player's box.
Placement is in scope because it is the only thing that exercises *removing* from the
inventory.

## Items, drops, and inventory

An `ItemStack` is an `ItemType` and a count. An `ItemEntity` is a stack with a position
and velocity, falling through the same `Physics::moveAndCollide` as the player. Within a
magnet radius it accelerates toward the player; on contact it is absorbed.

`Inventory` is a fixed array of 40 slots; the first 10 are the hotbar.

`add(stack)` tops up existing matching stacks before opening a fresh slot, respects each
item's max stack size, and **returns the leftover count** rather than silently discarding
it. The consequence is a real rule, not an edge case:

> If the inventory is full, the item entity stays on the ground and keeps trying.

Nothing is ever destroyed by a full bag. `removeOne(slot)` decrements a stack and clears
the slot at zero.

Ground stacks do not merge with each other. That is a deliberate omission — it is not
needed and can be added when it is.

## Rendering

The world draws through a chunk renderer (32x32 tiles). Each chunk bakes its tiles into
an `sf::VertexArray` once and rebuilds only when a block inside it changes; the camera's
view decides which chunks are drawn at all.

The current code issues roughly 3,700 draw calls per frame for a single screen. This
drops it to about a dozen, and it is what makes a 1000x500 world viable.

**Accepted visual change:** batching means dropping the per-tile black border, so tiles
render as flat colored quads. The border can return later via a texture atlas.

The camera is an `sf::View` wrapper that follows the player with smoothing and clamps to
the world edges, so the view never scrolls past the world into empty space.

The HUD draws the ten hotbar slots with the selected one highlighted. Number keys 1-0
and the scroll wheel change selection.

## Game loop

Physics runs on a **fixed timestep** (1/60 s) via an accumulator, with rendering at the
display rate. A frame hitch must not be able to launch the player through the floor.

## Error handling

| Condition | Behavior |
|---|---|
| Out-of-bounds tile read | Returns `Air`. Never undefined behavior. |
| Out-of-bounds tile write | Silently ignored. |
| Extreme terrain noise | Surface height clamped inside world bounds. |
| Inventory full on pickup | Leftover returned; item entity remains on the ground. |
| Placing a block inside the player | Rejected; inventory unchanged. |
| Missing HUD font in `assets/` | Slots still render, stack counts do not. Warning to stderr. Game runs. |
| Window creation failure | `main` returns non-zero. |

## Testing

A second CMake target, `Litharia_tests`, built on doctest (single vendored header). It
links **only SFML System** — no window, no graphics — which is possible precisely because
of the simulation/rendering split above.

Coverage:

- **World:** get/set round-trip; out-of-bounds reads return `Air`; `isSolid` agrees with
  the block registry.
- **TerrainGenerator:** the same seed produces an identical world, and different seeds do
  not; every column has a surface; ore tiles only ever replace stone; ores stay inside
  their depth band; caves produce air below the surface.
- **Inventory:** add into an empty slot; top up an existing stack; overflow spills into a
  second slot at max stack size; a full inventory returns the correct leftover;
  `removeOne` decrements and clears.
- **Physics:** a falling box lands on the ground and stops; horizontal motion is blocked
  by a wall; a fast-moving box does not tunnel through a 1-tile wall; `grounded` is only
  set when landing.
- **Item magnet:** an entity inside the radius accelerates toward the player; one outside
  it does not.

## Controls

| Input | Action |
|---|---|
| A / D | Move |
| Space | Jump |
| Left click | Mine (hold) |
| Right click | Place selected block |
| 1-0, scroll wheel | Select hotbar slot |

## Out of scope

Machines, conveyors, crafting, enemies, day/night, lighting, world save/load, biomes,
texture atlas, and ground-stack merging. Named here so they are not smuggled in.
