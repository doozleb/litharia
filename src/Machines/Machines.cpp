#include "Machines.h"

#include <algorithm>
#include <array>

#include "Recipes.h"
#include "../World/World.h"

namespace
{

// Adds one item to a single-stack buffer if it fits (empty, or same type below
// max). Returns false if the buffer is occupied by something else or full.
bool addToBuffer(ItemStack& buffer, ItemType item)
{
    if (buffer.empty())
    {
        buffer.type = item;
        buffer.count = 1;
        return true;
    }

    if (buffer.type == item && buffer.count < itemInfo(item).maxStack)
    {
        ++buffer.count;
        return true;
    }

    return false;
}

// True for blocks a drill should mine (they drop themselves as an ore/fuel item).
// Derives from DRILL_ORES so the mining logic and the tooltip's "Mines: ..."
// line can never list different ores.
bool isOre(BlockType b)
{
    const ItemType item = itemForBlock(b);
    return std::find(DRILL_ORES.begin(), DRILL_ORES.end(), item) != DRILL_ORES.end();
}

} // namespace

int Machines::indexAt(int x, int y) const
{
    const auto it = byTile.find(key(x, y));
    return it == byTile.end() ? -1 : it->second;
}

bool Machines::canPlace(int x, int y) const
{
    return indexAt(x, y) < 0;
}

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
    else if (type == MachineType::ItemAcceptor)
        m.storage = Inventory(ITEM_ACCEPTOR_SLOTS);

    machines.push_back(m);
    const int index = static_cast<int>(machines.size()) - 1;

    for (int dy = 0; dy < info.height; ++dy)
        for (int dx = 0; dx < info.width; ++dx)
            byTile[key(x + dx, y + dy)] = index;

    return &machines[index];
}

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

Machine* Machines::at(int x, int y)
{
    const int index = indexAt(x, y);
    return index < 0 ? nullptr : &machines[index];
}

const Machine* Machines::at(int x, int y) const
{
    const int index = indexAt(x, y);
    return index < 0 ? nullptr : &machines[index];
}

// --- Stubs filled in by later tasks. They must compile now. -------------------

bool Machines::tryInsert(int x, int y, ItemType item, std::optional<Direction> fromSide)
{
    if (item == ItemType::None)
        return false;

    Machine* m = at(x, y);
    if (m == nullptr)
        return false;

    const MachineInfo& info = machineInfo(m->type);

    if (info.transport)
    {
        if (m->carried != ItemType::None)
            return false;

        m->carried = item;
        m->carryTimer = info.actionTime;
        return true;
    }

    if (m->type == MachineType::ItemAcceptor)
        return m->storage.add({item, 1}) == 0;

    if (isSmelter(m->type))
    {
        // Like a drill, a smelter has exactly one input side: its facing. A
        // caller with no directional context (fromSide unset - the player's F
        // key, direct calls) stays unrestricted.
        if (fromSide.has_value() && fromSide.value() != m->facing)
            return false;

        if (smeltRecipeFor(item) == nullptr)
            return false;

        return addToBuffer(m->input, item);
    }

    if (m->type == MachineType::BurnerGenerator)
    {
        if (item != ItemType::Coal)
            return false;

        return addToBuffer(m->input, item);
    }

    return false;
}

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

    if (m->type == MachineType::ItemAcceptor)
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

    if (m->type == MachineType::ItemAcceptor)
    {
        m->storage.add(stack);
        return;
    }

    const MachineInfo& info = machineInfo(m->type);

    if (info.transport)
        m->carried = stack.type; // transport holds exactly one item; count is always 1
    else
        m->output = stack;
}

MachineStatus Machines::inspect(int x, int y, const World& world) const
{
    const Machine* m = at(x, y);
    if (m == nullptr)
        return {};

    MachineStatus status = barStatus(*m);
    status.reason = idleReason(*m, world);
    return status;
}

std::string Machines::idleReason(const Machine& m, const World& world) const
{
    const MachineInfo& info = machineInfo(m.type);

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

    if (isDrill(m.type))
    {
        if (!m.output.empty())
            return "Output is full.";

        for (int dy = 1; dy <= DRILL_REACH; ++dy)
            if (isOre(world.get(m.x, m.y + dy)))
                return "";

        return "No ore within " + std::to_string(DRILL_REACH) + " tiles below.";
    }

    if (isSmelter(m.type))
    {
        if (m.input.empty())
            return "Waiting for ore.";

        // tryInsert only ever accepts items with a recipe, so a non-empty input
        // is always smeltable: no null check needed here.
        const SmeltRecipe* recipe = smeltRecipeFor(m.input.type);

        const bool outputReady = m.output.empty()
            || (m.output.type == recipe->out && m.output.count < itemInfo(recipe->out).maxStack);

        return outputReady ? "" : "Output is full.";
    }

    if (m.type == MachineType::BurnerGenerator)
        return (m.fuel <= 0.0f && m.input.empty()) ? "Out of fuel: needs coal." : "";

    return "";
}

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

void Machines::insertOutput(Machine& m)
{
    if (m.output.empty())
        return;

    // facing is reserved for input (it should face an ore vein or a feeder
    // belt) and is never tried here - only the other 3 sides are output
    // candidates, in Direction's declared order (Up, Down, Left, Right).
    constexpr std::array<Direction, 4> ALL = {Direction::Up, Direction::Down, Direction::Left,
                                               Direction::Right};

    std::array<Direction, 3> candidates{};
    int candidateCount = 0;
    for (Direction d : ALL)
        if (d != m.facing)
            candidates[candidateCount++] = d;

    int startIndex = 0;
    for (int i = 0; i < candidateCount; ++i)
        if (candidates[i] == m.outputCursor)
            startIndex = i;

    for (int i = 0; i < candidateCount; ++i)
    {
        const Direction dir = candidates[(startIndex + i) % candidateCount];
        const int tx = m.x + dirDX(dir);
        const int ty = m.y + dirDY(dir);

        if (!tryInsert(tx, ty, m.output.type, oppositeDirection(dir)))
            continue;

        --m.output.count;
        if (m.output.count == 0)
            m.output.type = ItemType::None;

        // Next attempt starts one past the side that just worked, so several
        // belts around the machine take turns instead of one hogging delivery.
        m.outputCursor = candidates[(startIndex + i + 1) % candidateCount];
        return;
    }
}

void Machines::tickTransport(float dt)
{
    for (Machine& m : machines)
    {
        const MachineInfo& info = machineInfo(m.type);

        if (!info.transport || m.carried == ItemType::None)
            continue;

        m.carryTimer -= dt;
        if (m.carryTimer > 0.0f)
            continue;

        m.carryTimer = 0.0f;

        // Chutes always drop down; belts move toward their facing.
        const Direction dir = isChute(m.type) ? Direction::Down : m.facing;
        const int tx = m.x + dirDX(dir);
        const int ty = m.y + dirDY(dir);

        // tryInsert may relocate storage on nothing here (it does not place), so it
        // is safe. On success the item leaves this belt.
        if (tryInsert(tx, ty, m.carried, oppositeDirection(dir)))
            m.carried = ItemType::None;
    }
}

void Machines::tickGenerators(float dt)
{
    for (Machine& m : machines)
    {
        if (m.type != MachineType::BurnerGenerator)
            continue;

        // Light a fresh coal only when the last one is spent.
        if (m.fuel <= 0.0f && m.input.type == ItemType::Coal && m.input.count > 0)
        {
            --m.input.count;
            if (m.input.count == 0)
                m.input.type = ItemType::None;

            m.fuel += COAL_BURN_SECONDS;
        }

        // Burn only while actually powering something. Asking whether anything
        // nearby *wants* power is not enough: a generator whose neighbours all
        // went unpowered would burn coal for nothing.
        if (m.fuel > 0.0f && m.supplying)
            m.fuel = std::max(0.0f, m.fuel - dt);
    }
}

void Machines::tickDrills(World& world, float dt, std::vector<sf::Vector2i>& minedTiles)
{
    for (Machine& m : machines)
    {
        if (!isDrill(m.type))
            continue;

        // Always try to push any held output onto the machine ahead.
        insertOutput(m);

        if (!m.powered || !m.output.empty())
        {
            m.progress = 0.0f;
            continue;
        }

        // Find the nearest ore straight below, within reach.
        int oreY = -1;
        for (int dy = 1; dy <= DRILL_REACH; ++dy)
        {
            if (isOre(world.get(m.x, m.y + dy)))
            {
                oreY = m.y + dy;
                break;
            }
        }

        if (oreY < 0)
        {
            m.progress = 0.0f; // nothing to mine: idle
            continue;
        }

        m.progress += dt;

        if (m.progress >= machineInfo(m.type).actionTime)
        {
            // The vein is inexhaustible: mining does not remove the block, so the
            // same tile keeps producing and the chunk mesh never needs a redraw.
            m.output = {itemForBlock(world.get(m.x, oreY)), 1};
            m.progress = 0.0f;
        }
    }
}

void Machines::tickSmelters(float dt)
{
    for (Machine& m : machines)
    {
        if (!isSmelter(m.type))
            continue;

        insertOutput(m);

        const SmeltRecipe* recipe = m.input.empty() ? nullptr : smeltRecipeFor(m.input.type);

        const bool outputReady = m.output.empty()
            || (recipe != nullptr && m.output.type == recipe->out
                && m.output.count < itemInfo(recipe->out).maxStack);

        if (!m.powered || recipe == nullptr || !outputReady)
        {
            if (recipe == nullptr)
                m.progress = 0.0f;
            continue;
        }

        m.progress += dt;

        if (m.progress >= recipe->seconds * machineInfo(m.type).speedMultiplier)
        {
            --m.input.count;
            if (m.input.count == 0)
                m.input.type = ItemType::None;

            if (m.output.empty())
                m.output = {recipe->out, 1};
            else
                ++m.output.count;

            m.progress = 0.0f;
        }
    }
}

void Machines::tick(World& world, float dt, std::vector<sf::Vector2i>& minedTiles)
{
    updatePower();
    tickGenerators(dt);
    tickDrills(world, dt, minedTiles);
    tickSmelters(dt);
    tickTransport(dt);
}
