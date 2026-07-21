# Wall Light Penetration: Seeing a Few Tiles Into Lit Rock

## Purpose

Right now a solid tile's rendered brightness (per `LightRenderer::draw`,
`src/World/LightRenderer.cpp:92-124`) only ever borrows from its 4 *immediate*
orthogonal neighbours, one step dimmer than the brightest of them. That's
enough to make a lit tunnel's walls look lit, but it means the moment you're
looking at a wall with two solid tiles between you and a torch-lit cavity (or
an ore seam one tile behind a wall you haven't dug into yet), that wall
reads as fully dark/hidden - there's no sense that light is straining a
little way into the rock. This spec extends that same borrow rule so a
sufficiently bright, real light source (sky, Torch, or Lava alike - confirmed
with the project owner: all three channels get this, not just placed
light sources) can be glimpsed through up to 3 tiles of solid rock, dimmer
each tile in, revealing what's actually there (ore reads differently from
plain stone purely because `ChunkRenderer` already draws the real block
colour underneath - any nonzero brightness here reveals it, no separate
"ore bonus" needed, unlike the ambient-outline mechanic).

This is purely a rendering-time enhancement to `LightRenderer` - it does not
touch `Lighting`'s stored grid, its recompute triggers, or any of its
existing tested invariants (a solid tile still always reads 0 from
`skyLight`/`torchLight`/`lavaLight` directly; "blocked entirely by a solid
tile" still means light never *propagates* into or through solid ground for
gameplay/mechanical purposes - this only changes what the renderer chooses to
paint on top of a solid tile it's already drawing).

## Design

### Two rejected alternatives, for the record

- **Storing this in `Lighting`'s grid** (a real BFS pass in `recomputeAll`
  that lets sky/torch/lava leak a few steps into solid tiles) would be
  cheaper per frame, but breaks the tested invariant that a solid tile always
  reads 0 real light (`tests/test_lighting.cpp`'s occlusion tests), and
  conflates "genuinely open air" with "wall-penetration glow" inside the same
  channel semantics.
- **A brand-new stored channel** dedicated to this effect would keep that
  invariant clean, but doubles down on `Lighting`'s footprint and complexity
  for a purely cosmetic effect, right after that system just stabilized.

Both are rejected in favour of keeping this entirely inside `LightRenderer`,
which already does exactly this kind of per-frame, per-visible-tile
brightness computation for the existing 1-step borrow rule - this is a
generalization of that same mechanism, not a new one.

### The mechanism

Replace the current 4-immediate-neighbour lookup (`skyN`/`torchN`/`lavaN` in
`LightRenderer.cpp`, each currently `std::max` of 4 direct neighbours, then a
flat `-1`) with a small bounded search over every tile within Manhattan
distance 1-3 of the solid tile being rendered (a 24-cell diamond: 4 tiles at
distance 1, 8 at distance 2, 12 at distance 3 - the existing 4-neighbour case
is exactly this diamond's distance-1 ring). For each such offset, the
candidate brightness is `channelValueAtThatTile - distance`; the solid tile's
borrowed brightness is the max candidate across the whole diamond, clamped to
0. This is a direct generalization, not a new rule: at distance 1 it's
identical to today's behaviour, it simply no longer stops there.

No special-casing for "is that offset tile open or solid" is needed: a solid
offset tile already always reads 0 from `skyLight`/`torchLight`/`lavaLight`
(Lighting's BFS never assigns them a value), so it can never be the winning
candidate over an actually-lit open tile - the search can walk through solid
tiles freely (that's the point, we're tunnelling *through* rock to find a
light source) without extra bookkeeping to distinguish them.

Torch keeps its existing `torchAt` helper (folding the player's held-Torch
light, which lives outside the stored grid, into the lookup) - `torchAt` gets
called at each of the 24 offsets instead of just the 4 immediate ones, same
helper, just evaluated over the wider diamond.

Sky, Torch, and Lava all get this treatment identically - per the confirmed
answer, there's no channel-specific carve-out. A sunlit surface tile can
therefore glimmer faintly through up to 3 tiles of the rock directly beneath
it, exactly like a Torch or Lava pool would.

### Constants

- Penetration depth: fixed at **3 tiles**, matching the request. Not
  proportional to source brightness - a dim, nearly-decayed torch glow 3
  tiles from its source and a fresh level-9 sky tile 3 tiles down both use
  the same fixed distance-3 reach, just with correspondingly different
  resulting brightness after the `-distance` subtraction.
- Decay: linear, `-1` per tile of distance, matching the decay rate already
  used everywhere else in this system (`Lighting::floodFill`, the existing
  1-step borrow rule).

### Interaction with the rest of the rendering pipeline

Unchanged: the result of this computation replaces `skyRaw`/`torchRaw`/
`lavaRaw` for solid tiles exactly as today, feeding into the same
proportional tint-blending and ambient-outline-fallback logic already in
`LightRenderer::draw` (`LightRenderer.cpp:126-171`) without any change to
that logic - `total > 0.0f` still means "real light wins outright over the
ambient-outline floor," now just reachable from slightly further away.

### Performance

Bounded to a fixed 24-tile diamond search per solid tile, evaluated only for
tiles already inside the existing view-culled render loop (the same loop
that previously did a 4-tile lookup now does a 24-tile one) - same cost
class as `Lighting::ambientOutline`'s existing per-frame bounded BFS, and
nowhere near the expensive whole-world `recomputeAll` path (untouched by this
spec). No change to `recomputeAll`'s trigger frequency or cost.

## Testing

`LightRenderer` has no unit test today (SFML Graphics code, matches the
project's existing convention for `ChunkRenderer`/`MachineRenderer`) and this
spec doesn't change that - verification is a clean build of the `Litharia`
executable target, plus a manual/visual check (a solid wall 2-3 tiles from a
lit torch or sunlit shaft should read as a faint, dimming glow instead of
solid black; a wall 4+ tiles away should be unaffected, falling back to the
ambient-outline floor or black exactly as before this spec).

## Out of scope

Any change to `Lighting`'s stored grid, recompute triggers, or existing
channel semantics; any ore-specific brightness bonus (unnecessary - real
light already reveals the underlying block colour); extending the ambient
-outline mechanism itself (unaffected by this spec - it remains the
player-position-rooted, BFS-through-open-tiles-only fallback it already is).
