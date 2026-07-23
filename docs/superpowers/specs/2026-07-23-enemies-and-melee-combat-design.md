# Enemies and melee combat

**Date:** 2026-07-23
**Status:** Approved design, ready for implementation planning

## Goal

Add the game's first mobs — two enemy types that spawn just outside the
player's view, chase them down with simple physics-based AI, and hurt them on
contact — plus a five-tier sword line (Wood/Stone/Copper/Iron/Obsidian) as the
player's way to fight back. This is the first combat content in the game; the
health-and-damage design already named "mobs" as the next damage source and
left `Player::applyDamage` as the extension point for it.

## Scope

In scope: two enemy types, their spawn rules, chase-and-jump AI, contact
damage against the player, despawning, and five sword items with a swing
attack that damages enemies.

Out of scope: loot drops (enemies just vanish on death), enemies taking
environmental damage (lava, fluid buoyancy — they ignore fluids as if they
were open space), real pathfinding (enemies never route around an obstacle,
only jump over/through it — see AI section), multiplayer/multiple targets
(there is exactly one player to chase), sound/animation beyond the sword's
rotating-blade sweep, and any enemy attack beyond contact damage.

## Enemy data model

A small registry, the same shape as `Items.cpp`'s `ItemInfo` table. New
`EnemyType` enum (`Nightstalker`, `Sunroamer`) and an `EnemyInfo` struct
(name, size, color, max HP, move speed, jump speed, contact damage, contact
damage interval), looked up by `enemyInfo(EnemyType)`. Lives in a new
`src/Enemies/` folder alongside `Blocks/`, `Items/`, `Player/` etc.

| | **Nightstalker** (common) | **Sunroamer** (rare) |
|---|---|---|
| Spawns | Night above ground, or underground/caves any time | Daytime above ground, low roll chance |
| Max HP | 40 | 20 |
| Move speed | 190 px/s | 140 px/s |
| Jump speed | 470 px/s (matches the player's jump) | 420 px/s |
| Contact damage | 8 per 0.6s of continuous contact | 4 per 0.6s of continuous contact |
| Size | 28 x 42 px | 24 x 34 px |
| Color | dark purple-black | pale gold |

"Underground" for both spawn eligibility and despawn checks means the tile's
y is greater than `TerrainGenerator::surfaceHeight(x)` at that column — no
new cave-detection is needed, this reuses the existing height function.
"Night" means `DayNightClock::daylightFactor() < 0.5f`; "day" is the
complement.

## Spawning

New `EnemySpawner` module: a pure function of world state, deterministic
given a salt (same testable-without-a-window pattern as
`TerrainGenerator::scatterSharpRocks`/`scatterTrees`), so it can be unit
tested without SFML.

```cpp
struct ViewBounds { float left, top, right, bottom; };

struct EnemySpawn { EnemyType type; sf::Vector2f position; };

// Deterministic given `salt` (vary per call, e.g. an incrementing counter,
// same convention as TerrainGenerator::randomSurfaceSpot). Returns nullopt
// if no valid foothold was found or nothing rolled eligible this attempt.
std::optional<EnemySpawn> attemptSpawn(const World& world,
                                        const TerrainGenerator& generator,
                                        ViewBounds view,
                                        float daylightFactor,
                                        int currentEnemyCount,
                                        std::uint32_t salt);
```

`Game` calls this from `fixedUpdate` on a timer, not every tick.

Constants:
- `SPAWN_ATTEMPT_INTERVAL = 3.0f` seconds between attempts.
- `MAX_ENEMIES = 10` concurrent, both types combined — `attemptSpawn` returns
  `nullopt` immediately if `currentEnemyCount >= MAX_ENEMIES`.
- `SPAWN_MARGIN_TILES = 3` — how far past the camera's edge candidates are
  placed.
- `SPAWN_VERTICAL_SEARCH_TILES = 40` — how far the foothold search looks
  before giving up.
- `SUNROAMER_SPAWN_CHANCE = 0.08f` — per eligible daytime attempt.

Algorithm per attempt:
1. If at the enemy cap, stop.
2. Hash-pick the left or right edge of `view`, offset `SPAWN_MARGIN_TILES`
   tiles further out, giving a candidate x. (Left/right only — this is a
   side-scrolling world, and starting the vertical search from the view's
   own vertical center, next step, naturally covers the cave case: the
   camera follows the player, so if the player is deep underground, "just
   outside their view" at the view's own depth is still inside/near the
   cave system.)
3. Starting from the tile-y at the vertical center of `view`, search up to
   `SPAWN_VERTICAL_SEARCH_TILES` tiles (scanning downward, matching
   `scatterTrees`' search direction) for the first y where `(x, y)` is
   non-solid and `(x, y+1)` is solid — a foothold. Found nowhere → return
   `nullopt`.
4. Classify the foothold: `underground = y > generator.surfaceHeight(x)`.
5. Decide eligibility and roll:
   - `underground` → spawn a Nightstalker (always, subject to the cap check
     already done in step 1).
   - `!underground && daylightFactor < 0.5f` (night) → spawn a Nightstalker.
   - `!underground && daylightFactor >= 0.5f` (day) → hash-roll against
     `SUNROAMER_SPAWN_CHANCE`; spawn a Sunroamer only on success, otherwise
     `nullopt`.

## Despawning

Every tick, `Game` checks each live Nightstalker: if `daylightFactor >= 0.5f`
(day) **and** the enemy is above ground (its y is at or above
`surfaceHeight` for its column) **and** its box does not overlap the
camera's current view rect, it is removed immediately — no fade, consistent
with this game's plain, no-animation style. Cave Nightstalkers are
unaffected (never "above ground"), and it never disappears while the player
can actually see it. Sunroamers never despawn from time-of-day; the shared
`MAX_ENEMIES` cap is their only population control.

## Enemy AI

`Enemy::update(const World& world, sf::Vector2f playerCenter, float dt)`
drives movement through `physics::moveAndCollide` — the same function the
player and dropped items already use, so enemies get identical solid-tile
collision for free.

- Horizontal velocity is set toward the player's x every tick, full move
  speed, zero once roughly aligned. No pathfinding: this is the entire
  targeting logic, always on, always the single player.
- Standard gravity, same constant magnitude the player uses.
- **Auto-jump**: while grounded, jump (`speed.y = -jumpSpeed`, one-shot like
  the player's jump) if *either*:
  - the enemy was blocked horizontally by a solid tile this tick, or
  - the player's center is more than one tile above the enemy's.
- This is the entire "automatically jump if needed" behavior, and it's also
  what produces the requested ceiling behavior for free: if a solid ceiling
  sits between the enemy and a player above it, the second condition keeps
  firing every time the enemy lands from its last hop, so it just keeps
  jumping into the underside of that ceiling rather than ever routing
  around — it reads as "stuck on the ceiling," using nothing but the
  existing blocked-axis-zeroes-velocity collision behavior.
- Enemies ignore fluids entirely (no buoyancy, no lava damage) — explicitly
  out of scope, see Scope section.

## Contact damage

`Enemy` owns a `contactTimer` (same accumulate-and-drain shape as `Player`'s
existing lava timer): it increases by `dt` while the enemy's box overlaps
the player's box, and resets to 0 the instant it stops overlapping. Each
time it crosses the enemy type's contact interval (0.6s for both types),
`Game` calls a new public method:

```cpp
// Player.h
void takeDamage(int amount); // public - the mob damage extension point the
                              // health-and-damage design already flagged
```

which routes through the existing private `applyDamage`. `Game` also feeds
that amount into the existing floating damage-popup system, so a mob hit
looks identical to a fall/lava hit.

## Where it lives

New `src/Enemies/` folder:
- `Enemy.h/.cpp` — the entity type/registry, per-instance state (hp, box,
  velocity, grounded, contact timer), the chase/jump `update`, `applyDamage`,
  `isDead`.
- `EnemySpawner.h/.cpp` — the pure spawn-decision function above.

`Game` gains:
```cpp
std::vector<Enemy> enemies;
float enemySpawnTimer = 0.0f;
std::uint32_t enemySpawnCounter = 0; // salt, incremented per attempt
```
and, in `fixedUpdate`: advance `enemySpawnTimer`, call `attemptSpawn` when it
crosses `SPAWN_ATTEMPT_INTERVAL`, update every enemy, resolve contact damage,
run the despawn check, and (see below) resolve sword hits. Rendering draws
each enemy as a colored, outlined rectangle — the same primitive-shape style
already used for the player and item drops; no sprites exist yet anywhere in
this game.

## Swords

`ItemInfo` gains two trailing fields, both defaulted so every existing
registry row keeps compiling unchanged (the same trick `tier` used when it
was added):

```cpp
bool isSword = false;
int meleeDamage = 0; // meaningless unless isSword
```

Swords get `ToolType::None` — they don't mine anything — but reuse the
existing `ToolTier tier` field for their tier. This is a new, parallel item
category: equipping a sword doesn't touch the pickaxe/axe tool-tier system
at all, it just changes what the `mine` input does while a sword is the
selected hotbar item.

| Tier | Item | `meleeDamage` | Swing duration |
|---|---|---|---|
| Wood | WoodSword | 8 | 0.8s |
| Stone | StoneSword | 13 | 0.75s |
| Copper | CopperSword | 18 | 0.7s |
| Iron | IronSword | 23 | 0.65s |
| Obsidian | ObsidianSword | 28 | 0.6s |

A fixed 0.5s delay follows every swing regardless of tier
(`SWORD_SWING_DELAY = 0.5f`, alongside the per-tier duration table). Swing
durations live in a `SWORD_SWING_SECONDS` array indexed by `ToolTier`, the
same pattern as `TOOL_TIER_SPEED_MULTIPLIER`.

No starting sword — unlike Wood pickaxe/axe, WoodSword is crafted like every
other tier, at the Crafting Table:

| Output | Ingredients | Seconds |
|---|---|---|
| WoodSword | 2 Stick, 1 Oak Log | 1.5 |
| StoneSword | 2 Stick, 2 Sharp Rock | 2.0 |
| CopperSword | 2 Stick, 3 Copper Plate | 2.0 |
| IronSword | 2 Stick, 3 Iron Plate | 2.0 |
| ObsidianSword | 2 Stick, 2 Obsidian | 2.0 |

## Swing mechanic

`Player` gains a `Direction facing` (Left/Right only ever set), updated
whenever horizontal input is nonzero and held otherwise — needed for both
hit-direction and the swing's render angle.

A swing is a small state machine driven by the existing `mine` input bool
(the same "hold to act" input that already drives mining) whenever the
selected item `isSword`:

- **Idle** → while `input.mine` is held, start a swing: record the held
  sword's `meleeDamage` and look up its tier's swing duration.
- **Swinging** → progress runs 0→1 over the swing duration. The blade angle
  is lerped across the described arc: 10 degrees off vertical at the top to
  10 degrees off vertical at the bottom, sweeping through 90 degrees
  (horizontal, full extension, in the facing direction) at the midpoint.
  **Hit resolution fires exactly once per swing**, the tick progress crosses
  0.5: any enemy within a 2.5-tile radius of the player's center, on the
  side `facing` points toward, takes the recorded `meleeDamage`. One hit per
  swing, no re-triggering across multiple ticks.
- Swing finishes → **Delay** for `SWORD_SWING_DELAY` (0.5s), during which a
  new swing cannot start even if `mine` is held.
- Delay elapses → back to Idle; a new swing starts only if `mine` is still
  held at that point.
- Releasing `mine` mid-swing does not cancel the swing or its trailing
  delay — both always run to completion. It only stops a new swing from
  starting once Idle is reached again.

`ActionResult` gains:
```cpp
bool meleeHit = false;   // true only on the hit-resolution tick
int meleeDamage = 0;     // valid only when meleeHit is true
```
`Player` reports intent, exactly like it does for mining
(`BrokenTile`/`damageTaken`) — it doesn't know about `enemies` (`Game` owns
that vector), so `Game` reads `meleeHit`/`meleeDamage` off the
`ActionResult` each tick and, when true, applies damage to every enemy
within reach and facing-side of `player.center()`.

Rendering: a thin rectangle ("blade") pivoting around the player's center,
rotated to the swing's current angle (mirrored left/right to match
`facing`), drawn only while `Swinging`. Same plain-shapes style as
everything else.

## Testing

Core-testable (no window), following the existing per-family pattern:

**Enemies** (`test_enemies.cpp`, new):
- `EnemyInfo` reports the right stats per type.
- Chase AI: horizontal velocity points toward a player placed left/right;
  zero once aligned.
- Auto-jump: a grounded enemy blocked by a 1-tile-high wall jumps; a
  grounded enemy with the player positioned more than a tile above it jumps
  even with no horizontal obstruction; an airborne enemy does not
  double-jump.
- Ceiling-stuck case: player above a solid ceiling — enemy repeatedly jumps
  and lands in place, never crosses the ceiling, never routes sideways to
  find a gap that isn't there.
- Contact timer: overlapping the player accumulates damage exactly on the
  0.6s interval (mirrors the existing lava-timer tests); stepping apart
  resets it; `Player::takeDamage` reduces health and clamps at 0 the same
  way `applyDamage` already does.
- `isDead()` true at 0 hp; `applyDamage` clamps, doesn't go negative.

**Spawning** (`test_enemy_spawn.cpp`, new):
- Underground foothold → always Nightstalker, regardless of daylight value.
- Above-ground foothold at night → Nightstalker; by day → Sunroamer only
  some fraction of attempts (statistical check over many salts against the
  8% rate), never a Nightstalker by day.
- No foothold within the search range → `nullopt`.
- At `MAX_ENEMIES`, `attemptSpawn` always returns `nullopt` regardless of
  every other condition.
- Same `(world, view, daylightFactor, count, salt)` always yields the same
  result (determinism, matching the terrain generator's own tests).

**Despawn** (folded into `test_enemies.cpp` or `Game`-level if it needs
camera/view state): a Nightstalker above ground by day outside the view
rect is removed; the same enemy inside the view rect is not; one
underground is never removed regardless of time of day.

**Swords/swing** (`test_player.cpp`, extending the existing suite):
- Selecting a sword and holding `mine` starts a swing; `meleeHit` is false
  every tick except the one where progress crosses 0.5.
- `meleeDamage` on that tick matches the held sword's tier.
- Swing duration matches the tier's entry in `SWORD_SWING_SECONDS`; after it
  ends, a fixed 0.5s passes with no new swing regardless of held input.
- Releasing `mine` mid-swing does not shorten the swing or skip the delay.
- Items: each of the 5 sword `ItemType`s reports `isSword = true`, the
  right `meleeDamage`/tier; every existing non-sword item still reports
  `isSword = false` (regression check that the new defaulted fields didn't
  disturb the existing registry rows).
- Recipes: each of the 5 new recipes costs exactly its documented
  ingredients and requires the Crafting Table.

Game-level wiring (spawn timer firing, enemies rendering, sword hit
resolution reaching `enemies`, despawn using the live camera view) is
integration, verified by build + manual in-game check, like the rest of
this game's Game-layer code.
