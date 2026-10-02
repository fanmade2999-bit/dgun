// Internal command-console and summon implementation seam.
// This file is included into native-lib.cpp while the legacy shared game state
// is being migrated behind explicit subsystem interfaces.

static bool summonEntity(int kind,float x,float z,std::string& result){
    const bool boss=summonKindIsBoss(kind);
    if(boss)features.summonedBosses=true;
    else features.summonedNpcs=true;

    for(auto& e:summonedEntities){
        if(e.active)continue;
        e=SummonedEntity{};
        e.active=true;
        e.kind=kind;
        e.id=nextSummonedId++;
        e.x=wrapWorld(x);
        e.z=wrapWorld(z);
        e.homeX=e.x;
        e.homeZ=e.z;
        e.yaw=yaw;
        e.maxHp=boss
            ? 300.0f+90.0f*float(kind-SUMMON_BOSS_SAMURAI)
            : 75.0f+8.0f*float(kind);
        e.hp=e.maxHp;
        e.thinkTimer=0.2f;
        e.actionTimer=boss?1.0f:1.5f;
        e.seed=worldHash(int(e.x*7.0f)+e.id,int(e.z*7.0f)-e.id);
        result="SUMMONED "+std::string(summonKindName(kind))+" #"+std::to_string(e.id);
        return true;
    }
    result="SUMMON FAILED: ENTITY SLOTS FULL";
    return false;
}

static std::string executeAdminCommand(const std::string& raw){
    const auto p=console::splitCommand(raw);
    if(p.empty())return {};
    std::string cmd=p[0];
    if(!cmd.empty()&&cmd[0]=='/')cmd=cmd.substr(1);

    if(cmd=="summon"){
        if(p.size()<2)return "USAGE: /summon <npc|boss> <name> [x] [z]";
        const bool typed=p[1]=="npc"||p[1]=="boss";
        if(typed&&p.size()<3)return "USAGE: /summon "+p[1]+" <name> [x] [z]";
        const std::string name=typed?p[2]:p[1];
        const int kind=commandEntityKind(name);
        if(kind<0)return "UNKNOWN ENTITY: "+name;
        if(typed&&((p[1]=="npc")==summonKindIsBoss(kind)))return "TYPE MISMATCH";
        const int coord=typed?3:2;
        float x=px,z=pz;
        if((int)p.size()==coord){
            const float angle=float(nextSummonedId)*0.91f;
            const float radius=summonKindIsBoss(kind)?4.0f:2.4f;
            x=px+std::cos(angle)*radius;
            z=pz+std::sin(angle)*radius;
        }
        if((int)p.size()>coord&&!console::parseCommandFloat(p[coord],px,x))return "BAD X";
        if((int)p.size()>coord+1&&!console::parseCommandFloat(p[coord+1],pz,z))return "BAD Z";
        std::string result;
        summonEntity(kind,x,z,result);
        return result;
    }

    if(cmd=="list"){
        int total=0,npcs=0,bosses=0;
        for(const auto& e:summonedEntities)if(e.active){
            ++total;
            if(summonKindIsBoss(e.kind))++bosses;else++npcs;
        }
        return "SUMMONS "+std::to_string(total)+" | NPC "+std::to_string(npcs)+" | BOSSES "+std::to_string(bosses);
    }

    if(cmd=="kill"){
        if(p.size()<2)return "USAGE: /kill <id|all|npcs|bosses>";
        int count=0;
        for(auto& e:summonedEntities){
            if(!e.active)continue;
            bool match=p[1]=="all" ||
                (p[1]=="npcs"&&!summonKindIsBoss(e.kind)) ||
                (p[1]=="bosses"&&summonKindIsBoss(e.kind));
            if(!match){try{match=e.id==std::stoi(p[1]);}catch(...){}}
            if(match){e.active=false;++count;}
        }
        return "KILLED "+std::to_string(count);
    }

    if(cmd=="clear"){
        int count=0;
        for(auto& e:summonedEntities)if(e.active){e.active=false;++count;}
        return "CLEARED "+std::to_string(count)+" SUMMONS";
    }

    if(cmd=="tp"){
        if(p.size()<3)return "USAGE: /tp <x> <z>";
        float x,z;
        if(!console::parseCommandFloat(p[1],px,x)||!console::parseCommandFloat(p[2],pz,z))
            return "BAD COORDS";
        px=wrapWorld(x);
        pz=wrapWorld(z);
        return "TELEPORTED TO "+std::to_string((int)std::round(px))+
               " "+std::to_string((int)std::round(pz));
    }

    return "UNKNOWN COMMAND";
}

static void updateAdminCommandQueue(){
    std::string command;
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        if(pendingAdminCommand.empty())return;
        command.swap(pendingAdminCommand);
    }
    const std::string result=executeAdminCommand(command);
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        commandLastResult=result;
    }
}

static void updateSummonedEntities(float dt){
    for(auto& e:summonedEntities){
        if(!e.active)continue;
        e.lifetime+=dt;
        e.thinkTimer-=dt;

        if(summonKindIsBoss(e.kind)){
            const float dx=wrappedDelta(e.x,px);
            const float dz=wrappedDelta(e.z,pz);
            const float d=std::sqrt(std::max(0.0001f,dx*dx+dz*dz));
            if(d>5.0f){
                const float v=0.40f+0.045f*float(e.kind-SUMMON_BOSS_SAMURAI);
                e.x=wrapWorld(e.x+(dx/d)*v*dt);
                e.z=wrapWorld(e.z+(dz/d)*v*dt);
                e.yaw=std::atan2(dx,-dz);
            }
        }else if(e.thinkTimer<=0.0f){
            e.thinkTimer=1.2f+float((e.seed>>4)&3u)*0.3f;
            const float a=(float((e.seed>>8)&255u)/255.0f+e.lifetime*0.05f)*2.0f*PI;
            const float radius=2.5f+float((e.seed>>16)&7u)*0.7f;
            const float tx=e.homeX+std::cos(a)*radius;
            const float tz=e.homeZ+std::sin(a)*radius;
            const float dx=wrappedDelta(e.x,tx);
            const float dz=wrappedDelta(e.z,tz);
            const float d=std::sqrt(std::max(0.0001f,dx*dx+dz*dz));
            if(d>0.25f){
                const float v=0.35f+0.05f*float(e.kind);
                e.x=wrapWorld(e.x+(dx/d)*v);
                e.z=wrapWorld(e.z+(dz/d)*v);
                e.yaw=std::atan2(dx,-dz);
            }
        }
    }
}

