# Drill Non-Destructive Mining + Machine I/O Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drills mine an ore vein forever without destroying the block (slower, to compensate), and the player gets a single, symmetric `F` key that inserts whatever item they're holding into a machine/belt under the cursor, or — with an empty hand — pulls a machine's finished output (or a stopped belt's carried item) into their inventory.

**Architecture:** Two independent pure-logic changes in `Litharia_core` (drill behavior in `Machines.cpp`/`MachineRegistry.cpp`; a new `Machines::tryExtract`/`Machines::putBack` pair, the mirror image of the existing `tryInsert`), then one wiring change in `Game.cpp` that replaces the old coal-only `loadFuelAtCursor()` with a unified `interactAtCursor()` built on top of both. Nothing here touches the machine-feedback bars/tooltip from the prior feature — `barStatus()` already reads `machineInfo(Drill).actionTime` from the registry, so the progress bar automatically reflects the new speed with no changes of its own.

**Tech Stack:** C++20, SFML 3 (System only for core; Graphics/Window for the game), CMake, doctest (vendored single header).

## Global Constraints

- **C++ standard:** C++20, already configured — do not change.
- **Simulation/rendering split:** `Machines.h/.cpp` and `MachineRegistry.cpp` are in `Litharia_core` — no SFML Graphics/Window includes. Only `Game.cpp` (already an SFML Graphics/Window file) is touched for the wiring task.
- **Never destroy an item:** this codebase has an explicit rule (see `Inventory::add`'s doc comment and how dropped items keep their leftover rather than vanishing) that a full inventory never silently deletes a stack. `Machines::putBack` exists specifically to uphold that rule for extraction — if `Inventory::add` can't fit everything taken from a machine, the remainder goes back into that same machine's buffer, never discarded.
- **Registry is the single source of truth for timing:** `Drill`'s `actionTime` lives in `MachineRegistry.cpp`'s table and nowhere else. Do not hardcode `3.0f` anywhere else that needs the drill's cycle time.
- **Extraction from transport only when stopped:** a `Belt`/`Chute`'s carried item may only be taken via `tryExtract` when `carryTimer <= 0.0f` (it has finished waiting to move, i.e. is not "mid-transfer" this instant). While `carryTimer > 0.0f`, extraction must refuse and leave the item in place.
- **Commit after every task** with a `feat:` prefixed message.
- **Build/test commands:** the primary command is `cmake --build build --target <target> --config Debug`. If `cmake` is not on your shell's PATH, fall back to MSBuild directly (verified working in this environment):
  `& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" build\<Target>.vcxproj /p:Configuration=Debug /nologo /v:minimal`
  where `<Target>` is `Litharia_tests` or `Litharia`. Run the test binary directly at `build\Debug\Litharia_tests.exe` (or `./build/Debug/Litharia_tests.exe` from bash).

## Design Summary (from brainstorming, 2026-07-15)

Decisions this slice implements, for reviewer context:

- **Drill ore is infinite, never destroyed:** mining a block no longer calls `World::set(..., BlockType::Air)`. The same tile can be mined forever. Compensated by tripling the drill's cycle time (1.0s → 3.0s).
- **No chunk redraw needed for drilling anymore:** since the block never changes, `tickDrills` stops reporting the tile into `minedTiles`. The `minedTiles` parameter itself is left in place on `Machines::tick`/`tickDrills` (still used conceptually for any future block-destroying machine) — it will just always come back empty from drilling now. This is a deliberate scope cut: renaming/removing the parameter would touch every test that calls `tick()` for no behavioral benefit.
- **`F` is now one context-sensitive key:** holding an item → try to insert it wherever the cursor points (unchanged acceptance rules: generator=coal only, smelter=smeltable ore only, belt/chute=anything). Empty-handed → try to extract from wherever the cursor points (a Drill/Smelter's whole output stack, or a stopped belt/chute's carried item).
- **Extraction takes the whole stack, not one unit at a time** — the point is collecting finished goods in fewer key presses, unlike insertion which is naturally one-unit-at-a-time (you're feeding individual items in).

---

## File Structure

**Modified:**
- `src/Machines/MachineRegistry.cpp` — `Drill`'s `actionTime` becomes `3.0f`.
- `src/Machines/Machines.h` / `src/Machines/Machines.cpp` — `tickDrills` no longer destroys the mined block or reports it for redraw; new `tryExtract`/`putBack` methods.
- `src/Game/Game.h` / `src/Game/Game.cpp` — `loadFuelAtCursor()` replaced by `interactAtCursor()`.
- `tests/test_processing.cpp` — fix the drill test's now-wrong "ore becomes Air" assertion and timing; add an infinite-mining test.
- `tests/test_factory.cpp` — fix the integration test's "vein becomes Air" assertion; assert production exceeds what the old finite vein could ever have produced.
- `tests/test_machine_inspect.cpp` — bump one test's tick count so it still completes a mining cycle at the new, slower speed.
- `CMakeLists.txt` — register the new test file.

**New:**
- `tests/test_extraction.cpp` — covers `tryExtract`/`putBack`.

---

## Canonical Interfaces (defined once, referenced by all tasks)

```cpp
// src/Machines/Machines.h  (Task 2, added to the existing class)
class Machines
{
public:
    // ...existing members unchanged...

    // Takes whatever a machine is holding to give away: a Drill/Smelter's whole
    // output stack, or a Belt/Chute's carried item (only once carryTimer <= 0.0f -
    // mid-transfer refuses). Empty stack if there is nothing to take. The
    // counterpart to tryInsert.
    ItemStack tryExtract(int x, int y);

    // Gives a stack back to the machine at (x, y), into whichever buffer
    // tryExtract() would have taken it from. No-op for an empty stack or an
    // empty tile. Used when the taker (the player's bag) could not hold
    // everything tryExtract() handed over, so nothing is ever destroyed.
    void putBack(int x, int y, ItemStack stack);
};

// src/Game/Game.h  (Task 3 — replaces the existing loadFuelAtCursor() declaration)
void interactAtCursor();
```

---

### Task 1: Drill mines forever, three times slower

**Files:**
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Machines.cpp` (`tickDrills`)
- Modify: `tests/test_processing.cpp`
- Modify: `tests/test_factory.cpp`
- Modify: `tests/test_machine_inspect.cpp`

**Interfaces:**
- Consumes: nothing new — this only changes existing behavior of `machineInfo(MachineType::Drill).actionTime` and `Machines::tickDrills`.
- Produces: no new interface. `barStatus()` and `Machines::inspect()` (from the prior feature) automatically reflect the new `actionTime` with no changes of their own, since they already read it from the registry each call.

- [ ] **Step 1: Update the failing/changing tests first**

In `tests/test_processing.cpp`, find the test `"a powered drill eats the ore below it and outputs onto a belt"` (currently asserts the ore tile becomes `Air` and checks `mined` for a reported tile) and replace its body with:

```cpp
TEST_CASE("a powered drill eats the ore below it and outputs onto a belt")
{
    World world;
    world.fill(BlockType::Air);
    world.set(0, 1, BlockType::CopperOre); // directly beneath the drill

    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    m.place(MachineType::Drill, 1, 0, Direction::Right); // outputs to the right
    m.place(MachineType::Belt, 2, 0, Direction::Right);
    REQUIRE(m.at(1, 0) != nullptr);

    // Put the ore under the drill at (1,2) as well: drill at (1,0) scans down.
    world.set(1, 1, BlockType::CopperOre);

    // Fuel the generator so the drill is powered.
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // Drill work time is 3.0s; run 4s to be safe, plus belt handoff.
    for (int i = 0; i < 240; ++i)
        m.tick(world, step, mined);

    // The ore is not destroyed: the vein is inexhaustible now.
    CHECK(world.get(1, 1) == BlockType::CopperOre);

    // ...and copper ore reached the belt (or is sitting in the drill output).
    const bool onBelt = m.at(2, 0)->carried == ItemType::CopperOre;
    const bool inDrill = m.at(1, 0)->output.type == ItemType::CopperOre;
    CHECK((onBelt || inDrill));
}
```

(This drops the old "mined coordinate reported for redraw" check — drilling no longer changes the world, so there is nothing to report.)

Then add a new test directly after it, in the same file:

```cpp
TEST_CASE("a drill's vein never runs out: it mines the same tile again and again")
{
    World world;
    world.fill(BlockType::Air);
    world.set(1, 1, BlockType::CopperOre);

    Machines m;
    m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    // Facing Up with nothing to receive output, so it fills and we can count how
    // many times it has mined without a downstream machine interfering.
    m.place(MachineType::Drill, 1, 0, Direction::Up);
    REQUIRE(m.tryInsert(0, 0, ItemType::Coal));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // One mining cycle (3.0s) fills the output; it then stays full since nothing
    // is there to take it. The tile must still be ore no matter how long this runs -
    // a finite vein of one tile would already be Air well before 10 seconds in.
    for (int i = 0; i < 600; ++i) // 10 seconds
        m.tick(world, step, mined);

    CHECK(world.get(1, 1) == BlockType::CopperOre);
    CHECK(m.at(1, 0)->output.type == ItemType::CopperOre);
}
```

In `tests/test_factory.cpp`, replace the two lines:

```cpp
    // The vein has been eaten...
    for (int y = 1; y <= 4; ++y)
        CHECK(world.get(10, y) == BlockType::Air);
```

with:

```cpp
    // The vein is untouched - mining no longer destroys the block...
    for (int y = 1; y <= 4; ++y)
        CHECK(world.get(10, y) == BlockType::CopperOre);
```

and change the final line of that test from:

```cpp
    CHECK(m.at(12, 0)->output.count >= 1);
```

to:

```cpp
    // A one-tile vein could only ever have produced 1 ore total under the old
    // destroy-on-mine rule (there is only one reachable ore tile here - see the
    // drill's placement above). Comfortably exceeding that proves the vein is
    // being mined over and over, not drained.
    CHECK(m.at(12, 0)->output.count >= 5);
```

In `tests/test_machine_inspect.cpp`, find the test `"a drill whose output cannot be pushed anywhere reports that its output is full"` and change:

```cpp
    for (int i = 0; i < 120; ++i) // long enough to mine once and fill the output
        m.tick(world, STEP, mined);
```

to:

```cpp
    for (int i = 0; i < 240; ++i) // long enough (4s > 3.0s cycle) to mine once and fill the output
        m.tick(world, STEP, mined);
```

- [ ] **Step 2: Run tests to verify the updated/new ones fail against the old behavior**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: FAIL — the updated assertions (`CopperOre` instead of `Air`, `>= 5` instead of `>= 1`) don't match the current destroy-on-mine, 1.0s-cycle behavior yet.

- [ ] **Step 3: Implement the change**

In `src/Machines/MachineRegistry.cpp`, change the `Drill` row from:

```cpp
    {"Drill",             {150, 150, 160}, false, true,  false, 5.0f,  1.0f},
```

to:

```cpp
    {"Drill",             {150, 150, 160}, false, true,  false, 5.0f,  3.0f},
```

In `src/Machines/Machines.cpp`, inside `Machines::tickDrills`, find:

```cpp
        if (m.progress >= machineInfo(m.type).actionTime)
        {
            const BlockType ore = world.get(m.x, oreY);
            const ItemType drop = itemForBlock(ore);

            world.set(m.x, oreY, BlockType::Air);
            minedTiles.push_back({m.x, oreY});

            m.output = {drop, 1};
            m.progress = 0.0f;
        }
```

and replace it with:

```cpp
        if (m.progress >= machineInfo(m.type).actionTime)
        {
            // The vein is inexhaustible: mining does not remove the block, so the
            // same tile keeps producing and the chunk mesh never needs a redraw.
            m.output = {itemForBlock(world.get(m.x, oreY)), 1};
            m.progress = 0.0f;
        }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: all test cases pass (120 total: 119 pre-existing with 2 updated + 1 new).

- [ ] **Step 5: Commit**

```bash
git add src/Machines/MachineRegistry.cpp src/Machines/Machines.cpp tests/test_processing.cpp tests/test_factory.cpp tests/test_machine_inspect.cpp
git commit -m "feat: drills mine ore forever, three times slower, instead of destroying it"
```

---

### Task 2: `Machines::tryExtract()` / `Machines::putBack()`

**Files:**
- Modify: `src/Machines/Machines.h`, `src/Machines/Machines.cpp`
- Test: `tests/test_extraction.cpp` (create)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `Machine`, `MachineInfo`, `machineInfo()`, `ItemStack`.
- Produces: `Machines::tryExtract(int x, int y) -> ItemStack` and `Machines::putBack(int x, int y, ItemStack stack)` (see Canonical Interfaces), used by Task 3.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_extraction.cpp`:

```cpp
#include "doctest.h"

#include "Machines/Machines.h"

TEST_CASE("extracting from a drill takes its whole output and empties it")
{
    Machines m;
    Machine* drill = m.place(MachineType::Drill, 0, 0, Direction::Right);
    REQUIRE(drill != nullptr);
    drill->output = {ItemType::CopperOre, 1};

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.type == ItemType::CopperOre);
    CHECK(taken.count == 1);
    CHECK(m.at(0, 0)->output.empty());
}

TEST_CASE("extracting from a smelter takes the whole stacked output")
{
    Machines m;
    Machine* smelter = m.place(MachineType::Smelter, 0, 0, Direction::Right);
    REQUIRE(smelter != nullptr);
    smelter->output = {ItemType::CopperPlate, 5};

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.type == ItemType::CopperPlate);
    CHECK(taken.count == 5);
    CHECK(m.at(0, 0)->output.empty());
}

TEST_CASE("extracting from an empty output returns nothing")
{
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Right);

    CHECK(m.tryExtract(0, 0).empty());
}

TEST_CASE("extracting from a stopped belt takes its carried item")
{
    Machines m;
    Machine* belt = m.place(MachineType::Belt, 0, 0, Direction::Right);
    REQUIRE(belt != nullptr);
    belt->carried = ItemType::Coal;
    belt->carryTimer = 0.0f; // finished waiting: stopped

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.type == ItemType::Coal);
    CHECK(taken.count == 1);
    CHECK(m.at(0, 0)->carried == ItemType::None);
}

TEST_CASE("extracting from a belt mid-transfer is refused")
{
    Machines m;
    Machine* belt = m.place(MachineType::Belt, 0, 0, Direction::Right);
    REQUIRE(belt != nullptr);
    belt->carried = ItemType::Coal;
    belt->carryTimer = 0.3f; // still counting down: moving

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.empty());
    CHECK(m.at(0, 0)->carried == ItemType::Coal); // untouched
}

TEST_CASE("extracting from a generator is always refused: it has no output")
{
    Machines m;
    Machine* gen = m.place(MachineType::BurnerGenerator, 0, 0, Direction::Right);
    REQUIRE(gen != nullptr);
    gen->fuel = 10.0f;

    CHECK(m.tryExtract(0, 0).empty());
}

TEST_CASE("extracting from an empty tile returns nothing")
{
    Machines m;
    CHECK(m.tryExtract(5, 5).empty());
}

TEST_CASE("putBack restores a stack to a machine's output")
{
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Right);

    m.putBack(0, 0, {ItemType::CopperPlate, 3});

    CHECK(m.at(0, 0)->output.type == ItemType::CopperPlate);
    CHECK(m.at(0, 0)->output.count == 3);
}

TEST_CASE("putBack restores a single item to a belt's carried slot")
{
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);

    m.putBack(0, 0, {ItemType::Coal, 1});

    CHECK(m.at(0, 0)->carried == ItemType::Coal);
}

TEST_CASE("putBack does nothing for an empty stack or an empty tile")
{
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Right);

    m.putBack(0, 0, {});
    CHECK(m.at(0, 0)->output.empty());

    m.putBack(9, 9, {ItemType::Coal, 1}); // no machine there: must not crash
}
```

- [ ] **Step 2: Register the new test file in CMake**

In `CMakeLists.txt`, add to the `Litharia_tests` source list (after `tests/test_machine_inspect.cpp`):

```cmake
    tests/test_extraction.cpp
```

- [ ] **Step 3: Run tests to verify they fail to compile**

Run: `cmake --build build --target Litharia_tests --config Debug`
Expected: FAIL to compile — `Machines::tryExtract`/`Machines::putBack` are not members of `Machines`.

- [ ] **Step 4: Write minimal implementation**

In `src/Machines/Machines.h`, add the public methods directly below the existing `tryInsert` declaration:

```cpp
    // Takes whatever a machine is holding to give away: a Drill/Smelter's whole
    // output stack, or a Belt/Chute's carried item (only once carryTimer <= 0.0f -
    // mid-transfer refuses). Empty stack if there is nothing to take. The
    // counterpart to tryInsert.
    ItemStack tryExtract(int x, int y);

    // Gives a stack back to the machine at (x, y), into whichever buffer
    // tryExtract() would have taken it from. No-op for an empty stack or an
    // empty tile. Used when the taker (the player's bag) could not hold
    // everything tryExtract() handed over, so nothing is ever destroyed.
    void putBack(int x, int y, ItemStack stack);
```

In `src/Machines/Machines.cpp`, add the two implementations directly after the existing `Machines::tryInsert` definition (before `Machines::inspect`):

```cpp
ItemStack Machines::tryExtract(int x, int y)
{
    Machine* m = at(x, y);
    if (m == nullptr)
        return {};

    const MachineInfo& info = machineInfo(m->type);

    if (info.transport)
    {
        if (m->carried == ItemType::None || m->carryTimer > 0.0f)
            return {};

        const ItemStack taken{m->carried, 1};
        m->carried = ItemType::None;
        return taken;
    }

    if (m->output.empty())
        return {};

    const ItemStack taken = m->output;
    m->output = {};
    return taken;
}

void Machines::putBack(int x, int y, ItemStack stack)
{
    if (stack.empty())
        return;

    Machine* m = at(x, y);
    if (m == nullptr)
        return;

    const MachineInfo& info = machineInfo(m->type);

    if (info.transport)
        m->carried = stack.type; // transport holds exactly one item; count is always 1
    else
        m->output = stack;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: all test cases pass (130 total: 120 from Task 1 + 10 new).

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Machines.h src/Machines/Machines.cpp tests/test_extraction.cpp CMakeLists.txt
git commit -m "feat: add Machines::tryExtract()/putBack() for pulling output from a machine"
```

---

### Task 3: Unify the `F` key into insert-or-extract

**Files:**
- Modify: `src/Game/Game.h`, `src/Game/Game.cpp`

**Interfaces:**
- Consumes: `Machines::tryInsert` (existing), `Machines::tryExtract`/`Machines::putBack` (Task 2), `Inventory::slot`/`removeOne`/`add` (existing), `Player::selectedSlot()` (existing, already used elsewhere in `Game.cpp`).
- Produces: nothing further — this is the final integration point for this plan.

No automated test: this is wiring already-tested logic into the keyboard-input path, same reasoning as the prior feature's `Game.cpp` wiring task. Verified manually.

- [ ] **Step 1: Replace the declaration**

In `src/Game/Game.h`, find:

```cpp
    void loadFuelAtCursor();
```

and replace it with:

```cpp
    void interactAtCursor();
```

- [ ] **Step 2: Build to verify the old call site now fails**

Run: `cmake --build build --target Litharia --config Debug`
Expected: FAIL to compile — `Game.cpp` still calls `loadFuelAtCursor()`, which no longer exists (either at its definition or its call site, depending on which the compiler reaches first).

- [ ] **Step 3: Replace the implementation and call site**

In `src/Game/Game.cpp`, find the entire `Game::loadFuelAtCursor()` function:

```cpp
void Game::loadFuelAtCursor()
{
    const sf::Vector2i tile = cursorTile();

    // Only spend a coal if the machine actually accepts it.
    Inventory& bag = player.inventory();
    if (bag.count(ItemType::Coal) <= 0)
        return;

    if (machines.tryInsert(tile.x, tile.y, ItemType::Coal))
    {
        // Remove one coal from wherever it sits in the bag.
        for (int i = 0; i < Inventory::SIZE; ++i)
        {
            if (bag.slot(i).type == ItemType::Coal)
            {
                bag.removeOne(i);
                break;
            }
        }
    }
}
```

and replace it with:

```cpp
void Game::interactAtCursor()
{
    const sf::Vector2i tile = cursorTile();
    Inventory& bag = player.inventory();
    const int slot = player.selectedSlot();
    const ItemStack& held = bag.slot(slot);

    if (!held.empty())
    {
        // Holding something: offer it to whatever's under the cursor.
        if (machines.tryInsert(tile.x, tile.y, held.type))
            bag.removeOne(slot);

        return;
    }

    // Empty-handed: try to take from whatever's under the cursor instead.
    const ItemStack extracted = machines.tryExtract(tile.x, tile.y);
    if (extracted.empty())
        return;

    const int leftover = bag.add(extracted);
    if (leftover > 0)
        machines.putBack(tile.x, tile.y, {extracted.type, leftover});
}
```

In `Game::handleEvents`, find:

```cpp
            if (key->code == Key::F)
                loadFuelAtCursor();
```

and change it to:

```cpp
            if (key->code == Key::F)
                interactAtCursor();
```

- [ ] **Step 4: Build**

Run: `cmake --build build --target Litharia --config Debug`
Expected: builds with no errors.

- [ ] **Step 5: Run the full test suite once more**

Run: `cmake --build build --target Litharia_tests --config Debug` then `./build/Debug/Litharia_tests.exe`
Expected: all 130 test cases still pass — this task touched no `Litharia_core` logic, only `Game.cpp` wiring.

- [ ] **Step 6: Manually verify end-to-end**

Run: `./build/Debug/Litharia.exe`

- Place a Drill over a shallow ore vein and a Burner Generator next to it; load coal into the generator with `F` while empty-handed... actually: press `1`-`0` to select an empty hotbar slot first, point at the generator, and confirm `F` does nothing (you're empty-handed and a generator has no output to take) — then select coal in your hotbar, point at the generator, and confirm `F` now loads it (title bar / tooltip shows the generator gaining fuel).
- Let the drill mine for a while (3s per ore now); confirm the block it's mining is never destroyed no matter how long you wait.
- Point at the drill with an empty-selected slot and press `F`: confirm the mined ore moves into your inventory (hotbar count goes up) and the drill's output empties (tooltip/bar reflects it).
- Place a belt, put an ore on it by hand (select the ore in your hotbar, point at the belt, press `F`), then immediately try to grab it back with an empty hand before it moves — confirm it's refused while `carryTimer > 0`, then succeeds once the belt would otherwise be idle-full (nothing downstream to hand it to).
- Place a Smelter, feed it ore via `F` (ore selected in hotbar), let it produce a plate, then grab the plate with `F` while empty-handed.

- [ ] **Step 7: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: unify the F key into context-sensitive insert-or-extract"
```
