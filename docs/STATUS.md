# MAX-FX reverse engineering — current status

Last updated 2026-09-27 (noclip + cutscene regression fixes against the
Android 1.0 decompile; `[Animation]`/`[Properties]` sibling pairing;
GM_ChangeGameSpeed / bullet-time game speed; skinning verified end to end on
the shipped Max Payne model — see `docs/ANIMATION.md`).
Comments and this file are in English; the code is C++11. The target is the **PC** Max Payne 1
MAX-FX format. The Android `libMaxPayne.so` decompile in `docs/` is used for
names and version numbers only — its loaders were stripped and can disagree
with the PC layout.

## Goal

Reusable parsers (not viewer-only hacks) that a future engine port can link,
plus an SDL3 OpenGL viewer that shows a real level from
`data/database/levels/` next to the executable.

## What works

### Tagged binary stream (`src/maxfx/core`)

Every MAX-FX database (LDB, KF2, KFS, SKD) stores values as `{tag}{payload}`.
`TaggedReader` implements the compact integer / float encodings and the
vector / map / pair container tags. KF2 chunk headers are a 13-byte prefix
(`0x0C` + raw little-endian id/version/size) on top of that stream.

**PC quirk:** KF2 `chunk_size` includes the next sibling's 13-byte header, so
the reader never seeks by size. Known chunks are consumed field-by-field;
unknown chunks skip tagged values until the next `0x0C`.

### LDB levels (`src/maxfx/ldb`)

PC version 32 is fully walked: BSP, textures/materials, lightmaps, exits,
static and dynamic meshes, lights, waypoints, FSMs, characters, triggers,
items, rooms. UV coordinates are stored as-is (Direct3D top-left). Lightmap
and material GPU slots are indexed by both array position and on-disk id.

### R_Script (`src/maxfx/script`)

Preprocessor (`#include`, `#define`, comments, UTF-8 BOM skip) plus the
nested `[tag] { … }` grammar. Missing includes can be skipped so official
`levels.txt` still loads without `globaldefines.h`.

Unbraced blocks used all over the database are now parsed:

```
[Cardboard]
DrawPolygons = TRUE;

[Geometry] ExportData = foo.kf2; SkinData = foo.skd;
```

Quoted rvalues keep interior quotes until the `";` terminator, which is how
official `[Message] string = "foo->Bar( "name" );";` files are written.
Lvalues may start with a digit (`3DSound`).

`materials.txt`, `skins/*.txt`, `level_items/*.txt`, `sounds/*.txt`,
`music/music.txt` and `decals/decals.txt` depend on this.

### Shared database (`src/maxfx/db`)

`DatabaseReader::locateRoot` walks from the exe / an `.ldb` / `docs/` looking
for `materials.txt`. Every `*.txt` under that root is catalogued.

Parsed subsets:

| Source | Used for |
| --- | --- |
| `materials.txt` | `DrawPolygons`, `DetailOffset`, `SortPriority`, `WritesZBuffer`, `AlphaReference` |
| `skins/*.txt` | `[LOD][Geometry] ExportData` / `SkinData` |
| `level_items/*.txt` | pickup / prop KF2 paths |
| `decals/decals.txt` | `Filename` / `AlphaFilename` per material |
| `sounds/*.txt` | cue name, WAV path, 3D/loop/volume |
| `music/music.txt` | looping soundtrack WAVs |
| `levels.txt` `WorldSphere` | `worldspheres/bg_<name>.kf2` |

ExportData is resolved against the script directory, `scriptDir/<stem>/`,
and the database root with `../` stripped (flattened extracts put
`beretta.txt` next to `weapons/beretta/*.kf2`). KF2/KFS/SKD files referenced
by skins and items that actually appear in the loaded LDB are parsed on
demand and cached.

### KF2 / KFS / SKD (`src/maxfx/kf2`)

Same chunk format for all three (KFS = mesh, SKD = skin weights, KF2 = mesh
and/or keyframe animation). Material lists, meshes (geometry, triangles, UV,
polygon materials, node transforms), skin chunks, and **keyframe animation**
channels are parsed.

A character clip (`Walk.kf2`, `Widepose.kf2`, …) is one `0x00010012`
`KeyframeAnimation` chunk per bone. Layout matches
`KeyframeAnimationChunk::operator>>` in the decompile:

```
nested Animation 0x00010013: target name, frame rate, looping
parent name, loop interpolation, total keyframes, key count
keys: {int frame, Mat4x3 objectToParent}   // stride 52 on Android
version > 0: visibility keys; > 1: loop-to-frame; > 2: lerp method; > 3: maintain scale
```

`kf2BuildDrawMeshes` produces triangle lists in node-local space, including
v2 primitive-local index rebasing. Bind-pose world matrices follow the node
parent chain. `kf2BuildSkinnedDrawMeshes` does linear-blend skinning
(KFS mesh + SKD weights + CHARANIM_POSE bind + current clip). Mesh vertices
are object-local; the KFS node (90° 3ds Max Z-up → Y-up) is the mesh bind
when the mesh name is not a skeleton bone. Skipping that left every NPC
lying on the floor and stretched. Output is model space so the renderer
applies only the entity transform.

Play clips store `interpolationMethod = 0`; the engine still lerps in
`LinearlyOptimizedContainer<M_Matrix4x3>::getItem`. Treating 0 as a step
hold froze sparse Stand channels (shoulders: 2 keys / 250 frames). Root XZ
is locked in-place by sliding **every** bone — patching only Pelvis after
the parent walk tore the waist. Skeleton worlds are built from parent
indices (no per-compare string alloc). Multi-mesh SKDs skin listed objects
with a concatenated vertex base.

`sampleChannel` is a full port of the decompiled sampler (see
`docs/ANIMATION.md`): looping channels wrap into
`[LoopToFrame, TotalKeyframeCount)` so the intro plays once,
`UseLoopInterpolation` blends the last key towards the loop-start key during
the final frame, non-looping channels hold the last key from
`total - 1`, and the component lerp is **raw** — re-orthogonalisation only
happens for `interpolationMethod` 1 (row normalize) / 2
(`M_Matrix3::orthonormalize`), with `maintainMatrixScaling` re-applying row
lengths lerped in frame space. `levels-test` pins all of that with synthetic
channels (`testKf2EngineSampling`, no game data needed). Character
locomotion uses `|EndPosition| / clipLength` (`X_CRSplineMovementUpdate`
moves the spline over the clip duration), cached per actor in
`CharacterActor::moveSpeed` once the walk clip has been loaded.

Self-test: `beretta_levelitem.kf2`, `Alex_Balder_L0.kfs` + `.SKD`,
`Widepose.kf2` / `Walk.kf2`.

### Images (`src/maxfx/image`)

- JPEG / TGA via stb_image
- 8-bit paletted PCX, 8-bit 3/4-plane PCX
- Greyscale paletted PCX (MAX-FX `*_alpha.pcx`) writes the grey value into
  **alpha**, so opacity maps display. Confirmed on `docs/baseballbat_alpha.pcx`
  (128x32, identity grey palette)
- Companion `stem_alpha.pcx` / `.jpg` next to a colour file is composited
- LDB materials with a separate alpha texture are composited at upload

SCX and DDS are detected but not decoded.

### levels.txt (`src/maxfx/levels`)

Official playlist next to the exe (`data/database/levels/levels.txt`). Left /
Right switch maps. Spawn at `PlayerStartingPlace`. Recursive fallback if the
playlist is missing.

### Viewer (`src/viewer`)

| Key | Action |
| --- | --- |
| F3 | lit (lightmapx2) -> diffuse -> lightmap -> **vertex** |
| F6 | show service / no-draw materials |
| F7 | mute music / one-shots |
| F8 | item debug overlay (model resolution + LDB rotation rows) |
| F9 | respawn at the starting place |
| 1..0 | C_SelectWeapon slot row |
| Wheel / R | cycle weapons / C_Reload |
| C | play the next scripted cinematic clip of the player skin |
| N / F10 | noclip (Space up, Ctrl down) |
| Left/Right | next map in `levels.txt` |

Vertex colour is LDB radiosity (`std::map<int, vec3>` on static/dynamic
meshes) when present, otherwise Lambert from point lights + static lights.

Decal z-fighting: materials with `DetailOffset > 0` or alpha test are drawn
later with `glPolygonOffset`. `WritesZBuffer = FALSE` disables depth writes.

Alpha maps are a **separate texture** in the LDB (often a different resolution
than the colour map: smoked glass 32×32 vs 64×64, water vs 8×8 `alpha_50.pcx`).
`applyAlphaMap` bilinear-resamples instead of dropping the mask. JPEG / 24-bit
masks live in RGB luminance; paletted greyscale PCX already copies grey into A.
`materials.txt` `BlendedAlphaTest` + `AlphaReference` are honoured (graffiti /
blood / halos blend; leaves stay a hard 127 cutout). KF2 opacity maps
(Alex Balder glasses) are composited onto the diffuse.

Every parsed LDB entity is placed in the room:

- Items / characters: KF2/KFS when ExportData exists on disk, otherwise a
  solid coloured box (yellow pickups, red capsules). Line helpers remain.
- World sphere: `worldspheres.txt` `[name] { ExportData = bg_*.kf2; }`, drawn
  two-sided around the camera (depth write off) so it works on maps far from
  the origin.
- Point / static lights and FSMs: small solid cubes plus helper overlays.

KF2 batches are vertex-lit, two-sided, and never hidden as service geometry.
Vertex-lit shading no longer multiplies `uLmScale` (the lightmap ×2 boost)
and no longer lifts vertex colour by `0.45*c+0.55`, which is what made
untextured characters read as fully white / inverted-overexposed. Missing
KF2 textures use the grey placeholder and search the material list's
`textureDirs` plus `sharedtextures`.

The stripped sample database has weapon KF2s and one character KFS
(`balder_alex`); missing skins show the red placeholder. Characters with a
KFS/SKD are skinned every frame from cached `CHARANIM_POSE` + STAND / WALK
clips (not the bind T-pose). Rest topology is cached per KFS; posed verts
are written in place. KF2 texture paths and `existingPathIgnoreCase` are
cached (the per-frame directory list was the hitch on Windows). Animated
CPU buffers keep capacity; GPU VBOs stay `GL_STREAM_DRAW`. The default
camera is the player capsule (walk / jump / gravity). F10 toggles noclip.

Frame-time hitches on large maps are gone:

- uniform locations are resolved once per program (`MeshUniforms`), not per
  batch per frame
- static geometry stays in one `GL_STATIC_DRAW` upload; **dynamic level
  meshes (doors, trains) are uploaded once in object space** and from then
  on only a `uWorld` matrix moves them (`setDynamicMeshWorld`), so opening
  a door no longer re-streams vertices
- every batch (static / dynamic / animated) is frustum-culled by AABB
  (Gribb-Hartmann planes extracted from the cached view-projection matrix)
- dynamic batches never merge across owners, so a moving mesh rebinds only
  its own VAO

Animated characters keep a persistent, grow-only VAO pool (entering or
leaving the 40 m skinning range no longer rebuilds GL objects): vertices
re-fill via `glBufferSubData` into the kept allocation and the index list
uploads only when the topology actually changes. Remaining known per-frame
cost: every character within 40 m is re-skinned each frame.

### Game runtime (`src/maxfx/game`)

PC `MP.exe` message table plus the database scripts:

| Piece | Source |
| --- | --- |
| Menu | `MaxPayne_MenuMode` + custom **Jump to Level** (`levels.txt`) |
| Player | `max_payne.txt` (`PLAYER_MOVEMENT` 4.2, `AirborneSpeed` 2.5, `C_Jump(7.5)`), gravity −9.81 m/s² (levels.txt −981 cm/s²) |
| Hitscan | `CROSSHAIR_CASTLENGTH` / beretta `[Attributes]` + `bullet_beretta` Damage |
| Triggers | `X_LevelRuntimeTrigger` port: types 0/1/2/3/4 = action button (use) / player collide / projectile / character collide / look-at; `activate()` latches (one fire), `T_Enable(bool)` re-arms, `T_Activate` dispatches to the trigger's own FSM only (the level FSM named like the trigger minus `.TRIGGER`) |
| FSMs | startup messages run at level load (level intro comic via `::startroom::fsm_start`); `FSM_Switch`, `FSM_Send`, `DO_Animate`/`DO_*` and `T_*` route by message target; MPGNM note queue |
| Doors | `DO_Animate` / `DO_InvertAnimation` on dynamic-mesh clips; `dynamicMeshPose` interpolates the `MeshAnimation` start/end transforms (translation/rotation graphs as easing when sane), the viewer streams the meshes at the animated transform every frame and rebuilds collision when a `dynamicCollisions` object crosses half-open |
| Comics | `graphicnovelpages.txt` chapters + full-screen reader (KF2 plate, not a world billboard) |
| OnInit | `C_PickupWeapon`, `C_PickupAmmo`, `C_SetHealth`, `C_DisplayCrosshair`, `GM_SetPlayerControls` |

Esc opens the menu. WASD walk, Space jump, E use, LMB shoot,
LMB fire plays the weapon's fire sound (3D at the muzzle) + muzzle-flash
effect, spawns the impact decal / sparks via the projectile's
`WEAPONANIM_HIT` messages (blood on characters), 1..0 select weapon slots,
wheel cycles `weaponpriority.txt` `[CycleWeapons]` order, R reloads
pocket -> clip (auto-reload at 0 with a 0.6 s cooldown).

Weapons come from `data/weaponpriority.txt` + every `weapons/*.txt` /
`level_items/*.txt` `[Attributes]` block: `WeaponID/SlotIndex/InventoryID`
(`weaponid.h` defines resolved), clip / pocket sizes, `castLength`, the fire
sound and muzzle effect from `WEAPONANIM_SHOOT*` message lists. The player
starts with all weapons (`giveAllWeapons`) for viewer testing.

HUD is the PC `data/hud/hud.txt` (`src/maxfx/game/Hud.cpp`):
`[Health]` sprite + background, `[ActiveWeapon]` per `WeaponID` sprite
(bitmaps + `_alpha` masks, `ReferencePoint` anchoring, 640x480-scaled),
`[Weapons]` ammo counters. Missing hud.txt falls back to the built-in
text HUD.

Effects (`src/maxfx/game/Effects.cpp`): `PS_StartEffect` /
`PS_StopAllEmissions` / `D_CreateDecal` dispatch with a built-in profile
library (muzzle flash, impacts, blood, smoke, explosion; caps 2048
particles / 64 effects / 256 decals FIFO). `D_CreateDecal` and
`PS_StartEffect` orient against the **projectile hit normal**
(`GameRuntime::impactNormal`), so wall shots project the decal on the
wall instead of a floor-plane quad seen edge-on. The viewer renders
decals as surface-projected textured quads (decals.txt radius + random
roll) and particles as camera billboards; particle material bitmaps
wait on the binary `.pse`/particles.txt chain, so profiles use a
generated soft-dot texture with the profile colour / alpha ramps.
Effect definitions live in a `std::deque` (stable addresses): the
`PS_StartEffect` name fallback appends renamed copies at runtime, and a
`std::vector` realloc there used to dangle every live instance's def
pointer — heap-use-after-free reads in `update()` (seen as random
crashes and particles dying instantly once the allocator reused the
block; ASAN loop harness catches it).

Level dynamic meshes (doors, trains) upload once in object space and
pose through a per-batch `uWorld`; batches force `service = false` /
`vertexLit = true` (the old per-frame animated-stream semantics —
always drawn, radiosity/vertex lighting, never lightmaps), with
vertex-lighting samples taken at the mesh's bind-pose world transform.

Cutscenes always terminate and always hand control back: Esc is a full
abort (clip + camera path + fade + letterbox), moving cancels an
`Abortable` camera path once the clip is done
(`userAbortCameraPathIfAbortable`), the camera-path duration can never
stay unresolved (0 would mean "not resolved yet" every frame), and a
finished fade-to-black no longer outlives the cutscene - the
presentation state clears when the whole cutscene (clip + path) is over.
`C_EnableCinematicMode` is cutscene-scoped: a clip that ends while
cinematic mode is still on hands the controls back (scripts pair the
message with `false`, clips that end first must not leave the player
uncontrollable).

Two regressions found against the real database and the Android 1.0
decompile (`docs/Android1.0V/libMaxPayne.so.c`) are fixed:

* **Noclip / walking was dead.** `tickCinematicFrame` applied the
  cinematic start entity to the player EVERY frame, and outside
  cutscenes that entity is a stale identity - the player was teleported
  to the world origin and pinned there every frame. Root motion now
  applies only while `cine.active`, the start entity is captured on the
  cutscene's rising edge (whichever entry point started it) and anchored
  to the spawn point at level load.
* **Scripted clips found no frame hooks.** Authored skins and cinematics
  write `[Animation] Index = ..; Filename = ..;` unbraced followed by a
  braced `[Properties] { [Message] Frame = N; ... }` as a **sibling**
  block (`X_SharedDBAnimationContainer::construct` pairs animation *i*
  with the block at `getBlockIndex("animation", i) + 1` when it is
  `properties` - `X_SharedDBAnimationContainer::construct`,
  `docs/Android1.0V` @ `0xAD5B90`). The clip harvest
  now pairs each `[Animation]` with the immediately following
  `[Properties]` sibling (children-first, then sibling, so synthetic
  fixtures keep working). `max_payne.txt` alone yields 127 hooked clips
  with 528 frame messages (previously 0), so key **C** plays real clips
  with live effects (dust puffs, sounds, slow motion).

`GM_ChangeGameSpeed(speed, seconds)` and `GM_EnableBulletTime(bool)` now
drive a global game speed (`GameRuntime::gameSpeed`, linear ramp on real
time): the simulation (player, actors, doors, triggers, effects,
cinematic timing) runs on scaled time while audio and UI stay real. The
dodge-clip demo plays at 0.5x exactly like the scripted slow motion,
then ramps back to 1.0.

Cutscene camera sampling walks the KF2 parent chain (the camera node is
parented to a root that carries the path placement — sampling the bare
channel local put the camera at the origin, inside geometry = black
screen), parented / in-place bases are built in LDB space, and both the
clip and the camera path always terminate (duration falls back to the
last frame hook + 1s when the clip KF2 is missing; a missing path KF2
ends the path immediately after running its `[Exit]` messages).

Cutscenes (in-engine cinematics): `camerapaths.txt`
(`src/maxfx/game/CameraPaths.cpp`) provides the camera paths
(`[Attributes]` FadeIn/Out + Abortable, `[AnimationSet]` KF2, `[Exit]`
messages). `CAM_AnimateAbsolute(block, room)` / `InPlace` / `Parented`
/ `PlayerParented`, `C_EnableCinematicMode`, `GM_EnableWideScreen`,
`MPHM_EnableHUD`, `MPHM_FadeToColor` and `C_TeleportXYZ` dispatch on
the runtime's `CinematicState`. Scripted clips carry their
`[Properties] [Message] Frame = N` hooks and the `[Movement] Filename`
root-motion KF2 (`CharacterAnimClip::frameMessages` /
`resolvedMovement`); the runtime fires the frame hooks on their edges
and runs the path's `[Exit]` list when it ends, while the viewer
samples the path KF2 for the camera, plays the player skin with the
clip (root **not** locked to bind) and the movement file carrying the
entity, and draws the widescreen letterbox + fade. Key **C** cycles the
player skin's scripted clips (the stock trigger into a cutscene is the
level FSM's state machine, not yet parsed); Esc aborts.

The shipped Max Payne model (`docs/database/skins/max_payne/`, KFS + SKD +
textures) drives the whole chain on real data: `testRealSkinning` in
`levels-test` locks the SKD weight model (874 slots, sums exactly 1, ≤4
bones), the 28/28 name-matched bone palette, an **exact** bind-pose identity
(max error 0.0000 over 5 460 vertices) and sane animated poses; the
`maxpayne-collision.kfs` node hierarchy doubles as the bind skeleton
reference for the character-root basis (Y up, Pelvis y≈0.94).

Character animation blending: clip switches cross-fade over 0.25 s the
way `X_ObjectAnimation::crossAnimateObject` + `fixCrossAnimation` do
(lerp the sampled bone worlds, then re-orthonormalize) instead of
hard-popping. Root XZ locking to the bind pose (locomotion plays
in place, the capsule carries the character) is now optional and off
for cinematic / root-motion clips.

Dropped sounds are fixed end to end: runtime message dispatch
(`A_Play3DSound` / `A_PlayFloating3DSound` / `A_PlaySound` /
`A_PlayMusic` / `A_StopMusic`) queues `SoundRequest`s positioned at the
message origin (trigger world position, door keyframe entity position,
impact point); the viewer drains the queue every frame into OpenAL
one-shot voices (`[Random]` file pick, mono buffers for 3D, hotspot /
falloff distances, cap 32 live). Music only plays when a level script
asks (`A_PlayMusic`), never auto-started. One-shot voices never loop
and `setEnvPaused(false)` only resumes sources that are actually
`AL_PAUSED` — `alSourcePlay` on a playing source restarts it from
sample 0, and calling that every frame sliced every sound into a 60 Hz
restart buzz (the crackle); it also leaked a source per request
dropped while the reader was up.

PCM WAV loader (`src/maxfx/sound`) plus **OpenAL Soft** in the viewer
(`src/viewer/Audio.cpp`). Music is a 2D relative source. FSM startup
`A_Play3DSound` (Part1_Level1 has 11: fans, speakers, coke hum, drip)
becomes `S_SoundOmni` voices: `AL_LINEAR_DISTANCE_CLAMPED` with
`AL_REFERENCE_DISTANCE = Hotspot` and `AL_MAX_DISTANCE = FallOff`.
`Database::findSound(category, name)` falls back to name-only so
`ambient, electric_hum_loop` still resolves from `dynamic.txt`.
Playback pitch follows `X_SoundFactory::getSound`:
`setFrequency(Pitch / wavSampleRate)` — the script `Pitch` is a rate in Hz
divided by the file's own sample rate (`soundPitchMultiplier`), not a
hardcoded 22050 divisor (that made 44.1 kHz graphic-novel narration run at
double speed). The comic page sound no longer restarts every frame.
The viewer stamps its git hash into the window title, the menu HUD and
stdout (`b83b840b`-style) — `scripts/build-windows.bat` copies the exe
next to the game data, and that copy silently goes stale on rebuild.
CMake fetches OpenAL Soft 1.23.1 if the system has none. The stripped
tree only ships silent `placeholder.wav`; the HUD then reads
`3d 0/N  (wavs not extracted)` while the emitters still start. F7 mutes.

### Character AI (`src/maxfx/char`)

Skin `[AI]` / `[Properties]` (capsule, health, CHARANIM clips) plus
`skeletons/default_skeleton.txt` are parsed into `CharacterConfig`. The
runtime `CharacterActor` implements the activity set named by
`X_AIStateMachineStack` (bodies stripped in the decompile):

| Activity | When | Clip |
| --- | --- | --- |
| Idle | nothing perceived | CHARANIM_STAND / W_STAND |
| Alert | player in visual or general radius | stand, turn at `TurnLeftRightSpeed` |
| Hunt | last seen, out of shooting cone | CHARANIM_WALK toward last seen |
| Combat | inside `ShootingCone` and ~10 m | W_STAND, face player |
| Patrol | idle timeout | slow walk |
| Pain / Dead | health API | GETDAMAGE / RANDOMDEATH1 |

Perception is the full `X_SharedDBSkin` static set: `VisualPerceivingRadius`
(LOS ray vs BSP inside the `AimingSpeedCone` sight cone) plus the three
noise groups — `PerceivingGroupOneRadius` hears the player's gunfire
through walls, `GroupTwo` hears running, `GroupThree` peeking — and
`GeneralPerceivingRadius`. `ActivationReactionTime` delays the switch to
combat; `ActivateOtherCharactersRadius` wakes neighbours; `EnemyInterestTime`
drops the AI back to idle when the player is lost.

Character deaths run the LDB `OnDeath` list (`applyCharacterDamage`):
death counters, story FSM sends and camera hooks all fire — e.g. killing
one finale enemy sends `::p5::death_counter->fsm_send(add1)`, which switches
the survivor to `standandshootstatic` + `C_GoToAndShoot`. While a list
runs, `this->` addresses the character itself (the engine registers the
character as its own `X_MessageLocalReceivers` "this" receiver).

**Enemies shoot.** A character with a weapon (skin `[OnInit]`
`C_PickupWeapon`, or `C_RemoveAllWeapons(keep)` / `C_PickupWeapon` runtime
messages) fires at the player inside its `ShootingCone` on a clear line of
fire, at the weapon's `DefaultShootingFrequency` divided by the skin's
`ShootingFrequencyMultiplier`, playing the weapon's `CHARANIM_SHOOT<weapon>`
clip (characteranimid.h 310-326) with every trigger pull. Every shot plays the weapon's
`WEAPONANIM_SHOOT` messages — `A_Play3DSound(weapons, shoot_*)` fire sound
and `PS_StartEffect(Muzzle_*)` muzzle flash (the placeholder synthesis
stands in for stripped WAVs) — then resolves as a hitscan against the
player capsule, other characters and the world, with damage from the
weapon's projectile script (`bullet_beretta` = 5 hp). Enemies pathfind to
the player over the level's binary `.ai` movement network (A*, 0.7 s
re-plan throttle) instead of walking in a straight line.

Character entities are full message receivers: `c_setstatemachine`
(nonreactive / idle / mobstercombat / standandshoot / ...), `c_gotoplayer`,
`c_gotoandshoot(waypoint, speed)`, `c_goto`, `c_teleport`, `c_kill`,
`c_sethealth`, `c_setidle`, `c_setimmortal` / `c_setinvulnerable`,
`c_sendspecial`, `a_play3dsound`, `c_removeallweapons`, `c_pickupweapon`
all drive the named NPC (`::gate::enemy`-style targets, plus `Activator`
from character-collide triggers). The LDB `OnInit` / `OnActivate` /
`OnSpecial` lists run at spawn / first AI activation / `C_SendSpecial` —
that is what starts the staged fights (e.g. `::teleport::e1` activating
sends `::p5::script->FSM_Send(start)`, which switches both finale enemies
to `mobstercombat` / `crouchandshoot` and sends them `c_gotoplayer`).

### AI movement network (`src/maxfx/ai`)

The binary `.ai` file next to each LDB (`X_GlobalAIObject`) is fully parsed
with `TaggedReader` (see `docs/AI_FORMAT.md`): exit-hull arrays between
rooms plus one `X_RoomMovementNetwork` per room, nodes with four link slots
and pre-computed link distances. World positions resolve through
`roomMatrix(room) * arrayTransform(room)` (exit hulls) or
`roomMatrix(room)` (room grids). `X_RoomMovementNetwork::save` writes the
room index **twice** (once inside the node-array record, once after it) —
the parser mirrors that. A* (`findPath`) searches node ids with
Euclidean heuristics; `nearestNode` biases same-room nodes. Part1_Level1:
4295 nodes / 14787 links / 38 room networks, parsed in ~5 ms. A missing or
incompatible file leaves the graph empty and the game plays on, exactly
like the engine's `X_GlobalAIObject` exception path.

### Collision (`src/maxfx/collision`)

`CollisionWorld` fan-triangulates **room static meshes** (the same triangles
the renderer draws). The LDB BSP is a vis / partition mesh — on Part1_Level1
its vertices sit metres away from spawned NPCs, so using it as a floor made
everyone T-pose-fall through the map while 110k triangles were tested every
frame. Queries go through a 2 m uniform grid.

`X_Character::collideObjects` is object-object dispatch (characters, items,
triggers, dynamic meshes). The viewer:

- slides the camera as a 0.22 m sphere (LDB space, X-unmirrored)
- snaps each character capsule onto the static-mesh floor (keeps spawn Y if
  the downward ray misses, instead of applying gravity into the void)
- CHARANIM_POSE / STAND / WALK clips are cached with the skin so characters
  are skinned from the idle/walk pose, not the bind T-pose

## What is still missing (engine-port debt, not viewer hacks)

- Shootdodge / bullet-time / CHARANIM_SHOOT* first-person overlay
- Enemy dodge / cover / wounded locomotion and upper-body aiming
  (`X_AimSetup` blends aim/up/down clips; enemies currently fire from the
  standing shoot clip only)
- Clip cross-fade (`crossAnimateObject` adds two samples, then
  `fixCrossAnimation` re-orthonormalizes — see docs/ANIMATION.md)
- Level-exit streaming
- Dynamic mesh animation (doors, trains)
- Save / load (death currently respawns in place — F9)
- Full FSM / `[Message]` execution beyond the targeted subset
  (T_/DO_/FSM_Switch/FSM_Send/MPGNM/s_modeswitch/A_Play*)
- SCX / DDS texture decode
- Graphic-novel KF2 cameras (pages are framed from the plate AABB)
- KF2 cameras / point-light animation chunks
- Additive light halos (currently alpha-blend)
- MAX-ED editor-only maps that the PC file still stores but the game ignores

## Tests

```
make test    # levels-test: R_Script (nested quotes, 3DSound), levels.txt,
             # unbraced blocks, PCX alpha, KF2 beretta + alex KFS,
             # engine keyframe sampling (loop wrap / seam blend / methods),
             # trigger latch / T_Enable / own-FSM dispatch on Part1_Level1,
             # skin AI + BSP capsule collision,
             # materials / items / skins / sounds / music, placeholder.wav,
             # S_SoundOmni gain, FSM A_Play3DSound collect, comic chapters,
             # camera paths / cinematic frame hooks / fades / [Exit] dispatch,
             # dynamic-object poses / door state, noclip descend,
             # .ai graph parse + A* detour, enemy perception / reaction /
             # weapon fire + sound pairing + shoot clip, nonreactive gate,
             # character message routing (C_SetStateMachine /
             # C_PickupWeapon), onDeath lists -> FSM death counters,
             # player death / F9 respawn
```

No SDL required. The viewer is `cmake -S . -B build && cmake --build build`
(needs OpenGL 3.3 + SDL3 + OpenAL Soft; both SDL3 and OpenAL Soft are
fetched if missing).

## Layout

```
src/maxfx/core     TaggedReader, math, filesystem
src/maxfx/script   R_Script
src/maxfx/levels   levels.txt
src/maxfx/ldb      Level database
src/maxfx/kf2         KF2 / KFS / SKD + keyframe animation / skinning
src/maxfx/db          shared text database (skins include [AI] / clips)
src/maxfx/ai          binary .ai movement network + A*
src/maxfx/char        CharacterConfig + activity FSM + combat AI
src/maxfx/collision   BSP triangle soup, sphere slide, capsule
src/maxfx/sound       WAV + S_SoundOmni gain
src/maxfx/image       texture decode
src/viewer            SDL3 OpenGL + OpenAL Soft + AI / collision
docs/STATUS.md     this file
```

Built `ldb-viewer.exe` is meant to sit next to the game's `data/` folder so
it can read `data/database/levels/levels.txt` and
`data/database/materials.txt`.
