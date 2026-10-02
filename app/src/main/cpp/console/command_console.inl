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

static void updateAdminCommandQueue(){
    std::string command;
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        if(pendingAdminCommand.empty()) return;
        command.swap(pendingAdminCommand);
    }

    console::CommandGameState state;
    state.playerX=px;
    state.playerZ=pz;
    state.nextSummonedId=nextSummonedId;
    state.resolveEntityKind=[](const std::string& name){
        return commandEntityKind(name);
    };
    state.isBossKind=[](int kind){
        return summonKindIsBoss(kind);
    };
    state.summon=[](int kind,float x,float z){
        std::string result;
        const bool success=summonEntity(kind,x,z,result);
        return console::SpawnResult{success,result};
    };
    state.countSummons=[](){
        console::SummonCounts counts;
        for(const auto& e:summonedEntities) if(e.active){
            ++counts.total;
            if(summonKindIsBoss(e.kind)) ++counts.bosses;
            else ++counts.npcs;
        }
        return counts;
    };
    state.killSummons=[](const std::string& selector){
        int count=0;
        for(auto& e:summonedEntities){
            if(!e.active) continue;
            bool match=selector=="all" ||
                (selector=="npcs"&&!summonKindIsBoss(e.kind)) ||
                (selector=="bosses"&&summonKindIsBoss(e.kind));
            if(!match){
                try{match=e.id==std::stoi(selector);}catch(...){ }
            }
            if(match){e.active=false;++count;}
        }
        return count;
    };
    state.clearSummons=[](){
        int count=0;
        for(auto& e:summonedEntities) if(e.active){e.active=false;++count;}
        return count;
    };
    state.teleportPlayer=[](float x,float z){
        px=wrapWorld(x);
        pz=wrapWorld(z);
    };

    const std::string result=console::executeCommand(command,state);
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

