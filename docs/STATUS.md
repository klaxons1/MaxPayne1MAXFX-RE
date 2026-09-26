# MAX-FX reverse engineering — current status

Last updated 2026-09-26 (upright skinning / camera-follow world sphere). Comments and this file
are in English; the code is C++11. The target is the **PC** Max Payne 1
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
clips (not the bind T-pose). Animated VBOs are streamed, not recreated.

PCM WAV loader (`src/maxfx/sound`) plus an SDL3 mixer (`src/viewer/Audio.cpp`)
loops the level theme when the official banks are next to the exe. The
stripped tree only ships silent `placeholder.wav`; the HUD then reads
`wavs not extracted`. F7 mutes.

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

- Player controller (walk / shootdodge / bullet-time) — viewer is still a flycam
- Weapons, hitscan, projectiles, ammo, CHARANIM_SHOOT* overlay
- Dodge / cover / wounded locomotion (clips are parsed, not selected)
- Binary `.ai` path graph next to each `.ldb` (tagged, not R_Script)
- Full FSM / `[Message]` execution (`C_DisplayCrosshair`, `A_Play3DSound`, …)
- Triggers (radius / look-at / collide) and level-exit streaming
- Runtime bullet-hole / blood decals from `decals/decals.txt`
- Dynamic mesh animation (doors, trains)
- Particles (sparks, shells, smoke)
- HUD (health, ammo, graphic novel), menus, save/load
- 3D positional cues attached to FSM A_PlaySound (scripts are parsed; the
  viewer only loops the level theme / first available WAV)
- SCX / DDS texture decode
- Graphic-novel page KF2s (environment chunk is skipped)
- KF2 cameras / point-light animation chunks
- Additive light halos (currently alpha-blend)
- MAX-ED editor-only maps that the PC file still stores but the game ignores

## Tests

```
make test    # levels-test: R_Script (nested quotes, 3DSound), levels.txt,
             # unbraced blocks, PCX alpha, KF2 beretta + alex KFS,
             # keyframe animation + skin AI + BSP capsule collision,
             # materials / items / skins / sounds / music, placeholder.wav
```

No SDL required. The viewer is `cmake -S . -B build && cmake --build build`
(needs OpenGL 3.3 + SDL3, fetched if missing).

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
src/maxfx/sound       WAV
src/maxfx/image       texture decode
src/viewer            SDL3 OpenGL viewer + mixer + AI / collision
docs/STATUS.md     this file
```

Built `ldb-viewer.exe` is meant to sit next to the game's `data/` folder so
it can read `data/database/levels/levels.txt` and
`data/database/materials.txt`.
