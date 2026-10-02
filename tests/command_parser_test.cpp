#include "console/command_parser.h"

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

int main() {
    testSplitCommand();
    testSuggestions();
    testCoordinates();
    std::cout << "command_parser_test: PASS\n";
    return 0;
}
