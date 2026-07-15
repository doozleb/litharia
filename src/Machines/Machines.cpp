#include "Machines.h"

#include <algorithm>
#include <queue>

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
bool isOre(BlockType b)
{
    return b == BlockType::CopperOre || b == BlockType::IronOre || b == BlockType::Coal;
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
    if (!canPlace(x, y))
        return nullptr;

    Machine m;
    m.type = type;
    m.x = x;
    m.y = y;
    m.facing = facing;

    machines.push_back(m);
    const int index = static_cast<int>(machines.size()) - 1;
    byTile[key(x, y)] = index;

    return &machines[index];
}

bool Machines::remove(int x, int y)
{
    const int index = indexAt(x, y);
    if (index < 0)
        return false;

    const int last = static_cast<int>(machines.size()) - 1;

    // Swap the doomed machine with the last, so the vector stays dense, then fix
    // the moved machine's tile entry.
    if (index != last)
    {
        machines[index] = machines[last];
        byTile[key(machines[index].x, machines[index].y)] = index;
    }

    machines.pop_back();
    byTile.erase(key(x, y));

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

bool Machines::tryInsert(int x, int y, ItemType item)
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

    if (m->type == MachineType::Smelter)
    {
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
        const bool hasSupply = m.network >= 0
            && m.network < static_cast<int>(networkSupply.size())
            && networkSupply[m.network] > 0.0f;

        return hasSupply ? "No power: network demand exceeds supply."
                          : "No power: no fuel in this network.";
    }

    if (m.type == MachineType::Drill)
    {
        if (!m.output.empty())
            return "Output is full.";

        for (int dy = 1; dy <= DRILL_REACH; ++dy)
            if (isOre(world.get(m.x, m.y + dy)))
                return "";

        return "No ore within " + std::to_string(DRILL_REACH) + " tiles below.";
    }

    if (m.type == MachineType::Smelter)
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

void Machines::assignNetworks()
{
    for (Machine& m : machines)
        m.network = -1;

    int next = 0;

    for (std::size_t start = 0; start < machines.size(); ++start)
    {
        if (machines[start].network != -1)
            continue;

        // Flood fill orthogonally connected machines into one network.
        const int id = next++;
        std::queue<int> frontier;
        machines[start].network = id;
        frontier.push(static_cast<int>(start));

        while (!frontier.empty())
        {
            const Machine& m = machines[frontier.front()];
            frontier.pop();

            const int nx[4] = {m.x - 1, m.x + 1, m.x, m.x};
            const int ny[4] = {m.y, m.y, m.y - 1, m.y + 1};

            for (int i = 0; i < 4; ++i)
            {
                const int neighbour = indexAt(nx[i], ny[i]);
                if (neighbour >= 0 && machines[neighbour].network == -1)
                {
                    machines[neighbour].network = id;
                    frontier.push(neighbour);
                }
            }
        }
    }
}

void Machines::updatePower()
{
    assignNetworks();

    int networkCount = 0;
    for (const Machine& m : machines)
        networkCount = std::max(networkCount, m.network + 1);

    std::vector<float> supply(networkCount, 0.0f);
    std::vector<float> demand(networkCount, 0.0f);

    for (const Machine& m : machines)
    {
        const MachineInfo& info = machineInfo(m.type);

        if (info.generator && m.fuel > 0.0f)
            supply[m.network] += info.powerRating;

        if (info.consumer)
            demand[m.network] += info.powerRating;
    }

    for (Machine& m : machines)
    {
        const MachineInfo& info = machineInfo(m.type);
        m.powered = info.consumer && supply[m.network] >= demand[m.network];
    }

    networkDemand = demand;
    networkSupply = supply;
}

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
        const Direction dir = (m.type == MachineType::Chute) ? Direction::Down : m.facing;
        const int tx = m.x + dirDX(dir);
        const int ty = m.y + dirDY(dir);

        // tryInsert may relocate storage on nothing here (it does not place), so it
        // is safe. On success the item leaves this belt.
        if (tryInsert(tx, ty, m.carried))
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

        // Burn only under load, so an idle base does not drain its fuel.
        const bool hasLoad = m.network >= 0
            && m.network < static_cast<int>(networkDemand.size())
            && networkDemand[m.network] > 0.0f;

        if (m.fuel > 0.0f && hasLoad)
            m.fuel = std::max(0.0f, m.fuel - dt);
    }
}

void Machines::tickDrills(World& world, float dt, std::vector<sf::Vector2i>& minedTiles)
{
    for (Machine& m : machines)
    {
        if (m.type != MachineType::Drill)
            continue;

        // Always try to push any held output onto the machine ahead.
        insertOutputAhead(m);

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
            const BlockType ore = world.get(m.x, oreY);
            const ItemType drop = itemForBlock(ore);

            world.set(m.x, oreY, BlockType::Air);
            minedTiles.push_back({m.x, oreY});

            m.output = {drop, 1};
            m.progress = 0.0f;
        }
    }
}

void Machines::tickSmelters(float dt)
{
    for (Machine& m : machines)
    {
        if (m.type != MachineType::Smelter)
            continue;

        insertOutputAhead(m);

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

        if (m.progress >= recipe->seconds)
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
