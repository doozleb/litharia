# Brighter, Whiter Torch

## Purpose

The Torch currently seeds its light channel at the same level as sky and
Lava (`Lighting::MAX_LIGHT_LEVEL`, 9), and renders with a fairly saturated
orange-yellow tint (`TORCH_TINT = (255, 200, 110)`). Requested: make the
Torch specifically brighter and farther-reaching than that shared baseline,
and shift its tint partway toward white. Sky and Lava are unaffected by
either change.

## Design

### Brighter, farther-reaching: a per-source seed level, not a new mechanic

Each of `LightLevel`'s three channels (`sky`, `torch`, `lava`, in
`src/World/Lighting.h`) is a 4-bit field - 0-15 representable, of which only
0-`MAX_LIGHT_LEVEL` (9) is currently ever used. That spare headroom is
exactly what "brighter and farther" needs, with zero data-model change: a
new constant, `Lighting::TORCH_LIGHT_LEVEL = 15`, replaces `MAX_LIGHT_LEVEL`
at the two places a Torch's own light gets seeded -

- `recomputeAll`'s `torchSeeds` loop (a placed Torch machine), and
- `heldTorchLight`'s single seed (the player's carried Torch) -

while sky's and lava's seeding, and every other use of `MAX_LIGHT_LEVEL`
(the render-time brightness divisor, the ambient-outline levels, the
wall-penetration search depth/penalties), are completely untouched. Decay
stays exactly 1 per orthogonal step, same as every other source in this
system - only the starting level differs.

Because `LightRenderer` still normalizes brightness as
`effectiveValue / Lighting::MAX_LIGHT_LEVEL` (9) - unchanged by this spec -
a Torch seeded at 15 stays clipped to full, un-dimmed brightness for about
6 tiles (15 - 9) before its *effective* brightness even starts dropping
below 1.0, and only reaches 0 at 15 tiles out instead of 9. That's the
entire "brighter and shows more area" request, delivered by one constant,
with no change to the brightness formula, the BFS, or the rendering
pipeline. It also naturally makes torch-lit walls glow brighter and farther
through the existing wall-penetration search (`LightRenderer.cpp`'s
`torchAt`-fed diamond scan) - a side effect of reusing the same formula,
not a separate change.

Held and placed Torches get the same treatment deliberately: `Lighting.h`'s
own existing doc comment on `heldTorchLight` already promises it behaves
identically to a placed Torch's `torchLight` ("the same brightness and
decay/occlusion rule") - leaving one at level 9 while the other jumps to 15
would break that promise and read as an inconsistent bug (a carried torch
dimmer than one you just set down).

### Whiter tint

`TORCH_TINT` in `src/World/LightRenderer.cpp` moves from `(255, 200, 110)`
to `(255, 228, 183)` - a literal 50/50 blend with white
(`(255,255,255)`), per the confirmed answer. `LAVA_TINT`, the sky
gradient, and the ambient-outline tint are all untouched - only the Torch's
own color changes.

## Testing

`Lighting`'s existing Torch-related tests currently assert against
`Lighting::MAX_LIGHT_LEVEL` (e.g. "a placed Torch is a max-level block light
source," the held-Torch decay tests) - these get updated to assert against
the new `Lighting::TORCH_LIGHT_LEVEL` instead, since that's now the actual
seeded value for a Torch specifically. Sky- and Lava-only tests are
untouched (they still seed at, and assert against, `MAX_LIGHT_LEVEL`).
`LightRenderer`'s tint constant has no unit test today (SFML Graphics code,
matches the project's existing convention) - verified by a clean build plus
a manual/visual check.

## Out of scope

Any change to sky or Lava's own light level; any change to the
wall-penetration penalty table or search depth; any change to the
ambient-outline mechanic; any change to `LAVA_TINT` or the sky gradient;
raising `MAX_LIGHT_LEVEL` itself (it stays 9, still governing sky/lava and
the shared brightness-normalization divisor - only Torch gets its own,
higher seed constant).
