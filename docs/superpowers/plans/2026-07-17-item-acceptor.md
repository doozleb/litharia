# Item Acceptor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a new build-mode machine, the Item Acceptor (a 10-slot, all-4-sides-open storage box belts/chutes/the player's F-key can feed), and make it take over the Chest's old network-facing role entirely — the Chest keeps its player-storage role (E-panel, hotbar-place, mine-to-remove) but stops accepting `tryInsert`/`tryExtract` from anywhere.

**Architecture:** The entire behavioral split is one relocation: `Machines::tryInsert`/`tryExtract`/`putBack`'s existing Chest-specific branch moves to `MachineType::ItemAcceptor` unchanged. Everything else (the E-panel, drag-and-drop, Deposit All/Collect All) already operates on `Machine::storage` directly, never through those three functions, so it needs only to be told the panel can now belong to either of two machine types instead of one — generalized rather than duplicated.

**Tech Stack:** C++20, SFML 3, doctest, CMake + Visual Studio generator (existing project stack — no new dependencies).

## Global Constraints

- Build (tests): `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`. `cmake` is **not** on PATH; the full path is `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
- Build (game): `cmake --build C:\Litharia\build --config Debug --target Litharia`.
- Test binary: `C:\Litharia\build\Debug\Litharia_tests.exe`. Game binary: `C:\Litharia\build\Debug\Litharia.exe`.
- Baseline before this work: **209 test cases, 209 passed, 0 failed**, at commit `7438787`.
- `Machines.cpp`, `Recipes.cpp`, `MachineRegistry.cpp`, `Items.cpp` compile into `Litharia_core`, reachable from `Litharia_tests`. `Hud.cpp`, `Game.cpp` compile only into the `Litharia` executable and are **not** linked into the test binary — changes there are build-verified, not doctest-verified, matching every prior plan.
- Item Acceptor: `MachineType::ItemAcceptor`, 1x1, no power role (`generator=consumer=transport=false`), **not** furniture (stays in build mode, unlike Chest/Crafting Table/Furnace). Storage: **10 slots** (`ITEM_ACCEPTOR_SLOTS`), all 4 sides accept `tryInsert` (no directional restriction, same as the Chest's old behavior). Craft cost: **10 Stone + 3 Copper Plate, 3.0s**, requires a placed Crafting Table.
- Chest's existing craft recipe changes from `8 Oak Log` alone to **8 Oak Log + 2 Copper Plate** (craft time unchanged, `2.0s`). `CHEST_SLOTS` stays 20 — Chest's storage size and player-facing behavior (E-panel, drag-and-drop, hotbar-place, mine-to-remove) are unchanged; only its participation in `tryInsert`/`tryExtract`/`putBack` is removed.
- Spec: `docs/superpowers/specs/2026-07-17-item-acceptor-design.md`.

---

## Task 1: Item Acceptor item, machine type, and storage sizing

Registers the new type end-to-end at the data level: a craftable item, a `MachineType` entry, and the 10-slot storage `Machines::place()` gives it. Also wires its build-mode direct-select hotkey (`F6`, free since an earlier plan removed the old Chest/Crafting Table F6/F7) — this needs no other code, since the build palette, cycling, and placement already work generically off `MachineInfo`/`itemForMachine`/`isFurniture` for every non-furniture type.

**Files:**
- Modify: `src/Items/Items.h` (`enum class ItemType`, ~line 11-34)
- Modify: `src/Items/Items.cpp` (`registry`, ~line 9-30)
- Modify: `src/Machines/MachineType.h` (`enum class MachineType`, ~line 18-38)
- Modify: `src/Machines/MachineRegistry.cpp` (`registry`, `itemForMachine`, ~line 9-63)
- Modify: `src/Machines/Machines.cpp` (`place()`, ~line 54-85)
- Modify: `src/Game/Game.cpp` (`handleEvents`, F-key block, ~line 756-760)
- Test: `tests/test_machines.cpp`, `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: nothing new from other tasks (this is the plan's first task).
- Produces: `ItemType::ItemAcceptor`; `MachineType::ItemAcceptor`; `itemForMachine(MachineType::ItemAcceptor) == ItemType::ItemAcceptor`; `inline constexpr int ITEM_ACCEPTOR_SLOTS = 10;` in `MachineType.h`. Tasks 2-4 all depend on `MachineType::ItemAcceptor` existing.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("the machine registry has a row for Item Acceptor: no power role, 1x1")
{
    const MachineInfo& info = machineInfo(MachineType::ItemAcceptor);
    CHECK(info.name == "Item Acceptor");
    CHECK_FALSE(info.generator);
    CHECK_FALSE(info.consumer);
    CHECK_FALSE(info.transport);
    CHECK(info.width == 1);
    CHECK(info.height == 1);
}

TEST_CASE("itemForMachine maps Item Acceptor to its own item")
{
    CHECK(itemForMachine(MachineType::ItemAcceptor) == ItemType::ItemAcceptor);
}

TEST_CASE("Item Acceptor is not furniture - it stays in build mode")
{
    CHECK_FALSE(isFurniture(MachineType::ItemAcceptor));
}

TEST_CASE("a placed Item Acceptor gets 10 empty storage slots")
{
    Machines machines;
    Machine* acceptor = machines.place(MachineType::ItemAcceptor, 0, 0, Direction::Right);

    REQUIRE(acceptor != nullptr);
    CHECK(acceptor->storage.slotCount() == ITEM_ACCEPTOR_SLOTS);
    CHECK(acceptor->storage.isEmpty());
}
```

Append one more line inside the existing `tests/test_recipes.cpp` `TEST_CASE("every placeable machine has a matching craftable item")`, right before its closing `}`:

```cpp
    CHECK(itemInfo(ItemType::ItemAcceptor).name == "Item Acceptor");
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: **compile error** — `'ItemAcceptor': is not a member of 'ItemType'` / `'ItemAcceptor': is not a member of 'MachineType'` / `'ITEM_ACCEPTOR_SLOTS': undeclared identifier`.

- [ ] **Step 3: Add `ItemType::ItemAcceptor`**

In `src/Items/Items.h`, change the enum (append `ItemAcceptor` after `Furnace`, before `Count`):

```cpp
enum class ItemType : std::uint8_t
{
    None,
    Dirt,
    Stone,
    CopperOre,
    IronOre,
    Coal,
    CopperPlate,
    IronPlate,
    Pickaxe,
    Axe,
    OakLog,
    CraftingTable,
    BurnerGenerator,
    Drill,
    Belt,
    Chute,
    Smelter,
    Chest,
    Furnace,
    ItemAcceptor,

    Count
};
```

In `src/Items/Items.cpp`, add one more row to `registry`, after the `"Furnace"` row:

```cpp
    {"Item Acceptor",    10,  BlockType::Air,       ToolType::None,    { 80, 140, 190}},
```

- [ ] **Step 4: Add `MachineType::ItemAcceptor` and `ITEM_ACCEPTOR_SLOTS`**

In `src/Machines/MachineType.h`, change the enum (append `ItemAcceptor` after `Furnace`, before `Count`):

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
    CraftingTable,
    Furnace,
    ItemAcceptor,

    Count
};
```

Add the new constant directly below the existing `CHEST_SLOTS`:

```cpp
// How many item slots an Item Acceptor holds (1 row of 10 in the UI).
inline constexpr int ITEM_ACCEPTOR_SLOTS = 10;
```

- [ ] **Step 5: Update the registry and `itemForMachine`**

In `src/Machines/MachineRegistry.cpp`, add one more row to `registry`, after the `"Furnace"` row:

```cpp
    {"Item Acceptor",     { 80, 140, 190}, false, false, false, 0.0f,  0.0f,  1, 1},
```

Add an `ItemAcceptor` case to `itemForMachine`:

```cpp
ItemType itemForMachine(MachineType type)
{
    switch (type)
    {
        case MachineType::BurnerGenerator: return ItemType::BurnerGenerator;
        case MachineType::Drill:           return ItemType::Drill;
        case MachineType::Belt:            return ItemType::Belt;
        case MachineType::Chute:           return ItemType::Chute;
        case MachineType::Smelter:         return ItemType::Smelter;
        case MachineType::Chest:           return ItemType::Chest;
        case MachineType::CraftingTable:   return ItemType::CraftingTable;
        case MachineType::Furnace:         return ItemType::Furnace;
        case MachineType::ItemAcceptor:    return ItemType::ItemAcceptor;
        default:                           return ItemType::None;
    }
}
```

`isFurniture` needs no change — it already only returns `true` for `Chest`/`CraftingTable`/`Furnace`, so `ItemAcceptor` correctly falls through to `false` (stays in build mode).

- [ ] **Step 6: Size an Item Acceptor's storage on placement**

In `src/Machines/Machines.cpp`, inside `Machines::place()`, change:

```cpp
    if (type == MachineType::Chest)
        m.storage = Inventory(CHEST_SLOTS);
```

to:

```cpp
    if (type == MachineType::Chest)
        m.storage = Inventory(CHEST_SLOTS);
    else if (type == MachineType::ItemAcceptor)
        m.storage = Inventory(ITEM_ACCEPTOR_SLOTS);
```

- [ ] **Step 7: Add the `F6` build-mode hotkey**

In `src/Game/Game.cpp`'s `handleEvents()`, directly below `if (key->code == Key::F5) setBuildType(MachineType::Smelter);`, add:

```cpp
            if (key->code == Key::F6) setBuildType(MachineType::ItemAcceptor);
```

- [ ] **Step 8: Run the tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 213 | 213 passed | 0 failed`.

- [ ] **Step 9: Build the game executable to confirm the F6 change compiles**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 10: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h src/Machines/MachineRegistry.cpp src/Machines/Machines.cpp src/Game/Game.cpp tests/test_machines.cpp tests/test_recipes.cpp
git commit -m "feat: register the Item Acceptor machine and item"
```

---

## Task 2: Move the network special-case from Chest to Item Acceptor

The entire behavioral split for this feature: `Machines::tryInsert`/`tryExtract`/`putBack`'s existing `Chest`-specific branch moves to `ItemAcceptor`, unchanged. Chest falls through to each function's default (refuse / empty / generic fallback), so belts, chutes, and the player's F-key (`Game::interactAtCursor`, which calls these same three functions) can no longer push into or pull from a Chest — while the separate E-panel/drag-and-drop code (untouched by this task) keeps working on Chest exactly as before.

**Files:**
- Modify: `src/Machines/Machines.cpp` (`tryInsert`, `tryExtract`, `putBack`, ~line 137-243)
- Test: `tests/test_extraction.cpp`, `tests/test_transport.cpp`

**Interfaces:**
- Consumes: `MachineType::ItemAcceptor` (Task 1).
- Produces: no new symbols — `Machines::tryInsert`/`tryExtract`/`putBack` keep their existing signatures; only which `MachineType` their storage-branch applies to changes.

- [ ] **Step 1: Retarget the existing Chest tests to Item Acceptor, and add Chest-rejection tests**

In `tests/test_extraction.cpp`, replace these three test cases:

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

with:

```cpp
TEST_CASE("extracting from an item acceptor takes the first non-empty stack")
{
    Machines m;
    m.place(MachineType::ItemAcceptor, 0, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));
    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));

    const ItemStack taken = m.tryExtract(0, 0);

    CHECK(taken.type == ItemType::IronOre);
    CHECK(taken.count == 2);
    CHECK(m.at(0, 0)->storage.isEmpty());
}

TEST_CASE("extracting from an empty item acceptor returns nothing")
{
    Machines m;
    m.place(MachineType::ItemAcceptor, 0, 0, Direction::Right);

    CHECK(m.tryExtract(0, 0).empty());
}

TEST_CASE("putBack restores a stack into an item acceptor rather than destroying it")
{
    Machines m;
    m.place(MachineType::ItemAcceptor, 0, 0, Direction::Right);

    m.putBack(0, 0, {ItemType::Coal, 4});

    CHECK(m.at(0, 0)->storage.count(ItemType::Coal) == 4);
}

TEST_CASE("a chest is no longer a network node: tryInsert refuses it")
{
    Machines m;
    m.place(MachineType::Chest, 0, 0, Direction::Right);

    CHECK_FALSE(m.tryInsert(0, 0, ItemType::IronOre));
    CHECK(m.at(0, 0)->storage.isEmpty());
}

TEST_CASE("a chest is no longer a network node: tryExtract takes nothing from it")
{
    Machines m;
    Machine* chest = m.place(MachineType::Chest, 0, 0, Direction::Right);
    REQUIRE(chest != nullptr);

    // Put contents in directly (bypassing tryInsert, which now refuses a
    // chest) to prove tryExtract ignores existing contents too, not just
    // that nothing can get in.
    chest->storage.add({ItemType::IronOre, 3});

    CHECK(m.tryExtract(0, 0).empty());
    CHECK(m.at(0, 0)->storage.count(ItemType::IronOre) == 3);
}
```

In `tests/test_transport.cpp`, replace these three test cases:

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

with:

```cpp
TEST_CASE("an item acceptor accepts items into multiple slots, not just one")
{
    Machines m;
    m.place(MachineType::ItemAcceptor, 0, 0, Direction::Right);

    const int max = itemInfo(ItemType::Stone).maxStack;

    for (int i = 0; i < max + 3; ++i)
        CHECK(m.tryInsert(0, 0, ItemType::Stone));

    CHECK(m.at(0, 0)->storage.count(ItemType::Stone) == max + 3);
    CHECK(m.at(0, 0)->storage.slot(0).count == max);
    CHECK(m.at(0, 0)->storage.slot(1).count == 3);
}

TEST_CASE("an item acceptor with every slot full refuses further items")
{
    Machines m;
    m.place(MachineType::ItemAcceptor, 0, 0, Direction::Right);

    const int max = itemInfo(ItemType::Dirt).maxStack;
    Machine* acceptor = m.at(0, 0);
    for (int i = 0; i < acceptor->storage.slotCount(); ++i)
        acceptor->storage.exchange(i, {ItemType::Dirt, max});

    CHECK_FALSE(m.tryInsert(0, 0, ItemType::Dirt));
}

TEST_CASE("a belt delivers its carried item into an item acceptor ahead of it")
{
    World world;
    Machines m;
    m.place(MachineType::Belt, 0, 0, Direction::Right);
    m.place(MachineType::ItemAcceptor, 1, 0, Direction::Right);

    REQUIRE(m.tryInsert(0, 0, ItemType::IronOre));

    std::vector<sf::Vector2i> mined;
    const float step = 1.0f / 60.0f;
    for (int i = 0; i < 40; ++i)
        m.tick(world, step, mined);

    CHECK(m.at(0, 0)->carried == ItemType::None);
    CHECK(m.at(1, 0)->storage.count(ItemType::IronOre) == 1);
}

TEST_CASE("a belt cannot deliver into a chest: it just backs up")
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

    // Nowhere to go: the belt is still holding it, and the chest never
    // received anything.
    CHECK(m.at(0, 0)->carried == ItemType::IronOre);
    CHECK(m.at(1, 0)->storage.isEmpty());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: builds clean (no new symbols needed), but several new/retargeted test cases **FAIL** — the retargeted Item Acceptor tests fail because `ItemAcceptor` doesn't accept inserts yet (the branch is still on `Chest`), and the two "chest is no longer a network node" tests plus "a belt cannot deliver into a chest" fail because `Chest` still has the branch and so still accepts/extracts/receives normally.

- [ ] **Step 3: Move the branch in `tryInsert`**

In `src/Machines/Machines.cpp`, inside `Machines::tryInsert`, change:

```cpp
    if (m->type == MachineType::Chest)
        return m->storage.add({item, 1}) == 0;
```

to:

```cpp
    if (m->type == MachineType::ItemAcceptor)
        return m->storage.add({item, 1}) == 0;
```

- [ ] **Step 4: Move the branch in `tryExtract`**

Change:

```cpp
    if (m->type == MachineType::Chest)
    {
        for (int i = 0; i < m->storage.slotCount(); ++i)
        {
            if (!m->storage.slot(i).empty())
                return m->storage.take(i);
        }
        return {};
    }
```

to:

```cpp
    if (m->type == MachineType::ItemAcceptor)
    {
        for (int i = 0; i < m->storage.slotCount(); ++i)
        {
            if (!m->storage.slot(i).empty())
                return m->storage.take(i);
        }
        return {};
    }
```

- [ ] **Step 5: Move the branch in `putBack`**

Change:

```cpp
    if (m->type == MachineType::Chest)
    {
        m->storage.add(stack);
        return;
    }
```

to:

```cpp
    if (m->type == MachineType::ItemAcceptor)
    {
        m->storage.add(stack);
        return;
    }
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 215 | 215 passed | 0 failed`.

- [ ] **Step 7: Commit**

```bash
git add src/Machines/Machines.cpp tests/test_extraction.cpp tests/test_transport.cpp
git commit -m "feat: move machine-network storage access from Chest to Item Acceptor"
```

---

## Task 3: Recipes — Item Acceptor's craft cost, Chest's cost change

**Files:**
- Modify: `src/Machines/Recipes.cpp` (`craftRecipes`, ~line 13-22)
- Test: `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: `ItemType::ItemAcceptor` (Task 1).
- Produces: no new symbols — one new entry in the existing `craftRecipes` array, one existing entry's ingredients changed.

- [ ] **Step 1: Write the failing tests**

Replace the existing `tests/test_recipes.cpp` test case:

```cpp
TEST_CASE("allCraftRecipes exposes every craftable item exactly once")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    CHECK(all.size() == 8);
}
```

with:

```cpp
TEST_CASE("allCraftRecipes exposes every craftable item exactly once")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    CHECK(all.size() == 9);
}

TEST_CASE("the Chest recipe costs 8 oak logs and 2 copper plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::Chest; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::OakLog);
    CHECK(it->ingredients[0].count == 8);
    CHECK(it->ingredients[1].item == ItemType::CopperPlate);
    CHECK(it->ingredients[1].count == 2);
}

TEST_CASE("the Item Acceptor recipe costs 10 stone and 3 copper plates")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::ItemAcceptor; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stone);
    CHECK(it->ingredients[0].count == 10);
    CHECK(it->ingredients[1].item == ItemType::CopperPlate);
    CHECK(it->ingredients[1].count == 3);
    CHECK(it->seconds == doctest::Approx(3.0f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: `all.size() == 9` fails (still 8); the Chest recipe test fails (`ingredients[1].item` is currently `ItemType::None`, not `CopperPlate`); the Item Acceptor recipe test fails (`find_if` finds `all.end()`, no such recipe exists yet).

- [ ] **Step 3: Update the Chest recipe and add the Item Acceptor recipe**

In `src/Machines/Recipes.cpp`, inside `craftRecipes`, change the `Chest` row:

```cpp
    {ItemType::Chest,           {{{ItemType::OakLog, 8}, {}}},                           2.0f, true},
```

to:

```cpp
    {ItemType::Chest,           {{{ItemType::OakLog, 8}, {ItemType::CopperPlate, 2}}},    2.0f, true},
```

and append one more row, after the `Furnace` row:

```cpp
    {ItemType::ItemAcceptor,    {{{ItemType::Stone, 10}, {ItemType::CopperPlate, 3}}},    3.0f, true},
```

`craftRecipes`'s declared size (`std::array<CraftRecipe, 8>`) must become `std::array<CraftRecipe, 9>` to fit the new row.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 217 | 217 passed | 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/Machines/Recipes.cpp tests/test_recipes.cpp
git commit -m "feat: add the Item Acceptor recipe and raise the Chest's cost"
```

---

## Task 4: Generalize the storage panel (E-key) to Chest and Item Acceptor

Both machine types now need the exact same player-facing panel (open with `E`, drag-and-drop, Deposit All/Collect All) — this task generalizes the existing chest-only code to open for either, and fixes the one real gap: `Hud::hitTestPanels` hard-codes a 2-row grid, which would leave the Item Acceptor's 1-row (10-slot) panel with a dead, clickable-but-empty second row if left as-is.

**Files:**
- Modify: `src/Hud/Hud.h` (`SlotHit`, `hitTestPanels`, ~line 58-68)
- Modify: `src/Hud/Hud.cpp` (`hitTestPanels`, ~line 318-347)
- Modify: `src/Game/Game.h` (renamed state/methods, ~line 49-111)
- Modify: `src/Game/Game.cpp` (`toggleInventory`, `drawInventoryPanels`, `beginDrag`, `endDrag`, `depositAllToChest`/`collectAllFromChest` renamed, `handleEvents`)

**Interfaces:**
- Consumes: `MachineType::ItemAcceptor` (Task 1).
- Produces: `Hud::hitTestPanels(sf::Vector2f, sf::Vector2f, int openStorageSlots)` (signature change: `bool chestOpen` -> `int openStorageSlots`); `Hud::SlotHit::isStorage` (renamed from `isChest`). `Game::openStorageTile` (renamed from `openChestTile`); `Game::depositAllToStorage()`/`collectAllFromStorage()` (renamed from `depositAllToChest()`/`collectAllFromChest()`); `Game::InventoryPanel::Storage` (renamed from `::Chest`). No other task depends on these — this is the plan's last code task.
- `Hud::drawChestPanel`, `drawChestButtons`, `hitTestChestButton`, `ChestButton`, `CHEST_SLOT_SIZE` etc. keep their existing names deliberately — they're already generic over "whatever storage panel is open," so renaming them isn't needed and would widen this diff for no behavioral reason.

- [ ] **Step 1: Change `Hud::hitTestPanels`'s signature and `SlotHit`**

In `src/Hud/Hud.h`, change:

```cpp
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

to:

```cpp
    struct SlotHit
    {
        bool isStorage = false; // false: the player's bag; true: the open chest/item acceptor
        int index = -1;         // index into that Inventory
    };

    // Screen position -> which open panel/slot it lands on, or nullopt if
    // neither. `openStorageSlots` is the slot count of whichever storage
    // panel (chest or item acceptor) is currently drawn alongside the bag,
    // or 0 if none is open - must match what drawChestPanel was actually
    // called with this frame, so hit-testing and drawing never disagree.
    std::optional<SlotHit> hitTestPanels(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                          int openStorageSlots) const;
```

- [ ] **Step 2: Implement the size-derived row count**

In `src/Hud/Hud.cpp`, replace `Hud::hitTestPanels`:

```cpp
std::optional<Hud::SlotHit> Hud::hitTestPanels(sf::Vector2f screenPos, sf::Vector2f windowSize,
                                                int openStorageSlots) const
{
    constexpr int COLUMNS = Inventory::HOTBAR_SIZE;
    constexpr int BAG_ROWS = 3;

    if (openStorageSlots > 0)
    {
        const int storageRows = (openStorageSlots + COLUMNS - 1) / COLUMNS;
        const int storageIndex = hudLayout::hitTestGrid(screenPos, chestPanelOrigin(windowSize), COLUMNS,
                                                          storageRows, CHEST_SLOT_SIZE, SLOT_GAP);
        if (storageIndex >= 0 && storageIndex < openStorageSlots)
            return SlotHit{true, storageIndex};
    }

    const int bagGridIndex = hudLayout::hitTestGrid(screenPos, bagPanelOrigin(windowSize), COLUMNS,
                                                      BAG_ROWS, SLOT_SIZE, SLOT_GAP);
    if (bagGridIndex >= 0)
        return SlotHit{false, Inventory::HOTBAR_SIZE + bagGridIndex};

    // The hotbar is drawn every frame, not just while a panel is open, but it's
    // still a bag slot - drag-and-drop must reach it too, or items get stuck
    // there with no way back into the bag/chest grid.
    const int hotbarIndex =
        hudLayout::hitTestGrid(screenPos, hotbarOrigin(windowSize), COLUMNS, 1, SLOT_SIZE, SLOT_GAP);
    if (hotbarIndex >= 0)
        return SlotHit{false, hotbarIndex};

    return std::nullopt;
}
```

- [ ] **Step 3: Rename the Game-side state and methods**

In `src/Game/Game.h`, change:

```cpp
    void depositAllToChest();
    void collectAllFromChest();
```

to:

```cpp
    void depositAllToStorage();
    void collectAllFromStorage();
```

Change:

```cpp
    std::optional<sf::Vector2i> openChestTile;
```

to:

```cpp
    std::optional<sf::Vector2i> openStorageTile;
```

Change:

```cpp
    enum class InventoryPanel { Bag, Chest };

    bool dragging = false;
    ItemStack dragStack;
    InventoryPanel dragSourcePanel = InventoryPanel::Bag;
```

to:

```cpp
    enum class InventoryPanel { Bag, Storage };

    bool dragging = false;
    ItemStack dragStack;
    InventoryPanel dragSourcePanel = InventoryPanel::Bag;
```

- [ ] **Step 4: Generalize `toggleInventory` and `drawInventoryPanels`**

In `src/Game/Game.cpp`, replace `Game::toggleInventory`:

```cpp
void Game::toggleInventory()
{
    if (dragging)
        return;

    if (inventoryOpen)
    {
        inventoryOpen = false;
        openStorageTile.reset();
        openCraftingTableTile.reset();
        openFurnaceTile.reset();
        return;
    }

    const sf::Vector2i tile = cursorTile();
    const Machine* machine = machines.at(tile.x, tile.y);

    openStorageTile.reset();
    openCraftingTableTile.reset();
    openFurnaceTile.reset();

    if (machine != nullptr
        && (machine->type == MachineType::Chest || machine->type == MachineType::ItemAcceptor))
        openStorageTile = tile;
    else if (machine != nullptr && machine->type == MachineType::CraftingTable)
        openCraftingTableTile = tile;
    else if (machine != nullptr && machine->type == MachineType::Furnace)
        openFurnaceTile = tile;

    inventoryOpen = true;
    buildMode = false;
}
```

Replace `Game::drawInventoryPanels`:

```cpp
void Game::drawInventoryPanels()
{
    if (openStorageTile.has_value())
    {
        const Machine* storage = machines.at(openStorageTile->x, openStorageTile->y);

        // The machine might have vanished by other means while the panel was
        // open; fall back to the bag-only view rather than touch a stale tile.
        if (storage == nullptr
            || (storage->type != MachineType::Chest && storage->type != MachineType::ItemAcceptor))
            openStorageTile.reset();
    }

    if (openCraftingTableTile.has_value())
    {
        const Machine* table = machines.at(openCraftingTableTile->x, openCraftingTableTile->y);

        if (table == nullptr || table->type != MachineType::CraftingTable)
            openCraftingTableTile.reset();
    }

    if (openFurnaceTile.has_value())
    {
        const Machine* furnace = machines.at(openFurnaceTile->x, openFurnaceTile->y);

        if (furnace == nullptr || furnace->type != MachineType::Furnace)
            openFurnaceTile.reset();
    }

    hud.drawInventoryPanel(window, player.inventory());

    if (openStorageTile.has_value())
    {
        hud.drawChestPanel(window, machines.at(openStorageTile->x, openStorageTile->y)->storage);
        hud.drawChestButtons(window);
    }
    else if (openFurnaceTile.has_value())
    {
        hud.drawSmeltPanel(window, player.inventory(), smelting, smeltingRecipeIndex, smeltProgress);
    }
    else
    {
        hud.drawCraftPanel(window, player.inventory(), openCraftingTableTile.has_value(), crafting,
                            craftingRecipeIndex, craftProgress);
    }
}
```

- [ ] **Step 5: Generalize `beginDrag`/`endDrag`**

Replace `Game::beginDrag`:

```cpp
void Game::beginDrag()
{
    const sf::Vector2f screenPos(sf::Mouse::getPosition(window));
    const sf::Vector2f windowSize(window.getSize());

    const int openSlots = openStorageTile.has_value()
        ? machines.at(openStorageTile->x, openStorageTile->y)->storage.slotCount()
        : 0;

    const auto hit = hud.hitTestPanels(screenPos, windowSize, openSlots);
    if (!hit.has_value())
        return;

    Inventory& source = hit->isStorage ? machines.at(openStorageTile->x, openStorageTile->y)->storage
                                        : player.inventory();

    const ItemStack taken = source.take(hit->index);
    if (taken.empty())
        return;

    dragging = true;
    dragStack = taken;
    dragSourcePanel = hit->isStorage ? InventoryPanel::Storage : InventoryPanel::Bag;
    dragSourceSlot = hit->index;
}
```

Replace `Game::endDrag`:

```cpp
void Game::endDrag()
{
    const sf::Vector2f screenPos(sf::Mouse::getPosition(window));
    const sf::Vector2f windowSize(window.getSize());

    Inventory& sourceInventory = (dragSourcePanel == InventoryPanel::Storage)
        ? machines.at(openStorageTile->x, openStorageTile->y)->storage
        : player.inventory();

    const int openSlots = openStorageTile.has_value()
        ? machines.at(openStorageTile->x, openStorageTile->y)->storage.slotCount()
        : 0;

    const auto hit = hud.hitTestPanels(screenPos, windowSize, openSlots);

    if (!hit.has_value())
    {
        // Released outside any slot: put it back where it came from.
        sourceInventory.exchange(dragSourceSlot, dragStack);
        dragging = false;
        return;
    }

    Inventory& destInventory = hit->isStorage ? machines.at(openStorageTile->x, openStorageTile->y)->storage
                                               : player.inventory();

    const ItemStack leftover = destInventory.exchange(hit->index, dragStack);

    if (!leftover.empty())
        sourceInventory.exchange(dragSourceSlot, leftover);

    dragging = false;
}
```

- [ ] **Step 6: Rename `depositAllToChest`/`collectAllFromChest`**

Replace `Game::depositAllToChest`:

```cpp
void Game::depositAllToStorage()
{
    if (!openStorageTile.has_value())
        return;

    Inventory& storage = machines.at(openStorageTile->x, openStorageTile->y)->storage;
    Inventory& bag = player.inventory();

    // Bag-panel slots only (HOTBAR_SIZE..slotCount()-1) - the hotbar is left
    // alone, same as the hotbar being excluded from what Deposit All sweeps.
    for (int i = Inventory::HOTBAR_SIZE; i < bag.slotCount(); ++i)
    {
        const ItemStack taken = bag.take(i);
        if (taken.empty())
            continue;

        // Whatever doesn't fit goes right back into the slot it came from -
        // take() already emptied it, so this can only refill it, never swap
        // with something else.
        const int leftover = storage.add(taken);
        if (leftover > 0)
            bag.exchange(i, {taken.type, leftover});
    }
}
```

Replace `Game::collectAllFromChest`:

```cpp
void Game::collectAllFromStorage()
{
    if (!openStorageTile.has_value())
        return;

    Inventory& storage = machines.at(openStorageTile->x, openStorageTile->y)->storage;
    Inventory& bag = player.inventory();

    for (int i = 0; i < storage.slotCount(); ++i)
    {
        const ItemStack taken = storage.take(i);
        if (taken.empty())
            continue;

        // Bag panel only, same as Deposit All - the hotbar is never a
        // Collect All destination.
        const int leftover = bag.add(taken, Inventory::HOTBAR_SIZE);
        if (leftover > 0)
            storage.exchange(i, {taken.type, leftover});
    }
}
```

- [ ] **Step 7: Update `handleEvents`'s two remaining `openChestTile` references**

In `src/Game/Game.cpp`'s `handleEvents()`, change the `B` key block:

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

to:

```cpp
            if (key->code == Key::B && !dragging)
            {
                buildMode = !buildMode;
                if (buildMode)
                {
                    inventoryOpen = false;
                    openStorageTile.reset();
                }
            }
```

Change the inventory click-dispatch block:

```cpp
            else if (inventoryOpen && mouse->button == sf::Mouse::Button::Left)
            {
                const sf::Vector2f screenPos(sf::Mouse::getPosition(window));
                const sf::Vector2f windowSize(window.getSize());

                if (openChestTile.has_value())
                {
                    const auto chestButton = hud.hitTestChestButton(screenPos, windowSize);

                    if (chestButton == Hud::ChestButton::DepositAll)
                        depositAllToChest();
                    else if (chestButton == Hud::ChestButton::CollectAll)
                        collectAllFromChest();
                    else
                        beginDrag();
                }
                else if (openFurnaceTile.has_value())
```

to:

```cpp
            else if (inventoryOpen && mouse->button == sf::Mouse::Button::Left)
            {
                const sf::Vector2f screenPos(sf::Mouse::getPosition(window));
                const sf::Vector2f windowSize(window.getSize());

                if (openStorageTile.has_value())
                {
                    const auto chestButton = hud.hitTestChestButton(screenPos, windowSize);

                    if (chestButton == Hud::ChestButton::DepositAll)
                        depositAllToStorage();
                    else if (chestButton == Hud::ChestButton::CollectAll)
                        collectAllFromStorage();
                    else
                        beginDrag();
                }
                else if (openFurnaceTile.has_value())
```

(the rest of that `if`/`else if` chain, from `openFurnaceTile` onward, is unchanged.)

- [ ] **Step 8: Build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 9: Run the test suite to confirm no regression**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 217 | 217 passed | 0 failed` (unchanged from Task 3 - this task touches only executable-only files).

- [ ] **Step 10: Commit**

```bash
git add src/Hud/Hud.h src/Hud/Hud.cpp src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: generalize the storage panel to Chest and Item Acceptor"
```

---

## Task 5: End-to-end verification

No code changes. Build/test-verify from the agent side; the actual play-testing is the human's to run.

**Files:** none.

- [ ] **Step 1: Full build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia` and `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: both build clean; `C:\Litharia\build\Debug\Litharia_tests.exe` shows `test cases: 217 | 217 passed | 0 failed`.

- [ ] **Step 2: Hand off a manual-verification checklist**

Report the following checklist to the user rather than attempting to drive the game:

1. Craft an Item Acceptor at a placed Crafting Table (10 Stone + 3 Copper Plate) - confirm it costs the right ingredients and takes ~3s.
2. Press `B`, confirm Item Acceptor appears in the build palette (and via `F6`) alongside the other 5 factory types - NOT alongside Chest/Crafting Table/Furnace, which stay out of build mode.
3. Place an Item Acceptor, aim a belt/chute into each of its 4 sides in turn, confirm all 4 accept a pushed item.
4. Press `E` on the placed Item Acceptor - confirm a 1-row (10-slot) panel opens next to the bag, with working drag-and-drop and Deposit All/Collect All, and that clicking below the visible row does nothing (no dead-zone misclick).
5. Place a Chest (hotbar-select + right-click, as before), aim a belt at it - confirm the belt backs up (refused), and confirm the player's `F` key no longer inserts/extracts on a Chest either.
6. Press `E` on the Chest - confirm its panel still opens with full drag-and-drop/Deposit All/Collect All, exactly as before this plan.
7. Confirm a Crafting Table and Furnace still open their own panels correctly on `E` (three-way, now effectively four-way with Item Acceptor sharing the Chest's slot in the resolution), and that build mode still filters/counts/consumes/refunds the 5 factory types plus Item Acceptor correctly.
