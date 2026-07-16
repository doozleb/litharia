# Burner Generators Power Only Adjacent Machines

## Purpose

Today a Burner Generator powers every machine in its *network*: `updatePower()`
flood-fills all orthogonally touching machines into one group, sums supply
against demand across the whole group, and powers everything if supply wins.
Because belts and chutes are machines too, they conduct power — a burner can
feed a drill several tiles away through a belt chain.

This replaces network-scoped power with adjacency-scoped power: a machine runs
only if it physically touches a fuelled generator with capacity to spare.

## Consequence, stated up front

A Burner supplies 10; a Drill and a Smelter each draw 5. So **one burner
powers at most two machines**, and every pair of consumers needs its own
burner beside it. Belts no longer carry power. This substantially changes
factory layout — it is the intended effect, not a side effect.

## The network concept is removed

Deleted outright, since nothing else reads them:

- `Machine::network`
- `Machines::assignNetworks()`
- `Machines::networkSupply` / `Machines::networkDemand`

## Adjacency

"Adjacent" means **4-connected orthogonal** — up, down, left, right. No
diagonals. This matches what `assignNetworks()` and `insertOutput()` already
use, so the codebase keeps one definition of neighbour.

## Placement order

Power is claimed in the order machines were built: **first placed wins**.
Adding a machine never steals power from one already running — a new machine
takes the lowest priority and simply goes unpowered if nothing is left.

The `machines` vector cannot carry this ordering on its own: `remove()` uses
swap-and-pop, so deleting a machine drops the last-placed one into the freed
slot and jumps it up the queue. `Machine` therefore gains an explicit
placement sequence number, assigned from a monotonic counter in `Machines`
when the machine is placed, and the power solve visits consumers in ascending
sequence order. This keeps "first placed wins" true after deletions too.

## The power solve

`updatePower()` becomes a greedy allocation:

1. Give every generator a budget: its `powerRating` if `fuel > 0`, else 0.
2. Clear every machine's `powered` flag.
3. For each consumer, in ascending placement sequence:
   - Collect its 4-connected neighbours that are generators.
   - Sum their remaining budgets into `available`.
   - If `available >= demand`: mark the consumer powered, then deduct
     `demand` from those neighbours' budgets, drawing from each in turn until
     satisfied. Drawing across more than one neighbour is what lets adjacent
     burners' supplies combine.
   - If `available < demand`: the consumer stays unpowered and deducts
     **nothing**.

Step 3's all-or-nothing rule matters: a consumer that cannot be fully fed
must not reserve a partial draw, or it would strand power that a
later-sequenced consumer could have used in full.

## Fuel burn

`tickGenerators()` currently decides whether to burn coal with
`networkDemand[m.network] > 0.0f` — "is there any demand on my network?" That
is wrong today in a way this change fixes for free: when a network's demand
exceeds its supply, nothing runs, yet demand is still non-zero, so the burner
consumes coal while powering nothing.

The new rule reads straight off the allocation: a generator burns fuel only
if it actually supplied power this tick, i.e. its spent budget
(`powerRating - remaining`) is greater than zero. A burner with nothing to
feed — or whose only neighbours were skipped as unaffordable — stops burning.

## Idle reasons

`idleReason()`'s two current strings both say "network" and stop being true.
An unpowered consumer now gets one of three, distinguishing the cases a
player can act on:

| Situation | Message |
|---|---|
| No adjacent generator at all | `No power: not next to a burner generator.` |
| Adjacent generator(s), none fuelled | `No power: the adjacent burner has no fuel.` |
| Adjacent generator(s) fuelled, but budget already claimed | `No power: the adjacent burner is already at capacity.` |

## Testing

`tests/test_power.cpp` needs real rework — several existing cases encode
network semantics that no longer exist:

- `"a fuelled generator powers an adjacent consumer"` and `"two separated
  networks do not share power"` assert on `network` ids, which are gone. The
  underlying behaviour still holds; the `network` assertions drop out.
- `"demand beyond supply browns out the whole network"` is now wrong in
  premise: with a burner at (0,0) and drills at (1,0), (2,0), (3,0), only the
  drill at (1,0) touches the burner. It should be powered (5 of 10 used);
  the other two are unpowered for not touching a generator at all, not
  because of a brownout. Replace it with a case that tests the real capacity
  limit: three drills all adjacent to one burner, of which exactly two run.

New coverage to add:

- A consumer two tiles from a burner, connected via a belt, is unpowered
  (belts no longer conduct).
- Four drills around one burner: exactly the first two placed run.
- A consumer adjacent to two burners draws from both (supplies combine).
- An unaffordable consumer reserves nothing: a later-placed consumer that
  *can* afford its draw still gets powered.
- Placement order survives a deletion (the counter, not vector order,
  decides).
- A burner with no powered neighbours does not burn fuel.
- Each of the three idle-reason strings.
