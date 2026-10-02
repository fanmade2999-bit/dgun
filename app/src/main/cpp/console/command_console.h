#pragma once

#include <functional>
#include <string>

namespace dg::console {

struct SpawnResult {
    bool success=false;
    std::string message;
};

struct SummonCounts {
    int total=0;
    int npcs=0;
    int bosses=0;
};

// The command system depends on this narrow game-facing contract rather than
// directly reaching into the renderer or the legacy global game state.
struct CommandGameState {
    float playerX=0.0f;
    float playerZ=0.0f;
    int nextSummonedId=1;

    std::function<int(const std::string&)> resolveEntityKind;
    std::function<bool(int)> isBossKind;
    std::function<SpawnResult(int,float,float)> summon;
    std::function<SummonCounts()> countSummons;
    std::function<int(const std::string&)> killSummons;
    std::function<int()> clearSummons;
    std::function<void(float,float)> teleportPlayer;
};

std::string executeCommand(const std::string& raw,const CommandGameState& state);

} // namespace dg::console
