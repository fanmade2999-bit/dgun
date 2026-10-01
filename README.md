# DG Build 0004

Playable Android combat foundation for the Destroy Gunners-inspired persistent exploration project.

## 0004: remote-body / salvage foundation

The player is treated as a remote consciousness rather than a permanently attached pilot.

- The current mech is a physical **body** that can be destroyed and left behind.
- The wreck remains at the death location.
- Every major mech section has its own simplified hitbox/health state: core, head, left/right arm, left/right leg, weapon.
- Damage is applied to individual parts instead of only a single invisible HP pool.
- Wreck salvage quality is calculated from the surviving condition of the individual parts; severe damage reduces recoverable value disproportionately.
- The HUD exposes the current number of spare assembled bodies.
- On death, an available spare body is deployed from the home-base concept.
- When the assembled-body pool is exhausted, the prototype switches to a small temporary recovery-bot body.
- The foundation is ready for the later base system: land base or orbital/space base, component scarcity, fabrication time, deliberate body swapping, and recovery/deployment sequences.

## DG-0005 fixes

- HP no longer depends solely on the currently selected hitbox, so it cannot stall at a body-part boundary.
- Every successful enemy hit now applies localized part damage plus a small chassis-integrity hit.
- Chassis integrity is reset with each new body.
- Aim pitch is reset with each new body.
- Vertical aiming is clamped to a practical combat range instead of allowing the beam to point almost straight down.
- Laser rendering now accounts for ground intersection so a downward beam cannot visually continue below the ground.
- The gameplay hit test uses the same effective 3D beam direction/range as the visual.

## Laser

- The laser visual is restored and made more visible with a core beam, cheap glow lines, and a muzzle pulse.
- Beam range and collision use the same trace.
- Obstacles have material-specific penetration resistance.
- Light debris can be pierced with reduced energy.
- Dense concrete/industrial/metal barriers consume progressively more penetration.
- A sufficiently resistant obstacle stops the beam.
- Targets behind penetrable obstacles receive reduced laser damage according to remaining beam energy.

## Controls

**MOVE:** hold/drag the left virtual joystick.

**AIM:** swipe in the right-side aim area.

**FIRE:** press and hold the dedicated lower-right FIRE button.

MOVE + AIM + FIRE can be used together.

## What remains prototype-level

The current body system is a gameplay foundation, not the final inventory/base UI. Body construction scarcity, fabrication timers, body swapping, physical wreck harvesting, component-by-component salvage inventory, land/space base transitions, and mini-bot deployment animation will be expanded as persistent-world systems.

## Build / test

Manus monitors the source and compiles the Android build.

Verification loop:
source -> Manus compile -> Android emulator QA -> physical-device test -> performance profiling.
