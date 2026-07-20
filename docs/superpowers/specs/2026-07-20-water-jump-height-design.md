# Water jump height cap

**Date:** 2026-07-20
**Status:** Approved design, ready for implementation planning

## Goal

Water already applies 0.3x gravity to the player (`Player.cpp`, from the
2026-07-19 fluids tweak), but jump speed itself is unchanged in water, so a
submerged jump currently reaches roughly 12.8 tiles — far higher than
intended. Cap it so a water jump feels floaty (slow rise, same 0.3x gravity)
but only reaches about 2 tiles higher than a normal land jump.

## Decisions (from brainstorming)

- Jump stays gated on `grounded` only — no free-float "swim jump" while not
  touching solid ground. Smallest change; keeps the existing anti-spam rule.
- Land jump apex is `JUMP_SPEED^2 / (2 * GRAVITY) / TILE_SIZE` ≈ 3.84 tiles
  (`JUMP_SPEED = 470`, `GRAVITY = 1800`, `TILE_SIZE = 16`).
- Target water jump apex: land apex + 2 ≈ 5.84 tiles, achieved under the
  existing water gravity of `GRAVITY * 0.3` = 540 px/s².
- Required initial speed: `sqrt(2 * 540 * 5.84 * 16)` ≈ 317.5 px/s.
- Time-to-apex at that speed is ~0.59s vs land's ~0.26s — still reads as
  slow/floaty even though it caps lower than the current behavior.

## Implementation

In `Player.cpp`'s anonymous namespace, alongside `JUMP_SPEED`:

```cpp
constexpr float WATER_JUMP_SPEED = 317.5f; // px/s, ~2 tiles above land's ~3.84-tile apex, under 0.3x water gravity
```

In `Player::move`, where jump is applied:

```cpp
if (input.jump && grounded)
    speed.y = physics::overlapsFluid(body, world) ? -WATER_JUMP_SPEED : -JUMP_SPEED;
```

Reuses the same `physics::overlapsFluid` check already driving the gravity
toggle two lines below, so "is this jump a water jump" and "is gravity
weakened" stay consistent with each other automatically.

If the player leaves the water partway through the ascent, gravity reverts to
the full 1800 for the rest of the arc, which only decelerates the remaining
rise faster — so 5.84 tiles is an upper bound, not a guarantee, for shallow
pools. This matches the "cap", not "always reach", framing of the ask.

## Testing

Core-testable on `Player` and `Physics` (no window), in `tests/test_player.cpp`,
matching the existing jump/gravity test style:

- Jumping while grounded and overlapping fluid sets `speed.y` to
  `-WATER_JUMP_SPEED`, not `-JUMP_SPEED`.
- Jumping while grounded and not overlapping fluid is unchanged
  (`-JUMP_SPEED`).

## Out of scope (for now)

Free-floating "swim jump" while not grounded, horizontal water speed changes,
and any change to the existing 0.3x water gravity value.
