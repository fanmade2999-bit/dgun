#include "command_console.h"

#include "command_parser.h"

#include <cmath>
#include <string>

namespace dg::console {

std::string executeCommand(const std::string& raw,const CommandGameState& state) {
    const auto p=splitCommand(raw);
    if(p.empty()) return {};

    std::string cmd=p[0];
    if(!cmd.empty() && cmd[0]=='/') cmd=cmd.substr(1);

    if(cmd=="summon") {
        if(p.size()<2) return "USAGE: /summon <npc|boss> <name> [x] [z]";
        const bool typed=p[1]=="npc" || p[1]=="boss";
        if(typed && p.size()<3) return "USAGE: /summon "+p[1]+" <name> [x] [z]";
        const std::string name=typed?p[2]:p[1];
        const int kind=state.resolveEntityKind(name);
        if(kind<0) return "UNKNOWN ENTITY: "+name;
        if(typed && ((p[1]=="npc")==state.isBossKind(kind))) return "TYPE MISMATCH";

        const int coord=typed?3:2;
        float x=state.playerX;
        float z=state.playerZ;
        if(static_cast<int>(p.size())==coord) {
            const float angle=float(state.nextSummonedId)*0.91f;
            const float radius=state.isBossKind(kind)?4.0f:2.4f;
            x=state.playerX+std::cos(angle)*radius;
            z=state.playerZ+std::sin(angle)*radius;
        }
        if(static_cast<int>(p.size())>coord && !parseCommandFloat(p[coord],state.playerX,x))
            return "BAD X";
        if(static_cast<int>(p.size())>coord+1 && !parseCommandFloat(p[coord+1],state.playerZ,z))
            return "BAD Z";

        const SpawnResult result=state.summon(kind,x,z);
        return result.message;
    }

    if(cmd=="list") {
        const SummonCounts counts=state.countSummons();
        return "SUMMONS "+std::to_string(counts.total)+
               " | NPC "+std::to_string(counts.npcs)+
               " | BOSSES "+std::to_string(counts.bosses);
    }

    if(cmd=="kill") {
        if(p.size()<2) return "USAGE: /kill <id|all|npcs|bosses>";
        return "KILLED "+std::to_string(state.killSummons(p[1]));
    }

    if(cmd=="clear") {
        return "CLEARED "+std::to_string(state.clearSummons())+" SUMMONS";
    }

    if(cmd=="tp") {
        if(p.size()<3) return "USAGE: /tp <x> <z>";
        float x=0.0f;
        float z=0.0f;
        if(!parseCommandFloat(p[1],state.playerX,x) ||
           !parseCommandFloat(p[2],state.playerZ,z)) return "BAD COORDS";
        state.teleportPlayer(x,z);
        return "TELEPORTED TO "+std::to_string(static_cast<int>(std::round(x)))+
               " "+std::to_string(static_cast<int>(std::round(z)));
    }

    return "UNKNOWN COMMAND";
}

} // namespace dg::console
