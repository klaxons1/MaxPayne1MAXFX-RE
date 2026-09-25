# Max Payne 1 `levels.txt` (X_SharedDBLevel)

`X_SharedDB::construct` loads the level table as `("Levels", "levels.txt")`.
The file lives at `data/database/levels/levels.txt`. Each top-level `[id]`
block is one `X_SharedDBLevel`; `gm_init(id)` / the console use that id.

Android `libMaxPayne.so` (`X_SharedDBLevel::construct`) does:

```
blockCheck("properties")
lock("Properties");  parse(off_DB3F48);  unlock();
lock("Difficulty");  parse(MaximumHealth...);  unlock();
lock("Timedmode");   parse(PlayTime, EnemyTimeBonus);  unlock();
path = dataDir + "/" + Directory + "/" + Level
```

The Properties table starts at `off_DB3F48 = "LevelName"` and runs for 22
32-byte records (name, dest, unused, `R_Script` type) before the next symbol
at `off_DB4228`. Destinations are BSS buffers that construct then copies onto
the object. UpVector is **not** a script field: it is `normalise(-Gravity)`.

## File grammar (`R_ScriptLoader`)

```
#include <file>          # top level only, <> or ""
#define NAME value       # top level only, name ≤ 159, value < 160
// line comment
/* block comment */

[blockname]
{
    lvalue = rvalue;
    [child]
    {
        ...
    }
}
```

- Whitespace: NUL, TAB, LF, VT, CR, SPACE (`isWhiteSpace` bitmask `0x100002E01`).
- End of line is `\n` only.
- Block names and lvalues are case-insensitive (stored lower-cased).
- Quoted `"rvalues"` keep interior spaces and original case; quotes are stripped.
- Unquoted rvalues keep file case (`Level = Part1_Level1.ldb`).
- `#define` replacement is case-insensitive and happens before parsing.
- `TRUE`/`FALSE` are normally defined in `globaldefines.h`; the typed parsers
  also accept those words so a snippet without the header still works.
- Trailing `%` on numbers is ignored (`MaximumHealth = 300%`).
- `R_Script::t_float3` is sscanf `"(%f,%f,%f)"`.

## `[Properties]` fields (PC MaxEd + Android)

| Key | Type | Object | Notes |
| --- | --- | --- | --- |
| LevelName | string | +32 | UI name |
| Directory | string | +64 | relative to `levels/` |
| Level | string | +96 | `.ldb` file name |
| Gravity | float3 | +128 | UpVector at +140 is computed |
| AmbientColor | float3 | +152 | 0–255 style in the SDK examples |
| PostProcessMultiply | float3 | +176 | Android extra, default `(1,1,1)` |
| PostProcessAdd | float3 | +164 | Android extra, default `(0,0,0)` |
| WorldSphere | string | +188 | empty = none |
| DebrisProjectileCountInLevel | int | +252 | |
| DebrisProjectileCountPerRoom | int | +256 | |
| PlayerSkinName | string | +260 | must exist in skins.txt |
| LoadingScreen | string | +292 | |
| EnableAI | int/bool | +324 | |
| PlayerStartingPlace | string | +220 | `"::room::Jumppoint 00"` |
| Fogging | int/bool | +325 | |
| FogColor | float3 | +328 | |
| FogStart | float | +340 | |
| FogEnd | float | +344 | |
| StartupLevel | int/bool | +348 | |
| ExitLevel | int/bool | +349 | |
| AINodeCastHeight | float | +352 | 0 = room bbox height |

Unknown Properties keys are retained on `LevelInfo::extraProperties`.

## `[Difficulty]` / `[Timedmode]`

Difficulty dests (from the stack-built table in `construct`):

`MaximumHealth`, `MinimumHealth` (float), `MinimumDeaths`, `MaximumDeaths` (int),
`MinimumTime`, `MaximumTime` (float), `HardcoreHealth` (float),
`HardcoreDeaths` (int), `HardcoreTime` (float).

Timedmode: `PlayTime`, `EnemyTimeBonus` (float).

## Tools

```
make dump          # ldb-dump + levels-dump + levels-test
./levels-dump docs/levels.txt
./levels-test
```
