# DG Build 0001

First Android engineering slice for the Destroy Gunners-inspired project.

## What is in 0001

- Native C++17 game core and OpenGL ES 2.0 renderer.
- One controllable mech in a 40 x 40 meter test arena.
- Third-person camera with movement.
- Left-side virtual joystick: hold and drag to move continuously.
- Right-side touch area: hold to fire the laser.
- Heat mechanic and simple cooldown.
- One target dummy that resets when hit.
- No external textures, models, audio, or third-party runtime dependencies.
- Java is only the Android shell; gameplay/rendering lives in C++.

## Intended direction after 0001

0002: better mech model + aiming, enemy pursuit, hit reactions.
0003: component/equipment inventory and UNKNOWN EQUIPMENT discovery.
0004: first facility + persistent world save.

## Build requirements

This project requires the Android SDK, Android SDK Build-Tools, Android NDK, CMake, and a Gradle/Android Studio environment capable of building an Android application. The ChatGPT execution environment used for this artifact does not currently contain the Android SDK/NDK, so the APK was not compiled inside the conversation runtime.

Recommended first target: an Android emulator using the Test Android Apps / Emulator QA tooling, followed by a physical-device pass.
