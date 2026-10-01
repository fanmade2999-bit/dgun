# DG Build 0008

Integrated prototype test loop for the persistent Destroy Gunners-inspired game.

## What can now be tested in one run

### Combat / movement
- Continuous virtual movement.
- Independent right-side aiming.
- Dedicated FIRE control.
- Proper MOVE + AIM + FIRE multitouch.
- Enemy pursuit and attack.
- Individual mech part damage.
- Overall chassis integrity.
- HP reaches zero reliably.
- Body visual parts darken as local damage accumulates.

### Laser
- Visible laser beam restored with cheap glow/muzzle layers.
- Long baseline range.
- Obstacle-aware beam trace.
- Material-specific penetration resistance.
- Light obstacle -> penetrates with reduced energy.
- Heavy obstacle -> consumes penetration and can stop the beam.
- Gameplay collision uses the same traced range/energy as the visual beam.

### ULTRON-style body system
- Active consciousness uses a physical chassis.
- Multiple assembled bodies exist in a small body pool.
- SWAP is available at the home base.
- Swapping preserves the condition of the body left in storage.
- Destroyed chassis becomes a world wreck instead of disappearing.
- No complete chassis -> temporary recovery bot mode.
- Recovery-bot state cannot accidentally create a new assembled chassis.

### Salvage
- Wreck remembers the remaining HP of each body part.
- Close to a wreck + ACT converts recoverable sections into SCRAP/CIRCUIT resources.
- More intact parts yield more recoverable material.
- Destroyed/near-destroyed parts yield little or nothing.

### Facility / unknown equipment
- A test discovery facility exists in the world.
- A visible unknown equipment pod sits outside it.
- Close to the facility + ACT identifies the unknown equipment.
- This is intentionally location-driven rather than random-stat spam.

### Toroidal procedural world
- The playable world is now a finite **128 × 128 world-unit torus**.
- Moving past the eastern boundary wraps to the western side; the same works north/south.
- Terrain is generated deterministically from wrapped tile coordinates, so the same location is reproducible after restarting.
- The renderer only draws a local generated window around the player, so the world can be larger than the visible area without drawing the entire map.
- World interactions, enemy pursuit, boss pursuit, and legacy landmarks use shortest toroidal distance.
- Seam rendering uses the nearest wrapped image so landmarks and entities remain visible when crossing the boundary.

### Landmark boss + equipment progression
- A large persistent boss now lives at a fixed world landmark.
- Boss has its own HP, movement pressure, attack cycle, hit flash and laser.
- Defeating the boss is a one-time progression event: it adds UNKNOWN EQUIPMENT plus SCRAP/CIRCUIT.
- Visiting the discovery facility identifies an UNKNOWN EQUIPMENT piece.
- The first identified pieces unlock/upgrade the **LANCE** laser and immediately change fire damage/heat behavior.

### Body fabrication
- At the base + ACT starts fabrication when the required resources and an empty body slot exist.
- Current test recipe: 6 SCRAP + 2 CIRCUIT.
- Fabrication takes 6 seconds.
- A completed chassis is added to the body pool.

## Suggested test sequence

1. Start at the home base and press **SWAP**. Confirm the active mech changes while the previous chassis remains stored.
2. Move toward the enemy. Use MOVE + AIM + FIRE simultaneously.
3. Fight while deliberately exposing different body areas and watch local damage accumulate.
4. Let the current chassis reach zero HP. Confirm the wreck remains where the fight happened.
5. Return to/approach the wreck and press **ACT**. Confirm SCRAP/CIRCUIT resources increase according to the wreck's surviving parts.
6. Travel to the discovery facility and press **ACT** beside the unknown pod. Confirm the unknown equipment is identified.
7. Travel to the boss landmark and defeat the boss. Confirm it stays defeated and a new UNKNOWN EQUIPMENT piece appears in the progression HUD.
8. Visit the discovery facility and press **ACT** again. Confirm the LANCE level increases and the laser behavior changes.
9. Return to base. With an empty body slot and sufficient resources, press **ACT** and watch the fabrication timer.
10. After fabrication, press **SWAP** and confirm the newly fabricated body is available.
11. Exhaust all assembled bodies through deaths and confirm the game falls back to the temporary recovery bot.

## Architecture direction

The prototype now has the core shape of:
**remote consciousness -> physical body -> localized damage -> abandoned wreck -> salvage -> scarce components -> fabrication -> replacement body -> continued exploration**

Persistent saves, a larger procedural terrain/biome vocabulary, named facilities beyond the test site, more boss variants, a persistent world map, equipment rarity/identity beyond the prototype LANCE path, and deeper salvage/component inventories remain later systems.

## Build / test

Manus monitors the source and compiles the Android build.

Verification loop:
source -> Manus compile -> Android emulator QA -> physical-device test -> performance profiling.
