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
}

void Machines::insertOutputAhead(Machine&)
{
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

void Machines::tick(World& world, float dt, std::vector<sf::Vector2i>& minedTiles)
{
    (void)world;
    (void)minedTiles;

    updatePower();
    tickTransport(dt);
}
