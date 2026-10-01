# DG Build 0003

First playable Android combat slice with clarified touch controls and physical arena obstacles.

## 0003 changes

- **Blue bar = HEAT**, now explicitly labeled.
- **Green bar = HP**, now explicitly labeled.
- **MOVE** label for the left virtual joystick.
- **AIM** label for the right-side aim area.
- **FIRE** label and dedicated fire button on the lower-right.
- Swiping in the AIM area **does not fire**.
- Firing occurs only when the explicit FIRE control is pressed.
- Proper simultaneous MOVE + AIM/FIRE multitouch remains supported.
- Player collision against all current arena obstacle blocks.
- Enemy collision against current arena obstacle blocks.
- Aim pitch now responds to swipe delta instead of absolute screen position.
- Player respawn no longer destroys the active movement pointer, so holding the joystick through a reboot does not permanently lock movement.
- Firing is cleared on death so the mech does not automatically resume firing after reboot.
- Android version bumped to 0.0.3 / versionCode 3.

## Controls

**MOVE:** hold/drag the left circular control.

**AIM:** swipe anywhere in the right-side area above/away from the FIRE button.

**FIRE:** press and hold the lower-right FIRE button.

MOVE + AIM + FIRE can be used together with multiple fingers.

## Intended direction

0004: equipment/components system and UNKNOWN EQUIPMENT discovery.
0005: first explorable facility and persistent world save.
Later: world generation, named facilities, bosses, rare/iconic equipment, and the persistent exploration loop.

## Build / test

Manus is monitoring the source and handling compilation.

Verification loop:
source change -> Manus compile -> Android emulator QA -> physical-device test -> performance profiling.
