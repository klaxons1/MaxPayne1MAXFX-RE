# MAX-FX reverse engineering — current status

Last updated 2026-09-26. Comments and this file are in English; the code is
C++11. The target is the **PC** Max Payne 1 MAX-FX format. The Android
`libMaxPayne.so` decompile in `docs/` is used for names and version numbers
only — its loaders were stripped and can disagree with the PC layout.

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

`materials.txt`, `skins/*.txt`, `level_items/*.txt` and `decals/decals.txt`
depend on this.

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

KF2/KFS/SKD files referenced by skins and items that actually appear in the
loaded LDB are parsed on demand and cached.

### KF2 / KFS / SKD (`src/maxfx/kf2`)

Same chunk format for all three (KFS = mesh, SKD = skin weights, KF2 = mesh
and/or keyframe animation). Material lists, meshes (geometry, triangles, UV,
polygon materials, node transforms), and skin chunks are parsed. Lights,
cameras and animation chunks are skipped without desynchronising.

`kf2BuildDrawMeshes` produces triangle lists in node-local space, including
v2 primitive-local index rebasing. Bind-pose world matrices follow the node
parent chain.

Self-test: `beretta_levelitem.kf2` (339 verts, 336 triangles) and
`Alex_Balder_L0.kfs`.

### Images (`src/maxfx/image`)

- JPEG / TGA via stb_image
- 8-bit paletted PCX, 8-bit 3/4-plane PCX
- Greyscale paletted PCX (MAX-FX `*_alpha.pcx`) writes the grey value into
  **alpha**, so opacity maps display. Confirmed on `docs/baseballbat_alpha.pcx`
  (128×32, identity grey palette)
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
| F3 | lit (lightmap×2) → diffuse → lightmap → **vertex** |
| F6 | show service / no-draw materials |
| Left/Right | next map in `levels.txt` |

Vertex colour is LDB radiosity (`std::map<int, vec3>` on static/dynamic
meshes) when present, otherwise Lambert from point lights + static lights.

Decal z-fighting: materials with `DetailOffset > 0` or alpha test are drawn
later with `glPolygonOffset`. `WritesZBuffer = FALSE` disables depth writes.

Characters and items: LDB transform × KF2 node bind pose, textured from
`ExportData` directories (`textures;..\sharedtextures`). The stripped sample
database has weapon KF2s and one character KFS (`balder_alex`); most skin
folders have scripts only, so those actors stay as helper diamonds.

## What is still missing (engine-port debt, not viewer hacks)

- Skeletal animation playback (KF2 keyframe chunks + SKD weights + default
  skeleton). Rest pose is drawn.
- SCX / DDS texture decode
- Graphic-novel page KF2s (environment chunk is skipped; pages are not
  placed in the level viewer)
- Full FSM / message execution
- Collision, AI, projectiles, particles
- MAX-ED editor-only maps that the PC file still stores but the game ignores

## Tests

```
make test    # levels-test: R_Script, levels.txt, unbraced blocks,
             # PCX alpha, KF2 beretta + alex KFS, materials.txt
```

No SDL required. The viewer is `cmake -S . -B build && cmake --build build`
(needs OpenGL 3.3 + SDL3, fetched if missing).

## Layout

```
src/maxfx/core     TaggedReader, math, filesystem
src/maxfx/script   R_Script
src/maxfx/levels   levels.txt
src/maxfx/ldb      Level database
src/maxfx/kf2      KF2 / KFS / SKD
src/maxfx/db       shared text database
src/maxfx/image    texture decode
src/viewer         SDL3 OpenGL viewer
docs/STATUS.md     this file
```

Built `ldb-viewer.exe` is meant to sit next to the game's `data/` folder so
it can read `data/database/levels/levels.txt` and
`data/database/materials.txt`.
