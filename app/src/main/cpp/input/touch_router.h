#pragma once

namespace dg::input {

enum class TouchTarget {
    NONE,
    ADMIN_TOGGLE,
    ADMIN_COMMAND,
    ADMIN_FEATURE,
    MAP,
    CAMERA,
    JUMP,
    MOVE,
    SWAP,
    ACTION,
    FIRE,
    AIM
};

struct TouchFeatures {
    bool player=false;
    bool navigationHud=false;
    bool camera=false;
    bool playerJump=false;
    bool playerMovement=false;
    bool worldStructures=false;
    bool playerCombat=false;
    bool playerAim=false;
};

struct PointerAvailability {
    bool map=false;
    bool camera=false;
    bool jump=false;
    bool move=false;
    bool swap=false;
    bool action=false;
    bool fire=false;
    bool aim=false;
};

struct TouchUiState {
    bool adminOpen=false;
    bool mapExpanded=false;
};

TouchTarget classifyTouchDown(float x,float y,float width,float height,
                              const TouchFeatures& features,
                              const PointerAvailability& pointers,
                              const TouchUiState& ui);

struct StickVector {
    float x=0.0f;
    float y=0.0f;
};

StickVector movementStick(float x,float y,float width,float height);

} // namespace dg::input
