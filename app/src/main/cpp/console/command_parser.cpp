#include "command_parser.h"

#include <cmath>
#include <cstdlib>

namespace dg::console {

std::vector<std::string> splitCommand(const std::string& line) {
    std::vector<std::string> out;
    std::string token;
    for(char c:line) {
        if(c==' ' || c=='\t' || c=='\n' || c=='\r') {
            if(!token.empty()) {
                out.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if(!token.empty()) out.push_back(token);
    return out;
}

std::vector<std::string> commandSuggestions(const std::string& text) {
    const std::vector<std::string> commands={"/summon","/list","/kill","/tp","/clear"};
    const std::vector<std::string> npcs={"survivor","mechanic","scientist","trader","scout","medic","ranger"};
    const std::vector<std::string> bosses={"samurai","tyrant","dreadnought","raptor","behemoth","sentinel"};
    std::vector<std::string> out;
    if(text.empty() || text=="/") return commands;

    const bool trailing=!text.empty() && (text.back()==' ' || text.back()=='\t');
    const auto p=splitCommand(text);

    if(p.size()==1 && !trailing) {
        if(p[0]=="summon" || p[0]=="/summon")
            return {"/summon npc","/summon boss"};
        for(const auto& c:commands)
            if(c.rfind(text,0)==0) out.push_back(c);
        return out;
    }

    if(!p.empty() && (p[0]=="summon" || p[0]=="/summon")) {
        if(p.size()<=1) return {"/summon npc","/summon boss"};
        const std::string& type=p[1];
        if((type=="npc" || type=="boss") && p.size()==2) {
            if(trailing || p[1]==type) {
                const auto& names=(type=="boss")?bosses:npcs;
                for(const auto& n:names) out.push_back("/summon "+type+" "+n);
                return out;
            }
        }
        if(p.size()>=3) {
            const auto& names=(type=="boss")?bosses:npcs;
            for(const auto& n:names)
                if(n.rfind(p[2],0)==0) out.push_back("/summon "+type+" "+n);
        } else if(p.size()==2) {
            if(type.rfind("npc",0)==0) out.push_back("/summon npc");
            if(type.rfind("boss",0)==0) out.push_back("/summon boss");
        }
        return out;
    }

    if((p[0]=="kill" || p[0]=="/kill") && p.size()<=1)
        return {"/kill all","/kill npcs","/kill bosses"};
    if((p[0]=="tp" || p[0]=="/tp") && p.size()<=1)
        return {"/tp ~ ~","/tp 0 0","/tp 10 10"};
    return out;
}

bool parseCommandFloat(const std::string& token,float reference,float& value) {
    try {
        size_t consumed=0;
        if(token=="~") {
            value=reference;
            return true;
        }
        if(!token.empty() && token[0]=='~') {
            const std::string offset=token.substr(1);
            const float delta=offset.empty()?0.0f:std::stof(offset,&consumed);
            if(consumed!=offset.size()) return false;
            value=reference+delta;
            return std::isfinite(value);
        }
        value=std::stof(token,&consumed);
        return consumed==token.size() && std::isfinite(value);
    } catch(...) {
        return false;
    }
}

} // namespace dg::console
