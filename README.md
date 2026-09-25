# MAX-FX reverse engineering — LDB level viewer

C++11 toolkit for Max Payne 1: a complete tagged-binary `*.ldb` parser, an
SDL3 / OpenGL 3.3 fly-through viewer, and an `R_Script` parser for
`data/database/levels/levels.txt` (`X_SharedDBLevel`).

The long-term goal is a source port of the MAX-FX engine. This tree is the
first brick — load every block of a PC LDB (version 32) and look at the level.

The Android `libMaxPayne.so` decompile in `docs/` is useful for names and
version numbers, but its `X_LevelDBLevel::load` was optimised down to a
version check (`expected 32`). The on-disk layout implemented here is the
**PC** format, which that check agrees with.

## What’s in an LDB

Remedy stores every field as `{tag}{payload}` (`R_MemoryFile` tagged I/O).
Integers and floats may be compacted (1/2/3-byte ints, IEEE-754 binary16).
Containers use extra tags (`0x1C` vector, `0x1F` map, `0x25` pair).

Block order for version 32:

1. Collision BSP (vertices, polygons, nodes, indices)
2. File version
3. Embedded textures (JPG / TGA / PCX) and materials
4. Lightmaps (TGA)
5. Exits / portals
6. Static meshes (shared texture-vertex pool + per-room geometry)
7. Static lights, waypoints, FSMs, characters, triggers
8. Dynamic meshes (geometry, animations, FSM messages)
9. Items, point lights
10. Rooms (name + object lists)

See `docs/LDB.md` for the tagged type table.

## Building

### Parser / dump tool (no extra deps)

```bash
make dump
./ldb-dump docs/Part1_Level1.ldb
./levels-dump docs/levels.txt
make test
```

### Level viewer (SDL3 + OpenGL 3.3)

Linux / macOS:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/ldb-viewer docs/Part1_Level1.ldb
```

CMake will `find_package(SDL3)` or fetch SDL 3.2 from GitHub.

### Windows (Visual Studio 2022)

From an **x64 Native Tools Command Prompt for VS 2022**:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Or run `scripts\build-windows.bat`.

The viewer is linked against static SDL3 and the static MSVC runtime, so
`build\Release\ldb-viewer.exe` is a single file. Copy it next to the game
`data` folder:

```
ldb-viewer.exe
data\
  database\
    levels\
      Part1_Level1.ldb
      Part1_Level2.ldb
      ...
```

Double-click the exe (no arguments). It scans `data\database\levels\*.ldb`
and **Left / Right** arrows switch maps. You can still pass a file or folder:

```
ldb-viewer.exe D:\games\MaxPayne\data\database\levels\Part2_Level1.ldb
ldb-viewer.exe D:\games\MaxPayne\data
```

Needs a GPU with OpenGL 3.3 (any driver from the last decade).

## Viewer controls

| Key | Action |
| --- | --- |
| W A S D | Fly |
| Q / E or Ctrl / Space | Down / up |
| Shift | Sprint |
| Mouse | Look (click to recapture) |
| Left / Right | Previous / next `.ldb` in `data\database\levels` |
| F1 | Toggle help |
| F2 | Wireframe |
| F3 | Shading: lit → diffuse → lightmap → flat |
| F4 | Helper gizmos (waypoints, triggers, characters, items, lights, exits) |
| F5 | Toggle dynamic meshes |
| `[` `]` | Isolate one room / show all |
| PgUp / PgDn | Jump between jumppoints |
| R | Reset to `::startroom` jumppoint |
| Esc | Release mouse, Esc again quits |

Lit shading is `diffuse * lightmap * 2`, the classic MAX-FX look.

## Layout

```
src/maxfx/core     tagged stream + math + filesystem helpers
src/maxfx/script   R_Script / R_ScriptLoader (.txt databases)
src/maxfx/levels   levels.txt → X_SharedDBLevel
src/maxfx/ldb      LDB structures + reader
src/maxfx/image    JPG/TGA (stb) + PCX decoder
src/viewer         SDL3 OpenGL viewer
src/apps           ldb-dump, ldb-viewer, levels-dump, levels-test
docs/              Android decompile, sample LDB, sample levels.txt
```

`levels.txt` field tables and the script grammar are documented in
`docs/LEVELS.md`. Drop the real game file at
`data/database/levels/levels.txt` (next to the `.ldb` files) and run
`levels-dump` with no arguments.

## PC vs Android

Both expect LDB **version 32**. Android may later diverge in texture formats
or missing editor-only maps; the reader fails fast on an unknown version
rather than silently desynchronising. SCX (Remedy proprietary) textures are
detected but not decoded.

## Sample level

`docs/Part1_Level1.ldb` is Part 1, Level 1 (NYC subway / Roscoe Street station).
`ldb-dump` on that file reports 38 rooms, 212 textures, 24 lightmaps, 322
dynamic meshes and consumes the entire 31 MiB stream.
