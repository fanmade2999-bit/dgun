# DG Build 0019

Current development slice remains player + procedural terrain. Build 0019 adds an in-game ADMIN switchboard so registered runtime features can be turned on/off without changing the main loop.

## Build 0019 — in-game feature switchboard
- Permanent ADMIN button remains available even when PLAYER is disabled.
- Feature controls are organized into PLAYER, WORLD, GAMEPLAY and UI groups.
- Every registered FeatureFlags system has an ON/OFF control.
- Physical features show a temporary animated world beacon/ring at their corresponding physical anchor when enabled.
- Information-only features briefly glow in the admin panel when enabled, then return to normal.
- Every toggle shows an immediate ON/OFF message.
- Disabling a feature clears relevant transient input/overlay state.
- The modular update/render orchestration remains unchanged; ADMIN changes state through the central feature switchboard.

### Registered admin features
PLAYER, TERRAIN, STRUCT, WEATHER, ECO, ENEMY, BOSS, WRECK, COMBAT, NAV, PROGRESS, DEBUG.

### Visual feedback
- PHY: temporary animated world beacon/rings above the corresponding physical anchor.
- INFO: temporary admin-panel glow for the toggled information feature.
- Toggle message: feature name plus ON/OFF.

## Previous build history




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

### Chunked toroidal procedural world
- The 1024 × 1024 world is partitioned into **16 × 16 tile chunks** (32 world units per chunk), giving a 32 × 32 chunk torus.
- Chunk coordinates and local tile coordinates are shown in the HUD.
- Chunk borders are rendered around the active area for debugging/navigation.
- Terrain remains deterministic: the same chunk/tile coordinates regenerate the same terrain.

### Toroidal procedural world
- The playable world is a finite **1024 × 1024 world-unit torus**.
- Moving past the eastern boundary wraps to the western side; the same works north/south.
- Terrain is generated deterministically from wrapped tile coordinates, so the same location is reproducible after restarting.
- The renderer only draws a local generated window around the player, so the world can be larger than the visible area without drawing the entire map.
- World interactions, enemy pursuit, boss pursuit, and legacy landmarks use shortest toroidal distance.
- Seam rendering uses the nearest wrapped image so landmarks and entities remain visible when crossing the boundary.

### Autonomous ecosystem
- A world-owned ecosystem now runs independently of the player.
- 48 autonomous actors are initially distributed across the full world; the simulation can grow to 96.
- Grazers seek food patches, hunters choose prey and feed, and scavengers seek carcasses.
- Hunger, energy, age, local movement, predation, starvation, reproduction, and corpses are simulated.
- The ecosystem advances on a background simulation clock even when the player is far away, dead, or viewing the expanded map.
- Actors use toroidal shortest-distance movement and can cross world seams normally.
- Only nearby actors are rendered for performance; their off-screen simulation continues.
- Minimap cells include population-density shading so the larger world has visible life patterns.
- Chunk state now includes food, water, shelter and local danger.
- Food and water regenerate globally, with weather affecting the rate.
- Creatures have hunger, thirst, energy and fear needs and react to local conditions.
- Hunters increase local danger; grazers become more cautious and move differently at night.
- Predation and starvation are tracked as separate ecosystem events.
- Individual creatures now have persistent group identity, loyalty, alertness, and a personal den/home location for the session.
- Hunters can share prey targets with trusted pack members.
- Grazers maintain loose herd cohesion based on group identity.
- Creatures actively seek water when thirsty.
- Crowded populations migrate toward better chunks instead of remaining packed forever.
- Reproduction is constrained by local ecological carrying capacity.
- Recent births, hunts, deaths and scavenging are retained as world-event records.
- The expanded world map shows recent ecosystem activity traces, allowing the player to discover that events happened elsewhere without causing those events to wait for the player.
- Local HUD ecology reports the current chunk's population, food and water rather than a player-centered quest state.
- Creatures maintain groups, personal dens, loyalty, alertness and condition across their session.
- Wildlife reacts to the player's physical presence and weapon noise without being spawned around the player.
- Alarm can propagate between nearby members of the same creature group, allowing one animal to alert others.
- Non-hunter wildlife can flee from a nearby player or a loud laser disturbance.
- The player's laser can hit and kill ecosystem actors, creating a world event that other creatures can react to.
- Unattended mech wrecks are now scavengeable by autonomous scavengers, so salvage can decay over time.
- Mutable game state now persists across app restarts: player position/body state, resources, progression, boss state, wrecks, ecosystem actors/chunks, weather clock and event history are saved.
- Persistent world state is restored on relaunch rather than reinitializing a fresh ecosystem.
- Ecosystem population now uses chunk-level LOD: distant wildlife is represented as aggregate dormant populations, while nearby wildlife is promoted into detailed individual actors.
- Aggregate population change now keeps fractional progress instead of rounding every tick away.
- Distant grazers consume chunk food, and distant hunters exert aggregate predation pressure on grazer/scavenger populations, so the off-screen food chain can change without spawning fake actors.
- The world can therefore contain many more conceptual creatures than the 96 active actor slots without simulating every distant animal every frame.
- HUD/map population readouts distinguish active detailed actors from the larger conceptual population.

### World cycle and weather
- The autonomous world has a 240-second ecological cycle.
- Day/night state changes creature activity and scene lighting.
- Rain intensity is generated from world time rather than from the player.
- Rain replenishes world water and accelerates food recovery.
- Visible rain is rendered locally around the player, but the underlying weather clock is global.
- Autonomous creatures have night/rest routines tied to shelter and personal dens.

### Coordinates + expandable minimap
- Live **X/Y/Z** coordinates are displayed.
- Current chunk and local tile coordinates are displayed alongside them.
- A compact minimap shows nearby chunks and persistent landmark markers.
- Tapping **MAP** expands it into the full 32 × 32 chunk world map.
- The expanded map highlights the current chunk and shows base/facility/boss positions.
- Recent autonomous ecosystem events appear as fading strategic-map traces, including player-caused kills.
- Group alarm propagation makes local disturbances spread through the ecosystem naturally.
- Save/load restores the living world instead of regenerating a fresh ecosystem each launch.
- Autosave runs during play and a final save is requested when the Android Activity pauses.
- Save schema v4 persists chunk-level dormant population state, fractional dormant population progress, and the global aggregate migration step.
- Local ecology remains visible without turning the map into a player-centric quest tracker.
- Recent autonomous ecosystem events are shown as temporary activity markers on the expanded map.

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
11. Observe wildlife/mechanical life around the world. Move away from a group, spend time elsewhere, then return and confirm actors have moved, reproduced, starved, hunted, or left corpses without requiring the player to be nearby.
12. Fire the laser near wildlife and observe non-hunter creatures become alert/flee; verify a direct hit can kill an ecosystem actor and creates a SHOT world event.
13. Leave a mech wreck unattended near scavengers and return later; verify the wreck's salvage condition can decrease even without player interaction.
14. Walk away from wildlife until it leaves the active bubble, then return later; nearby dormant population should be promoted back into individual creatures.
15. Pause/background the app, relaunch it, and confirm your position, body condition, inventory, defeated boss state, wreck state and ecosystem state continue from the prior session.

## Architecture direction

The prototype now has the core shape of:
**remote consciousness -> physical body -> localized damage -> abandoned wreck -> salvage -> scarce components -> fabrication -> replacement body -> continued exploration**

Persistent saves for ecosystem state, more species, richer aggregate food-chain interactions, births across multiple generations, ecosystem interaction with world facilities/ruins, richer procedural terrain/biomes, more boss variants, and deeper salvage/component inventories remain later systems.

## Build / test

Manus monitors the source and compiles the Android build.

Verification loop:
source -> Manus compile -> Android emulator QA -> physical-device test -> performance profiling.
