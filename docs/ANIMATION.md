# MAX-FX keyframe animation (KF2 `0x00010012`)

How the engine stores, wraps, blends and post-processes skeletal animation,
decoded from the Android `libMaxPayne.so` decompile and implemented in
`src/maxfx/kf2/Kf2.cpp` (`sampleChannel` + friends). PC Max Payne 1 uses the
same chunk layout; the Android port is only used for names and semantics.

A character clip (`Walk.kf2`, `Widepose.kf2`, …) is one `0x00010012`
`KeyframeAnimation` chunk **per bone**.

## Chunk layout (`operator>>(R_MemoryFile&, KeyframeAnimationChunk&)`)

```
nested Animation 0x00010013:
    string  target name
    int     frame rate          (FPS; seconds -> frames is fps * t)
    bool    looping             (AnimationChunk::isLooping)
string  parent name
bool    use loop interpolation  (LoopInterpolation)
int     total keyframe count    (see the +1 below)
int     key count
keys:   { int frame, Mat4x3 objectToParent }[]     // stride 52 on Android
version > 0:   int count, { int frame, float visibility }[]
version > 1:   int loop to frame
version > 2:   int frame-to-frame interpolation method (0 / 1 / 2)
version > 3:   bool maintain matrix scaling
version <= 4:  TotalKeyframeCount += 1             // unconditional
```

The `+1` matters: on disk the field is effectively *last frame index*, in
memory it is *frame count*. A 30-frame walk cycle at 30 FPS stores `29` and
runs one second.

## Container state (`KF2::KF_KeyframeAnimation::construct`)

Each channel owns a `LinearlyOptimizedContainer<M_Matrix4x3>` holding the
key frames (`+28`), matrices (`+60`) and:

| Field | Value |
| --- | --- |
| `total` (`+16`) | TotalKeyframeCount (after the `+1`) |
| `looping` (`+1`) | AnimationChunk::isLooping |
| `loopInterp` (`+2`) | UseLoopInterpolation |
| `loopFrom` (`+4`) | LoopToFrame when `looping` (or `total == 0`), else `total` |

The two cached indices (`+8`, `+12`) are the previous sample's key pair —
pure search optimisation, no effect on the result.

## Sampling (`LinearlyOptimizedContainer<M_Matrix4x3>::getItem`)

`KF_KeyframeAnimation::animate` converts time to frames with
`frame = frameRate * timeSeconds`, then:

```
wrapLimit = (looping && loopInterp) ? total : total - 1

if frame > wrapLimit:                       // loop wrap
    span  = total - loopFrom
    frame = span != 0 ? loopFrom + fmod(frame - loopFrom, span) : loopFrom
else if frame < 0:                          // negative time plays backwards
    frame = wrapLimit - fmod(-frame, wrapLimit)

floorFrame = floor(frame)
frac       = frame - floorFrame

if !looping && frame >= total - 1:          // clamp: hold the last key
    return keys[last]

if loopInterp && floorFrame >= total - 1:   // LOOP SEAM BLEND
    from = keys[last]
    to   = first key (index >= 1) with frame >= loopFrom, else keys[0]
           (keys[last] when no key reaches loopFrom)
    return lerp(from, to, frac)             // smooth wrap-around

// segment search: `to` = first key with frame > floorFrame
// (the engine walks from the cached index; same segment)
if frame is past the last key:  return keys[last]
if frame is before the first:   return keys[first]
t = (frame - keys[from].frame) / (keys[to].frame - keys[from].frame)
return lerp(keys[from], keys[to], t)
```

The lerp is a **plain component-wise blend of all 12 floats** — no
quaternions, no re-orthogonalisation inside the container. Two consequences
that are visible in the real game:

* Looping clips replay only `[loopToFrame, total)` after the first pass;
  the intro `[0, loopToFrame)` plays once.
* With `UseLoopInterpolation` the last frame of the cycle blends the last
  key towards the loop-start key instead of snapping.

Because keys are integers, `t` is always inside `[0, 1)` in the segment
path; `frame < keys[0].frame` holds the first key through a degenerate
zero-length span.

## Post-processing (`KF_KeyframeAnimation::animateFrameWithLastFrame`)

Runs on **every** sample, exact key hits included:

```
interpolationMethod 1:  scale each 3x3 row to unit length
interpolationMethod 2:  M_Matrix3::orthonormalize (below)
maintainMatrixScaling:  multiply each 3x3 row by a source row length —
                       the exact key's length, or between two keys the
                       lengths lerped in frame space:
                       (|from|*(f_to - frame) + |to|*(frame - f_from))
                            / (f_to - f_from)
```

`M_Matrix3Template<float>::orthonormalize` is row-wise Gram-Schmidt:

```
r0 = normalize(r0)
r1 -= r0 * dot(r0, r1);  r1 = normalize(r1)
r2 -= r0 * dot(r0, r2);  r2 -= r1 * dot(r1, r2);  r2 = normalize(r2)
```

Method 1 + maintain scaling is therefore "normalise the rotation, put the
interpolated length back": the scale never drifts during long blends.
Method 0 (what PC play clips store) skips all of it — mid-blend rows shrink
by up to `1 - cos(theta/2)` on large rotations, exactly as shipped.

`KF_ObjectAnimation::fixCrossAnimation` re-orthonormalises bones after
`crossAnimateObject` adds two clip samples (matrix addition) for
cross-fades; our runtime does not cross-fade yet.

## Movement (`X_CRSplineMovementUpdate`)

`[Movement] EndPosition` / `MiddlePosition1/2` in a skin's clip entry is
root motion: the engine moves the character along a Catmull-Rom spline from
the origin to `EndPosition` **over the clip duration**. Hence the locomotion
speed the AI uses is

```
speed = |EndPosition| / (TotalKeyframeCount / frameRate)
```

`KF_ObjectAnimation::getAnimationLength` is `maximumAnimationIndex /
animationFPS`, i.e. frame count over frame rate.

## What went wrong before this was decoded

The viewer's sampler had four divergences, each now covered by a synthetic
test in `src/apps/levels_test.cpp` (`testKf2EngineSampling`, no game data
needed):

| Symptom | Cause |
| --- | --- |
| every loop replayed the clip intro (visible hitch each cycle) | wrap went to `[0, total)` instead of `[loopToFrame, total)` |
| one-frame pop at each loop boundary | `UseLoopInterpolation` seam blend not implemented |
| blended poses subtly wrong, bone scale lost | `lerpMat` re-orthogonalised unconditionally; the engine only does that for methods 1/2, and `maintainMatrixScaling` was ignored |
| NPCs skated forwards (foot slide) | walk speed used `|EndPosition|` directly instead of dividing by the clip duration |

## PC vs Android caveats

The sampler is a port of the **Android** `libMaxPayne.so` decompile. The two
builds share the MAX-FX lineage, but the user-visible warning stands: the
Android reader may differ in small ways from PC `MP.exe`. Evidence status of
every behavioural constant:

| Behaviour | Source | Confidence |
| --- | --- | --- |
| component-wise 12-float lerp | `LinearlyOptimizedContainer<M_Matrix4x3>::getItem` | high (arithmetic, no I/O) |
| wrap into `[LoopToFrame, Total)` | same | high |
| seam blend during the final frame | same | high |
| methods 1/2 + maintain-scaling post | `animateFrameWithLastFrame` | high |
| `TotalKeyframeCount += 1` for chunk version ≤ 4 | Android `operator>>` only | **medium — not verified against a PC reader** |
| `interpolationMethod = 0` on PC play clips | observed on shipped PC KF2s | high |

If PC clips ever look one frame short / early-wrapped, the `+1` in
`parseKeyframeAnimation` (`src/maxfx/kf2/Kf2.cpp`) is the first thing to
A/B — it is the only loader-side quirk this port trusts from Android.
Frame-rate, loop flags and key layout are plain on-disk fields and cannot
differ between the ports (both read the same files).

## Skinning versus the Android 1.0 decompile (verified 2026-09-27)

The new `docs/Android1.0V/libMaxPayne.so.c` decompile was checked against our
skinning pipeline (`kf2BuildSkeletonWorlds` → `buildBonePalette` →
`poseVertexArrays` in `src/maxfx/kf2/Kf2.cpp`). There is **no dedicated
skinning class** in either decompile: skinning lives in
`KF2::KF_VertexAnimation` + the `P_SkinMesh` render path, driven through
`KF2::KF_BoneHandle` / `KF_BoneGroupHandle` scene-graph objects. What the
decompile confirms, piece by piece:

| Engine fact (decompile reference) | Our implementation | Verdict |
| --- | --- | --- |
| `SkinChunk` stores per-primitive vertex lists with per-vertex bone indices (`getVertexBone`) and float weights (`getVertexWeight`), indexed through per-primitive data offsets (`getVertexDataOffset`) — member layout at `SkinChunk::getVertexBoneCount` etc. | `Kf2Skin` / `Kf2SkinVertex` (bones + weights, per primitive), read in `parseSkinChunk` | matches |
| Bone palette is matched **by object name**: `KF2::KF_VertexAnimation::setupBoneMatrices` walks `SkinChunk::getSkinObjectNameCount` and pairs skin object names with skeleton object names | `buildBonePalette` looks every `skeletonObjectNames[i]` up by name in the bind/play worlds | matches |
| Hard limit **4 bones per vertex** — `KF2::KF_SkinMeshCallback::allocatePrimitive` throws `P_DriverException_TooManyBonesPerVertex` ("DirectX allows only 4 bones per vertex") when any vertex exceeds it | real SKDs never exceed it (synthetic test data included); `poseVertexArrays` blends whatever the file carries | matches (no clamp needed) |
| `setupBoneMatrices` precomputes the static inverse-bind matrices once (static `vecBoneMatrix`, matrix product chains at setup) and resizes the per-bone palette to the skeleton bone count | `invBindPal = inverseRigid(bindWorld)` per bone, rebuilt per frame — same values, lazier but correct | matches |
| Cross-fade: `KF2::KF_ObjectAnimation::crossAnimateObject` lerps the per-bone matrices element-wise, then `fixCrossAnimation` re-orthonormalizes | `blendSkeletons` lerps the bone worlds then `orthonormalizeMat3` | matches |
| Per-frame bone worlds come from the parent chain (`KF_BoneHandle::getWorldMatrix` via `P_BaseObject::invalidateMatrices`) | `kf2BuildSkeletonWorlds` parent-chain walk with the pose clip as fallback for missing bones | matches |

The linear-blend formula itself
(`posed = Σ w_b · playPal_b · invBindPal_b · meshBind · v`, weights divided by
their sum) is the standard MAX-FX/DirectX fixed-function skin: mesh vertices
live in mesh-object space, the inverse bind takes them to bone space, the
animated bone world brings them back out. The decompile's
`KF2::KF_SkinMeshCallback::allocatePrimitive` hands exactly the per-vertex
bones + weights and the per-bone matrices to `P_SkinMesh`, which folds the
bind composition into its matrix palette on the CPU/GPU.

Empirical checks on the real `docs/database` fixtures (2026-09-27):

* the shipped `.kfs` meshes and `.skd` skins are **not part of the uploaded
  database** (only `maxpayne-collision.kfs` and 762 animation KF2s are), so
  the end-to-end vertex blend runs on the synthetic beretta/alex fixtures
  (`levels-test`, 285 tests green);
* every real skeleton animation sampled through our sampler is clean:
  `Pose.kf2` 0.07 s / `Stand.kf2` 14.67 s / `Walk.kf2` 1.25 s / `Run.kf2`
  0.80 s — 28 bone channels each, all matrices orthonormal (|det|=1), no NaN,
  translations within human scale (max |t| ≈ 1.63 m);
* bind-pose identity holds (skinning with play = bind = pose clip at t=0
  reproduces the unskinned node-transformed vertices), which is the defining
  property of the inverse-bind chain.

Verdict: **the skinning pipeline agrees with the Android 1.0 decompile**; the
one visible gap is that `P_SkinMesh`'s GPU-side matrix palette folding is a
render-path detail we already reproduce mathematically on the CPU side.
