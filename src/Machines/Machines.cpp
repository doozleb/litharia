#include "Machines.h"

#include <algorithm>
#include <queue>

#include "Recipes.h"
#include "../World/World.h"

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

bool Machines::tryInsert(int, int, ItemType)
{
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

void Machines::tick(World&, float, std::vector<sf::Vector2i>&)
{
}
