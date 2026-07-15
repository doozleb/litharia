#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <SFML/System/Vector2.hpp>

#include "Machine.h"
#include "MachineStatus.h"

class World;

// Owns every placed machine, indexes them by tile, and drives the whole factory
// one fixed step at a time. Pure logic: no SFML Graphics, so it lives in the core
// library and the test binary.
class Machines
{
public:
    bool canPlace(int x, int y) const;

    // Places a machine, or returns nullptr if the tile is taken. The pointer is
    // valid only until the next remove(): removal may relocate storage.
    Machine* place(MachineType type, int x, int y, Direction facing);

    bool remove(int x, int y);

    Machine* at(int x, int y);
    const Machine* at(int x, int y) const;

    // Hands one item into the machine at (x, y). Returns true if it was accepted.
    // Added behaviour in Task 8.
    bool tryInsert(int x, int y, ItemType item);

    // Takes whatever a machine is holding to give away: a Drill/Smelter's whole
    // output stack, or a Belt/Chute's carried item (only once carryTimer <= 0.0f -
    // mid-transfer refuses). Empty stack if there is nothing to take. The
    // counterpart to tryInsert.
    ItemStack tryExtract(int x, int y);

    // Gives a stack back to the machine at (x, y), into whichever buffer
    // tryExtract() would have taken it from. No-op for an empty stack or an
    // empty tile. Used when the taker (the player's bag) could not hold
    // everything tryExtract() handed over, so nothing is ever destroyed.
    // Assumes that buffer is the one tryExtract() just emptied: it overwrites
    // rather than merges, so calling it on an already-occupied buffer clobbers it.
    void putBack(int x, int y, ItemStack stack);

    // Read-only: bar + fraction from barStatus(), plus a reason string that
    // explains why the machine is idle/unpowered (empty when it is running fine,
    // or the tile is empty/not a processing machine).
    MachineStatus inspect(int x, int y, const World& world) const;

    // Rebuilds power networks and sets each machine's powered flag. Task 7.
    void updatePower();

    // One fixed simulation step of the whole factory. Fills minedTiles with any
    // world tiles a machine destroyed this step, so the caller can flag them for
    // redraw. Currently always empty: drilling no longer destroys the block it
    // mines. Kept for whatever future machine does turn a tile to air.
    // Task 13 assembles the full body; earlier tasks build the helpers it calls.
    void tick(World& world, float dt, std::vector<sf::Vector2i>& minedTiles);

    std::size_t count() const { return machines.size(); }
    const std::vector<Machine>& all() const { return machines; }

private:
    static long long key(int x, int y)
    {
        return (static_cast<long long>(x) << 32) ^ static_cast<unsigned>(y);
    }

    int indexAt(int x, int y) const; // -1 if no machine there

    // Task 7 helpers.
    void assignNetworks();

    // Task 8-13 helpers.
    void insertOutputAhead(Machine& m);
    void tickTransport(float dt);
    void tickGenerators(float dt);
    void tickDrills(World& world, float dt, std::vector<sf::Vector2i>& minedTiles);
    void tickSmelters(float dt);

    std::string idleReason(const Machine& m, const World& world) const;

    std::vector<Machine> machines;
    std::unordered_map<long long, int> byTile;

    std::vector<float> networkDemand; // demand per network id, filled by updatePower
    std::vector<float> networkSupply; // per-network supply, alongside networkDemand
};
