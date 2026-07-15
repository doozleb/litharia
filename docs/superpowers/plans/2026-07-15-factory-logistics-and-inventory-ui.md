# Factory Logistics, Chests, and Inventory UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Machine output round-robins across all four adjacent tiles, chests exist as multi-slot storage machines belts can feed, blocks can no longer be placed on top of factory equipment, and the player can open (E) a bag panel or a chest+bag panel and drag items between slots, with a build-mode palette and a tooltip that actually anchors to the machine it describes.

**Architecture:** Engine changes (round-robin output, chests, variable-size `Inventory`, placement collision) land first in `Litharia_core`, each with doctest coverage, exactly like the existing drill/belt/smelter slices. UI changes (tooltip, build palette, inventory/chest panels, drag-and-drop) land after, in `Game`/`Hud`, backed by a new pure `HudLayout` module (SFML System only, no Window/Graphics) so the trickiest bit — screen-position-to-slot hit-testing — is unit-tested even though `Hud`/`Game` themselves aren't linked into the test binary.

**Tech Stack:** C++20, SFML 3 (System/Window/Graphics), doctest, CMake + Visual Studio generator.

## Global Constraints

- Chests hold 20 slots (2 rows of 10) — `CHEST_SLOTS = 20` in `MachineType.h`.
- The player's bag stays 40 slots by default (10 hotbar + 30 more) — unchanged from today.
- Machine output tries all 4 sides round-robin, starting from (and advancing past) the side it last succeeded on, seeded from the machine's `facing` when placed.
- `Player::update()`'s new `Machines*` parameter is trailing and defaults to `nullptr`, so no existing call site (test or otherwise) is required to change.
- Nothing is ever silently destroyed by a UI action: a drag that can't fully land returns its leftover to the source slot, mirroring the `tryExtract`/`putBack` contract `Machines` already upholds.
- Build: `cmake --build build --config Debug`. Tests: `build/Debug/Litharia_tests.exe` (currently 130 cases, all green — confirm this before Task 1). Game: `build/Debug/Litharia.exe`.

---

### Task 1: Round-robin multi-side machine output

**Files:**
- Modify: `src/Machines/Machine.h`
- Modify: `src/Machines/Machines.h`
- Modify: `src/Machines/Machines.cpp`
- Test: `tests/test_transport.cpp`

**Interfaces:**
- Consumes: existing `Machine::facing`, `Machines::tryInsert(int,int,ItemType)`, `dirDX`/`dirDY` from `Core/Direction.h`.
- Produces: `Machine::outputCursor` (a `Direction`), seeded to `facing` on `place()`. `Machines::insertOutput(Machine&)` (renamed from `insertOutputAhead`), called by `tickDrills` and `tickSmelters`, tries all 4 sides starting at `outputCursor` and advances it past whichever side succeeds. No other task depends on this beyond it existing and working.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_transport.cpp`:

```cpp
TEST_CASE("a smelter round-robins its output across belts on multiple sides")
{
    World world;
    Machines m;

    m.place(MachineType::Smelter, 5, 5, Direction::Right);
    m.place(MachineType::Belt, 6, 5, Direction::Right); // to the right
    m.place(MachineType::Belt, 4, 5, Direction::Right); // to the left

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;

    // First send: goes to the right belt, the smelter's facing/starting side.
    m.at(5, 5)->output = {ItemType::CopperPlate, 1};
    m.tick(world, step, mined);
    CHECK(m.at(6, 5)->carried == ItemType::CopperPlate);
    CHECK(m.at(4, 5)->carried == ItemType::None);

    // Clear the right belt so it can accept again, then send a second item: it
    // should go to the left belt this time, not pile onto the right one again.
    m.at(6, 5)->carried = ItemType::None;
    m.at(5, 5)->output = {ItemType::CopperPlate, 1};
    m.tick(world, step, mined);
    CHECK(m.at(4, 5)->carried == ItemType::CopperPlate);
    CHECK(m.at(6, 5)->carried == ItemType::None);

    // A third send goes back to the right, completing the alternation.
    m.at(4, 5)->carried = ItemType::None;
    m.at(5, 5)->output = {ItemType::CopperPlate, 1};
    m.tick(world, step, mined);
    CHECK(m.at(6, 5)->carried == ItemType::CopperPlate);
}

TEST_CASE("output with nothing on any of the 4 sides just waits")
{
    World world;
    Machines m;
    m.place(MachineType::Smelter, 0, 0, Direction::Right);

    m.at(0, 0)->output = {ItemType::IronPlate, 2};

    std::vector<sf::Vector2i> mined;
    m.tick(world, 1.0f / 60.0f, mined);

    // Nothing to take it: it stays right where it was.
    CHECK(m.at(0, 0)->output.type == ItemType::IronPlate);
    CHECK(m.at(0, 0)->output.count == 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: builds fine (the test only uses existing public API), then `build/Debug/Litharia_tests.exe` FAILS the round-robin test — the second send lands back on the right belt instead of the left, because `insertOutputAhead` today only ever tries `facing`.

- [ ] **Step 3: Add `outputCursor` to `Machine`**

In `src/Machines/Machine.h`, add a field right after `facing`:

```cpp
    Direction facing = Direction::Right;

    // Which of the 4 sides output delivery tries first. Seeded to `facing` when
    // placed, then advanced past whichever side a send last succeeded on, so two
    // belts flanking a machine alternate rather than one starving the other.
    Direction outputCursor = Direction::Right;
```

- [ ] **Step 4: Seed `outputCursor` in `Machines::place()`**

In `src/Machines/Machines.cpp`, `Machines::place()` currently reads:

```cpp
    Machine m;
    m.type = type;
    m.x = x;
    m.y = y;
    m.facing = facing;

    machines.push_back(m);
```

Change to:

```cpp
    Machine m;
    m.type = type;
    m.x = x;
    m.y = y;
    m.facing = facing;
    m.outputCursor = facing;

    machines.push_back(m);
```

- [ ] **Step 5: Rewrite `insertOutputAhead` into round-robin `insertOutput`**

In `src/Machines/Machines.h`, rename the private helper declaration:

```cpp
    void insertOutputAhead(Machine& m);
```

to:

```cpp
    void insertOutput(Machine& m);
```

In `src/Machines/Machines.cpp`, add `#include <array>` to the top include block (alongside the existing `<algorithm>` and `<queue>`), then replace the whole `insertOutputAhead` function:

```cpp
void Machines::insertOutputAhead(Machine& m)
{
    if (m.output.empty())
        return;

    const int tx = m.x + dirDX(m.facing);
    const int ty = m.y + dirDY(m.facing);

    if (tryInsert(tx, ty, m.output.type))
    {
        --m.output.count;
        if (m.output.count == 0)
            m.output.type = ItemType::None;
    }
}
```

with:

```cpp
void Machines::insertOutput(Machine& m)
{
    if (m.output.empty())
        return;

    // Direction's declared order (Up, Down, Left, Right) doubles as a 0-3 index,
    // so rotating this array by the cursor's own value starts the search there.
    constexpr std::array<Direction, 4> SIDES = {Direction::Up, Direction::Down,
                                                 Direction::Left, Direction::Right};

    for (int i = 0; i < 4; ++i)
    {
        const Direction dir = SIDES[(static_cast<int>(m.outputCursor) + i) % 4];
        const int tx = m.x + dirDX(dir);
        const int ty = m.y + dirDY(dir);

        if (!tryInsert(tx, ty, m.output.type))
            continue;

        --m.output.count;
        if (m.output.count == 0)
            m.output.type = ItemType::None;

        // Next attempt starts one past the side that just worked.
        m.outputCursor = SIDES[(static_cast<int>(dir) + 1) % 4];
        return;
    }
}
```

- [ ] **Step 6: Update the two call sites**

In `src/Machines/Machines.cpp`, `tickDrills` and `tickSmelters` each call `insertOutputAhead(m);` near the top of their loop body. Change both call sites to `insertOutput(m);`.

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Run: `build/Debug/Litharia_tests.exe`
Expected: all cases pass, including the two new ones.

- [ ] **Step 8: Commit**

```bash
git add src/Machines/Machine.h src/Machines/Machines.h src/Machines/Machines.cpp tests/test_transport.cpp
git commit -m "feat: round-robin machine output across all 4 sides"
```

---

### Task 2: `Inventory` becomes variably sized, with `take()`/`exchange()`

**Files:**
- Modify: `src/Items/Inventory.h`
- Modify: `src/Items/Inventory.cpp`
- Test: `tests/test_inventory.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `explicit Inventory(int slotCount = SIZE)`, `int slotCount() const`, `ItemStack take(int index)`, `ItemStack exchange(int index, ItemStack incoming)`. Task 3 (chests) constructs `Inventory(CHEST_SLOTS)`; Task 8 (drag-and-drop) calls `take`/`exchange` on both the player's bag and a chest's storage.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_inventory.cpp`:

```cpp
TEST_CASE("take empties a slot and returns what was there")
{
    Inventory bag;
    bag.add({ItemType::Stone, 4});

    const ItemStack taken = bag.take(0);

    CHECK(taken.type == ItemType::Stone);
    CHECK(taken.count == 4);
    CHECK(bag.slot(0).empty());
}

TEST_CASE("take on an empty slot returns an empty stack")
{
    Inventory bag;
    CHECK(bag.take(0).empty());
}

TEST_CASE("exchange drops a stack into an empty slot")
{
    Inventory bag;

    const ItemStack leftover = bag.exchange(0, {ItemType::IronOre, 3});

    CHECK(leftover.empty());
    CHECK(bag.slot(0).type == ItemType::IronOre);
    CHECK(bag.slot(0).count == 3);
}

TEST_CASE("exchange merges onto a matching stack and returns the overflow")
{
    Inventory bag;
    const int max = itemInfo(ItemType::Stone).maxStack;
    bag.add({ItemType::Stone, max - 2});

    const ItemStack leftover = bag.exchange(0, {ItemType::Stone, 5});

    CHECK(bag.slot(0).count == max);
    CHECK(leftover.type == ItemType::Stone);
    CHECK(leftover.count == 3);
}

TEST_CASE("exchange swaps wholesale when the slot holds a different item")
{
    Inventory bag;
    bag.add({ItemType::Dirt, 2});

    const ItemStack displaced = bag.exchange(0, {ItemType::Coal, 1});

    CHECK(bag.slot(0).type == ItemType::Coal);
    CHECK(bag.slot(0).count == 1);
    CHECK(displaced.type == ItemType::Dirt);
    CHECK(displaced.count == 2);
}

TEST_CASE("exchanging an empty stack in changes nothing")
{
    Inventory bag;
    bag.add({ItemType::Dirt, 2});

    const ItemStack result = bag.exchange(0, {});

    CHECK(result.empty());
    CHECK(bag.slot(0).type == ItemType::Dirt);
    CHECK(bag.slot(0).count == 2);
}

TEST_CASE("exchange on an out-of-range slot is refused, not undefined")
{
    Inventory bag;

    const ItemStack bounced = bag.exchange(999, {ItemType::Stone, 1});

    CHECK(bounced.type == ItemType::Stone);
    CHECK(bounced.count == 1);
}

TEST_CASE("a smaller inventory reports its own slot count and stays within it")
{
    Inventory chest(20);

    CHECK(chest.slotCount() == 20);
    CHECK(chest.slot(19).empty());
    CHECK(chest.slot(20).empty()); // out of range: refused, not undefined
    CHECK_FALSE(chest.removeOne(20));
}

TEST_CASE("the default inventory size is still the player's 40 slots")
{
    Inventory bag;
    CHECK(bag.slotCount() == Inventory::SIZE);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAILS to compile — `Inventory` has no `take`, `exchange`, `slotCount`, or single-int constructor yet.

- [ ] **Step 3: Rewrite `Inventory.h`**

Replace the full contents of `src/Items/Inventory.h`:

```cpp
#pragma once

#include <vector>

#include "Items.h"

// A bag of item-stack slots. A player's bag treats the first HOTBAR_SIZE slots
// as the hotbar; a chest uses every slot the same way, with no hotbar concept
// at all.
class Inventory
{
public:
    static constexpr int SIZE = 40;
    static constexpr int HOTBAR_SIZE = 10;

    // slotCount defaults to a player-sized bag. A chest constructs a smaller one
    // explicitly, e.g. Inventory(20).
    explicit Inventory(int slotCount = SIZE);

    // Tops up existing matching stacks before opening a fresh slot, and respects
    // each item's max stack size.
    //
    // Returns the LEFTOVER count - what would not fit. Nothing is ever silently
    // destroyed by a full bag; the caller is responsible for the remainder, which
    // is what lets a dropped item stay on the ground and keep trying.
    int add(ItemStack stack);

    // Decrements a stack, clearing the slot when it hits zero. False if the slot
    // was already empty.
    bool removeOne(int slot);

    // Empties a slot outright and returns whatever was in it (an empty stack if
    // the slot already was). Used to lift a stack off the grid, e.g. dragging.
    ItemStack take(int index);

    // Puts `incoming` into `index`: merges onto a matching stack as far as it
    // fits, or swaps wholesale if the slot holds something else. Returns
    // whatever doesn't end up in the slot - a merge remainder, or the stack
    // that got swapped out - so nothing dragged is ever destroyed. An
    // out-of-range index bounces `incoming` straight back unchanged.
    ItemStack exchange(int index, ItemStack incoming);

    const ItemStack& slot(int index) const;

    bool isEmpty() const;

    // How many of an item type the bag holds in total.
    int count(ItemType type) const;

    int slotCount() const { return static_cast<int>(slots.size()); }

private:
    bool validSlot(int index) const
    {
        return index >= 0 && index < static_cast<int>(slots.size());
    }

    std::vector<ItemStack> slots;
};
```

- [ ] **Step 4: Update `Inventory.cpp`**

In `src/Items/Inventory.cpp`, add the constructor right after the anonymous namespace, and add `take`/`exchange` after `removeOne`. The existing `add`, `removeOne`, `slot`, `isEmpty`, `count` bodies are unchanged (they already work over any iterable container).

Replace the top of the file (the `namespace { ... }` block and everything down to `int Inventory::add`) so the file reads:

```cpp
#include "Inventory.h"

#include <algorithm>

namespace
{

const ItemStack emptyStack{};

} // namespace

Inventory::Inventory(int slotCount)
    : slots(static_cast<std::size_t>(std::max(0, slotCount)))
{
}

int Inventory::add(ItemStack stack)
{
```

(everything from the old `int Inventory::add(ItemStack stack)` body onward is unchanged until after `bool Inventory::removeOne(int slot)`'s closing brace).

Then, immediately after `Inventory::removeOne`'s closing brace and before `const ItemStack& Inventory::slot(int index) const`, insert:

```cpp
ItemStack Inventory::take(int index)
{
    if (!validSlot(index))
        return {};

    ItemStack& existing = slots[static_cast<std::size_t>(index)];
    const ItemStack taken = existing;
    existing = ItemStack{};
    return taken;
}

ItemStack Inventory::exchange(int index, ItemStack incoming)
{
    if (!validSlot(index))
        return incoming;

    if (incoming.empty())
        return {};

    ItemStack& existing = slots[static_cast<std::size_t>(index)];

    if (existing.empty())
    {
        existing = incoming;
        return {};
    }

    if (existing.type != incoming.type)
    {
        const ItemStack displaced = existing;
        existing = incoming;
        return displaced;
    }

    // Same item: merge as far as it fits, hand back the remainder.
    const int max = itemInfo(existing.type).maxStack;
    const int room = max - existing.count;
    const int moved = std::min(room, incoming.count);

    existing.count += moved;
    const int remaining = incoming.count - moved;

    return remaining > 0 ? ItemStack{incoming.type, remaining} : ItemStack{};
}
```

The rest of the file (`slot`, `isEmpty`, `count`) is unchanged.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Run: `build/Debug/Litharia_tests.exe`
Expected: all cases pass.

- [ ] **Step 6: Commit**

```bash
git add src/Items/Inventory.h src/Items/Inventory.cpp tests/test_inventory.cpp
git commit -m "feat: make Inventory variably sized and add take/exchange"
```

---

### Task 3: Chests

**Files:**
- Modify: `src/Machines/MachineType.h`
- Modify: `src/Machines/MachineRegistry.cpp`
- Modify: `src/Machines/Machine.h`
- Modify: `src/Machines/Machines.cpp`
- Test: `tests/test_machines.cpp`
- Test: `tests/test_transport.cpp`
- Test: `tests/test_extraction.cpp`

**Interfaces:**
- Consumes: `Inventory` (Task 2)'s `explicit Inventory(int slotCount)`, `add`, `slot`, `slotCount`, `take`.
- Produces: `MachineType::Chest`, `CHEST_SLOTS = 20`, `Machine::storage` (an `Inventory`, 0 slots for every non-chest machine). `Machines::tryInsert`/`tryExtract`/`putBack` all handle `MachineType::Chest`, so belts (via Task 1's round-robin) and the player's existing F key both work against a chest with no other code changes.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_machines.cpp`:

```cpp
TEST_CASE("a chest is placed with 20 empty storage slots and no power role")
{
    Machines machines;
    Machine* chest = machines.place(MachineType::Chest, 2, 2, Direction::Right);

    REQUIRE(chest != nullptr);
    CHECK(chest->storage.slotCount() == CHEST_SLOTS);
    CHECK(chest->storage.isEmpty());

    const MachineInfo& info = machineInfo(MachineType::Chest);
    CHECK_FALSE(info.generator);
    CHECK_FALSE(info.consumer);
    CHECK_FALSE(info.transport);
}

TEST_CASE("a non-chest machine carries no storage overhead")
{
    Machines machines;
    Machine* belt = machines.place(MachineType::Belt, 0, 0, Direction::Right);

    REQUIRE(belt != nullptr);
    CHECK(belt->storage.slotCount() == 0);
}
```

Add to `tests/test_transport.cpp`:

```cpp
TEST_CASE("a chest accepts items into multiple slots, not just one")
{
    Machines m;
    m.place(MachineType::Chest, 0, 0, Direction::Right);

    const int max = itemInfo(ItemType::Stone).maxStack;

    for (int i = 0; i < max + 3; ++i)
        CHECK(m.tryInsert(0, 0, ItemType::Stone));

    CHECK(m.at(0, 0)->storage.count(ItemType::Stone) == max + 3);
    CHECK(m.at(0, 0)->storage.slot(0).count == max);
    CHECK(m.at(0, 0)->storage.slot(1).count == 3);
}

TEST_CASE("a chest with every slot full refuses further items")
{
    Machines m;
    m.place(MachineType::Chest, 0, 0, Direction::Right);

    const int max = itemInfo(ItemType::Dirt).maxStack;
    Machine* chest = m.at(0, 0);
    for (int i = 0; i < chest->storage.slotCount(); ++i)
        chest->storage.exchange(i, {ItemType::Dirt, max});

    CHECK_FALSE(m.tryInsert(0, 0, ItemType::Dirt));
}

TEST_CASE("a belt delivers its carried item into a chest ahead of it")
{
    World world;
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);
    m.place(MachineType::Chest, 1, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(1, 0)->storage.count(ItemType::IronOre) == 1);
}
```

Add to `tests/test_extraction.cpp`:

```cpp
TEST_CASE("extracting from a chest takes the first non-empty stack")
{
    Machines m;
    m.place(MachineType::Chest, 0, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));
    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.type == ItemType::IronOre);
    CHECK(taken.count == 2);
    CHECK(m.at(0, 0)->storage.isEmpty());
}

TEST_CASE("extracting from an empty chest returns nothing")
{
    Machines m;
    m.place(MachineType::Chest, 0, 0, Direction::Right);

    CHECK(m.tryExtract(0, 0).empty());
}

TEST_CASE("putBack restores a stack into a chest rather than destroying it")
{
    Machines m;
    m.place(MachineType::Chest, 0, 0, Direction::Right);

    m.putBack(0, 0, {ItemType::Coal, 4});

    CHECK(m.at(0, 0)->storage.count(ItemType::Coal) == 4);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAILS to compile — `MachineType::Chest`, `CHEST_SLOTS`, and `Machine::storage` don't exist yet.

- [ ] **Step 3: Add `Chest` to the registry**

In `src/Machines/MachineType.h`, add the constant near the other two:

```cpp
// How many tiles straight down a drill scans for ore to eat.
inline constexpr int DRILL_REACH = 4;

// How many item slots a chest holds (2 rows of 10 in the UI).
inline constexpr int CHEST_SLOTS = 20;
```

And add `Chest` to the enum, before `Count`:

```cpp
enum class MachineType : std::uint8_t
{
    None,
    BurnerGenerator,
    Drill,
    Belt,
    Chute,
    Smelter,
    Chest,

    Count
};
```

In `src/Machines/MachineRegistry.cpp`, add a row for it (order must match the enum):

```cpp
constexpr std::array<MachineInfo, static_cast<std::size_t>(MachineType::Count)> registry = {{
    //  name                 color            gen    con    trans  power  action
    {"None",              {  0,   0,   0}, false, false, false, 0.0f,  0.0f},
    {"Burner Generator",  {190, 120,  60}, true,  false, false, 10.0f, 0.0f},
    {"Drill",             {150, 150, 160}, false, true,  false, 5.0f,  3.0f},
    {"Belt",              { 90,  90, 100}, false, false, true,  0.0f,  0.5f},
    {"Chute",             { 70,  70,  80}, false, false, true,  0.0f,  0.5f},
    {"Smelter",           {200,  90,  70}, false, true,  false, 5.0f,  0.0f},
    {"Chest",             {140,  95,  50}, false, false, false, 0.0f,  0.0f},
}};
```

- [ ] **Step 4: Add `storage` to `Machine`**

In `src/Machines/Machine.h`, add the include and the field:

```cpp
#include "../Core/Direction.h"
#include "../Items/Inventory.h"
#include "../Items/Items.h"
#include "MachineType.h"
```

```cpp
    // Transport machines (belt, chute): one carried item and its move timer.
    ItemType carried = ItemType::None;
    float carryTimer = 0.0f; // counts down; the item advances when it reaches 0

    // Chest: a bank of slots. 0 slots (the default) for every other machine
    // type - Machines::place() sizes this to CHEST_SLOTS only for a Chest.
    Inventory storage{0};

    bool empty() const { return type == MachineType::None; }
```

- [ ] **Step 5: Size storage on placement**

In `src/Machines/Machines.cpp`, `Machines::place()`:

```cpp
    Machine m;
    m.type = type;
    m.x = x;
    m.y = y;
    m.facing = facing;
    m.outputCursor = facing;

    machines.push_back(m);
```

Change to:

```cpp
    Machine m;
    m.type = type;
    m.x = x;
    m.y = y;
    m.facing = facing;
    m.outputCursor = facing;

    if (type == MachineType::Chest)
        m.storage = Inventory(CHEST_SLOTS);

    machines.push_back(m);
```

- [ ] **Step 6: Handle `Chest` in `tryInsert`, `tryExtract`, `putBack`**

In `src/Machines/Machines.cpp`, `Machines::tryInsert`, add a branch right after the `info.transport` block and before the `Smelter` check:

```cpp
    if (info.transport)
    {
        if (m->carried != ItemType::None)
            return false;

        m->carried = item;
        m->carryTimer = info.actionTime;
        return true;
    }

    if (m->type == MachineType::Chest)
        return m->storage.add({item, 1}) == 0;

    if (m->type == MachineType::Smelter)
```

In `Machines::tryExtract`, add a branch right after the `info.transport` block and before the generic output-buffer fallback:

```cpp
    if (info.transport)
    {
        if (m->carried == ItemType::None || m->carryTimer > 0.0f)
            return {};

        const ItemStack taken{m->carried, 1};
        m->carried = ItemType::None;
        return taken;
    }

    if (m->type == MachineType::Chest)
    {
        for (int i = 0; i < m->storage.slotCount(); ++i)
        {
            if (!m->storage.slot(i).empty())
                return m->storage.take(i);
        }
        return {};
    }

    if (m->output.empty())
        return {};
```

In `Machines::putBack`, add a branch before the existing `info.transport` check:

```cpp
    Machine* m = at(x, y);
    if (m == nullptr)
        return;

    if (m->type == MachineType::Chest)
    {
        m->storage.add(stack);
        return;
    }

    const MachineInfo& info = machineInfo(m->type);
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Run: `build/Debug/Litharia_tests.exe`
Expected: all cases pass.

- [ ] **Step 8: Commit**

```bash
git add src/Machines/MachineType.h src/Machines/MachineRegistry.cpp src/Machines/Machine.h src/Machines/Machines.cpp tests/test_machines.cpp tests/test_transport.cpp tests/test_extraction.cpp
git commit -m "feat: add chests as 20-slot storage machines"
```

---

### Task 4: Block placement refused on machine tiles

**Files:**
- Modify: `src/Player/Player.h`
- Modify: `src/Player/Player.cpp`
- Modify: `src/Game/Game.cpp`
- Test: `tests/test_placing.cpp`

**Interfaces:**
- Consumes: `Machines::canPlace(int, int) const` (already exists).
- Produces: `Player::update(const PlayerInput&, World&, float, const Machines* machines = nullptr)`. `Game::fixedUpdate` now passes `&machines`.

- [ ] **Step 1: Write the failing tests**

At the top of `tests/test_placing.cpp`, add the include:

```cpp
#include "Machines/Machines.h"
```

Add these two cases:

```cpp
TEST_CASE("a block cannot be placed onto a tile a machine occupies")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);
    player.inventory().add({ItemType::Stone, 5});

    Machines machines;
    machines.place(MachineType::Belt, 13, 29, Direction::Right);

    const ActionResult result = player.update(placingAt(13, 29), world, STEP, &machines);

    CHECK_FALSE(result.placed);
    CHECK(world.get(13, 29) == BlockType::Air); // the tile itself was already empty
    CHECK(player.inventory().count(ItemType::Stone) == 5);
}

TEST_CASE("placing still works normally when no machines are passed in")
{
    World world;
    buildFloor(world, 30);

    Player player = standingAt(world, 10.0f, 30);
    player.inventory().add({ItemType::Stone, 1});

    const ActionResult result = player.update(placingAt(13, 29), world, STEP);

    CHECK(result.placed);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAILS to compile — `Player::update` doesn't accept a `Machines*` yet (the second new test compiles fine on its own, but the file won't build until the first one's call site is valid).

- [ ] **Step 3: Extend `Player`'s signature**

In `src/Player/Player.h`, add a forward declaration near the top:

```cpp
class World;
class Machines;
```

Change the `update` declaration:

```cpp
    ActionResult update(const PlayerInput& input, World& world, float dt);
```

to:

```cpp
    ActionResult update(const PlayerInput& input, World& world, float dt,
                         const Machines* machines = nullptr);
```

And the private `place` declaration:

```cpp
    void place(const PlayerInput& input, World& world, ActionResult& result);
```

to:

```cpp
    void place(const PlayerInput& input, World& world, const Machines* machines,
               ActionResult& result);
```

- [ ] **Step 4: Wire it through in `Player.cpp`**

In `src/Player/Player.cpp`, add the include:

```cpp
#include "../Core/Constants.h"
#include "../Machines/Machines.h"
#include "../World/World.h"
```

Change `Player::update`:

```cpp
ActionResult Player::update(const PlayerInput& input, World& world, float dt)
{
    ActionResult result;

    move(input, world, dt);

    mine(input, world, result, dt);
    place(input, world, result);

    return result;
}
```

to:

```cpp
ActionResult Player::update(const PlayerInput& input, World& world, float dt,
                             const Machines* machines)
{
    ActionResult result;

    move(input, world, dt);

    mine(input, world, result, dt);
    place(input, world, machines, result);

    return result;
}
```

Change `Player::place`'s signature and add the machine-occupancy check:

```cpp
void Player::place(const PlayerInput& input, World& world, ActionResult& result)
{
    if (!input.place)
        return;

    const ItemStack& held = bag.slot(selected);

    if (held.empty())
        return;

    const BlockType block = itemInfo(held.type).placeBlock;

    if (block == BlockType::Air)
        return;

    const int tileX = tileOf(input.cursor.x);
    const int tileY = tileOf(input.cursor.y);

    // Only into empty space, and only within reach.
    if (world.get(tileX, tileY) != BlockType::Air || !inReach(tileX, tileY))
        return;

    // A block may not be placed inside the player: it would trap them in a solid
    // tile, which the physics has no correct way to push out of.
    if (physics::overlaps(tileBox(tileX, tileY), body))
        return;

    world.set(tileX, tileY, block);

    // Placing is the only thing that removes from the inventory.
    bag.removeOne(selected);

    result.placed = true;
    result.placedX = tileX;
    result.placedY = tileY;
}
```

to:

```cpp
void Player::place(const PlayerInput& input, World& world, const Machines* machines,
                    ActionResult& result)
{
    if (!input.place)
        return;

    const ItemStack& held = bag.slot(selected);

    if (held.empty())
        return;

    const BlockType block = itemInfo(held.type).placeBlock;

    if (block == BlockType::Air)
        return;

    const int tileX = tileOf(input.cursor.x);
    const int tileY = tileOf(input.cursor.y);

    // Only into empty space, and only within reach.
    if (world.get(tileX, tileY) != BlockType::Air || !inReach(tileX, tileY))
        return;

    // Never onto a tile a piece of factory equipment already occupies.
    if (machines != nullptr && !machines->canPlace(tileX, tileY))
        return;

    // A block may not be placed inside the player: it would trap them in a solid
    // tile, which the physics has no correct way to push out of.
    if (physics::overlaps(tileBox(tileX, tileY), body))
        return;

    world.set(tileX, tileY, block);

    // Placing is the only thing that removes from the inventory.
    bag.removeOne(selected);

    result.placed = true;
    result.placedX = tileX;
    result.placedY = tileY;
}
```

- [ ] **Step 5: Pass `machines` from `Game`**

In `src/Game/Game.cpp`, `Game::fixedUpdate`:

```cpp
    const ActionResult result = player.update(readInput(), world, dt);
```

becomes:

```cpp
    const ActionResult result = player.update(readInput(), world, dt, &machines);
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Run: `build/Debug/Litharia_tests.exe`
Expected: all cases pass.

Also run: `cmake --build build --config Debug --target Litharia` to confirm the game executable still builds (it links `Player.cpp` and now `Machines.h` transitively — no new link requirement since `Machines.cpp` was already part of `Litharia_core`).

- [ ] **Step 7: Commit**

```bash
git add src/Player/Player.h src/Player/Player.cpp src/Game/Game.cpp tests/test_placing.cpp
git commit -m "fix: refuse block placement on tiles a machine occupies"
```

---

### Task 5: Pure `HudLayout` module and the tooltip anchoring fix

**Files:**
- Create: `src/Hud/HudLayout.h`
- Create: `src/Hud/HudLayout.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/Hud/Hud.h`
- Modify: `src/Hud/Hud.cpp`
- Modify: `src/Game/Game.cpp`
- Test: `tests/test_hud_layout.cpp` (new)

**Interfaces:**
- Consumes: `sf::Vector2f` (SFML System only).
- Produces: `hudLayout::tooltipTopLeft`, `hudLayout::gridSlotPosition`, `hudLayout::hitTestGrid` — pure functions with no window dependency, reused by Task 6 (build palette) and Task 8 (drag-and-drop hit-testing) so drawing and input hit-testing can never disagree about where a slot is on screen. Also produces the actual tooltip position fix, independent of the other two functions.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_hud_layout.cpp`:

```cpp
#include "doctest.h"

#include "Hud/HudLayout.h"

TEST_CASE("tooltipTopLeft centers the panel above the anchor with a margin")
{
    const sf::Vector2f anchor{100.0f, 200.0f};
    const sf::Vector2f panelSize{60.0f, 40.0f};

    const sf::Vector2f topLeft = hudLayout::tooltipTopLeft(anchor, panelSize, 10.0f);

    // Horizontally centered on the anchor...
    CHECK(topLeft.x == doctest::Approx(70.0f));
    // ...and its bottom edge sits `margin` above the anchor's y.
    CHECK(topLeft.y == doctest::Approx(150.0f));
    CHECK(topLeft.y + panelSize.y + 10.0f == doctest::Approx(anchor.y));
}

TEST_CASE("gridSlotPosition lays slots out row-major from the origin")
{
    const sf::Vector2f origin{10.0f, 20.0f};

    CHECK(hudLayout::gridSlotPosition(origin, 0, 10, 48.0f, 4.0f) == sf::Vector2f{10.0f, 20.0f});
    CHECK(hudLayout::gridSlotPosition(origin, 1, 10, 48.0f, 4.0f) == sf::Vector2f{62.0f, 20.0f});
    CHECK(hudLayout::gridSlotPosition(origin, 10, 10, 48.0f, 4.0f) == sf::Vector2f{10.0f, 72.0f});
    CHECK(hudLayout::gridSlotPosition(origin, 12, 10, 48.0f, 4.0f) == sf::Vector2f{114.0f, 72.0f});
}

TEST_CASE("hitTestGrid finds the slot under a point")
{
    const sf::Vector2f origin{0.0f, 0.0f};

    // Comfortably inside slot 0.
    CHECK(hudLayout::hitTestGrid({10.0f, 10.0f}, origin, 10, 3, 48.0f, 4.0f) == 0);

    // Comfortably inside slot 1 (second column, first row).
    CHECK(hudLayout::hitTestGrid({60.0f, 10.0f}, origin, 10, 3, 48.0f, 4.0f) == 1);

    // Second row, first column.
    CHECK(hudLayout::hitTestGrid({10.0f, 60.0f}, origin, 10, 3, 48.0f, 4.0f) == 10);
}

TEST_CASE("hitTestGrid rejects points outside every slot")
{
    const sf::Vector2f origin{0.0f, 0.0f};

    // Left/above the grid entirely.
    CHECK(hudLayout::hitTestGrid({-5.0f, 10.0f}, origin, 10, 3, 48.0f, 4.0f) == -1);
    CHECK(hudLayout::hitTestGrid({10.0f, -5.0f}, origin, 10, 3, 48.0f, 4.0f) == -1);

    // In the gap between two slots (slot is 48px, gap is 4px: x=49 is in the gap).
    CHECK(hudLayout::hitTestGrid({49.0f, 10.0f}, origin, 10, 3, 48.0f, 4.0f) == -1);

    // Past the last column/row.
    CHECK(hudLayout::hitTestGrid({10.0f, 500.0f}, origin, 10, 3, 48.0f, 4.0f) == -1);
    CHECK(hudLayout::hitTestGrid({5000.0f, 10.0f}, origin, 10, 3, 48.0f, 4.0f) == -1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target Litharia_tests`
Expected: FAILS — `Hud/HudLayout.h` doesn't exist, and the test isn't even in the CMake target yet.

- [ ] **Step 3: Create `HudLayout.h`**

Create `src/Hud/HudLayout.h`:

```cpp
#pragma once

#include <SFML/System/Vector2.hpp>

// Pure screen-space layout math for the HUD: no SFML Graphics or Window, so it
// lives in the core library and is unit-testable without a real render window.
// Hud.cpp (which does own a window) calls these so drawing and hit-testing can
// never drift out of sync with each other.
namespace hudLayout
{

// Where a panel's top-left corner should sit so its bottom edge is centered a
// fixed `verticalMargin` above `anchor` - used to anchor the machine tooltip
// above the machine's own tile instead of near the mouse.
sf::Vector2f tooltipTopLeft(sf::Vector2f anchor, sf::Vector2f panelSize, float verticalMargin);

// The top-left corner of grid slot `index` (0-based, filled row-major) inside
// a `columns`-wide grid whose first slot starts at `origin`.
sf::Vector2f gridSlotPosition(sf::Vector2f origin, int index, int columns, float slotSize,
                               float gap);

// Which slot of a `columns` x `rows` grid (first slot at `origin`) contains
// `point`, or -1 if `point` falls outside every slot, including the gaps
// between them.
int hitTestGrid(sf::Vector2f point, sf::Vector2f origin, int columns, int rows, float slotSize,
                 float gap);

} // namespace hudLayout
```

- [ ] **Step 4: Create `HudLayout.cpp`**

Create `src/Hud/HudLayout.cpp`:

```cpp
#include "HudLayout.h"

namespace hudLayout
{

sf::Vector2f tooltipTopLeft(sf::Vector2f anchor, sf::Vector2f panelSize, float verticalMargin)
{
    return {anchor.x - panelSize.x * 0.5f, anchor.y - panelSize.y - verticalMargin};
}

sf::Vector2f gridSlotPosition(sf::Vector2f origin, int index, int columns, float slotSize,
                               float gap)
{
    const int col = index % columns;
    const int row = index / columns;
    const float pitch = slotSize + gap;

    return {origin.x + static_cast<float>(col) * pitch, origin.y + static_cast<float>(row) * pitch};
}

int hitTestGrid(sf::Vector2f point, sf::Vector2f origin, int columns, int rows, float slotSize,
                 float gap)
{
    const sf::Vector2f local = point - origin;

    if (local.x < 0.0f || local.y < 0.0f)
        return -1;

    const float pitch = slotSize + gap;

    const int col = static_cast<int>(local.x / pitch);
    const int row = static_cast<int>(local.y / pitch);

    if (col < 0 || col >= columns || row < 0 || row >= rows)
        return -1;

    // Reject a point that landed in the gap between slots, not on a slot itself.
    const float withinCol = local.x - static_cast<float>(col) * pitch;
    const float withinRow = local.y - static_cast<float>(row) * pitch;

    if (withinCol > slotSize || withinRow > slotSize)
        return -1;

    return row * columns + col;
}

} // namespace hudLayout
```

- [ ] **Step 5: Wire the new files into CMake**

In `CMakeLists.txt`, add `HudLayout.cpp` to `Litharia_core` (it has no Graphics/Window dependency, so it belongs in the core lib alongside `Machines.cpp` etc.):

```cmake
add_library(Litharia_core STATIC
    src/Blocks/Blocks.cpp

    src/Core/Noise.cpp
    src/Core/Direction.cpp

    src/World/World.cpp
    src/World/TerrainGenerator.cpp

    src/Physics/Physics.cpp
    src/Player/Player.cpp

    src/Items/Items.cpp
    src/Items/ItemEntity.cpp
    src/Items/Inventory.cpp

    src/Machines/Recipes.cpp
    src/Machines/MachineRegistry.cpp
    src/Machines/Machines.cpp
    src/Machines/MachineStatus.cpp

    src/Hud/HudLayout.cpp
)
```

Add `test_hud_layout.cpp` to `Litharia_tests`:

```cmake
add_executable(Litharia_tests
    tests/test_main.cpp
    tests/test_machines.cpp
    tests/test_world.cpp
    tests/test_terrain.cpp
    tests/test_physics.cpp
    tests/test_player.cpp
    tests/test_spawn.cpp
    tests/test_mining.cpp
    tests/test_inventory.cpp
    tests/test_pickup.cpp
    tests/test_placing.cpp
    tests/test_recipes.cpp
    tests/test_power.cpp
    tests/test_transport.cpp
    tests/test_processing.cpp
    tests/test_factory.cpp
    tests/test_machine_status.cpp
    tests/test_machine_inspect.cpp
    tests/test_extraction.cpp
    tests/test_hud_layout.cpp
)
```

Re-run CMake configure: `cmake -S . -B build`

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target Litharia_tests`
Run: `build/Debug/Litharia_tests.exe`
Expected: all cases pass, including the new `HudLayout` ones.

- [ ] **Step 7: Fix the tooltip's anchor point**

In `src/Hud/Hud.h`, promote the layout constants from `Hud.cpp`'s anonymous namespace to public members (Task 6 and Task 7/8 also need them), by adding near the top of the class:

```cpp
class Hud
{
public:
    static constexpr float SLOT_SIZE = 48.0f;
    static constexpr float SLOT_GAP = 4.0f;
    static constexpr float MARGIN = 12.0f;

    Hud();
```

In `src/Hud/Hud.cpp`, remove the now-duplicate constants from the anonymous namespace:

```cpp
namespace
{

constexpr float SLOT_SIZE = 48.0f;
constexpr float SLOT_GAP = 4.0f;
constexpr float MARGIN = 12.0f;

constexpr float ICON_INSET = 10.0f;
```

becomes:

```cpp
namespace
{

constexpr float ICON_INSET = 10.0f;
```

(the rest of `Hud.cpp` referencing `SLOT_SIZE`/`SLOT_GAP`/`MARGIN` still compiles unchanged, since those are now `Hud::SLOT_SIZE` etc., visible unqualified inside member functions).

Add the include at the top of `Hud.cpp`:

```cpp
#include "Hud.h"

#include "HudLayout.h"

#include <algorithm>
```

Then, in `Hud::drawMachineTooltip`, replace:

```cpp
    const sf::Vector2f pos = screenPos + sf::Vector2f(16.0f, 16.0f);

    sf::RectangleShape panel({width, height});
    panel.setPosition(pos);
```

with:

```cpp
    constexpr float TOOLTIP_MARGIN = 10.0f;
    const sf::Vector2f pos = hudLayout::tooltipTopLeft(screenPos, {width, height}, TOOLTIP_MARGIN);

    sf::RectangleShape panel({width, height});
    panel.setPosition(pos);
```

(`screenPos` is now expected to be the machine's own anchor point, not the raw mouse position - fixed at the call site next.)

- [ ] **Step 8: Anchor `drawMachineTooltip`'s caller to the machine's tile**

In `src/Game/Game.cpp`, `Game::drawMachineTooltip`:

```cpp
void Game::drawMachineTooltip()
{
    const sf::Vector2i tile = cursorTile();
    const Machine* machine = machines.at(tile.x, tile.y);

    if (machine == nullptr)
        return;

    const MachineStatus status = machines.inspect(tile.x, tile.y, world);
    const sf::Vector2f screenPos(sf::Mouse::getPosition(window));

    hud.drawMachineTooltip(window, *machine, status, screenPos);
}
```

becomes:

```cpp
void Game::drawMachineTooltip()
{
    const sf::Vector2i tile = cursorTile();
    const Machine* machine = machines.at(tile.x, tile.y);

    if (machine == nullptr)
        return;

    const MachineStatus status = machines.inspect(tile.x, tile.y, world);

    // Anchor to the machine's own tile in screen space, not the raw mouse
    // position, so the tooltip tracks the machine (and the camera) instead of
    // drifting toward wherever the cursor happens to be.
    const sf::Vector2f tileTopCenter{(tile.x + 0.5f) * TILE_SIZE,
                                     static_cast<float>(tile.y * TILE_SIZE)};
    const sf::Vector2f screenPos(window.mapCoordsToPixel(tileTopCenter, camera.view()));

    hud.drawMachineTooltip(window, *machine, status, screenPos);
}
```

- [ ] **Step 9: Run tests, then build the game**

Run: `cmake --build build --config Debug`
Expected: both `Litharia_tests` and `Litharia` build cleanly.
Run: `build/Debug/Litharia_tests.exe`
Expected: all cases pass (this step doesn't touch anything the test binary exercises, but confirms nothing else broke).

- [ ] **Step 10: Manually verify the tooltip**

Use the project's `run` skill to launch `build/Debug/Litharia.exe`. Hover the cursor over any placed machine (place one in build mode first if none exist) from several different positions - directly below it, far to its right, etc. Confirm the tooltip panel appears centered above the machine's tile and stays anchored there (not drifting toward the cursor) as you move the mouse around while still hovering the same tile, and as the camera scrolls (walk the player away and back).

- [ ] **Step 11: Commit**

```bash
git add src/Hud/HudLayout.h src/Hud/HudLayout.cpp src/Hud/Hud.h src/Hud/Hud.cpp src/Game/Game.cpp CMakeLists.txt tests/test_hud_layout.cpp
git commit -m "fix: anchor the machine tooltip above its tile instead of the mouse"
```

---

### Task 6: Build-mode palette

**Files:**
- Modify: `src/Game/Game.h`
- Modify: `src/Game/Game.cpp`
- Modify: `src/Hud/Hud.h`
- Modify: `src/Hud/Hud.cpp`

**Interfaces:**
- Consumes: `Hud::SLOT_SIZE`/`MARGIN` (Task 5), `machineInfo(MachineType)`.
- Produces: `Game::cycleBuildType(int delta)`; `Hud::drawBuildPalette(sf::RenderWindow&, MachineType selected)`. No other task depends on these.

- [ ] **Step 1: Add `cycleBuildType` to `Game`**

In `src/Game/Game.h`, add a private method declaration next to the other build-mode helpers:

```cpp
    void placeMachineAtCursor();
    void removeMachineAtCursor();
    void cycleBuildType(int delta);
```

In `src/Game/Game.cpp`, add the implementation (anywhere among the other `Game::` free functions, e.g. right after `removeMachineAtCursor`):

```cpp
void Game::cycleBuildType(int delta)
{
    constexpr int first = 1; // skip MachineType::None
    const int count = static_cast<int>(MachineType::Count) - first;

    int index = static_cast<int>(buildType) - first;
    index = ((index + delta) % count + count) % count;

    buildType = static_cast<MachineType>(first + index);
}
```

- [ ] **Step 2: Repurpose the mouse wheel in build mode**

In `src/Game/Game.cpp`, `Game::handleEvents`:

```cpp
        else if (const auto* scroll = event->getIf<sf::Event::MouseWheelScrolled>())
        {
            // Scroll up moves toward slot 1, scroll down toward slot 0.
            player.cycleSelectedSlot(scroll->delta > 0.0f ? -1 : 1);
        }
```

becomes:

```cpp
        else if (const auto* scroll = event->getIf<sf::Event::MouseWheelScrolled>())
        {
            if (buildMode)
                cycleBuildType(scroll->delta > 0.0f ? -1 : 1);
            else
                // Scroll up moves toward slot 1, scroll down toward slot 0.
                player.cycleSelectedSlot(scroll->delta > 0.0f ? -1 : 1);
        }
```

- [ ] **Step 3: Draw the palette**

In `src/Hud/Hud.h`, add the method declaration:

```cpp
    void drawMachineTooltip(sf::RenderWindow& window,
                            const Machine& machine,
                            const MachineStatus& status,
                            sf::Vector2f screenPos);

    // The build-mode picker: a strip of machine-type swatches centered on
    // `selected`, plus a "left click: place / right click: destroy" caption.
    void drawBuildPalette(sf::RenderWindow& window, MachineType selected);
```

In `src/Hud/Hud.cpp`, add the implementation after `Hud::draw`:

```cpp
void Hud::drawBuildPalette(sf::RenderWindow& window, MachineType selected)
{
    const sf::View previous = window.getView();
    window.setView(window.getDefaultView());

    constexpr int VISIBLE = 5;
    constexpr float SWATCH = 40.0f;
    constexpr float GAP = 6.0f;
    constexpr int FIRST = 1; // skip MachineType::None

    const int total = static_cast<int>(MachineType::Count) - FIRST;
    const int selectedIndex = static_cast<int>(selected) - FIRST;
    const int half = VISIBLE / 2;

    const float totalWidth = VISIBLE * SWATCH + (VISIBLE - 1) * GAP;
    const sf::Vector2f windowSize = window.getDefaultView().getSize();
    const float startX = (windowSize.x - totalWidth) * 0.5f;
    const float y = windowSize.y - SLOT_SIZE - MARGIN - SWATCH - MARGIN * 2.0f;

    for (int slot = 0; slot < VISIBLE; ++slot)
    {
        const int index = ((selectedIndex + slot - half) % total + total) % total;
        const MachineType type = static_cast<MachineType>(FIRST + index);
        const MachineInfo& info = machineInfo(type);
        const bool isSelected = (slot == half);

        sf::RectangleShape swatch({SWATCH, SWATCH});
        swatch.setPosition({startX + slot * (SWATCH + GAP), y});
        swatch.setFillColor(toColor(info.color));
        swatch.setOutlineThickness(isSelected ? -3.0f : -1.0f);
        swatch.setOutlineColor(isSelected ? sf::Color(255, 236, 140) : sf::Color(90, 90, 105));
        window.draw(swatch);
    }

    if (!font)
    {
        window.setView(previous);
        return;
    }

    sf::Text name(*font, std::string(machineInfo(selected).name), 16);
    const sf::FloatRect nameBounds = name.getLocalBounds();
    name.setFillColor(sf::Color::White);
    name.setPosition({(windowSize.x - nameBounds.size.x) * 0.5f, y - 22.0f});
    window.draw(name);

    sf::Text hint(*font, "Left click: place    Right click: destroy", 14);
    const sf::FloatRect hintBounds = hint.getLocalBounds();
    hint.setFillColor(sf::Color(220, 220, 220));
    hint.setPosition({(windowSize.x - hintBounds.size.x) * 0.5f, y + SWATCH + 6.0f});
    window.draw(hint);

    window.setView(previous);
}
```

- [ ] **Step 4: Call it from `Game::render`**

In `src/Game/Game.cpp`, `Game::render`, right after `hud.draw(window, player.inventory(), player.selectedSlot());`:

```cpp
    hud.draw(window, player.inventory(), player.selectedSlot());
    drawMachineTooltip();
```

becomes:

```cpp
    hud.draw(window, player.inventory(), player.selectedSlot());

    if (buildMode)
        hud.drawBuildPalette(window, buildType);

    drawMachineTooltip();
```

- [ ] **Step 5: Build and manually verify**

Run: `cmake --build build --config Debug`
Run: `build/Debug/Litharia_tests.exe` (expected: unaffected, still all green - this task has no core-lib changes)

Use the `run` skill to launch `build/Debug/Litharia.exe`. Press `B` to enter build mode: confirm the palette strip appears above the hotbar, the middle swatch is highlighted and matches the machine you're currently placing (check the window title bar, which already shows `BUILD: <name>`), and that scrolling the mouse wheel cycles the highlighted swatch through all 7 machine types (including wrapping from the last back to the first). Confirm the "Left click: place / Right click: destroy" caption is visible and correct by actually placing and destroying a machine.

- [ ] **Step 6: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "feat: add a build-mode machine palette with wheel scrolling"
```

---

### Task 7: E-key bag panel

**Files:**
- Modify: `src/Game/Game.h`
- Modify: `src/Game/Game.cpp`
- Modify: `src/Hud/Hud.h`
- Modify: `src/Hud/Hud.cpp`

**Interfaces:**
- Consumes: `Inventory::HOTBAR_SIZE`, `Inventory::slotCount()`, `Inventory::slot()`, `hudLayout::gridSlotPosition` (Task 5).
- Produces: `Game::inventoryOpen`, `Game::openChestTile` (`std::optional<sf::Vector2i>`, always `nullopt` until Task 8 exists — the chest branch of `toggleInventory` is written now so Task 8 only has to add the chest *panel drawing/dragging*, not this state machine); `Hud::drawInventoryPanel(sf::RenderWindow&, const Inventory&)`. Task 8 adds `drawChestPanel`/`hitTestPanels`/drag-and-drop alongside this.

- [ ] **Step 1: Add panel state to `Game`**

In `src/Game/Game.h`, add the include:

```cpp
#include <optional>
#include <vector>
```

Add the method declarations near the other input/interaction helpers:

```cpp
    void interactAtCursor();
    void toggleInventory();
    sf::Vector2i cursorTile() const;
```

Add the state near `buildMode`:

```cpp
    bool buildMode = false;
    MachineType buildType = MachineType::Belt;
    Direction buildFacing = Direction::Right;

    bool inventoryOpen = false;
    std::optional<sf::Vector2i> openChestTile;
```

- [ ] **Step 2: Implement `toggleInventory` and gate build mode/mining against it**

In `src/Game/Game.cpp`, add the implementation (near `interactAtCursor`):

```cpp
void Game::toggleInventory()
{
    if (inventoryOpen)
    {
        inventoryOpen = false;
        openChestTile.reset();
        return;
    }

    const sf::Vector2i tile = cursorTile();
    const Machine* machine = machines.at(tile.x, tile.y);

    if (machine != nullptr && machine->type == MachineType::Chest)
        openChestTile = tile;
    else
        openChestTile.reset();

    inventoryOpen = true;
    buildMode = false;
}
```

In `Game::handleEvents`, add the `E` key next to the existing `F` handler:

```cpp
            if (key->code == Key::F)
                interactAtCursor();

            if (key->code == Key::E)
                toggleInventory();
```

Make `B` and inventory mutually exclusive:

```cpp
            if (key->code == Key::B)
                buildMode = !buildMode;
```

becomes:

```cpp
            if (key->code == Key::B)
            {
                buildMode = !buildMode;
                if (buildMode)
                {
                    inventoryOpen = false;
                    openChestTile.reset();
                }
            }
```

In `Game::readInput`, extend the mine/place gating:

```cpp
    input.mine = !buildMode && focused && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    input.place = !buildMode && focused && sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);
```

becomes:

```cpp
    input.mine = !buildMode && !inventoryOpen && focused
        && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    input.place = !buildMode && !inventoryOpen && focused
        && sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);
```

- [ ] **Step 3: Draw the bag panel**

In `src/Hud/Hud.h`, add the method declaration and the `#include <vector>`-free forward declare stays as-is (`class Inventory;` already present). Add:

```cpp
    void draw(sf::RenderWindow& window, const Inventory& inventory, int selectedSlot);

    // The 3 rows of the bag beyond the hotbar (slots HOTBAR_SIZE..slotCount()-1),
    // shown only while the player has the inventory open.
    void drawInventoryPanel(sf::RenderWindow& window, const Inventory& inventory);
```

In `src/Hud/Hud.cpp`, factor the existing per-slot drawing out of `Hud::draw` into a shared helper, then add `drawInventoryPanel`. First, add this to the anonymous namespace (after `itemColor`):

```cpp
// Draws one slot's background, item icon, and stack count - shared by the
// hotbar and the bag/chest panels so they render identically.
void drawSlot(sf::RenderWindow& window, const std::optional<sf::Font>& font, sf::Vector2f pos,
              const ItemStack& stack, bool highlighted)
{
    sf::RectangleShape slot({Hud::SLOT_SIZE, Hud::SLOT_SIZE});
    slot.setPosition(pos);
    slot.setFillColor(sf::Color(20, 20, 28, 170));
    slot.setOutlineThickness(highlighted ? -3.0f : -1.0f);
    slot.setOutlineColor(highlighted ? sf::Color(255, 236, 140) : sf::Color(90, 90, 105));
    window.draw(slot);

    if (stack.empty())
        return;

    sf::RectangleShape icon({Hud::SLOT_SIZE - ICON_INSET * 2.0f, Hud::SLOT_SIZE - ICON_INSET * 2.0f});
    icon.setPosition({pos.x + ICON_INSET, pos.y + ICON_INSET});
    icon.setFillColor(itemColor(stack.type));
    icon.setOutlineThickness(-1.0f);
    icon.setOutlineColor(sf::Color(20, 16, 14));
    window.draw(icon);

    if (!font)
        return;

    sf::Text count(*font, std::to_string(stack.count), 14);
    count.setFillColor(sf::Color::White);
    count.setOutlineThickness(2.0f);
    count.setOutlineColor(sf::Color(10, 10, 12));

    const sf::FloatRect bounds = count.getLocalBounds();
    count.setPosition({pos.x + Hud::SLOT_SIZE - bounds.size.x - 5.0f,
                       pos.y + Hud::SLOT_SIZE - bounds.size.y - 10.0f});
    window.draw(count);
}

// Where the bag panel's first slot sits: directly above the hotbar, sharing
// its horizontal centering.
sf::Vector2f bagPanelOrigin(sf::Vector2f windowSize)
{
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;
    constexpr int BAG_ROWS = 3;

    const float totalWidth = COLUMNS * Hud::SLOT_SIZE + (COLUMNS - 1) * Hud::SLOT_GAP;
    const float startX = (windowSize.x - totalWidth) * 0.5f;

    const float hotbarY = windowSize.y - Hud::SLOT_SIZE - Hud::MARGIN;
    const float bagY =
        hotbarY - Hud::MARGIN - BAG_ROWS * Hud::SLOT_SIZE - (BAG_ROWS - 1) * Hud::SLOT_GAP;

    return {startX, bagY};
}
```

Now replace the body of `Hud::draw` (everything from `for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i)` down to that loop's closing brace) with a call to `drawSlot`:

```cpp
    for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i)
    {
        const float x = startX + i * (SLOT_SIZE + SLOT_GAP);
        drawSlot(window, font, {x, y}, inventory.slot(i), i == selectedSlot);
    }
```

(This replaces the old loop body that separately built the slot rectangle, icon rectangle, and count text inline - `drawSlot` now does all of that. The `x`/`y`/`startX`/`totalWidth` computation above the loop is unchanged.)

Add the new method after `Hud::draw`:

```cpp
void Hud::drawInventoryPanel(sf::RenderWindow& window, const Inventory& inventory)
{
    const sf::View previous = window.getView();
    window.setView(window.getDefaultView());

    const sf::Vector2f origin = bagPanelOrigin(window.getDefaultView().getSize());
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;

    for (int i = Inventory::HOTBAR_SIZE; i < inventory.slotCount(); ++i)
    {
        const int gridIndex = i - Inventory::HOTBAR_SIZE;
        const sf::Vector2f pos =
            hudLayout::gridSlotPosition(origin, gridIndex, COLUMNS, SLOT_SIZE, SLOT_GAP);
        drawSlot(window, font, pos, inventory.slot(i), false);
    }

    window.setView(previous);
}
```

Add the needed include for `Inventory` (currently only forward-declared) at the top of `Hud.cpp`, since `drawInventoryPanel` calls `inventory.slotCount()`/`inventory.slot()`:

```cpp
#include "../Blocks/Blocks.h"
#include "../Items/Inventory.h"
#include "../Machines/MachineType.h"
```

- [ ] **Step 4: Call it from `Game::render`**

In `src/Game/Game.cpp`, `Game::render`, after the build-palette call added in Task 6:

```cpp
    if (buildMode)
        hud.drawBuildPalette(window, buildType);

    drawMachineTooltip();
```

becomes:

```cpp
    if (buildMode)
        hud.drawBuildPalette(window, buildType);

    if (inventoryOpen)
        hud.drawInventoryPanel(window, player.inventory());

    drawMachineTooltip();
```

- [ ] **Step 5: Build and manually verify**

Run: `cmake --build build --config Debug`
Run: `build/Debug/Litharia_tests.exe` (expected: unaffected, still all green)

Use the `run` skill to launch `build/Debug/Litharia.exe`. Pick up a handful of different items (mine some blocks) so more than 10 items are in the bag. Press `E`: confirm a 3-row panel appears above the hotbar showing the overflow items, and the hotbar itself is still visible and unchanged below it. Press `E` again: confirm it closes. Confirm mining/placing with left/right click does nothing while the panel is open (per the new gating), and works again once it's closed. Press `B` while the panel is open and confirm it closes automatically.

- [ ] **Step 6: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "feat: open a bag panel with E showing the inventory beyond the hotbar"
```

---

### Task 8: Chest panel and drag-and-drop

**Files:**
- Modify: `src/Game/Game.h`
- Modify: `src/Game/Game.cpp`
- Modify: `src/Hud/Hud.h`
- Modify: `src/Hud/Hud.cpp`

**Interfaces:**
- Consumes: `Inventory::take`/`exchange`/`slotCount` (Task 2), `Machine::storage` (Task 3), `hudLayout::hitTestGrid`/`gridSlotPosition` (Task 5), `Hud::drawInventoryPanel` and the `inventoryOpen`/`openChestTile` state machine (Task 7).
- Produces: full drag-and-drop between the bag panel and an open chest's panel. Nothing else depends on this — it's the last task.

- [ ] **Step 1: Add drag state to `Game`**

In `src/Game/Game.h`, add near the other private types/state:

```cpp
    enum class InventoryPanel { Bag, Chest };

    void toggleInventory();
    void beginDrag();
    void endDrag();
    void drawInventoryPanels();
```

```cpp
    bool inventoryOpen = false;
    std::optional<sf::Vector2i> openChestTile;

    bool dragging = false;
    ItemStack dragStack;
    InventoryPanel dragSourcePanel = InventoryPanel::Bag;
    int dragSourceSlot = -1;
```

- [ ] **Step 2: Guard chest/bag identity against being closed mid-drag**

In `src/Game/Game.cpp`, `Game::toggleInventory`, refuse to close while dragging (dropping the panel out from under an in-flight drag would strand the dragged stack):

```cpp
void Game::toggleInventory()
{
    if (dragging)
        return;

    if (inventoryOpen)
```

Similarly guard the `B` handler in `Game::handleEvents`:

```cpp
            if (key->code == Key::B)
            {
                buildMode = !buildMode;
                if (buildMode)
                {
                    inventoryOpen = false;
                    openChestTile.reset();
                }
            }
```

becomes:

```cpp
            if (key->code == Key::B && !dragging)
            {
                buildMode = !buildMode;
                if (buildMode)
                {
                    inventoryOpen = false;
                    openChestTile.reset();
                }
            }
```

- [ ] **Step 3: Wire up mouse down/up while a panel is open**

In `src/Game/Game.cpp`, `Game::handleEvents`, extend the `MouseButtonPressed` branch and add a `MouseButtonReleased` branch:

```cpp
        else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
        {
            if (buildMode && mouse->button == sf::Mouse::Button::Left)
                placeMachineAtCursor();
            else if (buildMode && mouse->button == sf::Mouse::Button::Right)
                removeMachineAtCursor();
        }
```

becomes:

```cpp
        else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
        {
            if (buildMode && mouse->button == sf::Mouse::Button::Left)
                placeMachineAtCursor();
            else if (buildMode && mouse->button == sf::Mouse::Button::Right)
                removeMachineAtCursor();
            else if (inventoryOpen && mouse->button == sf::Mouse::Button::Left)
                beginDrag();
        }
        else if (const auto* release = event->getIf<sf::Event::MouseButtonReleased>())
        {
            if (dragging && release->button == sf::Mouse::Button::Left)
                endDrag();
        }
```

- [ ] **Step 4: Implement `beginDrag`/`endDrag`/`drawInventoryPanels`**

In `src/Game/Game.cpp`, replace the direct `hud.drawInventoryPanel(window, player.inventory());` call added in Task 7 with a call to a new `drawInventoryPanels()` (this also re-validates `openChestTile` every frame, per the spec's guard against a stale tile):

```cpp
    if (inventoryOpen)
        hud.drawInventoryPanel(window, player.inventory());
```

becomes:

```cpp
    if (inventoryOpen)
        drawInventoryPanels();

    if (dragging)
        hud.drawDragGhost(window, dragStack, sf::Vector2f(sf::Mouse::getPosition(window)));
```

Add the three new methods (near `toggleInventory`):

```cpp
void Game::drawInventoryPanels()
{
    if (openChestTile.has_value())
    {
        const Machine* chest = machines.at(openChestTile->x, openChestTile->y);

        // The chest might have vanished by other means while the panel was
        // open; fall back to the bag-only view rather than touch a stale tile.
        if (chest == nullptr || chest->type != MachineType::Chest)
            openChestTile.reset();
    }

    hud.drawInventoryPanel(window, player.inventory());

    if (openChestTile.has_value())
        hud.drawChestPanel(window, machines.at(openChestTile->x, openChestTile->y)->storage);
}

void Game::beginDrag()
{
    const sf::Vector2f screenPos(sf::Mouse::getPosition(window));
    const sf::Vector2f windowSize(window.getSize());

    const auto hit = hud.hitTestPanels(screenPos, windowSize, openChestTile.has_value());
    if (!hit.has_value())
        return;

    Inventory& source = hit->isChest ? machines.at(openChestTile->x, openChestTile->y)->storage
                                      : player.inventory();

    const ItemStack taken = source.take(hit->index);
    if (taken.empty())
        return;

    dragging = true;
    dragStack = taken;
    dragSourcePanel = hit->isChest ? InventoryPanel::Chest : InventoryPanel::Bag;
    dragSourceSlot = hit->index;
}

void Game::endDrag()
{
    const sf::Vector2f screenPos(sf::Mouse::getPosition(window));
    const sf::Vector2f windowSize(window.getSize());

    Inventory& sourceInventory = (dragSourcePanel == InventoryPanel::Chest)
        ? machines.at(openChestTile->x, openChestTile->y)->storage
        : player.inventory();

    const auto hit = hud.hitTestPanels(screenPos, windowSize, openChestTile.has_value());

    if (!hit.has_value())
    {
        // Released outside any slot: put it back where it came from.
        sourceInventory.exchange(dragSourceSlot, dragStack);
        dragging = false;
        return;
    }

    Inventory& destInventory = hit->isChest ? machines.at(openChestTile->x, openChestTile->y)->storage
                                             : player.inventory();

    const ItemStack leftover = destInventory.exchange(hit->index, dragStack);

    if (!leftover.empty())
        sourceInventory.exchange(dragSourceSlot, leftover);

    dragging = false;
}
```

Add the `#include <optional>` and `Inventory`/`ItemStack` visibility: `Game.h` already includes `Machines.h` (which pulls in `Machine.h` -> `Items.h`), so `ItemStack` is visible; add `#include "../Items/Inventory.h"` to `Game.h` for the `Inventory&` references above (it was previously only used opaquely via `player.inventory()`'s return type).

- [ ] **Step 5: Draw the chest panel, the drag ghost, and hit-test both**

In `src/Hud/Hud.h`, add:

```cpp
    #include <optional>
```

is already present via the existing `std::optional<sf::Font> font;` member - no new include needed. Add the type and three methods:

```cpp
    void drawInventoryPanel(sf::RenderWindow& window, const Inventory& inventory);

    // A chest's own 20 slots, shown alongside the bag panel while a chest is open.
    void drawChestPanel(sf::RenderWindow& window, const Inventory& chestStorage);

    // The stack currently being dragged, drawn centered on the live cursor.
    void drawDragGhost(sf::RenderWindow& window, const ItemStack& stack, sf::Vector2f screenPos);

    struct SlotHit
    {
        bool isChest = false; // false: the player's bag; true: the open chest
        int index = -1;       // index into that Inventory
    };

    // Screen position -> which open panel/slot it lands on, or nullopt if
    // neither. `chestOpen` must match whether drawChestPanel was actually
    // called this frame, so hit-testing and drawing never disagree.
    std::optional<SlotHit> hitTestPanels(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                          bool chestOpen) const;
```

In `src/Hud/Hud.cpp`, add `#include "HudLayout.h"` if Task 5 didn't already add it (it did - skip if present). Extend the `bagPanelOrigin` helper from Task 7 into a two-panel version, and add the chest row count:

```cpp
// Where the bag panel's first slot sits: directly above the hotbar, sharing
// its horizontal centering.
sf::Vector2f bagPanelOrigin(sf::Vector2f windowSize)
{
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;
    constexpr int BAG_ROWS = 3;

    const float totalWidth = COLUMNS * Hud::SLOT_SIZE + (COLUMNS - 1) * Hud::SLOT_GAP;
    const float startX = (windowSize.x - totalWidth) * 0.5f;

    const float hotbarY = windowSize.y - Hud::SLOT_SIZE - Hud::MARGIN;
    const float bagY =
        hotbarY - Hud::MARGIN - BAG_ROWS * Hud::SLOT_SIZE - (BAG_ROWS - 1) * Hud::SLOT_GAP;

    return {startX, bagY};
}

// Where the chest panel's first slot sits: directly above the bag panel.
sf::Vector2f chestPanelOrigin(sf::Vector2f windowSize)
{
    constexpr int CHEST_ROWS = 2;

    const sf::Vector2f bagOrigin = bagPanelOrigin(windowSize);
    const float chestY =
        bagOrigin.y - Hud::MARGIN - CHEST_ROWS * Hud::SLOT_SIZE - (CHEST_ROWS - 1) * Hud::SLOT_GAP;

    return {bagOrigin.x, chestY};
}
```

Add the new methods after `Hud::drawInventoryPanel`:

```cpp
void Hud::drawChestPanel(sf::RenderWindow& window, const Inventory& chestStorage)
{
    const sf::View previous = window.getView();
    window.setView(window.getDefaultView());

    const sf::Vector2f origin = chestPanelOrigin(window.getDefaultView().getSize());
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;

    for (int i = 0; i < chestStorage.slotCount(); ++i)
    {
        const sf::Vector2f pos = hudLayout::gridSlotPosition(origin, i, COLUMNS, SLOT_SIZE, SLOT_GAP);
        drawSlot(window, font, pos, chestStorage.slot(i), false);
    }

    window.setView(previous);
}

void Hud::drawDragGhost(sf::RenderWindow& window, const ItemStack& stack, sf::Vector2f screenPos)
{
    if (stack.empty())
        return;

    const sf::View previous = window.getView();
    window.setView(window.getDefaultView());

    constexpr float SIZE = SLOT_SIZE - ICON_INSET * 2.0f;

    sf::RectangleShape icon({SIZE, SIZE});
    icon.setPosition(screenPos - sf::Vector2f{SIZE * 0.5f, SIZE * 0.5f});
    icon.setFillColor(itemColor(stack.type));
    icon.setOutlineThickness(-1.0f);
    icon.setOutlineColor(sf::Color(240, 240, 240));
    window.draw(icon);

    if (font)
    {
        sf::Text count(*font, std::to_string(stack.count), 14);
        count.setFillColor(sf::Color::White);
        count.setOutlineThickness(2.0f);
        count.setOutlineColor(sf::Color(10, 10, 12));
        count.setPosition(screenPos + sf::Vector2f{8.0f, 8.0f});
        window.draw(count);
    }

    window.setView(previous);
}

std::optional<Hud::SlotHit> Hud::hitTestPanels(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                                bool chestOpen) const
{
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;
    constexpr int BAG_ROWS = 3;
    constexpr int CHEST_ROWS = 2;

    if (chestOpen)
    {
        const int chestIndex = hudLayout::hitTestGrid(screenPos, chestPanelOrigin(windowSize),
                                                        COLUMNS, CHEST_ROWS, SLOT_SIZE, SLOT_GAP);
        if (chestIndex >= 0)
            return SlotHit{true, chestIndex};
    }

    const int bagGridIndex = hudLayout::hitTestGrid(screenPos, bagPanelOrigin(windowSize), COLUMNS,
                                                      BAG_ROWS, SLOT_SIZE, SLOT_GAP);
    if (bagGridIndex >= 0)
        return SlotHit{false, Inventory::HOTBAR_SIZE + bagGridIndex};

    return std::nullopt;
}
```

`ICON_INSET` is already visible (anonymous-namespace constant in the same translation unit).

- [ ] **Step 6: Build and manually verify**

Run: `cmake --build build --config Debug`
Run: `build/Debug/Litharia_tests.exe` (expected: unaffected, still all green - this task has no core-lib changes)

Use the `run` skill to launch `build/Debug/Litharia.exe`. Enter build mode, place a chest, exit build mode, pick up a stack of some block. Hover the chest and press `E`: confirm both the chest panel (2 rows) and the bag panel (3 rows) are visible, stacked above the hotbar. Click-and-drag an item from the bag panel into an empty chest slot; release: confirm it moved (left the bag slot, appeared in the chest). Drag it back the other way. Drag a stack onto a slot already holding the same item type: confirm it merges (or leaves the correct overflow behind if it doesn't fully fit). Drag a stack onto a slot holding a *different* item: confirm the two swap. Start a drag, then release the mouse somewhere outside any slot (e.g. over the world): confirm the stack returns to its original slot rather than disappearing. Close the panel with `E`, walk away from the chest, reopen `E` over open ground: confirm it shows the bag-only view with no chest panel.

- [ ] **Step 7: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "feat: add a chest panel with click-and-drag item transfer"
```
