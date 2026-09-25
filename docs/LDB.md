# Max Payne 1 LDB (version 32)

PC Max Payne 1 and the Android port both reject anything other than version
`32` (`X_LevelDB Wrong version, expected %d, found %d!`).

## Tagged values

Every value is `{uint8 tag}{payload}`. Multi-byte numbers are little-endian.

| Tag | Payload | Meaning |
| --- | --- | --- |
| `0x00` / `0x02` | i32 | signed 32-bit |
| `0x01` / `0x03` | u32 | unsigned 32-bit |
| `0x12` / `0x0F` | 3 bytes | packed 24-bit int / uint |
| `0x13` / `0x04` / `0x10` / `0x05` | 2 bytes | 16-bit int / uint |
| `0x14` / `0x06` / `0x07` / `0x08` / `0x11` | 1 byte | 8-bit |
| `0x09` | f32 | float |
| `0x26` | f16 | IEEE-754 binary16 |
| `0x0A` | f64 | double |
| `0x0E` | u8 | bool |
| `0x0D` | `{M_Int length}{bytes}` | Latin-1 string |
| `0x15` / `0x16` / `0x17` | 2/3/4 × f32 | vector (floats are **not** tagged) |
| `0x19` | 9 × f32 | 3×3 matrix |
| `0x1A` | 12 × f32 | 4×3 transform (3 basis rows + translation) |
| `0x1C` | `{M_Int count}{elements}` | vector |
| `0x1F` | `{M_Int count}{elements}` | map |
| `0x25` | pair marker | used inside some maps |

Some LDB blocks mix `0x1C` and `0x1F` for lists of the same shape; the reader
accepts either as a count header.

## File layout

```
BSP vertices            0x1C, Vec3[]
BSP polygons            0x1C, {i32 start, count, id, group, Vec3 n, Vec3 pivot}[]
BSP nodes               0x1C, {Vec3 orient, Vec3 pos, 2× {i32, i32, i32}}[]
BSP indices             0x1C, i32[]
version                 M_Int          (= 32)
textures                count, {string name, u32 type, i32 size, raw bytes}[]
                        type: 0 TGA, 2 SCX, 3 PCX, 4 JPG, 5 DDS
materials (used)        0x1F map of {id, 0x25, category, name}
materials (editor)      0x1F map of {0x25, category, name, id}
material properties     count of categories, each {name, count, {mat, diffuse, alpha, bool, bool}}
lightmaps               count, {i32 id, u32 type, i32 size, raw bytes}[]
exits                   count, portal polygons + destination + convex regions
static texverts         0x1C, {i32 vtx, Vec2 uv, Vec2 lm, u32 flags, bool smooth}[]
static meshes           count, {id, Vec3[] verts, Vec3[] nrm, Mat4x3, polygons, radiosity map}
static lights           count, entity properties + colour / cone
waypoints               count, entity properties + type (1 waypoint, 2 jumppoint)
FSMs                    count, states + message lists
characters              count, skin name + four message lists
triggers                count, radius + type
dynamic texverts        0x1C, same as static
dynamic meshes          count, geometry + entity + animations + flags + 4 BSP ints
items                   count, class name
point lights            count, colour + falloff
rooms                   count, object-name lists + ai density + 4 BSP ints
```

## Transforms

`Mat4x3` is row-vector: `p' = p * R + T` with `T` in the fourth row.
Room static meshes on PC v32 have identity `R` and a translation that is the
room origin. Dynamic-mesh `object_to_room` is combined with that origin.

Y is up. The viewer mirrors X (Direct3D left-handed → OpenGL right-handed)
and reverses triangle winding.

## Building polygons

A polygon stores `texture_vertex_start` + `vertex_count`. Each texture vertex
indexes the mesh vertex/normal pools and carries UV + lightmap UV. There is
no index buffer — fan-triangulate in order.
