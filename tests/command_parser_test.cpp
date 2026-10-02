#include "console/command_parser.h"
#include "console/command_console.h"

#include <cassert>
#include <cmath>
#include <iostream>

using dg::console::commandSuggestions;
using dg::console::parseCommandFloat;
using dg::console::splitCommand;

static void testSplitCommand() {
    const auto tokens=splitCommand(" /summon\tnpc mechanic  ~ 5\n");
    assert(tokens.size()==5);
    assert(tokens[0]=="/summon");
    assert(tokens[1]=="npc");
    assert(tokens[2]=="mechanic");
    assert(tokens[3]=="~");
    assert(tokens[4]=="5");
}

static void testSuggestions() {
    const auto root=commandSuggestions("/");
    assert(root.size()==5);
    const auto summon=commandSuggestions("/summon ");
    assert(summon.size()==2);
    assert(summon[0]=="/summon npc");
    const auto names=commandSuggestions("/summon npc ");
    assert(!names.empty());
    assert(names.front()=="/summon npc survivor");
    const auto filtered=commandSuggestions("/summon boss ty");
    assert(filtered.size()==1);
    assert(filtered.front()=="/summon boss tyrant");
}

static void testCoordinates() {
    float value=0.0f;
    assert(parseCommandFloat("~",10.0f,value) && value==10.0f);
    assert(parseCommandFloat("~5",10.0f,value) && value==15.0f);
    assert(parseCommandFloat("~-2.5",10.0f,value) && std::fabs(value-7.5f)<0.0001f);
    assert(parseCommandFloat("-3.25",10.0f,value) && std::fabs(value+3.25f)<0.0001f);
    assert(!parseCommandFloat("~oops",10.0f,value));
    assert(!parseCommandFloat("12oops",10.0f,value));
}

static void testCommandGameStateInterface() {
    dg::console::CommandGameState state;
    state.playerX=10.0f;
    state.playerZ=20.0f;
    state.nextSummonedId=4;
    float summonedX=0.0f;
    float summonedZ=0.0f;
    float teleportedX=0.0f;
    float teleportedZ=0.0f;
    state.resolveEntityKind=[](const std::string& name){ return name=="mechanic"?1:-1; };
    state.isBossKind=[](int kind){ return kind==2; };
    state.summon=[&](int kind,float x,float z){
        summonedX=x;
        summonedZ=z;
        return dg::console::SpawnResult{kind==1,"SUMMONED mechanic #4"};
    };
    state.countSummons=[](){ return dg::console::SummonCounts{3,2,1}; };
    state.killSummons=[](const std::string& selector){ return selector=="all"?3:1; };
    state.clearSummons=[](){ return 3; };
    state.teleportPlayer=[&](float x,float z){ teleportedX=x; teleportedZ=z; };

    assert(dg::console::executeCommand("/summon npc mechanic ~ ~5",state)=="SUMMONED mechanic #4");
    assert(std::fabs(summonedX-10.0f)<0.0001f);
    assert(std::fabs(summonedZ-25.0f)<0.0001f);
    assert(dg::console::executeCommand("/list",state)=="SUMMONS 3 | NPC 2 | BOSSES 1");
    assert(dg::console::executeCommand("/kill all",state)=="KILLED 3");
    assert(dg::console::executeCommand("/clear",state)=="CLEARED 3 SUMMONS");
    assert(dg::console::executeCommand("/tp ~-2 5",state)=="TELEPORTED TO 8 5");
    assert(std::fabs(teleportedX-8.0f)<0.0001f);
    assert(std::fabs(teleportedZ-5.0f)<0.0001f);
}

int main() {
    testSplitCommand();
    testSuggestions();
    testCoordinates();
    testCommandGameStateInterface();
    std::cout << "command_parser_test: PASS\n";
    return 0;
}
