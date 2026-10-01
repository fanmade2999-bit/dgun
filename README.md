# DG Build 0002

First playable Android combat slice for the Destroy Gunners-inspired project.

## 0002

- Native C++17 game core and OpenGL ES 2.0 renderer.
- Procedural multi-part mech silhouette: torso, head, shoulders, arms, legs, cannon.
- Third-person camera.
- Left virtual joystick for continuous movement.
- Right touch control for independent aim/yaw and cannon pitch.
- Correct Android multi-touch routing, so movement and firing can happen simultaneously.
- Hold-to-fire laser with heat buildup and cooldown.
- Laser collision and enemy damage.
- Enemy pursuit/orbit behavior.
- Enemy attack cycle and player damage.
- Hit flashes and automatic player respawn.
- Low-cost arena cover/landmark blocks.
- HP and heat HUD bars plus targeting reticle.
- No external runtime assets yet; geometry is procedural.

## Intended direction

0003: equipment/components system and UNKNOWN EQUIPMENT discovery.
0004: first explorable facility and persistent world save.
Later: world generation, named facilities, bosses, rare/iconic equipment, and the persistent exploration loop.

## Build / test

Manus is monitoring the source and handling compilation.

This project requires an Android SDK, Android SDK Build-Tools, Android NDK, CMake, and a Gradle/Android environment capable of building the application. The ChatGPT runtime is not the compiler host for the APK in this workflow.

The intended verification loop is:
source change -> Manus compile -> Android emulator QA -> device test -> performance profiling.
