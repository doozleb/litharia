# Knockback, slower enemies, denser cave spawns

**Date:** 2026-07-27
**Status:** Approved design, ready for implementation planning

## Goal

Three tuning/feature changes to the existing enemy system
(`2026-07-23-enemies-and-melee-combat-design.md`):

1. The player and enemies knock each other back on hit, instead of just
   overlapping and sliding.
2. Both enemy types become significantly slower than the player's max run
   speed, so a player who chooses to run away from one always can.
3. Enemies spawn much more often while the player is underground, so caves
   feel more dangerous to explore than they currently do.

## Scope

In scope: a knockback impulse + brief control lockout on `Enemy` and
`Player`, wired to the existing melee-hit and contact-damage-tick events;
new `moveSpeed` values for both enemy types; a shorter spawn-attempt
interval while the player is underground; raising `MAX_ENEMIES` to match.

Out of scope: knockback for anything other than the player/enemy pair (no
enemy-vs-enemy knockback), vertical/"pop" knockback (horizontal only),
per-enemy-type knockback tuning (one shared magnitude per direction), any
change to `attemptSpawn`'s own eligibility/classification logic (only the
cadence it's called at changes), and any change to contact damage amounts,
sword damage, or despawn rules.

## Knockback

Both `Enemy` and `Player` already have their per-tick velocity fully
overwritten by their own control logic every update — `Enemy::update` sets
`speed.x` to the chase direction every tick, `Player::move` sets it from
steering input/friction every tick. A one-frame velocity impulse would be
invisible without briefly suspending that overwrite, so both gain the same
small piece of state:

```cpp
// Enemy.h (private) / Player.h (private)
float knockbackTimer = 0.0f;

// Enemy.h (public) / Player.h (public)
void applyKnockback(float vx);
```

`applyKnockback(vx)` sets `speed.x = vx` and `knockbackTimer =
KNOCKBACK_LOCK_SECONDS` (0.25s, a file-local constant in each of
`Enemy.cpp`/`Player.cpp` — this codebase already duplicates
GRAVITY/TERMINAL_VELOCITY the same way rather than sharing a physics
constants header).

- `Enemy::update`: while `knockbackTimer > 0`, skip the chase-direction and
  auto-jump block (the lines that currently set `speed.x`/trigger a jump
  every tick) so the impulse survives. Gravity and
  `physics::moveAndCollide` still run unconditionally every tick regardless
  of lock state. `knockbackTimer` counts down by `dt`, floored at 0.
- `Player::move`: while `knockbackTimer > 0`, skip the `steer`-driven
  acceleration/friction block the same way. Jump input, gravity, and
  collision are unaffected — a knocked-back player can still jump or fall
  normally, they just can't steer horizontally until the lock expires.

**Wiring, in `Game.cpp`:**

- `resolveMeleeHit`: for every enemy a sword hit actually damages, also call
  `enemy.applyKnockback(direction * ENEMY_KNOCKBACK_SPEED)`, where
  `direction` is `+1`/`-1` based on the sign of `enemy.center().x -
  player.center().x` (away from the player; falls back to the player's
  facing direction if the enemy is exactly centered, to avoid a zero
  impulse).
- `updateEnemies`: whenever `enemy.tickContactDamage(...)` returns `> 0`
  (a contact-damage tick just fired, i.e. the interval the enemy already
  hurts the player on), also call `player.applyKnockback(direction *
  PLAYER_KNOCKBACK_SPEED)`, direction computed the same way but away from
  the enemy.
- New constants in `Game.cpp`'s anonymous namespace: `ENEMY_KNOCKBACK_SPEED
  = 260.0f`, `PLAYER_KNOCKBACK_SPEED = 220.0f`.

Net effect: every sword hit shoves the enemy away from the player; every
contact-damage tick shoves the player away from the enemy. Both are brief
enough (~0.25s) that a stationary attacker doesn't just glide straight back
into range, but short enough that repeated hits still land normally on the
existing 0.6s contact interval / per-swing cadence.

## Enemy speed

`registry` in `Enemy.cpp` changes `moveSpeed` only — nothing else in
`EnemyInfo` changes:

| | now | new |
|---|---|---|
| Nightstalker `moveSpeed` | 190 px/s | 100 px/s |
| Sunroamer `moveSpeed` | 140 px/s | 75 px/s |

Player's `MAX_RUN_SPEED` (230 px/s, unchanged) stays well above both, so a
player who chooses to run is never caught by either enemy type on flat
ground.

## Cave spawn frequency

`Game::spawnEnemiesIfNeeded` currently advances a single timer
(`enemySpawnTimer`) against one constant, `SPAWN_ATTEMPT_INTERVAL` (3.0s).
This changes to pick between two intervals each tick, based on whether the
player is *currently* underground — the same check `despawnEnemies` already
uses (`playerTileY > generator.surfaceHeight(playerTileX)`):

```cpp
// EnemySpawner.h
inline constexpr float SPAWN_ATTEMPT_INTERVAL = 3.0f;      // unchanged, surface/day cadence
inline constexpr float CAVE_SPAWN_ATTEMPT_INTERVAL = 1.0f; // new, underground cadence
inline constexpr int MAX_ENEMIES = 16;                     // was 10
```

`spawnEnemiesIfNeeded` computes the player's current tile once, checks it
against `generator.surfaceHeight`, and compares `enemySpawnTimer` against
`CAVE_SPAWN_ATTEMPT_INTERVAL` when underground or `SPAWN_ATTEMPT_INTERVAL`
otherwise — everything else about the function (the salt counter,
`currentViewBounds()`, the call into `attemptSpawn`) is unchanged.
`attemptSpawn` itself is not touched: it stays a pure function of
`(world, generator, view, daylightFactor, currentEnemyCount, salt)`, and its
own foothold-search/classification logic (underground → Nightstalker
always; above ground → night/day rules) is exactly as documented in the
original design. Only how often `Game` calls it changes.

`MAX_ENEMIES` moves from 10 to 16 so the faster cave cadence can actually
produce a denser cave population rather than just refilling the same
population ceiling faster.

## Testing

**Enemies** (`test_enemies.cpp`):
- Update the existing `moveSpeed` assertions (190/140) to the new values
  (100/75).
- New: `applyKnockback` sets `velocity().x` to the given value; while the
  lock is active, further `update()` calls with a player straight ahead
  do *not* re-point velocity back toward the player (the existing "walks
  toward the player" test's assertion, inverted for the locked window);
  once `knockbackTimer` has elapsed (enough `update()` ticks), the chase
  behavior resumes and velocity points at the player again.

**Player** (`test_player.cpp`):
- New: `applyKnockback` sets `velocity().x`; while locked, holding
  left/right input does not move `velocity().x` back toward the input
  direction; once the lock elapses, steering resumes normally.

**Spawning** (`test_enemy_spawn.cpp`):
- `MAX_ENEMIES` assertions (already parameterized off the constant, not a
  literal 10) continue to pass unchanged at the new value of 16 — no test
  changes needed there.
- The interval-selection logic itself lives in `Game::spawnEnemiesIfNeeded`,
  which is Game-layer/integration code without a window-independent seam
  (same category as the rest of `Game`'s wiring) — verified by build +
  existing suite passing, not a new unit test. `CAVE_SPAWN_ATTEMPT_INTERVAL`
  and `MAX_ENEMIES`'s new values are plain constants, visible at a glance.

Game-level wiring (knockback firing from the real melee/contact events, the
cave-cadence branch actually running against the live player position) is
integration, verified by build + you driving the actual playtest — per your
standing preference, no automated live-game input verification.
