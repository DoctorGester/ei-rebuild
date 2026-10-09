# Vector maths (ticket V1: catalog and proposed API)

Research only (2026-10-09). Counts are regex counts over src/game and src/render (comments
stripped); treat them as +-10%. "Site" = one source line.

## 1. What decides the result

- IEEE `+` and `*` are commutative and exact to swap, so `x*x + y*y` == `y*y + x*x` bit for bit.
  Only the **grouping** of sums decides the rounding. A 2-term sum has one result whatever the
  order; a 3-term sum has exactly three: `(x+y)+z`, `(z+y)+x`, `(z+x)+y` (named here by the pair
  added first: XYZ, ZYX, ZXY; YXZ == XYZ, YZX == ZYX, XZY == ZXY).
- The game's main thread runs the x87 at 24-bit precision (rule 7). Every FADD/FMUL/FDIV/FSQRT
  then rounds to a float significand, so "kept in ST0" and "stored to a float" give the same bits
  except for the extended exponent range (overflow / denormals only). Consequences:
  - float arithmetic with the right grouping reproduces the original on the main thread, and also
    in the SSE2 standalone build (single precision rounds per operation the same way);
  - the existing `D(x)` / `double` style (82 files under `float_control(precise)`) only differs from
    float maths on 53-bit threads and in the future SSE2 build, where it would NOT round per op;
  - FSIN / FCOS / FPATAN / the CRT acos ignore the precision control (64-bit results): where the
    original stores their result to a float, the rounding store is required (X3).
- So the helpers only have to fix the grouping, and round after transcendentals.

## 2. Catalog of hand-written patterns

| pattern | sites | files | example sites |
|---|---|---|---|
| sum of squares (length^2 / distance^2) | ~211 | 75 | unit_ai_memory.cpp:101, combat_tick.cpp:767, camera_motion.cpp:117 |
| - 2-term (2D), any order | 111 | | ai_motion.cpp:133, world_client.cpp:52, combat_sight.cpp:220 |
| - 3-term ZYX `(z+y)+x` | 55 | | core_transform_math.cpp:106, lightning_draw.cpp:170, unit_hits.cpp:266 |
| - 3-term XYZ `(x+y)+z` | 37 | | maths.cpp:20 (00401E20), map_object_draw.cpp:133, face_normal.cpp:67 |
| - 3-term ZXY `(z+x)+y` | 8 | | unit_ai_memory.cpp:39/101, lightning.cpp:330, lightning_draw.cpp:69 |
| dot product (3D) | ~66 | 24 | ZYX 51 (maths.cpp:14 = 00401E00, ground_decal.cpp:168); ZXY 8 (map_spec.cpp:593); XYZ 7 (quaternion.cpp:111) |
| dot product (2D) | ~37 | | unit_ai.cpp:387, particle_callbacks.cpp:1021 |
| sqrt calls | 143 | 65 | 88 `x87::fsqrt`, 55 `std::sqrt` |
| normalise | ~76 | | divide by length 57 (ground_decal.cpp:162, 005C7B00); times 1/length 19 (core_transform_math.cpp:106, camera.cpp:221) |
| cross product | ~63 lines | 12 | object_maths.cpp:13 (004489D0), core_transform_math.cpp:100, particle_tick.cpp:195, view_matrices.cpp:69 |
| quaternion product | ~16 | 10 | core_maths.cpp:71 (004018A0), core_transform_math.cpp:248 (00401E50), camera.cpp:300/434, ui_hud_portraits.cpp:124-146 |
| quaternion rotated axis (`w*w - (..) + 2zz`) | 33 lines | 16 | core_maths.cpp:83-103 (AxisX/Y/Z), core_transform_math.cpp:62-88, combat_update.cpp:522 |
| rotate vector by quaternion | 6 | 6 | map_spec.cpp:591 (0046A4B0), unit_figure_queries.cpp:53, core_transform.cpp:62, view_matrices.cpp:72 |
| matrix x point / normal | 21 | 6 | figure_mesh.cpp:600, model.cpp:32-35, world_client.cpp:87 (00560E10), world_client_maps.cpp:786-798, world_client_view.cpp:194 |
| matrix x matrix | 2 impl + callers | | matrix_math.cpp (004D9880, 004D9B80); 3 local `MultiplyMatrix` wrappers of the original |
| lerp `(b-a)*t + a` | ~20 | 7 | figure.cpp:13, figure_mesh.cpp:100-289, link_figure.cpp:36, clip.cpp (0042E4D0) |
| lerp `(1-t)*a + t*b` | 2 | | unit_client_path.cpp:102, quaternion.cpp:149 (Slerp linear branch) |
| atan2 (FPATAN) | 40 | 13 | ai_motion.cpp:134, combat_sight.cpp:403, core_transform_math.cpp:289 |
| acos / asin (`_CIacos`) | 18 | 10 | quaternion.cpp:143, camera_motion.cpp:43, world_units.cpp:176 |
| sin / cos | 134 | 24 | particle_callbacks.cpp 63, ai_motion.cpp 17 |
| `float a[3]` | 193 | 76 | 71 class members with `// +0x` (map_object_figures.h 17, sound.h 7, world.h 5); ~106 locals |
| `float a[4]` (quaternion / homogeneous) | 31 | 20 | map_object.h:188 orientation, map_object_animation.h:29, ui_camp.h:460 turn_ |
| `float a[16]` / `[4][4]` | 29 | 17 | ui.h:1224 placement_, figure_mesh.h:19, renderer_init.cpp:157; render_mesh.cpp 6 |
| `float a[12]` (3 rows of 4) | 3 | 3 | world.h:1237 MatrixRows, camera.h:99 CubicCurve3, figure_part_draw.cpp:530 |
| `float a[2]` | 12 | 10 | renderer_strip.cpp, unit_ai_sight.cpp |
| rounding-store helpers (`Store`, `StoreFloat`, `RoundToFloat`, `Round`, `F`) | 53 definitions, 2481 calls | 102 | particle_callbacks.cpp 935 calls |
| local `D(float)` helpers | 23 | | |
| naked `Fpatan` / `CIacos` / `CIasin` copies | 8 / 5 | | ai_motion, ai_path, camera, combat_sight, core_transform_math, map_object_shadow, ui_hud_map, world_party |

Orders in source were written from the disassembly by the porting workers; VC6's inliner and
scheduler produced them, so they do not follow from the original source (two functions next to
each other differ: unit_ai_memory.cpp NearestEnemy is ZXY, ForceSee is ZYX).

## 3. Orders checked against the disassembly

| site | what the original does |
|---|---|
| 00401E00 Dot | `(z*z' + y*y') + x*x'`, result unrounded in ST0 (ZYX) |
| 00401E20 Length | loads z, y, x; `(x*x + y*y) + z*z`, FSQRT, unrounded in ST0 (XYZ) |
| 004489D0 Cross | z, y, x components computed on the stack, then 3 FSTPs x, y, z (aliasing safe); each `a*b - c*d` |
| 005CB670 NearestEnemy | dx, dy, dz kept in ST (not stored); `(dz^2 + dx^2) + dy^2` (ZXY); stored only when kept as best |
| 005C7B00 AIPath::Start | `y^2 + x^2`, FSQRT, x / len and y / len (FDIV each), FPATAN; angle unrounded until FSTP |
| 0059FDF0 ExtendWorldBox | inline length `(x^2+y^2)+z^2` (Length inlined); point x row-vector matrix: `((z*m8 + y*m4) + x*m0) + m12`, translation last, one FSTP per coordinate; the 8 corners unrolled |
| 005064C0 SetMorph | Vector3 temporaries: every operator result (`b - a`, `* s`, `+ a`) FSTP'd per component; calls 00401DE0 / 00448A10 / 0040CDC0 out of line |

The ported out-of-line helpers (section 4) were each verified against their original when
written; their orders are recorded there.

## 4. Existing types and helpers

| type | where | layout | notes |
|---|---|---|---|
| `Vector3` | maths.h | x, y, z (12) | aggregate; out-of-line members below |
| `Quaternion` | maths.h | w, x, y, z (16) | w first |
| `Vector2` | object_maths.h | x, y (8) | used only by SubtractVectors2 |
| `Vector4` | object_maths.h | x, y, z, w (16) | `FromPoint` 004679D0 |
| `Matrix4` | model.h | float m[16] (64) | row-major; also a different local `Matrix4` in world_client_view.cpp (anonymous namespace, method only) |
| `MatrixRows` | world.h | float m[12] | 3 rows of a 4x4 (stride 16), `Transform` 00560E10 |
| `CubicSegment` / `CubicCurve3` | core_maths.h / camera.h | float c[4] / c[12] | |
| `render::detail::Matrix` | render/render_internal.h | float m[16] | same layout as Matrix4 |
| `Float4` | chunk_stream.h | uint32 bits[4] | 16 bytes by value, kept as bits (quaternion/colour passed through) |
| vertex structs with a position | ground_decal.h DecalVertex (x,y,z,u,v), render_internal.h Vertex, clip.h TLVertex, renderer_lines.h, effects.h:353, lightning.h LightningPoint (x,y,z,w) | position first | keep; position could become a `Vector3` member |
| place with a position | ui_camp.h GlobalMapPlace (+0x04 x,y,z) | | position -> `Vector3` |
| local duplicates | Vec3 (script_exec.cpp, ui_hero_preview.cpp), UiVec3 (ui_hud.cpp), V3 (figure_mesh.cpp, map_object_cage.cpp), Point (face_normal.cpp, scene_normals.cpp, scene_weld.cpp), Quat (ui_hero_preview.cpp), Quaternion4 (ui_item_view.cpp), 11 anonymous `{x,y,z}` in particle_callbacks.cpp | | all fold into Vector3 / Quaternion |

Hooked out-of-line originals (none is on the Rebirth-patched list); all stay hooked:

| address | function | order / notes |
|---|---|---|
| 00401DE0 | Vector3::Construct | returns this |
| 00401E00 | Dot (fastcall) | ZYX, ST0 unrounded; 45 callers in the exe |
| 00401E20 | Vector3::Length | XYZ, FSQRT; 7 callers |
| 004D8470 | Vector3::LengthSquared | XYZ, returned as double |
| 0040CDC0 / 0044A230 / 00402200 | AddVectors / SubtractVectors / NegateVector | all components before stores |
| 00410770 = 00448A10 | ScaleVector | `s * v` |
| 00410750 | Vector3::AddInPlace | returns &v |
| 004679B0 | Vector3::DivideInPlace | divides each component |
| 00488940 | DivideVector (effects.cpp) | |
| 004489D0 | CrossProduct | 18 callers |
| 0044CC10 | SubtractVectors2 | |
| 004679D0 | Vector4::FromPoint | dword copies |
| 004022D0 | ClampUnit | NaN -> -1 |
| 0040CB00, 004018A0, 0040D940 / 0040F560 / 0040D6B0 | Quaternion SetIdentity, PreMultiply, AxisX / Y / Z | AxisX keeps x*x in a float temporary |
| 0049E790 / 0049E800 / 0049ECC0 | SetAngleAxis / SetAxisAngle / FromAxisAngle | factor rounded to float |
| 0049E870 / 0049E950 / 0049ED20 | PreMultiplyConjugate / MultiplyByConjugate / Slerp | |
| 0046A4B0 | QuaternionRotate (map_spec.cpp) | dot ZXY-class `(x+z)+y`; `(m*v + b) + a` |
| 0040CB80 | CombineRotation (core_transform.cpp) | |
| 004D9880 / 004D9B80 | MatrixMultiply / PushMatrix | per-element sum orders differ (tables in matrix_math.cpp) |
| 004D9AA0, 00401570 | Quaternion::ToMatrix, CTransformObject::WorldMatrix | |
| 00560E10 | MatrixRows::Transform | `(m2*z + m1*y) + x*m0` per row (M * v) |
| 004F1370 / 004F13F0 / 004F0E80 | Viewport / Shear / Projection matrix | |
| 0040E3D0 / 0040E440 | CubicSegment SetBSpline / Evaluate | |
| 0049AEC0, 0044DCF0 | ComputeFaceNormal, ComputeMeshNormals | |

Two matrix conventions are in use: CTransformObject::WorldMatrix and the figure code use row
vectors (`v * M`, translation m[12..14], D3D); the transform pipeline, clip codes, decals and view
code use column vectors (`M * v`, translation m[3], m[7], m[11]; render_internal.h says so).

## 5. Proposed API: src/lib/vecmath.h

Types (layouts unchanged, `static_assert` sizes; aggregates, no constructors, so naked forwarders
and `{x, y, z}` initialisation keep working):

```cpp
struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; /* + the hooked members of section 4, declared here */ };
struct Vector4 { float x, y, z, w; };
struct Quaternion { float w, x, y, z; /* + hooked members */ };
struct Matrix4 { float m[16]; };          // row-major
struct MatrixRows { float m[12]; };       // world.h's 3-row form, moved
```

Types stay in the global namespace (1613 uses of Vector3); maths.h, object_maths.h and model.h
include vecmath.h and keep only their hooked declarations. Every helper is `__forceinline`, takes
and returns by value, and computes in `float`.

| helper | evaluation (fixed) | variants |
|---|---|---|
| `a + b`, `a - b`, `-a`, `a * s`, `s * a`, `a / s`, `+= -= *= /=` | per component, x, y, z; each component rounded once | none needed (single ops); `/ s` divides each (no reciprocal) |
| `Dot(a, b)` | `(x*x' + y*y') + z*z'` | `DotZYX` (00401E00, majority), `DotZXY` |
| `LengthSquared(v)`, `Length(v)` | `(x^2 + y^2) + z^2`, `std::sqrt` | `...ZYX`, `...ZXY` |
| `DistanceSquared(a, b)`, `Distance(a, b)` | `LengthSquared(a - b)` | `...ZYX`, `...ZXY` |
| `Dot(Vector2)`, `LengthSquared(Vector2)`, `Distance2D(a, b)`, `DistanceSquared2D` | `x*x' + y*y'` | none (2 terms) |
| `DistanceSquaredXY(Vector3, Vector3)` | 2D part of 3D positions | none |
| `Cross(a, b)` | `(y*z' - z*y', z*x' - x*z', x*y' - y*x')` | none (products commute) |
| `Normalised(v)` | `v / Length(v)` | `NormalisedReciprocal(v)` = `v * (1 / Length(v))`; both take the length variant as a template argument |
| `Lerp(a, b, t)` (float, Vector3) | `(b - a) * t + a` | `LerpWeighted(a, b, t)` = `(1 - t) * a + t * b` |
| `TransformRow(m, p)` / `TransformRowNormal` | `v * M`: `((x*m0 + y*m4) + z*m8) + m12` | sum order as template argument |
| `TransformColumn(m, p)` / `...Normal` | `M * v`: `((m0*x + m1*y) + m2*z) + m3` | sum order as template argument |
| `Mul(Quaternion a, Quaternion b)` | Hamilton a*b in 004018A0's order | `Conjugate(q)`; other product orders: open question 3 |
| `AxisX(q)`, `AxisY(q)`, `AxisZ(q)` | the orders of 0040D940 / 0040F560 / 0040D6B0 | value-returning; inline expansions that differ stay explicit or get a named variant |
| `Rotate(q, v)` | `(k*v + 2w(u x v)) + 2(u.v)u`, `k = w^2 - u.u` | dot order as template argument (0046A4B0 uses ZXY) |
| `RoundToFloat(double)` | volatile float store | replaces the 53 local Store / StoreFloat / Round copies |
| `Atan2(y, x)`, `Acos(c)`, `Asin(s)` | FPATAN / _CIacos results, unrounded (double) | forward to x87.h until the x87-helper ticket replaces them; callers round with `RoundToFloat` where the original stores |
| `ClampUnit` | 004022D0 | |

The sum order is one small enum shared by the 3-term helpers, so a variant is a name, never an
argument order: `enum class Sum3 { XYZ, ZYX, ZXY };` with `Dot<Sum3::ZYX>(a, b)` and the aliases
`DotZYX`, `LengthSquaredZYX`, ... Commutative re-spellings (YXZ, YZX, XZY) are documented as equal
and get no names. No `Matrix4 operator*`: game code calls `MatrixMultiply` (004D9880, per-element
orders); src/render (not compared bit for bit) may add a plain product there.

Out-of-line originals: all of section 4 stay hooked with their bodies; once vecmath exists their
bodies may be written with the helpers when the order matches (Dot = `DotZYX`, Length = `Length`,
CrossProduct = `Cross`, ScaleVector = `v * s`), keeping the all-loads-before-stores aliasing.
Rewritten callers use the inline helper instead of calling Dot / Length / CrossProduct (same bits).

## 6. Migration order (V3, each batch through the full suite)

1. Core: maths.cpp, core_maths.cpp, object_maths.cpp, quaternion.cpp, matrix_math.cpp,
   core_transform*.cpp on the helpers; fold the local duplicates (Vec3, UiVec3, V3, Point, Quat,
   Quaternion4, world_client_view's Matrix4, render::detail::Matrix).
2. Fields: the 71 `float [3]` class members, the 3 quaternion `float [4]` members and the
   `float [16]` members become Vector3 / Quaternion / Matrix4 (layout-identical, asserts kept).
3. 2D distances and dots (no order risk): unit_ai_*, combat_*, ai_*, world_client, sector_map,
   magic_trap, world_sync, script_addon / script_eval `Distance`.
4. 3D length / distance / normalise: camera*, lightning*, effects*, map_object_*, unit_hits,
   world_effect_*, ui_briefing_*.
5. Quaternions: camera*, core_transform*, view_matrices, ui_hero_preview, ui_hud_portraits,
   ui_item_view, ui_global_map, unit_figure_queries, map_spec, scene_bone_frames.
6. Matrix x point: model.cpp, figure_mesh.cpp, world_client*.cpp, ground_decal.cpp.
7. Particles last: particle_callbacks.cpp (27 sums, 935 rounding stores, 63 sin/cos),
   particle_emitters / create / tick.
src/render can switch at any time (frame-capture checks, no bitwise compare).

## 7. Risks

- **/fp:fast reassociation.** The grouping is only kept if MSVC does not reassociate; /fp:fast
  allows it (it already distributed a `* 1/6` in CubicSegment, now under `precise`). V2 should add
  a verify self-test with inputs where the three groupings give different bits, checking each
  helper, and inspect the release disassembly of one migrated site per batch.
- **Precise files.** 82 files use `float_control(precise)`; an inline helper takes the float
  semantics of the point where vecmath.h is included (normally fast). Equal on the 24-bit main
  thread; different on 53-bit threads. Migrating a precise file moves its maths from `double` to
  `float`: same bits at 24 bits, which is what the bitwise wrappers see.
- **53-bit threads.** Code running off the main thread (verify log "FPU precision ...") is where
  float helpers and the original's register values can differ; leave such functions explicit
  until checked.
- **Transcendentals.** FPATAN / FSIN / FCOS / acos results are 64-bit regardless of precision;
  dropping a `RoundToFloat` the original performs flips results (X3). Keep every such store.
- **Exponent range.** x87 intermediates do not overflow / underflow at float limits; float helpers
  do. Only matters for huge or denormal inputs (none expected).
- **Exact checks.** Wrappers using bitwise `Check` must keep passing; no CheckNear widening. A
  migration that changes a grouping is a bug, so each batch changes no grouping, only spelling.
- **Aliasing.** Out-of-line originals load all inputs before storing (out may alias an input);
  value-returning helpers keep that, in-place loops (`v = v * s` per component) do too.

## 8. Open questions

1. Default order: the ticket fixes the default as XYZ, but ZYX is the majority (55 sums, 51 dots,
   and 00401E00). Keep XYZ as the default and use the `...ZYX` names at most sites?
2. Do any migrated functions run on 53-bit threads (sound streaming, network)? If so, should
   vecmath offer `double` accumulation for them, or should they stay explicit?
3. Quaternion products: each component's two scalar-part terms come in either order and the w
   component in one of three groupings (up to 24 spellings; about 16 sites, few alike). Add
   `Mul<order mask>` or leave the products explicit?
4. Matrix conventions: one `Matrix4` with `TransformRow` / `TransformColumn`, or two types
   (row-vector world matrices vs column-vector pipeline matrices)?
5. Vertex structs (DecalVertex, Vertex, TLVertex, LightningPoint): give them a `Vector3 position`
   member or leave them as plain fields?
