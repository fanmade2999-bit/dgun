#include "touch_router.h"

#include <algorithm>

namespace dg::input {
namespace {

bool inCircle(float x,float y,float cx,float cy,float radius) {
    const float dx=x-cx;
    const float dy=y-cy;
    return dx*dx+dy*dy<=radius*radius;
}

bool inRect(float x,float y,float x1,float y1,float x2,float y2) {
    return x>=x1 && x<=x2 && y>=y1 && y<=y2;
}

bool inAdminButton(float x,float y,float w,float h) {
    return inRect(x,y,w*0.82f,h*0.012f,w*0.98f,h*0.082f);
}

bool inAdminCommandButton(float x,float y,float w,float h) {
    const float x1=w*0.06f+12.0f;
    return inRect(x,y,x1,h*0.945f-48.0f,x1+142.0f,h*0.945f-14.0f);
}

bool inMapButton(float x,float y,float w,float h) {
    return inCircle(x,y,w*0.92f,h*0.12f,h*0.055f);
}

bool inCameraGesture(float x,float y,float w,float h) {
    return x>w*0.46f && x<w*0.60f && y>h*0.08f && y<h*0.34f;
}

bool inJumpButton(float x,float y,float w,float h) {
    return inCircle(x,y,w*0.52f,h*0.78f,h*0.105f);
}

bool inSwapButton(float x,float y,float w,float h) {
    return inCircle(x,y,w*0.84f,h*0.25f,h*0.095f);
}

bool inActionButton(float x,float y,float w,float h) {
    return inCircle(x,y,w*0.68f,h*0.78f,h*0.115f);
}

bool inFireButton(float x,float y,float w,float h) {
    return inCircle(x,y,w*0.84f,h*0.78f,h*0.135f);
}

} // namespace

TouchTarget classifyTouchDown(float x,float y,float width,float height,
                              const TouchFeatures& features,
                              const PointerAvailability& pointers,
                              const TouchUiState& ui) {
    if(inAdminButton(x,y,width,height)) return TouchTarget::ADMIN_TOGGLE;
    if(ui.adminOpen) {
        return inAdminCommandButton(x,y,width,height)
            ? TouchTarget::ADMIN_COMMAND
            : TouchTarget::ADMIN_FEATURE;
    }
    if(!features.player) return TouchTarget::NONE;

    const bool leftZone=x<width*0.45f;
    if(features.navigationHud && !leftZone && pointers.map &&
       inMapButton(x,y,width,height)) return TouchTarget::MAP;
    if(ui.mapExpanded) return TouchTarget::NONE;

    if(features.camera && !leftZone && pointers.camera &&
       inCameraGesture(x,y,width,height)) return TouchTarget::CAMERA;
    if(features.playerJump && !leftZone && pointers.jump &&
       inJumpButton(x,y,width,height)) return TouchTarget::JUMP;
    if(features.playerMovement && leftZone && pointers.move)
        return TouchTarget::MOVE;
    if(features.worldStructures && !leftZone && pointers.swap &&
       inSwapButton(x,y,width,height)) return TouchTarget::SWAP;
    if(features.worldStructures && !leftZone && pointers.action &&
       inActionButton(x,y,width,height)) return TouchTarget::ACTION;
    if(features.playerCombat && !leftZone && pointers.fire &&
       inFireButton(x,y,width,height)) return TouchTarget::FIRE;
    if(features.playerAim && !leftZone && pointers.aim)
        return TouchTarget::AIM;
    return TouchTarget::NONE;
}

StickVector movementStick(float x,float y,float width,float height) {
    const float baseX=width*0.18f;
    const float baseY=height*0.78f;
    const float radius=height*0.20f;
    return {
        std::clamp((x-baseX)/radius,-1.0f,1.0f),
        std::clamp((y-baseY)/radius,-1.0f,1.0f)
    };
}

} // namespace dg::input
