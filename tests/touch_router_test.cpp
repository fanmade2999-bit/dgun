#include "input/touch_router.h"

#include <cassert>
#include <cmath>
#include <iostream>

using dg::input::PointerAvailability;
using dg::input::TouchFeatures;
using dg::input::TouchTarget;
using dg::input::TouchUiState;

static TouchFeatures gameplayFeatures() {
    return {true,true,true,true,true,true,true,true};
}

static PointerAvailability allPointers() {
    return {true,true,true,true,true,true,true,true};
}

static void testPriorityAndFeatureGating() {
    const float w=1000.0f;
    const float h=600.0f;
    const auto features=gameplayFeatures();
    const auto pointers=allPointers();
    const TouchUiState closed{false,false};

    assert(dg::input::classifyTouchDown(900,30,w,h,features,pointers,closed)==TouchTarget::ADMIN_TOGGLE);
    assert(dg::input::classifyTouchDown(920,468,w,h,features,pointers,closed)==TouchTarget::FIRE);
    assert(dg::input::classifyTouchDown(180,468,w,h,features,pointers,closed)==TouchTarget::MOVE);
    assert(dg::input::classifyTouchDown(520,468,w,h,features,pointers,closed)==TouchTarget::JUMP);

    const TouchUiState admin{true,false};
    assert(dg::input::classifyTouchDown(100,540,w,h,features,pointers,admin)==TouchTarget::ADMIN_COMMAND);
    assert(dg::input::classifyTouchDown(500,300,w,h,features,pointers,admin)==TouchTarget::ADMIN_FEATURE);

    const TouchUiState expanded{false,true};
    assert(dg::input::classifyTouchDown(700,400,w,h,features,pointers,expanded)==TouchTarget::NONE);

    auto noFire=features;
    noFire.playerCombat=false;
    assert(dg::input::classifyTouchDown(920,468,w,h,noFire,pointers,closed)==TouchTarget::AIM);
}

static void testStickNormalization() {
    const auto center=dg::input::movementStick(180,468,1000,600);
    assert(std::fabs(center.x)<0.0001f);
    assert(std::fabs(center.y)<0.0001f);
    const auto upperRight=dg::input::movementStick(1000,0,1000,600);
    assert(upperRight.x==1.0f);
    assert(upperRight.y==-1.0f);
}

int main() {
    testPriorityAndFeatureGating();
    testStickNormalization();
    std::cout << "touch_router_test: PASS\n";
    return 0;
}
