# Furnace and Furniture Placement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a craftable, 2x2 manual-smelting Furnace that breaks the plate-bootstrap softlock, and move Chest/Crafting Table/Furnace out of build mode into normal hotbar-select-and-place / mine-to-remove interaction — fixing the reported "can't place the Crafting Table" bug at its actual root (build mode's palette has no click-to-select, so a stale, unheld `buildType` silently blocks every placement).

**Architecture:** The existing multi-tile system (`MachineInfo::width`, built for the Crafting Table's 2x1 footprint) generalizes to `width x height` to support the Furnace's 2x2 footprint — one careful retrofit of `Machines::place()`/`remove()`'s already-reviewed footprint loops. The Furnace's own manual-smelting recipes live in a new, separate `FurnaceRecipe` table (deliberately not reusing the automated Smelter's `SmeltRecipe` timing) and get their own small Hud panel and Game-level single-slot progress state, mirroring the existing hand-craft flow's shape without merging into it. Separately, three machine types (Chest, Crafting Table, Furnace) are reclassified as "furniture": placed via the same hotbar-select-and-right-click path world blocks already use, and removed by mining them like a block, instead of through build mode's palette/F-keys/right-click-destroy.

**Tech Stack:** C++20, SFML 3, doctest, CMake + Visual Studio generator (existing project stack — no new dependencies).

## Global Constraints

- Build (tests): `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`. `cmake` is **not** on PATH; the full path is `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
- Build (game): `cmake --build C:\Litharia\build --config Debug --target Litharia`.
- Test binary: `C:\Litharia\build\Debug\Litharia_tests.exe`. Game binary: `C:\Litharia\build\Debug\Litharia.exe`.
- Baseline before this work: **201 test cases, 201 passed, 0 failed**, at commit `4aafbe0`.
- Registry arrays (`Items.cpp`'s `registry`, `MachineRegistry.cpp`'s `registry`) are positional aggregate-initialized `std::array`s indexed by their enum — order must match the enum, and every existing row must gain any new trailing field added to the struct.
- `Machines.cpp`, `Recipes.cpp`, `MachineRegistry.cpp`, `Items.cpp`, `Inventory.cpp` compile into `Litharia_core` and are reachable from `Litharia_tests`. `Hud.cpp`, `Game.cpp`, `MachineRenderer.cpp` compile only into the `Litharia` executable and are **not** linked into the test binary — changes there are verified by running the game, not by doctest, exactly as the crafting-menu plan's equivalent tasks were.
- Specs: `docs/superpowers/specs/2026-07-17-furnace-manual-smelting-design.md` and `docs/superpowers/specs/2026-07-17-furniture-placement-model-design.md`.
- Furnace footprint: **2x2**. Furnace craft cost: **20 Stone**, requires a placed Crafting Table, craft time **4.0s** (not separately specified; matches the Drill/Smelter tier). Manual smelting at a placed Furnace: Copper Ore -> Copper Plate in **5.0s**, Iron Ore -> Iron Plate in **7.5s** (both slower than the automated Smelter's existing 2.0s/3.5s, on purpose).
- Furniture set (placed/removed like a block, never through build mode): **Chest, Crafting Table, Furnace**. Mining any of them takes a flat **1.0 second**, no tool required.

---

## Task 1: Generalize multi-tile footprint to width x height, and register the 2x2 Furnace

The Furnace needs a footprint taller than one row, which today's `MachineInfo::width` (built only for the Crafting Table's 2x1) can't express. This task retrofits the already-shipped multi-tile system to two dimensions and, in the same task, adds the Furnace itself — the two can't be tested apart, since there is no way to exercise the height dimension without a real height>1 machine type.

**Files:**
- Modify: `src/Items/Items.h` (`enum class ItemType`, ~line 11-33)
- Modify: `src/Items/Items.cpp` (`registry`, ~line 9-29)
- Modify: `src/Machines/MachineType.h` (`enum class MachineType`, `MachineInfo`, ~line 25-60)
- Modify: `src/Machines/MachineRegistry.cpp` (`registry`, `itemForMachine`, ~line 9-61)
- Modify: `src/Machines/Machines.cpp` (`place()`, `remove()`, ~line 54-117)
- Modify: `src/Machines/Machine.h` (comment only, ~line 9-11)
- Modify: `src/Machines/MachineRenderer.cpp` (`draw()`, ~line 56-90)
- Test: `tests/test_machines.cpp`, `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: nothing new from other tasks (this is the plan's first task).
- Produces: `ItemType::Furnace`; `MachineType::Furnace`; `MachineInfo::height` (`int`, tile count along Y, growing downward from the placed `(x, y)`, `1` for every type except Furnace's `2`); `itemForMachine(MachineType::Furnace) == ItemType::Furnace`. Later tasks (2-6) all depend on `MachineType::Furnace` and `ItemType::Furnace` existing.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_recipes.cpp`, inside the existing `TEST_CASE("every placeable machine has a matching craftable item")` (add one more line at the end, right before the closing `}`):

```cpp
    CHECK(itemInfo(ItemType::Furnace).name == "Furnace");
```

Replace the existing `tests/test_machines.cpp` test case (lines 184-196):

```cpp
TEST_CASE("the machine registry declares a footprint width, 1 for every type except the Crafting Table")
{
    for (int i = 1; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineType type = static_cast<MachineType>(i);
        const int width = machineInfo(type).width;

        if (type == MachineType::CraftingTable)
            CHECK(width == 2);
        else
            CHECK(width == 1);
    }
}
```

with:

```cpp
TEST_CASE("the machine registry declares a footprint, 1x1 for every type except the Crafting Table (2x1) and the Furnace (2x2)")
{
    for (int i = 1; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineType type = static_cast<MachineType>(i);
        const MachineInfo& info = machineInfo(type);

        if (type == MachineType::CraftingTable)
        {
            CHECK(info.width == 2);
            CHECK(info.height == 1);
        }
        else if (type == MachineType::Furnace)
        {
            CHECK(info.width == 2);
            CHECK(info.height == 2);
        }
        else
        {
            CHECK(info.width == 1);
            CHECK(info.height == 1);
        }
    }
}
```

Then append these 5 new test cases to `tests/test_machines.cpp` (after the file's last test case, "swap-and-pop relocates every tile of a multi-tile machine, not just its origin"):

```cpp
TEST_CASE("itemForMachine maps the Furnace to its own item")
{
    CHECK(itemForMachine(MachineType::Furnace) == ItemType::Furnace);
}

TEST_CASE("a 2x2 machine occupies all four tiles it spans")
{
    Machines machines;
    Machine* furnace = machines.place(MachineType::Furnace, 10, 10, Direction::Right);

    REQUIRE(furnace != nullptr);
    CHECK(machines.at(10, 10) == furnace);
    CHECK(machines.at(11, 10) == furnace);
    CHECK(machines.at(10, 11) == furnace);
    CHECK(machines.at(11, 11) == furnace);
    CHECK(machines.at(12, 10) == nullptr);
    CHECK(machines.at(10, 12) == nullptr);
}

TEST_CASE("a 2x2 machine cannot be placed if any of its four tiles is taken")
{
    Machines machines;
    REQUIRE(machines.place(MachineType::Belt, 11, 11, Direction::Right) != nullptr);

    // (10,10), (11,10), (10,11) are free but (11,11) is not - the whole
    // placement must fail, not just settle for the tiles that were free.
    CHECK(machines.place(MachineType::Furnace, 10, 10, Direction::Right) == nullptr);
    CHECK(machines.count() == 1);
    CHECK(machines.at(10, 10) == nullptr);
}

TEST_CASE("removing a 2x2 machine via any of its four tiles clears all of them")
{
    Machines machines;
    machines.place(MachineType::Furnace, 10, 10, Direction::Right);

    REQUIRE(machines.remove(11, 11)); // remove via the far corner, not the origin
    CHECK(machines.count() == 0);
    CHECK(machines.at(10, 10) == nullptr);
    CHECK(machines.at(11, 10) == nullptr);
    CHECK(machines.at(10, 11) == nullptr);
    CHECK(machines.at(11, 11) == nullptr);
}

TEST_CASE("swap-and-pop relocates every tile of a 2x2 machine, including its vertical footprint")
{
    Machines machines;
    machines.place(MachineType::Belt, 0, 0, Direction::Right);        // index 0, doomed
    machines.place(MachineType::Belt, 1, 0, Direction::Right);        // index 1, untouched survivor
    machines.place(MachineType::Furnace, 20, 20, Direction::Right);   // index 2 -> swapped into slot 0

    REQUIRE(machines.count() == 3);
    REQUIRE(machines.remove(0, 0));

    Machine* furnace = machines.at(20, 20);
    REQUIRE(furnace != nullptr);
    CHECK(furnace->type == MachineType::Furnace);
    CHECK(machines.at(21, 20) == furnace);
    CHECK(machines.at(20, 21) == furnace);
    CHECK(machines.at(21, 21) == furnace);

    REQUIRE(machines.at(1, 0) != nullptr);
    CHECK(machines.at(1, 0)->type == MachineType::Belt);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: **compile error** — `'Furnace': is not a member of 'ItemType'` / `'Furnace': is not a member of 'MachineType'` / `'height': is not a member of 'MachineInfo'`.

- [ ] **Step 3: Add `ItemType::Furnace`**

In `src/Items/Items.h`, change the enum (append `Furnace` after `Chest`, before `Count`):

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

    Count
};
```

In `src/Items/Items.cpp`, add one more row to `registry`, after the `"Chest"` row:

```cpp
    {"Furnace",          10,  BlockType::Air,       ToolType::None,    {110, 110, 115}},
```

- [ ] **Step 4: Add `MachineType::Furnace` and the `height` field**

In `src/Machines/MachineType.h`, change the enum (append `Furnace` after `CraftingTable`, before `Count`):

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

    Count
};
```

Change `MachineInfo` to add `height` right after `width`:

```cpp
struct MachineInfo
{
    std::string_view name;
    BlockColor color;

    bool generator; // supplies power to the machines touching it
    bool consumer;  // draws power from a generator touching it
    bool transport; // belt/chute: carries one item toward its facing (chute: down)

    float powerRating; // supply if generator, demand if consumer
    float actionTime;  // drill: seconds per ore; transport: transfer interval

    // Tile footprint starting at the machine's placed (x, y), growing right
    // (width) and down (height). 1x1 for every machine except the Crafting
    // Table (2x1) and the Furnace (2x2).
    int width;
    int height;
};
```

- [ ] **Step 5: Update the registry and `itemForMachine`**

In `src/Machines/MachineRegistry.cpp`, replace the `registry` array (every existing row gains a trailing `height` of `1`; the new `Furnace` row is `2, 2`):

```cpp
constexpr std::array<MachineInfo, static_cast<std::size_t>(MachineType::Count)> registry = {{
    //  name                 color            gen    con    trans  power  action  width height
    {"None",              {  0,   0,   0}, false, false, false, 0.0f,  0.0f,  1, 1},
    {"Burner Generator",  {190, 120,  60}, true,  false, false, 10.0f, 0.0f,  1, 1},
    {"Drill",             {150, 150, 160}, false, true,  false, 5.0f,  3.0f,  1, 1},
    {"Belt",              { 90,  90, 100}, false, false, true,  0.0f,  0.5f,  1, 1},
    {"Chute",             { 70,  70,  80}, false, false, true,  0.0f,  0.5f,  1, 1},
    {"Smelter",           {200,  90,  70}, false, true,  false, 5.0f,  0.0f,  1, 1},
    {"Chest",             {140,  95,  50}, false, false, false, 0.0f,  0.0f,  1, 1},
    {"Crafting Table",    {120,  80,  40}, false, false, false, 0.0f,  0.0f,  2, 1},
    {"Furnace",           {110, 110, 115}, false, false, false, 0.0f,  0.0f,  2, 2},
}};
```

Add a `Furnace` case to `itemForMachine`:

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
        default:                           return ItemType::None;
    }
}
```

- [ ] **Step 6: Make `Machines::place()`/`remove()` iterate width x height**

In `src/Machines/Machines.cpp`, replace `Machines::place()`:

```cpp
Machine* Machines::place(MachineType type, int x, int y, Direction facing)
{
    const MachineInfo& info = machineInfo(type);

    for (int dy = 0; dy < info.height; ++dy)
        for (int dx = 0; dx < info.width; ++dx)
            if (!canPlace(x + dx, y + dy))
                return nullptr;

    Machine m;
    m.type = type;
    m.x = x;
    m.y = y;
    m.facing = facing;
    // facing itself is excluded from output (it's reserved for input); start the
    // rotation just past it.
    m.outputCursor = rotateCW(facing);

    m.placedSeq = nextSeq++;

    if (type == MachineType::Chest)
        m.storage = Inventory(CHEST_SLOTS);

    machines.push_back(m);
    const int index = static_cast<int>(machines.size()) - 1;

    for (int dy = 0; dy < info.height; ++dy)
        for (int dx = 0; dx < info.width; ++dx)
            byTile[key(x + dx, y + dy)] = index;

    return &machines[index];
}
```

Replace `Machines::remove()`:

```cpp
bool Machines::remove(int x, int y)
{
    const int index = indexAt(x, y);
    if (index < 0)
        return false;

    // Capture the doomed machine's own footprint before anything moves.
    const int doomedX = machines[index].x;
    const int doomedY = machines[index].y;
    const MachineInfo& doomedInfo = machineInfo(machines[index].type);

    const int last = static_cast<int>(machines.size()) - 1;

    // Swap the doomed machine with the last, so the vector stays dense, then fix
    // EVERY tile the moved machine occupies (both dimensions) - not just its
    // origin, or a multi-tile machine relocated into the freed slot leaves a
    // stale byTile entry on one of its other tiles.
    if (index != last)
    {
        machines[index] = machines[last];

        const MachineInfo& movedInfo = machineInfo(machines[index].type);
        for (int dy = 0; dy < movedInfo.height; ++dy)
            for (int dx = 0; dx < movedInfo.width; ++dx)
                byTile[key(machines[index].x + dx, machines[index].y + dy)] = index;
    }

    machines.pop_back();

    for (int dy = 0; dy < doomedInfo.height; ++dy)
        for (int dx = 0; dx < doomedInfo.width; ++dx)
            byTile.erase(key(doomedX + dx, doomedY + dy));

    return true;
}
```

- [ ] **Step 7: Update the stale comment on `Machine`**

In `src/Machines/Machine.h`, replace the comment above the struct:

```cpp
// One placed machine. Plain data: all behaviour lives in Machines. A machine
// occupies machineInfo(type).width x machineInfo(type).height tiles starting
// at (x, y), growing right and down - 1x1 for everything except the
// Crafting Table (2x1) and the Furnace (2x2).
```

- [ ] **Step 8: Size the renderer's body by height too, and skip decorations for the Furnace**

In `src/Machines/MachineRenderer.cpp`, inside `MachineRenderer::draw()`, change the body-size line:

```cpp
        body.setSize({static_cast<float>(info.width * TILE_SIZE), static_cast<float>(info.height * TILE_SIZE)});
```

Change the decoration-skip condition (currently `if (m.type == MachineType::CraftingTable) continue;`) to:

```cpp
        if (m.type == MachineType::CraftingTable || m.type == MachineType::Furnace)
            continue;
```

- [ ] **Step 9: Run the tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 206 | 206 passed | 0 failed`.

- [ ] **Step 10: Build the game executable to confirm the renderer change compiles**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 11: Commit**

```bash
git add src/Items/Items.h src/Items/Items.cpp src/Machines/MachineType.h src/Machines/MachineRegistry.cpp src/Machines/Machines.cpp src/Machines/Machine.h src/Machines/MachineRenderer.cpp tests/test_machines.cpp tests/test_recipes.cpp
git commit -m "feat: generalize multi-tile footprint to width x height, add the 2x2 Furnace"
```

---

## Task 2: Furnace recipes

The Furnace's own craft cost (joins the advanced table menu) and its manual-smelting recipes (a new, separate table from the automated Smelter's `SmeltRecipe`).

**Files:**
- Modify: `src/Machines/Recipes.h` (add `FurnaceRecipe`, `allFurnaceRecipes()`)
- Modify: `src/Machines/Recipes.cpp` (add the Furnace `CraftRecipe` entry, the `furnaceRecipes` table, and the accessor)
- Test: `tests/test_recipes.cpp`

**Interfaces:**
- Consumes: `ItemType::Furnace`, `MachineType::Furnace` (Task 1); `ItemType::Stone`, `CopperOre`, `IronOre`, `CopperPlate`, `IronPlate` (existing).
- Produces: `struct FurnaceRecipe { ItemType in; ItemType out; float seconds; }`; `std::span<const FurnaceRecipe> allFurnaceRecipes()`. Tasks 3-4 depend on this.

- [ ] **Step 1: Write the failing tests**

Replace the existing `tests/test_recipes.cpp` test case:

```cpp
TEST_CASE("allCraftRecipes exposes every craftable item exactly once")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    CHECK(all.size() == 7);
}
```

with:

```cpp
TEST_CASE("allCraftRecipes exposes every craftable item exactly once")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    CHECK(all.size() == 8);
}

TEST_CASE("the Furnace recipe requires a table and costs 20 stone")
{
    const std::span<const CraftRecipe> all = allCraftRecipes();
    const auto it = std::find_if(all.begin(), all.end(),
        [](const CraftRecipe& r) { return r.output == ItemType::Furnace; });

    REQUIRE(it != all.end());
    CHECK(it->requiresCraftingTable);
    CHECK(it->ingredients[0].item == ItemType::Stone);
    CHECK(it->ingredients[0].count == 20);
    CHECK(it->seconds == doctest::Approx(4.0f));
}

TEST_CASE("allFurnaceRecipes exposes manual smelting for both ores, slower than the automated Smelter")
{
    const std::span<const FurnaceRecipe> all = allFurnaceRecipes();
    REQUIRE(all.size() == 2);

    CHECK(all[0].in == ItemType::CopperOre);
    CHECK(all[0].out == ItemType::CopperPlate);
    CHECK(all[0].seconds == doctest::Approx(5.0f));

    CHECK(all[1].in == ItemType::IronOre);
    CHECK(all[1].out == ItemType::IronPlate);
    CHECK(all[1].seconds == doctest::Approx(7.5f));

    // Manual smelting is deliberately slower than the automated Smelter's own
    // SmeltRecipe timing, so building one is still worth it.
    CHECK(all[0].seconds > smeltRecipeFor(ItemType::CopperOre)->seconds);
    CHECK(all[1].seconds > smeltRecipeFor(ItemType::IronOre)->seconds);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: `all.size() == 8` fails (still 7); `'FurnaceRecipe': undeclared identifier` / `'allFurnaceRecipes': identifier not found` are compile errors.

- [ ] **Step 3: Declare `FurnaceRecipe` and its accessor**

In `src/Machines/Recipes.h`, append below the existing `allCraftRecipes()` declaration:

```cpp
// One manual-smelting recipe available at a placed Furnace: an input ore
// becomes an output plate after `seconds`. Deliberately a separate table
// from SmeltRecipe (the automated Smelter's own timing) - manual smelting is
// slower on purpose, and the two must stay free to rebalance independently.
struct FurnaceRecipe
{
    ItemType in;
    ItemType out;
    float seconds;
};

// Every defined manual-smelting recipe.
std::span<const FurnaceRecipe> allFurnaceRecipes();
```

- [ ] **Step 4: Add the Furnace's craft-cost entry and the furnace-recipe table**

In `src/Machines/Recipes.cpp`, add one more row to the `craftRecipes` array, after the `Smelter` row:

```cpp
    {ItemType::Furnace,         {{{ItemType::Stone, 20}, {}}},                           4.0f, true},
```

Below the `craftRecipes` array (still inside the anonymous namespace), add:

```cpp
constexpr std::array<FurnaceRecipe, 2> furnaceRecipes = {{
    {ItemType::CopperOre, ItemType::CopperPlate, 5.0f},
    {ItemType::IronOre,   ItemType::IronPlate,   7.5f},
}};
```

Below `std::span<const CraftRecipe> allCraftRecipes() { return craftRecipes; }`, add:

```cpp
std::span<const FurnaceRecipe> allFurnaceRecipes()
{
    return furnaceRecipes;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 208 | 208 passed | 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/Machines/Recipes.h src/Machines/Recipes.cpp tests/test_recipes.cpp
git commit -m "feat: add the Furnace's craft cost and its manual-smelting recipes"
```

---

## Task 3: Furnace smelt panel rendering and hit-testing in `Hud`

A smaller sibling of the hand-craft panel: no basic/advanced split (there's only one view, 2 buttons) and no multi-ingredient loop (`FurnaceRecipe` has exactly one input). `Hud.cpp` compiles only into `Litharia` (not test-linked) — this task is build-verified only, same as the original plan's equivalent Hud task; nothing calls these methods yet (Task 4 wires them up).

**Files:**
- Modify: `src/Hud/Hud.h` (new method declarations, ~line 91-108)
- Modify: `src/Hud/Hud.cpp` (new methods, appended after `hitTestCraftButton`)

**Interfaces:**
- Consumes: `FurnaceRecipe`, `allFurnaceRecipes()` (Task 2); the existing `craftPanelOrigin()` free function (already in `Hud.cpp`'s anonymous namespace) and `CRAFT_BUTTON_WIDTH`/`CRAFT_BUTTON_HEIGHT` constants (both already exist from the crafting-menu plan).
- Produces: `void Hud::drawSmeltPanel(sf::RenderWindow&, const Inventory& bag, bool smelting, int smeltingRecipeIndex, float smeltProgress)`; `std::optional<int> Hud::hitTestSmeltButton(sf::Vector2f screenPos, sf::Vector2f windowSize) const` — the returned `int` is an index into `allFurnaceRecipes()`. Task 4 depends on both.

- [ ] **Step 1: Add the method declarations**

In `src/Hud/Hud.h`, below the existing `hitTestCraftButton` declaration, add:

```cpp
    // The Furnace's manual-smelting panel: one button per FurnaceRecipe
    // (always both ore->plate conversions - no basic/advanced split, since
    // there is only one view). Same dimmed-while-smelting/in-progress-fill-bar
    // behavior as drawCraftPanel.
    void drawSmeltPanel(sf::RenderWindow& window, const Inventory& bag, bool smelting,
                         int smeltingRecipeIndex, float smeltProgress);

    // Screen position -> index into allFurnaceRecipes() for the button it
    // lands on. nullopt if the point misses every button.
    std::optional<int> hitTestSmeltButton(sf::Vector2f screenPos, sf::Vector2f windowSize) const;
```

- [ ] **Step 2: Implement both methods**

At the end of `src/Hud/Hud.cpp`, add:

```cpp
void Hud::drawSmeltPanel(sf::RenderWindow& window, const Inventory& bag, bool smelting,
                          int smeltingRecipeIndex, float smeltProgress)
{
    const sf::View previous = window.getView();
    window.setView(currentWindowView(window));

    const std::span<const FurnaceRecipe> all = allFurnaceRecipes();
    const sf::Vector2f origin = craftPanelOrigin(sf::Vector2f(window.getSize()));

    for (std::size_t i = 0; i < all.size(); ++i)
    {
        const FurnaceRecipe& recipe = all[i];
        const sf::Vector2f pos{origin.x,
                               origin.y + static_cast<float>(i) * (CRAFT_BUTTON_HEIGHT + SLOT_GAP)};

        const bool affordable = bag.count(recipe.in) > 0;
        const bool disabled = smelting || !affordable;
        const bool inProgress = smelting && static_cast<int>(i) == smeltingRecipeIndex;

        sf::RectangleShape button({CRAFT_BUTTON_WIDTH, CRAFT_BUTTON_HEIGHT});
        button.setPosition(pos);
        button.setFillColor(disabled ? sf::Color(40, 40, 46, 170) : BAG_SLOT_BACKGROUND);
        button.setOutlineThickness(-1.0f);
        button.setOutlineColor(sf::Color(90, 90, 105));
        window.draw(button);

        if (inProgress)
        {
            const float fraction = std::clamp(smeltProgress / recipe.seconds, 0.0f, 1.0f);
            sf::RectangleShape fill({CRAFT_BUTTON_WIDTH * fraction, CRAFT_BUTTON_HEIGHT});
            fill.setPosition(pos);
            fill.setFillColor(sf::Color(90, 200, 230, 120));
            window.draw(fill);
        }

        if (!font)
            continue;

        const std::string label =
            std::string(itemInfo(recipe.in).name) + " -> " + std::string(itemInfo(recipe.out).name);

        sf::Text text(*font, label, 13);
        text.setFillColor(disabled ? sf::Color(150, 150, 150) : sf::Color::White);
        text.setPosition({pos.x + 8.0f, pos.y + (CRAFT_BUTTON_HEIGHT - 13.0f) * 0.5f});
        window.draw(text);
    }

    window.setView(previous);
}

std::optional<int> Hud::hitTestSmeltButton(sf::Vector2f screenPos, sf::Vector2f windowSize) const
{
    const std::span<const FurnaceRecipe> all = allFurnaceRecipes();
    const sf::Vector2f origin = craftPanelOrigin(windowSize);

    for (std::size_t i = 0; i < all.size(); ++i)
    {
        const sf::Vector2f pos{origin.x,
                               origin.y + static_cast<float>(i) * (CRAFT_BUTTON_HEIGHT + SLOT_GAP)};
        const sf::FloatRect rect(pos, {CRAFT_BUTTON_WIDTH, CRAFT_BUTTON_HEIGHT});

        if (rect.contains(screenPos))
            return static_cast<int>(i);
    }

    return std::nullopt;
}
```

- [ ] **Step 3: Build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean (these two methods aren't called from anywhere yet - that's Task 4).

- [ ] **Step 4: Commit**

```bash
git add src/Hud/Hud.h src/Hud/Hud.cpp
git commit -m "feat: add the Furnace's manual-smelting panel to Hud"
```

---

## Task 4: Furnace smelting flow and E-key four-way resolution in `Game`

Wires the Furnace into play: standing on a placed Furnace and pressing `E` opens the smelt panel; clicking a button starts a background-ticking, single-slot smelt (no queue), mirroring the existing hand-craft flow's shape exactly but reading `FurnaceRecipe` instead of `CraftRecipe`, and writing its own `smelting`/`smeltingRecipeIndex`/`smeltProgress` state rather than reusing the crafting one (they operate over different recipe shapes; forcing a shared abstraction now would mean refactoring already-shipped, reviewed code for one additional consumer).

**Files:**
- Modify: `src/Game/Game.h` (new state + method decls, ~line 59-89)
- Modify: `src/Game/Game.cpp` (`toggleInventory`, `drawInventoryPanels`, `handleEvents`, `fixedUpdate`, new `startSmelt`/`updateSmelting`)

**Interfaces:**
- Consumes: `Hud::drawSmeltPanel`, `Hud::hitTestSmeltButton` (Task 3); `FurnaceRecipe`, `allFurnaceRecipes()` (Task 2); `Inventory::removeOne(ItemType)` (existing).
- Produces: `Game::startSmelt(int recipeIndex)`, `Game::updateSmelting(float dt)` — no other task in this plan depends on these.

- [ ] **Step 1: Add new state and method declarations**

In `src/Game/Game.h`, below `void updateCrafting(float dt);`, add:

```cpp
    void startSmelt(int recipeIndex);
    void updateSmelting(float dt);
```

In the private state section, below `std::optional<sf::Vector2i> openCraftingTableTile;`, add:

```cpp
    std::optional<sf::Vector2i> openFurnaceTile;

    bool smelting = false;
    int smeltingRecipeIndex = -1;
    float smeltProgress = 0.0f;
```

- [ ] **Step 2: Extend `toggleInventory` to a four-way tile resolution**

In `src/Game/Game.cpp`, replace `Game::toggleInventory`:

```cpp
void Game::toggleInventory()
{
    if (dragging)
        return;

    if (inventoryOpen)
    {
        inventoryOpen = false;
        openChestTile.reset();
        openCraftingTableTile.reset();
        openFurnaceTile.reset();
        return;
    }

    const sf::Vector2i tile = cursorTile();
    const Machine* machine = machines.at(tile.x, tile.y);

    openChestTile.reset();
    openCraftingTableTile.reset();
    openFurnaceTile.reset();

    if (machine != nullptr && machine->type == MachineType::Chest)
        openChestTile = tile;
    else if (machine != nullptr && machine->type == MachineType::CraftingTable)
        openCraftingTableTile = tile;
    else if (machine != nullptr && machine->type == MachineType::Furnace)
        openFurnaceTile = tile;

    inventoryOpen = true;
    buildMode = false;
}
```

- [ ] **Step 3: Draw the smelt panel when standing at a Furnace**

Replace `Game::drawInventoryPanels`:

```cpp
void Game::drawInventoryPanels()
{
    if (openChestTile.has_value())
    {
        const Machine* chest = machines.at(openChestTile->x, openChestTile->y);

        if (chest == nullptr || chest->type != MachineType::Chest)
            openChestTile.reset();
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

    if (openChestTile.has_value())
    {
        hud.drawChestPanel(window, machines.at(openChestTile->x, openChestTile->y)->storage);
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

- [ ] **Step 4: Dispatch clicks to the smelt panel**

In `src/Game/Game.cpp`'s `handleEvents()`, replace the `else if (inventoryOpen && mouse->button == sf::Mouse::Button::Left)` branch:

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
                {
                    const auto smeltHit = hud.hitTestSmeltButton(screenPos, windowSize);

                    if (smeltHit.has_value())
                        startSmelt(*smeltHit);
                    else
                        beginDrag();
                }
                else
                {
                    const auto craftHit =
                        hud.hitTestCraftButton(screenPos, windowSize, openCraftingTableTile.has_value());

                    if (craftHit.has_value())
                        startCraft(*craftHit);
                    else
                        beginDrag();
                }
            }
```

- [ ] **Step 5: Implement `startSmelt` and `updateSmelting`**

In `src/Game/Game.cpp`, below `Game::updateCrafting`, add:

```cpp
void Game::startSmelt(int recipeIndex)
{
    if (smelting)
        return;

    const std::span<const FurnaceRecipe> recipes = allFurnaceRecipes();
    if (recipeIndex < 0 || recipeIndex >= static_cast<int>(recipes.size()))
        return;

    const FurnaceRecipe& recipe = recipes[recipeIndex];
    Inventory& bag = player.inventory();

    if (bag.count(recipe.in) <= 0)
        return;

    bag.removeOne(recipe.in);

    smelting = true;
    smeltingRecipeIndex = recipeIndex;
    smeltProgress = 0.0f;
}

void Game::updateSmelting(float dt)
{
    if (!smelting)
        return;

    const std::span<const FurnaceRecipe> recipes = allFurnaceRecipes();
    const FurnaceRecipe& recipe = recipes[smeltingRecipeIndex];

    smeltProgress += dt;
    if (smeltProgress < recipe.seconds)
        return;

    Inventory& bag = player.inventory();
    const int leftover = bag.add({recipe.out, 1});

    if (leftover > 0)
    {
        const sf::Vector2f position =
            player.center() - sf::Vector2f{ItemEntity::SIZE * 0.5f, ItemEntity::SIZE * 0.5f};
        drops.emplace_back(ItemStack{recipe.out, leftover}, position, sf::Vector2f{0.0f, -60.0f});
    }

    smelting = false;
    smeltingRecipeIndex = -1;
    smeltProgress = 0.0f;
}
```

- [ ] **Step 6: Tick smelting every fixed step**

In `Game::fixedUpdate`, directly below `updateCrafting(dt);`, add:

```cpp
    updateSmelting(dt);
```

- [ ] **Step 7: Build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 8: Manually verify**

Run: `C:\Litharia\build\Debug\Litharia.exe`. (Placing a Furnace isn't wired up until Task 5, so for now: use build mode's existing F-keys/scroll to confirm the game still runs and nothing crashed. Full smelt-panel verification happens in Task 7, once a Furnace can actually be placed.)

- [ ] **Step 9: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: wire up the Furnace's manual-smelting flow and E-key four-way resolution"
```

---

## Task 5: Furniture placement — Chest, Crafting Table, and Furnace move to hotbar-select + right-click

Fixes the reported bug at its root: these three types are no longer part of build mode at all (no palette entry, no F-key, no scroll-cycling into them), so there is nothing to silently mis-select. They're placed exactly like a world block — select the item in your hotbar, right-click within reach.

**Files:**
- Modify: `src/Machines/MachineType.h` (new `isFurniture` declaration, ~line 60)
- Modify: `src/Machines/MachineRegistry.cpp` (implement `isFurniture`)
- Modify: `src/Hud/Hud.cpp` (`drawBuildPalette`'s filter loop)
- Modify: `src/Game/Game.h` (new method decl)
- Modify: `src/Game/Game.cpp` (`cycleBuildType`, `handleEvents` F-keys, `fixedUpdate`, new `placeFurnitureAtCursor`)
- Test: `tests/test_machines.cpp`

**Interfaces:**
- Consumes: `MachineType::Chest`, `CraftingTable`, `Furnace` (existing/Task 1); `Player::inReach(int, int)`, `Player::box()`, `Player::selectedSlot()` (existing, all public); `physics::overlaps(const AABB&, const AABB&)` (existing).
- Produces: `bool isFurniture(MachineType type)` — a single source of truth both `Hud::drawBuildPalette` and `Game::cycleBuildType` read, so the two can never disagree about which types build mode offers. `Game::placeFurnitureAtCursor(const PlayerInput&)` — Task 6 does not depend on this directly, but shares its file/vicinity.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_machines.cpp`:

```cpp
TEST_CASE("isFurniture is true for exactly Chest, Crafting Table, and Furnace")
{
    CHECK_FALSE(isFurniture(MachineType::None));
    CHECK_FALSE(isFurniture(MachineType::BurnerGenerator));
    CHECK_FALSE(isFurniture(MachineType::Drill));
    CHECK_FALSE(isFurniture(MachineType::Belt));
    CHECK_FALSE(isFurniture(MachineType::Chute));
    CHECK_FALSE(isFurniture(MachineType::Smelter));

    CHECK(isFurniture(MachineType::Chest));
    CHECK(isFurniture(MachineType::CraftingTable));
    CHECK(isFurniture(MachineType::Furnace));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: **compile error** — `'isFurniture': identifier not found`.

- [ ] **Step 3: Declare and implement `isFurniture`**

In `src/Machines/MachineType.h`, below the `itemForMachine` declaration, add:

```cpp
// True for machines placed/removed like a world block (hotbar-select,
// right-click to place, mine to remove) instead of through the build-mode
// palette: Chest, Crafting Table, Furnace.
bool isFurniture(MachineType type);
```

In `src/Machines/MachineRegistry.cpp`, below `itemForMachine`, add:

```cpp
bool isFurniture(MachineType type)
{
    return type == MachineType::Chest || type == MachineType::CraftingTable
        || type == MachineType::Furnace;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 209 | 209 passed | 0 failed`.

- [ ] **Step 5: Exclude furniture from the build palette**

In `src/Hud/Hud.cpp`, inside `Hud::drawBuildPalette`, change the `held` filter loop:

```cpp
    std::vector<MachineType> held;
    for (int i = FIRST; i < static_cast<int>(MachineType::Count); ++i)
    {
        const MachineType type = static_cast<MachineType>(i);

        if (isFurniture(type))
            continue;

        if (bag.count(itemForMachine(type)) > 0)
            held.push_back(type);
    }
```

- [ ] **Step 6: Exclude furniture from build-mode cycling**

In `src/Game/Game.cpp`, inside `Game::cycleBuildType`, change the search loop:

```cpp
    for (int step = 0; step < count; ++step)
    {
        index = ((index + delta) % count + count) % count;
        const MachineType candidate = static_cast<MachineType>(first + index);

        if (isFurniture(candidate))
            continue;

        if (bag.count(itemForMachine(candidate)) > 0)
        {
            setBuildType(candidate);
            return;
        }
    }
```

- [ ] **Step 7: Remove the Chest/Crafting Table F-keys**

In `src/Game/Game.cpp`'s `handleEvents()`, delete these two lines (leave F1-F5 exactly as they are):

```cpp
            if (key->code == Key::F6) setBuildType(MachineType::Chest);
            if (key->code == Key::F7) setBuildType(MachineType::CraftingTable);
```

- [ ] **Step 8: Add `placeFurnitureAtCursor`**

In `src/Game/Game.h`, below `void placeMachineAtCursor();`, add:

```cpp
    void placeFurnitureAtCursor(const PlayerInput& input);
```

In `src/Game/Game.cpp`, inside the anonymous namespace at the top of the file (alongside `toColor`/`itemColor`), add:

```cpp
// The item-to-machine reverse lookup only furniture placement needs -
// itemForMachine() goes the other way and is used by everything else.
MachineType furnitureMachineForItem(ItemType item)
{
    switch (item)
    {
        case ItemType::Chest:         return MachineType::Chest;
        case ItemType::CraftingTable: return MachineType::CraftingTable;
        case ItemType::Furnace:       return MachineType::Furnace;
        default:                     return MachineType::None;
    }
}
```

Below `Game::placeMachineAtCursor`, add:

```cpp
void Game::placeFurnitureAtCursor(const PlayerInput& input)
{
    if (!input.place)
        return;

    const ItemStack& held = player.inventory().slot(player.selectedSlot());
    const MachineType type = furnitureMachineForItem(held.type);

    if (type == MachineType::None)
        return;

    const sf::Vector2i tile = cursorTile();

    if (!player.inReach(tile.x, tile.y))
        return;

    const MachineInfo& info = machineInfo(type);

    for (int dy = 0; dy < info.height; ++dy)
        for (int dx = 0; dx < info.width; ++dx)
        {
            if (world.isSolid(tile.x + dx, tile.y + dy))
                return;

            const AABB tileBox{{static_cast<float>((tile.x + dx) * TILE_SIZE),
                                static_cast<float>((tile.y + dy) * TILE_SIZE)},
                               {static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)}};

            if (physics::overlaps(tileBox, player.box()))
                return;
        }

    if (machines.place(type, tile.x, tile.y, Direction::Right) == nullptr)
        return;

    player.inventory().removeOne(held.type);
}
```

- [ ] **Step 9: Call it from `fixedUpdate`**

In `src/Game/Game.cpp`, replace the top of `Game::fixedUpdate`:

```cpp
void Game::fixedUpdate(float dt)
{
    const ActionResult result = player.update(readInput(), world, dt, &machines);
```

with:

```cpp
void Game::fixedUpdate(float dt)
{
    const PlayerInput input = readInput();
    const ActionResult result = player.update(input, world, dt, &machines);
```

Then, directly below the existing `if (result.placed) chunks.markDirty(...)` block and before `updateDrops(dt);`, add:

```cpp
    placeFurnitureAtCursor(input);

```

- [ ] **Step 10: Build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 11: Manually verify**

Run: `C:\Litharia\build\Debug\Litharia.exe`. Press `B`: confirm the palette never offers Chest, Crafting Table, or Furnace even if you hold them (only Burner Generator/Drill/Belt/Chute/Smelter ever appear). Confirm `F6`/`F7` no longer do anything. If you hold a Crafting Table, select its hotbar slot and right-click within reach: confirm it places as a 2-wide machine exactly like build mode used to. Right-click out of reach, or onto solid rock: confirm nothing places and nothing is consumed.

- [ ] **Step 12: Commit**

```bash
git add src/Machines/MachineType.h src/Machines/MachineRegistry.cpp src/Hud/Hud.cpp src/Game/Game.h src/Game/Game.cpp tests/test_machines.cpp
git commit -m "feat: place Chest/Crafting Table/Furnace like blocks, out of build mode"
```

---

## Task 6: Mining furniture to remove it, with a shared refund helper

Completes the furniture interaction model: mine a placed Chest/Crafting Table/Furnace like a block (left-click-and-hold, outside build mode) to remove and refund it. Extracts the existing refund logic out of `removeMachineAtCursor` into a small shared helper so the new mining path doesn't duplicate it.

**Files:**
- Modify: `src/Game/Game.h` (new state + method decl, ~line 47-96)
- Modify: `src/Game/Game.cpp` (`removeMachineAtCursor`, new `refundMachineItem`/`mineFurnitureAtCursor`, `fixedUpdate`)

**Interfaces:**
- Consumes: `isFurniture(MachineType)` (Task 5); `Player::inReach(int, int)` (existing); `itemForMachine` (existing).
- Produces: `Game::refundMachineItem(MachineType type)`, `Game::mineFurnitureAtCursor(const PlayerInput&, float dt)` — no other task depends on these; this is the plan's last code task.

- [ ] **Step 1: Add new state and the method declaration**

In `src/Game/Game.h`, below `void placeFurnitureAtCursor(const PlayerInput& input);`, add:

```cpp
    void mineFurnitureAtCursor(const PlayerInput& input, float dt);
    void refundMachineItem(MachineType type);
```

In the private state section, below the `smelting`/`smeltingRecipeIndex`/`smeltProgress` fields, add:

```cpp
    bool miningFurniture = false;
    sf::Vector2i miningFurnitureTarget{0, 0};
    float miningFurnitureProgress = 0.0f;
```

- [ ] **Step 2: Extract the refund helper**

In `src/Game/Game.cpp`, replace `Game::removeMachineAtCursor`:

```cpp
void Game::removeMachineAtCursor()
{
    const sf::Vector2i tile = cursorTile();
    const Machine* target = machines.at(tile.x, tile.y);
    if (target == nullptr)
        return;

    const MachineType type = target->type;
    if (!machines.remove(tile.x, tile.y))
        return;

    refundMachineItem(type);
}

void Game::refundMachineItem(MachineType type)
{
    const ItemType item = itemForMachine(type);
    const int leftover = player.inventory().add({item, 1});

    if (leftover > 0)
    {
        const sf::Vector2f position =
            player.center() - sf::Vector2f{ItemEntity::SIZE * 0.5f, ItemEntity::SIZE * 0.5f};
        drops.emplace_back(ItemStack{item, leftover}, position, sf::Vector2f{0.0f, -60.0f});
    }
}
```

- [ ] **Step 3: Add `MINING_FURNITURE_SECONDS` and `mineFurnitureAtCursor`**

In `src/Game/Game.cpp`'s anonymous namespace at the top of the file, below `MAX_FRAME_TIME`, add:

```cpp
// How long mining down a placed Chest/Crafting Table/Furnace takes - flat,
// no tool requirement, unlike world blocks: you built it, you can always
// take it back down.
constexpr float MINING_FURNITURE_SECONDS = 1.0f;
```

Below `Game::placeFurnitureAtCursor`, add:

```cpp
void Game::mineFurnitureAtCursor(const PlayerInput& input, float dt)
{
    const sf::Vector2i tile = cursorTile();
    const Machine* target = machines.at(tile.x, tile.y);
    const bool validTarget = input.mine && target != nullptr && isFurniture(target->type)
        && player.inReach(tile.x, tile.y);

    if (!validTarget)
    {
        miningFurniture = false;
        miningFurnitureProgress = 0.0f;
        return;
    }

    if (!miningFurniture || miningFurnitureTarget.x != tile.x || miningFurnitureTarget.y != tile.y)
    {
        miningFurniture = true;
        miningFurnitureTarget = tile;
        miningFurnitureProgress = 0.0f;
    }

    miningFurnitureProgress += dt;
    if (miningFurnitureProgress < MINING_FURNITURE_SECONDS)
        return;

    const MachineType type = target->type;
    if (!machines.remove(tile.x, tile.y))
        return;

    refundMachineItem(type);

    miningFurniture = false;
    miningFurnitureProgress = 0.0f;
}
```

- [ ] **Step 4: Call it from `fixedUpdate`**

In `src/Game/Game.cpp`'s `fixedUpdate`, directly below `placeFurnitureAtCursor(input);`, add:

```cpp
    mineFurnitureAtCursor(input, dt);
```

- [ ] **Step 5: Build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia`
Expected: builds clean.

- [ ] **Step 6: Run the test suite to confirm no regression**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 209 | 209 passed | 0 failed` (unchanged from Task 5 - this task touches only executable-only files).

- [ ] **Step 7: Manually verify**

Run: `C:\Litharia\build\Debug\Litharia.exe`. Place a Chest (or Crafting Table). Aim at it and hold left-click outside build mode: confirm it takes about a second, then disappears, and the item reappears in your bag. Confirm releasing left-click partway through and re-aiming resets progress (matching how mining a block already behaves). Confirm mining an *ordinary world block* (dirt/stone) is completely unaffected by this change.

- [ ] **Step 8: Commit**

```bash
git add src/Game/Game.h src/Game/Game.cpp
git commit -m "feat: mine furniture to remove it, and share the refund helper"
```

---

## Task 7: End-to-end verification

No code changes. This supersedes the crafting-menu plan's own incomplete Task 10 (which never finished a live pass): it covers everything that plan deferred, plus everything this one adds, in one pass.

**Files:** none.

- [ ] **Step 1: Full build**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia` and `cmake --build C:\Litharia\build --config Debug --target Litharia_tests`
Expected: both build clean; `C:\Litharia\build\Debug\Litharia_tests.exe` shows `test cases: 209 | 209 passed | 0 failed`.

- [ ] **Step 2: The originally reported bug is fixed**

Run `C:\Litharia\build\Debug\Litharia.exe`. Chop >= 15 Oak Log. Press `E` in the open, craft a Crafting Table, wait for it to land in your bag. Select its hotbar slot, right-click within reach: confirm it places immediately as a 2-wide machine, with **no** need to press `B`, scroll, or press any F-key first. This is the original complaint - confirm it's simply gone.

- [ ] **Step 3: Furnace bootstrap chain**

Stand at the placed Crafting Table, press `E`: confirm the advanced menu now lists Chest, Belt, Chute, Burner Generator, Drill, Smelter, **and Furnace** (7 buttons), Furnace's costing 20 Stone. Mine 20 Stone, craft a Furnace, select its hotbar slot, right-click to place it: confirm it renders as a **2x2** block (visibly square, twice the width AND twice the height of a normal 1x1 machine), with no fuel/progress bar or I/O ticks. Stand at it, press `E`: confirm the smelt panel shows exactly 2 buttons ("Copper Ore -> Copper Plate", "Iron Ore -> Iron Plate"), dimmed if you have no matching ore. Mine some Copper Ore and/or Iron Ore, click a smelt button: confirm the ore is deducted immediately, a progress fill appears, the other button dims too while it's running, and a plate lands in your bag after the recipe's seconds (5s copper / 7.5s iron). With plates in hand, confirm you can now craft a Smelter, Drill, Belt, and/or Burner Generator at the table - the original softlock is gone.

- [ ] **Step 4: Furniture is fully out of build mode**

Press `B` while holding a Chest, Crafting Table, and/or Furnace: confirm none of them ever appear in the palette, and confirm `F6`/`F7` do nothing. Confirm the palette still correctly lists and counts Burner Generator/Drill/Belt/Chute/Smelter exactly as before, and that placing/destroying those through build mode still consumes/refunds correctly.

- [ ] **Step 5: Furniture removal**

Mine down a placed Chest (or Crafting Table, or Furnace) with left-click-and-hold: confirm it takes about a second and refunds the item to your bag. Confirm doing this to a Chest that still has items in its storage doesn't destroy those items - they should behave exactly as before this plan (test this only if convenient; if the Chest's stored-item handling on removal was already unspecified/untouched by this plan, note whatever you observe rather than assuming).

- [ ] **Step 6: Regression check - existing systems untouched**

Confirm mining/placing world blocks, the hotbar, chest deposit/collect, drag-and-drop between bag and chest, and machine power/smelting/drilling for Burner Generator/Drill/Smelter/Belt/Chute all still work exactly as before. Confirm the build-mode palette's counts and consume/refund behavior for the 5 remaining factory types (from the original crafting-menu plan) still work.

- [ ] **Step 7: Final automated test run**

Run: `cmake --build C:\Litharia\build --config Debug --target Litharia_tests` then `C:\Litharia\build\Debug\Litharia_tests.exe`
Expected: `test cases: 209 | 209 passed | 0 failed`.
