# Health and damage system

**Date:** 2026-07-19
**Status:** Approved design, ready for implementation planning

## Goal

Give the player health with two damage sources — fall damage and lava — plus
death and respawn. These are the only danger sources for now. Also retune the
underground fluid pool counts.

## Decisions (from brainstorming)

- **Max health:** 50 HP (integer), starts full.
- **Lava:** 20 damage per 0.5 s of contact. Three hits (at 0.5 / 1.0 / 1.5 s)
  deal 20 / 40 / 60 ≥ 50, so ~1.5 s in lava kills — this is "kills after 3
  shots."
- **Fall:** no damage for falls up to 7 tiles; above that, damage scales with
  impact speed, and a terminal-velocity fall (~21+ tiles) can kill from full.
- **Death:** respawn at the world spawn point with full health; inventory and
  world untouched.
- **Pools:** underground water pools 10 → 25, lava pools 15 → 40.

## Architecture

Health and damage live on `Player` (in the core library `Litharia_core`, so
fully unit-testable, and it already owns the velocity / grounded / position
state that fall and lava detection need). `Game` handles only respawn-on-death
and the HUD bar — the existing split where `Player` mutates state and `Game`
owns the world, spawn point, and rendering. No separate health subsystem: one
stat with two sources does not warrant it.

New `Player` state and API (member and accessor must have distinct names —
matches the codebase's `bag`/`inventory()`, `speed`/`velocity()` convention):
- `static constexpr int MAX_HEALTH = 50;` (public, so HUD and tests reference it)
- private `int hp` member, starts at `MAX_HEALTH`
- `int health() const { return hp; }`
- `bool isDead() const { return hp <= 0; }`
- `void respawn(sf::Vector2f topLeft);` — restores full health (`hp = MAX_HEALTH`),
  sets position, zeroes velocity, clears the lava timer and grounded flag
- private `void applyDamage(int amount);` — `hp = std::max(0, hp - amount)`
- private `float lavaTimer` member, starts at 0

New physics helper:
- `bool physics::overlapsLava(const AABB& box, const World& world);` — mirrors
  the existing `overlapsFluid`, testing `isLava` instead of `isFluid`.

## Damage rules

All damage is applied inside `Player::update(input, world, dt, machines)`, at the
fixed 60 Hz step, so it is deterministic.

### Fall damage

In `Player::move`, capture the downward `speed.y` *before* `moveAndCollide`
(which zeroes it on impact). On the airborne→grounded transition
(`!wasGrounded && grounded`), if that impact speed exceeds the 7-tile threshold,
apply scaled damage:

- `FALL_SAFE_TILES = 7`
- `FALL_SAFE_SPEED = sqrt(2 * GRAVITY * FALL_SAFE_TILES * TILE_SIZE)` ≈ 635 px/s
  (the speed a free fall of 7 tiles reaches; `GRAVITY = 1800`, `TILE_SIZE = 16`)
- `FALL_DAMAGE_SCALE = MAX_HEALTH / (TERMINAL_VELOCITY - FALL_SAFE_SPEED)`
  ≈ 50 / (1100 − 635) ≈ 0.1075 HP per px/s over threshold
- `damage = round((impact − FALL_SAFE_SPEED) * FALL_DAMAGE_SCALE)` when
  `impact > FALL_SAFE_SPEED`, else 0

Because the game caps falling at `TERMINAL_VELOCITY = 1100`, the maximum fall
damage is `round((1100 − 635) * 0.1075) = 50` — lethal from full. Landing in
deep water needs no special case: fluid already applies 0.3× gravity, so the
player decelerates before hitting the floor and the impact speed (hence damage)
is naturally low. `GRAVITY` and `TERMINAL_VELOCITY` are the constants already
defined in `Player.cpp`'s anonymous namespace; the fall constants join them
there.

### Lava damage

After `move`, in `update`:

```
if (physics::overlapsLava(body, world))
{
    lavaTimer += dt;
    while (lavaTimer >= LAVA_DAMAGE_INTERVAL)   // 0.5 s
    {
        applyDamage(LAVA_DAMAGE);               // 20
        lavaTimer -= LAVA_DAMAGE_INTERVAL;
    }
}
else
{
    lavaTimer = 0.0f;
}
```

First hit lands after 0.5 s of continuous contact; stepping out of lava resets
the timer so each contact starts fresh. `LAVA_DAMAGE = 20`,
`LAVA_DAMAGE_INTERVAL = 0.5f` (file-local constants in `Player.cpp`).

### Death

`applyDamage` clamps health at 0, so further same-tick damage is a no-op and
`isDead()` stays true. `Game::fixedUpdate`, right after `player.update(...)`,
checks `player.isDead()` and if so calls `player.respawn(findSpawn())` followed
by `camera.snapTo(player.center())` (the view jumps to spawn rather than lerping
across the map).

## HUD

Add `Hud::drawHealth(window, health, maxHealth)` — a small fixed-position bar in
a screen corner (red fill proportional to `health / maxHealth` over a dark
back). `Game::render` calls it with `player.health()` and `Player::MAX_HEALTH`.
Rendering lives in the game target and is not unit-tested; it is verified by
build and a manual visual check, like the rest of the HUD.

## Pool counts

In `src/World/TerrainGenerator.h`:
- `WATER_POOL_COUNT`: 10 → 25
- `LAVA_POOL_COUNT`: 15 → 40

The terrain tests that assert exact pool counts (`tests/test_terrain.cpp`) are
updated to the new numbers.

## Testing

Core-testable on `Player` and `Physics` (no window):

- `overlapsLava` is true only over lava tiles, false for water / air / solid.
- A fall under 7 tiles deals no damage; a fall from a large height deals scaled
  damage; a fall that reaches terminal velocity is lethal (health → 0).
- Lava contact deals 20 per 0.5 s; exactly three intervals brings 50 → dead;
  stepping out of lava resets the timer (no carryover).
- `isDead()` is true at 0 health; `respawn(p)` restores full health, moves the
  player to `p`, and zeroes velocity.
- Normal grounded movement (no lava, small steps) deals no damage.

Game respawn wiring and the HUD bar are integration, verified by build + manual
visual check.

## Out of scope (for now)

Any other damage source (drowning, mobs, starvation), healing/regeneration,
knockback, damage animation or sound, and a death screen. The design keeps
`applyDamage` and the health field as the single extension point for later
sources.
