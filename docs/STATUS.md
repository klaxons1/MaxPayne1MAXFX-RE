# MAX-FX reverse engineering — current status

Last updated 2026-09-26 (OpenAL Soft 3D env sounds, graphic-novel reader UI).
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
| F7 | mute music |
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

### Game runtime (`src/maxfx/game`)

PC `MP.exe` message table plus the database scripts:

| Piece | Source |
| --- | --- |
| Menu | `MaxPayne_MenuMode` + custom **Jump to Level** (`levels.txt`) |
| Player | `max_payne.txt` (`PLAYER_MOVEMENT` 4.2, `AirborneSpeed` 2.5, `C_Jump(7.5)`), gravity −9.81 m/s² (levels.txt −981 cm/s²) |
| Hitscan | `CROSSHAIR_CASTLENGTH` / beretta `[Attributes]` + `bullet_beretta` Damage |
| Triggers | LDB type 0..4 = action / player collide / projectile / character / look-at; `T_Activate` |
| Doors | `DO_Animate` / `DO_InvertAnimation` on dynamic-mesh clips |
| Comics | `graphicnovelpages.txt` chapters + full-screen reader (KF2 plate, not a world billboard) |
| OnInit | `C_PickupWeapon`, `C_PickupAmmo`, `C_SetHealth`, `C_DisplayCrosshair`, `GM_SetPlayerControls` |

Esc opens the menu. WASD walk, Space jump, E use, LMB shoot.

PCM WAV loader (`src/maxfx/sound`) plus **OpenAL Soft** in the viewer
(`src/viewer/Audio.cpp`). Music is a 2D relative source. FSM startup
`A_Play3DSound` (Part1_Level1 has 11: fans, speakers, coke hum, drip)
becomes `S_SoundOmni` voices: `AL_LINEAR_DISTANCE_CLAMPED` with
`AL_REFERENCE_DISTANCE = Hotspot` and `AL_MAX_DISTANCE = FallOff`.
`Database::findSound(category, name)` falls back to name-only so
`ambient, electric_hum_loop` still resolves from `dynamic.txt`.
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

Perception uses `VisualPerceivingRadius` (LOS ray vs BSP) and
`GeneralPerceivingRadius`. `ActivateOtherCharactersRadius` wakes neighbours.
This is the official script contract, not a viewer hack; projectile fire,
dodge FSM, and the binary `.ai` graph next to each LDB are still engine debt.

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
- Dodge / cover / wounded locomotion (clips are parsed, not selected)
- Dynamic-mesh GPU transform (doors animate in state; textured mesh stays at bind)
- Binary `.ai` path graph next to each `.ldb` (tagged, not R_Script)
- Level-exit streaming
- Runtime bullet-hole / blood decals from `decals/decals.txt`
- Dynamic mesh animation (doors, trains)
- Particles (sparks, shells, smoke)
- Save / load
- Full FSM / `[Message]` execution beyond startup A_Play3DSound / comics OnInit
- SCX / DDS texture decode
- Graphic-novel KF2 cameras (pages are framed from the plate AABB)
- KF2 cameras / point-light animation chunks
- Additive light halos (currently alpha-blend)
- MAX-ED editor-only maps that the PC file still stores but the game ignores

## Tests

```
make test    # levels-test: R_Script (nested quotes, 3DSound), levels.txt,
             # unbraced blocks, PCX alpha, KF2 beretta + alex KFS,
             # keyframe animation + skin AI + BSP capsule collision,
             # materials / items / skins / sounds / music, placeholder.wav,
             # S_SoundOmni gain, FSM A_Play3DSound collect, comic chapters
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
src/maxfx/char        CharacterConfig + activity FSM
src/maxfx/collision   BSP triangle soup, sphere slide, capsule
src/maxfx/sound       WAV + S_SoundOmni gain
src/maxfx/image       texture decode
src/viewer            SDL3 OpenGL + OpenAL Soft + AI / collision
docs/STATUS.md     this file
```

Built `ldb-viewer.exe` is meant to sit next to the game's `data/` folder so
it can read `data/database/levels/levels.txt` and
`data/database/materials.txt`.
