# Max Payne 1 AI movement network (`.ai`)

The binary `.ai` file sitting next to each `.ldb` (e.g. `Part1_Level1.ai`
beside `Part1_Level1.ldb`) is the pre-calculated AI movement network written
by `X_GlobalAIObject::save`. It is a single tagged-value stream (the same
`R_MemoryFile` encoding as the LDB, see docs/LDB.md "Tagged values") and
starts with the magic integer `123`. The engine loads it when a level starts;
on failure it prints

```
AI network file "%s" not found
Calculate AI network for corresponding level.
```
```
AI network file "%s" is incompatible
You need to recalculate the network.
```

Everything below is verified byte-exact against the PC sample
`docs/Part1_Level1.ai` (308,069 bytes, 76,672 tokens, zero unknown tags,
stream ends exactly at EOF) and against the decompiled save/load code
(`X_GlobalAIObject::save` line 352004, `X_MovementNodeArray::save` 359466,
`X_MovementNode::save` 357391, `X_RoomMovementNetwork::save` 367267).

## Tagged values

Same `{uint8 tag}{payload}` framing as the LDB. The `.ai` writer only emits a
subset of the tags:

| Tag | Payload | Meaning |
| --- | --- | --- |
| `0x02` | i32 | signed 32-bit |
| `0x09` | f32 | float |
| `0x13` | i16 | signed 16-bit |
| `0x14` | i8 | signed 8-bit |
| `0x16` | 3 × f32 | vector (floats are **not** tagged) |
| `0x1A` | 12 × f32 | 4×3 transform (3 basis rows + translation) |

The integer writer is adaptive: small values are stored as `0x14` (1 byte),
larger ones as `0x13` (2 bytes) or `0x02` (4 bytes). **A reader must accept
all three encodings for every integer field** — the same field of the same
record switches encoding per value (e.g. node IDs 58..13505, link counts,
room indices). In the sample the `0x02` tag never occurs (every int fits in
an i16); the tag census is `0x09`×22,000, `0x13`×33,788, `0x14`×16,253,
`0x16`×4,520, `0x1A`×111 = 76,672 tokens.

## Top-level layout

```
int  123                 magic / version, only value ever checked by the loader
int  exitArrayCount      number of exit-hull node arrays that follow
X_MovementNodeArray  × exitArrayCount
int  networkCount        number of per-room movement networks
X_RoomMovementNetwork × networkCount
<EOF>
```

`X_GlobalAIObject::save` writes nothing else. The exit arrays are implicitly
numbered `0 .. exitArrayCount-1` by their position in the section; that
index is what `X_RoomMovementNetwork::Exit::nodeArrayID` refers to.

Sample: `123`, `37` exit arrays, `38` room networks (rooms 0..37).

## X_MovementNodeArray record

One record per array. Saved by `X_MovementNodeArray::save` in exactly this
order:

```
int   nodeCount
X_MovementNode × nodeCount
int   roomIndex          room the array lives in (room-local space)
f32   gridDensity        LDB room AINetDensity (grids) or density*0.3 (exits)
f32   rayEpsilon         0.05 - vertical offset subtracted from cast origins
f32   maxLinkHeight      max vertical drop along gravity between linked nodes
f32   maxSafeWalk        per-direction walk cap for safe-node-area
vec3  gravity            (0, -20, 0) in the sample, from X_SharedDBLevel
f32   capsuleTop         2.0  - character capsule top limit
f32   capsuleBottom      0.0  - character capsule bottom limit
f32   capsuleRadius      0.31 - AI capsule radius
int   transformCount
{ int roomIndex, Mat4x3 transform } × transformCount
vec3  hullMin            grid bounds / exit hull, min corner (room-local)
vec3  hullMax            grid bounds / exit hull, max corner (room-local)
int   exitListCount      shared exit-reachability table (see below)
{ int idCount, int id × idCount } × exitListCount
```

(Runtime member offsets from the decompile: count `+24`, node pointer array
`+32`, room `+48`, density `+60`, epsilon `+64`, maxLinkHeight `+68`,
maxSafeWalk `+72`, gravity `+76`, capsule `+88/92/96`, room→matrix map
`+112`, hullMin `+136`, hullMax `+148`, reachability table `+184`. The
array's own ID is *not* stored — it is the section index.)

### The two kinds of array

Exit-hull arrays and room grids are the same record with different
parameters. `X_MovementNodeArray::construct` is called with an `isExit` flag
(7th argument: `1` from `X_GlobalAIObject::precalculate`, `0` from
`X_RoomMovementNetwork`'s constructor), and the two call sites pass the
constants 3.0/5.0 in **swapped order**, which the file preserves:

| Field | Exit-hull array | Room grid |
| --- | --- | --- |
| `gridDensity` | `min(src, dst AINetDensity) * 0.3` (0.3 / 0.15 observed) | raw room `AINetDensity` (1.0 / 0.5 / FLT_MAX) |
| `maxLinkHeight` | **3.0** | **5.0** |
| `maxSafeWalk` | **5.0** | **3.0** |
| node link capacity | 2 slots | 4 slots |
| built from | LDB exit convex hull (+ normal*0.1) | room AABB |

`gridDensity == 3.4028e38` (FLT_MAX) means "AI network disabled for this
room": the generator skips the grid entirely, so those arrays have
`nodeCount == 0` (12 of 38 rooms in the sample). Grid resolution: orthogonal
neighbour spacing equals `gridDensity` (measured link distances cluster at
1.0 m and 0.5 m; the rest are diagonals and sloped/stair links at
0.7–1.4 m).

`capsuleTop/Bottom/Radius` come from `X_GlobalCharacterAttributes::
getCapsuleTopLimit / getCapsuleBottomLimit / getAICapsuleRadius` (2.0 / 0.0 /
0.31 m in the sample). During generation a capsule of these dimensions is
swept in 0.2 m steps along each candidate link (`checkConnection`); any
collision rejects the link. `maxLinkHeight` gates the vertical component
before the sweep.

### Node transformations

`transformCount` entries map a **room index to a Mat4x3** that converts this
array's room-local coordinates into that room's coordinates. For every array
in the sample the key set is exactly the set of rooms reachable through its
exits (38/38 networks and 37/37 exit arrays verified; an exit-hull array
holds exactly one — its exit's destination room), e.g. room 1's grid holds
transforms for rooms 11 and 37 — the two destinations of room 1's exits.
Cross-room node links are checked in the transformed space.

Map iteration order at save time follows the runtime `std::map` keyed by
room **pointer**, so keys are ascending room indices in this sample but a
reader must not rely on it.

## X_MovementNode record

```
int   id                 global node id, unique across the whole file
int   linkCapacity       2 (exit arrays) / 4 (room grids); see note below
vec3  position           room-local, metres, always inside the array hull
{ int linkID, f32 walkDist } × 4     ALWAYS four slots, empty slot = id 0
f32   safeArea           openness 0..1 (see below)
int   exitListIndex      index into the parent array's reachability table
int   reverseLinkCount
int   reverseLinkID × reverseLinkCount
```

(Runtime offsets: isConnected flag byte `+9`, capacity/count `+12`,
position `+16`, link ids `+28..44`, link pointers `+48..80` (0xFFFFFFFF
until resolved), per-direction walk distances `+80..96`, safeArea `+96`,
exitListIndex `+124`, reverse links `+136`, reverseLinkCount `+144`. The id
is not stored inline: `getID` dereferences the node's entry in the global
`sm_mapIntToNode` (member `+152` points at it).)

Notes:

- The record **always contains four link slots** regardless of
  `linkCapacity`; `X_MovementNode::save` loops a fixed four times. Unused
  slots are `id = 0` (written as `i8 0`) with distance `0.0`.
- `linkCapacity` mirrors the runtime counter at `+12`, which the generator
  pre-initialises to the slot count and fills via `setLink` (which does not
  touch the counter). Observed values are exactly 2 for all 114 exit-hull
  nodes and 4 for all 4181 grid nodes.
- The `f32` paired with each link is **not** a path cost: it is the
  per-direction walkable distance produced by
  `X_MovementNode::calculateSafeNodeArea` — how far (in metres, accumulated
  as `gridDensity` steps) you can walk from this node in that link's
  direction before hitting a wall, a dead end or `maxSafeWalk`. Sample
  maxima: 4.95 in exit arrays (cap 5.0), 2.95 in room grids (cap 3.0).
- `safeArea = clamp(sum(walkDist[0..3]) / (maxSafeWalk * 4), 0, 1)` — how
  open the area around the node is (1.0 = open space, ~0 = corridor nook).
  The AI uses it as a stand-off / cover heuristic
  (`getSafeArea(node) >= 0.75` checks in the combat code).
- Forward links and reverse links are consistent (every reverse link of A
  points at a node whose forward links contain A; 0 dangling in the sample).
  The forward graph is 99.9 % symmetric (14,766 of 14,787 edges); the 21
  one-way links are step-downs of 0.15–0.40 m — the capsule walk accepted
  them downhill but the upward link was never created.
- Link targets are node **ids**, not indices; the runtime resolves them
  through the global `sm_mapIntToNode` after loading. Link ids are valid
  across arrays: 228 grid→exit and 228 exit→grid links, 0 direct
  exit→exit links.

## Exit-reachability table

The trailing `exitListCount` lists are a deduplicated table shared by the
array's nodes; each node's `exitListIndex` picks one row. Two different
builders fill it:

- **Room grids** (`findNodeToExitConnections`): flood-fill each connected
  island of the grid; collect the ids of *foreign* arrays reached by direct
  links (i.e. the exit-hull arrays attached to that island); identical sets
  are deduplicated (`findMatchingArray`); every node of the island gets the
  set's index. Islands that reach no exit keep the default index 0, and row
  0 is conventionally the empty list. E.g. room 10's grid has rows
  `[]`, `[14]`, `[13,15]` — two islands, one reaching only exit 14.
- **Exit-hull arrays** (`findExitToExitConnections`): the union of the exit
  sets of all room grids this exit connects to, minus itself. E.g. exit 2
  (room 1 → room 37) stores `[1, 36, 8, 32]`: exit 1 through room 1's grid,
  exits 36/8/32 through room 37's grid.

This table is the high-level pathfinding structure (`findHighLevelPath`):
from a node the AI knows which exits it can walk to, and exit-to-exit rows
chain exits across rooms without entering intermediate grids.

## X_RoomMovementNetwork record

```
X_MovementNodeArray    the room's grid (same record as above)
int   roomIndex        same value as the array's roomIndex (saved twice)
int   exitCount
{ int exitArrayID, int destinationRoomID } × exitCount
```

Each `Exit` (32 bytes at runtime) names a doorway touching the room **from
either side**: `exitArrayID` indexes the exit-array section, and
`destinationRoomID` is the room on the *other* side of that doorway. Room 11
stores `(16, 33)` — its own exit 16 leading to room 33 — and `(1, 1)` —
room 1's exit 1, which leads back to room 1. Room 37 (a hub) lists six
exits: `(36,36) (32,28) (2,1) (4,2) (8,5) (26,21)`.

## Generation summary (from `X_GlobalAIObject::precalculate`)

For each room and each LDB exit, an exit-hull array is created from the
exit's convex hull (grown by `normal * 0.1`), with
`density = min(src, dst) * 0.3`; the exit position/normal themselves are
**not** stored in the file. Then one `X_RoomMovementNetwork` per room with
the room AABB as hull. Grids are generated by casting rays down from
`AINodeCastHeight` (default hull height + 20, overridable via
`X_SharedDBLevel::getAINodeCastHeight`), placing nodes on the floor and
linking neighbours with the 0.2 m capsule sweep. Finally
`connectNodes` / `connectExitToRooms` / `findNodeToExitConnections` /
`findExitToExitConnections` / `addReverseNodeLinks` build the graph, and the
result is saved next to the level as `<levelfile without .ldb> + "ai"`.

## Loader behaviour and caveats

- Only the magic `123` is validated; a truncated or shifted stream is read
  as garbage rather than rejected.
- The Android loader (`X_GlobalAIObject::load`, line 352599) reads the
  magic, then two integers, then constructs exactly
  `MaxPayne_Level::getRoomCount()` room networks — the room count comes from
  the level, not the file. Its decompile shows no exit-array section being
  consumed, so either the decompiler collapsed that loop or the Android
  build ships simplified `.ai` files; the Android game never recalculates
  networks.
- The PC files must be read in the full order documented above: exit-array
  section first, then the network section. Node links and exit ids are
  global; build an id→node map over both sections before resolving.

## Sample statistics (docs/Part1_Level1.ai)

| | |
| --- | --- |
| bytes / tokens | 308,069 / 76,672 (parse ends exactly at EOF) |
| exit-hull arrays | 37 (114 nodes, all with exactly 2 links) |
| room networks | 38 (rooms 0..37; 12 rooms have no grid) |
| nodes | 4,295, unique ids 58..13505 |
| forward links | 14,787 non-empty slots of 17,180 (99.9 % symmetric, 21 one-way) |
| grid densities | 1.0 (23 rooms), 0.5 (rooms 6, 28, 33), FLT_MAX (12 rooms) |
| constant fields | rayEpsilon 0.05, gravity (0,-20,0), capsule 2.0/0.0/0.31 |

First bytes of the file, annotated:

```
14 7b                 i8  123          magic
14 25                 i8  37           exit array count
14 00                 i8  0            array[0].nodeCount (empty)
14 00                 i8  0            array[0].roomIndex
09 9a 99 99 3e        f32 0.3          gridDensity (exit: 1.0*0.3)
09 cd cc 4c 3d        f32 0.05         rayEpsilon
09 00 00 40 40        f32 3.0          maxLinkHeight (exit variant)
09 00 00 a0 40        f32 5.0          maxSafeWalk   (exit variant)
16 00..00 a0 c1       vec3 (0,-20,0)   gravity
09 00 00 00 40        f32 2.0          capsuleTop
09 00 00 00 00        f32 0.0          capsuleBottom
09 52 b8 9e 3e        f32 0.31         capsuleRadius
14 01                 i8  1            transformCount
14 15                 i8  21           transform[0].roomIndex
1a 00 00 80 3f ...    Mat4x3           transform[0].matrix
...
```
