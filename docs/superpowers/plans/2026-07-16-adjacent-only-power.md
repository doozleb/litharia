# Adjacent-Only Burner Power Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A machine runs only when it physically touches a fuelled generator that still has power to give, replacing today's network-wide power sharing.

**Architecture:** The network concept (flood-filled groups, per-network supply/demand vectors) is deleted outright. `updatePower()` becomes a greedy allocation: each fuelled generator gets a budget equal to its rating, then consumers claim power in placement order from their 4-connected generator neighbours. A new placement counter on `Machine` makes "first placed wins" survive `remove()`, which swap-and-pops and therefore scrambles vector order. `idleReason()` and the generator's fuel-burn rule both read off the new solve instead of network state.

**Tech Stack:** C++20, SFML 3, doctest, CMake + Visual Studio generator (existing project stack — no new dependencies).

## Global Constraints

- Build: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`. `cmake` is **not** on PATH; the full path is `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` — add it to PATH for the session or invoke it directly.
- Test binary: `C:\Litharia\build\Debug\Litharia_tests.exe`. Baseline before this work: **180 test cases, 180 passed, 0 failed**.
- "Adjacent" means **4-connected orthogonal** (up/down/left/right). No diagonals. This matches the `nx[4]`/`ny[4]` idiom already used by `assignNetworks()` and `insertOutput()`.
- Power ratings live in `src/Machines/MachineRegistry.cpp` and are **not** changed by this work: Burner Generator supplies `10.0f`; Drill and Smelter each demand `5.0f`. One burner therefore runs at most two machines.
- Spec: `docs/superpowers/specs/2026-07-16-adjacent-only-power-design.md`.

## Note on one spec-listed test that is deliberately not written

The spec asks for a test that "an unaffordable consumer reserves nothing: a
later-placed consumer that *can* afford its draw still gets powered." With the
current ratings this behaviour is **unobservable**, so no such test appears
below. Every generator budget starts at 10 and every draw is exactly 5, so a
budget is only ever 10, 5, or 0, and a consumer's available total is always a
multiple of 5. Since demand is exactly 5, `available < demand` can only be true
when `available == 0` — where there is nothing to reserve and nothing for a
later consumer to inherit. The all-or-nothing structure is still implemented
(it falls out of checking affordability before deducting, costing no extra
code) and would matter the moment a machine with a different rating is added,
but a test written today would assert nothing real. Writing a test that cannot
fail is worse than not writing it.

---

## Task 1: Placement sequence counter

Gives every machine a monotonic stamp at placement time. Task 2's power solve
orders consumers by it. It lands first and separately because it compiles and
tests green on its own, with no behaviour change.

**Files:**
- Modify: `src/Machines/Machine.h`
- Modify: `src/Machines/Machines.h`
- Modify: `src/Machines/Machines.cpp` (`place()`, ~line 55-77)
- Test: `tests/test_machines.cpp`

**Interfaces:**
- Produces: `Machine::placedSeq` (`std::uint32_t`, default `0`), assigned by `Machines::place()` from the private `Machines::nextSeq` counter. Task 2 sorts consumers by ascending `placedSeq`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("machines are stamped with an increasing placement sequence")
{
    Machines m;

    // Read the stamp straight off each returned pointer: place() may reallocate
    // the machine vector, so a pointer must not be held across the next place().
    const std::uint32_t first = m.place(MachineType::Drill, 0, 0, Direction::Right)->placedSeq;
    const std::uint32_t second = m.place(MachineType::Drill, 1, 0, Direction::Right)->placedSeq;

    CHECK(second > first);
}

TEST_CASE("a machine placed after a removal still sorts last")
{
    Machines m;
    m.place(MachineType::Drill, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Right);

    const std::uint32_t survivor = m.at(1, 0)->placedSeq;

    REQUIRE(m.remove(0, 0));

    // remove() swap-and-pops, so this machine lands in the freed vector slot -
    // but the counter never rewinds, so it cannot jump the queue.
    const std::uint32_t fresh = m.place(MachineType::Drill, 2, 0, Direction::Right)->placedSeq;

    CHECK(fresh > survivor);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: **compile error** — `'placedSeq': is not a member of 'Machine'`.

- [ ] **Step 3: Add the field to `Machine`**

In `src/Machines/Machine.h`, add `#include <cstdint>` to the top of the include block (above `#include "../Core/Direction.h"`), then add this field directly below the `Direction facing` / `outputCursor` block and above the `// Processing machines` comment:

```cpp
    // Ascending order of placement, stamped by Machines::place(). The power
    // solve visits consumers in this order, so a machine already running never
    // loses power to one built later. It cannot be read off the machines
    // vector: remove() swap-and-pops, which scrambles that order.
    std::uint32_t placedSeq = 0;
```

- [ ] **Step 4: Add the counter to `Machines` and stamp on place**

In `src/Machines/Machines.h`, add to the private section, directly above `std::vector<Machine> machines;`:

```cpp
    std::uint32_t nextSeq = 0; // stamps Machine::placedSeq; never rewinds
```

In `src/Machines/Machines.cpp`, in `place()`, add the stamp immediately after the `m.outputCursor = rotateCW(facing);` line and its comment:

```cpp
    m.placedSeq = nextSeq++;
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: builds clean; `test cases: 182 | 182 passed | 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Machine.h src/Machines/Machines.h src/Machines/Machines.cpp tests/test_machines.cpp
git commit -m "feat: stamp machines with a placement sequence number"
```

---

## Task 2: Replace network power with adjacency power

The core swap. It is one task because it cannot be split and still compile:
deleting `networkSupply` breaks `idleReason()`, and deleting `Machine::network`
breaks `tickGenerators()`. A reviewer could not sensibly approve half of it.

**Files:**
- Modify: `src/Machines/Machine.h` (replace the `network` field)
- Modify: `src/Machines/Machines.h` (drop `assignNetworks()` + the two network vectors, add `adjacentGenerators()`)
- Modify: `src/Machines/Machines.cpp` (delete `assignNetworks()`, rewrite `updatePower()` and `idleReason()`, fix `tickGenerators()`, drop `#include <queue>`)
- Modify: `src/Machines/MachineType.h` (two stale comments, ~line 43-44)
- Test: `tests/test_power.cpp` (rework)
- Test: `tests/test_machine_inspect.cpp` (two tests rewritten, one added)
- Test: `tests/test_processing.cpp` (one test added)

**Interfaces:**
- Consumes: `Machine::placedSeq` (Task 1).
- Produces: `Machine::supplying` (`bool`) — set by `updatePower()`, true when a generator handed out any power this tick; read by `tickGenerators()`. `Machines::adjacentGenerators(int x, int y, std::array<int, 4>& out) const` returning `int` (0-4), the count of generator indices written to `out`.

- [ ] **Step 1: Rework `tests/test_power.cpp`**

Replace the **entire file** with:

```cpp
#include "doctest.h"

#include "Machines/Machines.h"

namespace
{

// A generator is only a source once it has fuel; the power solve reads .fuel.
void fuel(Machines& m, int x, int y, float seconds)
{
    Machine* g = m.at(x, y);
    REQUIRE(g != nullptr);
    g->fuel = seconds;
}

} // namespace

TEST_CASE("a fuelled generator powers an adjacent consumer")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down);
    fuel(m, 0, 0, 10.0f);

    m.updatePower();

    CHECK(m.at(1, 0)->powered);
}

TEST_CASE("a consumer with no generator is unpowered")
{
    Machines m;
    m.place(MachineType::Drill, 4, 4, Direction::Down);

    m.updatePower();

    CHECK_FALSE(m.at(4, 4)->powered);
}

TEST_CASE("an unfuelled generator supplies nothing")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Down);
    // No fuel set.

    m.updatePower();

    CHECK_FALSE(m.at(1, 0)->powered);
}

TEST_CASE("power does not conduct through a belt")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Belt, 1, 0, Direction::Right);
    m.place(MachineType::Drill, 2, 0, Direction::Down); // two tiles from the generator
    fuel(m, 0, 0, 10.0f);

    m.updatePower();

    // Touching a belt that touches a generator is not touching a generator.
    CHECK_FALSE(m.at(2, 0)->powered);
}

TEST_CASE("a generator's supply caps how many neighbours it can run")
{
    Machines m;
    // Supply is 10 and each drill demands 5, so only the first two placed run,
    // even though all three are touching the generator.
    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);
    m.place(MachineType::Drill, 6, 5, Direction::Down);
    m.place(MachineType::Drill, 4, 5, Direction::Down);
    m.place(MachineType::Drill, 5, 6, Direction::Down);
    fuel(m, 5, 5, 10.0f);

    m.updatePower();

    CHECK(m.at(6, 5)->powered);
    CHECK(m.at(4, 5)->powered);
    CHECK_FALSE(m.at(5, 6)->powered);
}

TEST_CASE("a consumer falls back to a second adjacent generator when the first is spent")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);
    m.place(MachineType::BurnerGenerator, 7, 5, Direction::Right);

    // These two exhaust the first generator's whole 10.
    m.place(MachineType::Drill, 5, 4, Direction::Down);
    m.place(MachineType::Drill, 5, 6, Direction::Down);

    // This one touches both generators. The first has nothing left, so it runs
    // on the second.
    m.place(MachineType::Drill, 6, 5, Direction::Down);

    fuel(m, 5, 5, 10.0f);
    fuel(m, 7, 5, 10.0f);

    m.updatePower();

    CHECK(m.at(5, 4)->powered);
    CHECK(m.at(5, 6)->powered);
    CHECK(m.at(6, 5)->powered);
}

TEST_CASE("power is claimed in placement order, even after a removal")
{
    Machines m;
    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);

    // Four drills around one generator; the first two placed win its 10.
    m.place(MachineType::Drill, 5, 4, Direction::Down); // A
    m.place(MachineType::Drill, 6, 5, Direction::Down); // B
    m.place(MachineType::Drill, 4, 5, Direction::Down); // C
    m.place(MachineType::Drill, 5, 6, Direction::Down); // D
    fuel(m, 5, 5, 10.0f);

    m.updatePower();

    REQUIRE(m.at(5, 4)->powered);
    REQUIRE(m.at(6, 5)->powered);
    REQUIRE_FALSE(m.at(4, 5)->powered);
    REQUIRE_FALSE(m.at(5, 6)->powered);

    // Removing A swap-and-pops D into A's vector slot. Ordering by placement
    // sequence rather than vector position is what keeps B and C the winners:
    // read off the raw vector, D would wrongly jump ahead of both.
    REQUIRE(m.remove(5, 4));

    m.updatePower();

    CHECK(m.at(6, 5)->powered);
    CHECK(m.at(4, 5)->powered);
    CHECK_FALSE(m.at(5, 6)->powered);
}
```

Note what was dropped and why: the old `"demand beyond supply browns out the whole network"` is replaced by `"a generator's supply caps how many neighbours it can run"` — its old layout (drills in a row at (1,0), (2,0), (3,0)) no longer tests a brownout at all, since only the drill at (1,0) touches the generator. The old `"two separated networks do not share power"` is deleted outright: its point was the `network` id assertion, and its behavioural half (a far-away drill is unpowered) is already covered by `"a consumer with no generator is unpowered"`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: builds clean (nothing referenced by the new tests is missing yet), but **4 failures** — `"power does not conduct through a belt"`, `"a generator's supply caps how many neighbours it can run"`, `"a consumer falls back to a second adjacent generator when the first is spent"`, and `"power is claimed in placement order, even after a removal"`. The current network solve powers whole connected groups, so belts conduct and all four drills around a generator brown out together.

- [ ] **Step 3: Swap the `network` field for `supplying` in `Machine`**

In `src/Machines/Machine.h`, replace:

```cpp
    // Power, set every tick by Machines::updatePower().
    int network = -1;
    bool powered = false;
```

with:

```cpp
    // Power, set every tick by Machines::updatePower().
    bool powered = false;   // consumer: is my demand met by an adjacent generator?
    bool supplying = false; // generator: did I hand any power out this tick?
```

- [ ] **Step 4: Update the `Machines` header**

In `src/Machines/Machines.h`, add `#include <array>` to the top of the include block (above `#include <cstdint>`).

Replace the `updatePower()` declaration and its comment:

```cpp
    // Rebuilds power networks and sets each machine's powered flag. Task 7.
    void updatePower();
```

with:

```cpp
    // Sets every machine's powered/supplying flags. A consumer runs only if the
    // generators orthogonally touching it have enough unclaimed supply between
    // them; consumers claim in placement order, so building a new machine never
    // unpowers one that is already running.
    void updatePower();
```

Delete the `assignNetworks()` declaration and its `// Task 7 helpers.` comment:

```cpp
    // Task 7 helpers.
    void assignNetworks();
```

In its place, declare the neighbour scan:

```cpp
    // Fills `out` with the indices of the generators orthogonally touching
    // (x, y) and returns how many there are, 0 to 4. The power solve and the
    // idle reason both need the same scan.
    int adjacentGenerators(int x, int y, std::array<int, 4>& out) const;
```

Delete both network vectors:

```cpp
    std::vector<float> networkDemand; // demand per network id, filled by updatePower
    std::vector<float> networkSupply; // per-network supply, alongside networkDemand
```

- [ ] **Step 5: Delete `assignNetworks()` and rewrite `updatePower()`**

In `src/Machines/Machines.cpp`, remove `#include <queue>` from the include block — `assignNetworks()` was its only user.

Delete the whole `void Machines::assignNetworks() { ... }` function body (it starts at the `for (Machine& m : machines) m.network = -1;` loop and ends after the flood-fill's closing brace).

Replace the entire `void Machines::updatePower() { ... }` function with:

```cpp
int Machines::adjacentGenerators(int x, int y, std::array<int, 4>& out) const
{
    const int nx[4] = {x - 1, x + 1, x, x};
    const int ny[4] = {y, y, y - 1, y + 1};

    int count = 0;

    for (int i = 0; i < 4; ++i)
    {
        const int index = indexAt(nx[i], ny[i]);

        if (index >= 0 && machineInfo(machines[index].type).generator)
            out[count++] = index;
    }

    return count;
}

void Machines::updatePower()
{
    // Each generator starts the tick with its whole rating to give away; an
    // unfuelled one has nothing.
    std::vector<float> budget(machines.size(), 0.0f);

    for (std::size_t i = 0; i < machines.size(); ++i)
    {
        Machine& m = machines[i];
        const MachineInfo& info = machineInfo(m.type);

        if (info.generator && m.fuel > 0.0f)
            budget[i] = info.powerRating;

        m.powered = false;
        m.supplying = false;
    }

    // Consumers claim in the order they were built, not the order they happen to
    // sit in the vector - remove() swap-and-pops, so vector position is not
    // build order.
    std::vector<int> order(machines.size());
    for (std::size_t i = 0; i < machines.size(); ++i)
        order[i] = static_cast<int>(i);

    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return machines[a].placedSeq < machines[b].placedSeq;
    });

    for (const int index : order)
    {
        Machine& m = machines[index];
        const MachineInfo& info = machineInfo(m.type);

        if (!info.consumer)
            continue;

        std::array<int, 4> generators{};
        const int count = adjacentGenerators(m.x, m.y, generators);

        float available = 0.0f;
        for (int i = 0; i < count; ++i)
            available += budget[generators[i]];

        // All or nothing: a machine that cannot be fully fed draws nothing at
        // all, rather than stranding a part-share no one else can finish.
        if (available < info.powerRating)
            continue;

        m.powered = true;

        float need = info.powerRating;

        for (int i = 0; i < count && need > 0.0f; ++i)
        {
            const int g = generators[i];
            const float drawn = std::min(need, budget[g]);

            if (drawn <= 0.0f)
                continue;

            budget[g] -= drawn;
            need -= drawn;

            machines[g].supplying = true;
        }
    }
}
```

- [ ] **Step 6: Fix the generator's fuel-burn rule**

In `src/Machines/Machines.cpp`, in `tickGenerators()`, replace:

```cpp
        // Burn only under load, so an idle base does not drain its fuel.
        const bool hasLoad = m.network >= 0
            && m.network < static_cast<int>(networkDemand.size())
            && networkDemand[m.network] > 0.0f;

        if (m.fuel > 0.0f && hasLoad)
            m.fuel = std::max(0.0f, m.fuel - dt);
```

with:

```cpp
        // Burn only while actually powering something. Asking whether anything
        // nearby *wants* power is not enough: a generator whose neighbours all
        // went unpowered would burn coal for nothing.
        if (m.fuel > 0.0f && m.supplying)
            m.fuel = std::max(0.0f, m.fuel - dt);
```

- [ ] **Step 7: Rewrite `idleReason()`'s power branch**

In `src/Machines/Machines.cpp`, in `idleReason()`, replace:

```cpp
    if (info.consumer && !m.powered)
    {
        const bool hasSupply = m.network >= 0
            && m.network < static_cast<int>(networkSupply.size())
            && networkSupply[m.network] > 0.0f;

        return hasSupply ? "No power: network demand exceeds supply."
                          : "No power: no fuel in this network.";
    }
```

with:

```cpp
    if (info.consumer && !m.powered)
    {
        std::array<int, 4> generators{};
        const int count = adjacentGenerators(m.x, m.y, generators);

        if (count == 0)
            return "No power: not next to a burner generator.";

        // Touching a generator with fuel but still unpowered means its supply
        // went to machines built earlier.
        for (int i = 0; i < count; ++i)
            if (machines[generators[i]].fuel > 0.0f)
                return "No power: the adjacent burner is already at capacity.";

        return "No power: the adjacent burner has no fuel.";
    }
```

- [ ] **Step 8: Fix the two stale registry comments**

In `src/Machines/MachineType.h`, replace:

```cpp
    bool generator; // supplies power to its network
    bool consumer;  // draws power from its network
```

with:

```cpp
    bool generator; // supplies power to the machines touching it
    bool consumer;  // draws power from a generator touching it
```

- [ ] **Step 9: Run tests to verify the power suite passes**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: builds clean; all of `tests/test_power.cpp` passes. **Two failures remain**, both in `tests/test_machine_inspect.cpp` (`"a drill with no generator anywhere reports no fuel in its network"` and `"a network whose demand exceeds supply reports that reason"`) — they assert the old message strings. Step 10 fixes them.

- [ ] **Step 10: Update the idle-reason tests**

In `tests/test_machine_inspect.cpp`, replace:

```cpp
TEST_CASE("a drill with no generator anywhere reports no fuel in its network")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);

    m.place(MachineType::Drill, 0, 0, Direction::Right); // no generator

    std::vector<sf::Vector2i> mined;
    m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(0, 0, world);

    CHECK(status.reason == "No power: no fuel in this network.");
}

TEST_CASE("a network whose demand exceeds supply reports that reason")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);

    // Generator supply is 10; three drills demand 15 together.
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Right);
    m.place(MachineType::Drill, 2, 0, Direction::Right);
    m.place(MachineType::Drill, 3, 0, Direction::Right);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 10; ++i)
        m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(1, 0, world);

    CHECK(status.reason == "No power: network demand exceeds supply.");
}
```

with:

```cpp
TEST_CASE("a drill with no generator next to it says so")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);

    m.place(MachineType::Drill, 0, 0, Direction::Right); // no generator

    std::vector<sf::Vector2i> mined;
    m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(0, 0, world);

    CHECK(status.reason == "No power: not next to a burner generator.");
}

TEST_CASE("a drill next to an unfuelled burner blames the fuel")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);

    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);
    m.place(MachineType::Drill, 6, 5, Direction::Right);
    // No coal inserted: the burner is there, it just has nothing to burn.

    std::vector<sf::Vector2i> mined;
    m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(6, 5, world);

    CHECK(status.reason == "No power: the adjacent burner has no fuel.");
}

TEST_CASE("a drill whose burner is already spoken for says so")
{
    Machines m;
    World world;
    world.fill(BlockType::Air);

    // Supply is 10 and each drill demands 5, so the third one placed loses out
    // even though it is touching the burner.
    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);
    m.place(MachineType::Drill, 6, 5, Direction::Right);
    m.place(MachineType::Drill, 4, 5, Direction::Right);
    m.place(MachineType::Drill, 5, 6, Direction::Right);
    REQUIRE(m.tryInsert(5, 5, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 10; ++i)
        m.tick(world, STEP, mined);

    const MachineStatus status = m.inspect(5, 6, world);

    CHECK(status.reason == "No power: the adjacent burner is already at capacity.");
}
```

- [ ] **Step 11: Add the fuel-burn test**

Append to `tests/test_processing.cpp`:

```cpp
TEST_CASE("a generator supplying nothing does not burn, even next to a powered machine")
{
    World world;
    Machines m;

    // Both burners touch the drill, but its whole 5 comes from one of them.
    m.place(MachineType::BurnerGenerator, 5, 5, Direction::Right);
    m.place(MachineType::BurnerGenerator, 7, 5, Direction::Right);
    m.place(MachineType::Drill, 6, 5, Direction::Down);

    REQUIRE(m.tryInsert(5, 5, ItemType::Coal));
    REQUIRE(m.tryInsert(7, 5, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    for (int i = 0; i < 120; ++i)
        m.tick(world, STEP, mined);

    REQUIRE(m.at(6, 5)->powered);

    // Exactly one generator supplied it, so exactly one burned - asserted
    // without pinning down which, since that is down to neighbour scan order.
    const float left = m.at(5, 5)->fuel;
    const float right = m.at(7, 5)->fuel;

    const bool leftBurned = left < COAL_BURN_SECONDS
        && right == doctest::Approx(COAL_BURN_SECONDS);
    const bool rightBurned = right < COAL_BURN_SECONDS
        && left == doctest::Approx(COAL_BURN_SECONDS);

    CHECK((leftBurned || rightBurned));
}
```

- [ ] **Step 12: Run the full suite**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 186 | 186 passed | 0 failed`.

That count is 180 baseline + 2 (Task 1) + 2 net in `test_power.cpp` (5 cases become 7) + 1 in `test_machine_inspect.cpp` (2 rewritten, 1 added) + 1 in `test_processing.cpp`.

- [ ] **Step 13: Confirm no network references survive**

Run: `grep -rn "network\|assignNetworks" src/ tests/`
Expected: **no matches in any `.cpp`/`.h` file.** (Matches inside `docs/` are prose in planning documents and are fine.)

- [ ] **Step 14: Commit**

```bash
git add src/Machines/Machine.h src/Machines/Machines.h src/Machines/Machines.cpp src/Machines/MachineType.h tests/test_power.cpp tests/test_machine_inspect.cpp tests/test_processing.cpp
git commit -m "feat: burner generators power only the machines touching them"
```

---

## Task 3: Manual verification in the running game

The power rules are fully unit-tested, but the layout consequences are the
whole point of this change and only show up in a real base. Confirm it before
calling this done.

**Files:** none (verification only).

- [ ] **Step 1: Build and run the game**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia` then launch `C:\Litharia\build\Debug\Litharia.exe`.

Note: the build copies SFML DLLs into `build\Debug` and will fail with `Error copying directory ... Permission denied` if a previous `Litharia.exe` is still running. Close any running instance first.

- [ ] **Step 2: Confirm a touching machine runs**

Place a Burner Generator, put a Drill directly beside it, feed the burner coal. The drill should run.

- [ ] **Step 3: Confirm a belt no longer conducts power**

Place a Burner Generator, a Belt beside it, and a Drill on the far side of the belt (so the drill is two tiles from the burner). Feed the burner coal. The drill must **not** run, and hovering it should read `No power: not next to a burner generator.`

- [ ] **Step 4: Confirm the capacity cap and its message**

Put three Drills around one fuelled Burner Generator. Exactly two should run — the two placed first. Hover the third: it should read `No power: the adjacent burner is already at capacity.`

- [ ] **Step 5: Confirm the unfuelled message**

Place a Burner Generator with a Drill beside it and give it no coal. Hover the drill: `No power: the adjacent burner has no fuel.`

- [ ] **Step 6: Confirm fuel is not wasted**

Feed a lone Burner Generator (no machines beside it) a coal. Its fuel bar should light and then hold steady rather than draining. Add a Drill beside it: the fuel should now start dropping.

- [ ] **Step 7: Report back**

Summarise what was checked and anything that felt wrong in play — particularly whether needing a burner per two machines makes the factory tedious to build. That is a balance question for the owner, not a bug to fix here.

---

## Self-Review Notes

- **Spec coverage:** network removal (Task 2, Steps 3-5), placement counter (Task 1), the greedy solve incl. combining supplies (Task 2 Step 5), 4-connected adjacency (Task 2 Step 5, `adjacentGenerators`), the fuel-burn fix (Task 2 Step 6), three idle reasons (Task 2 Step 7). Every spec-listed test has a step except the "reserves nothing" case, which is documented above as unobservable with current ratings rather than silently dropped.
- **Type consistency:** `placedSeq` (`std::uint32_t`) is produced in Task 1 and consumed by Task 2's sort; `supplying` (`bool`) is produced by `updatePower()` and consumed by `tickGenerators()`; `adjacentGenerators(int, int, std::array<int,4>&) -> int` is used identically in `updatePower()` and `idleReason()`. Checked against each task's code blocks.
- **Test counts** assume each earlier task's count landed as written. Trust the suite's pass/fail status over the literal number.
