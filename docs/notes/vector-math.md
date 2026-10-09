# Vector maths (catalog of the original's orders; src/lib/vecmath.h; V3 migration plan)

V1 (research) and V2 (src/lib/vecmath.h, 2026-10-09). Counts are regex counts over src/game and src/render (comments
stripped) at V1; treat them as +-10%. "Site" = one source line.

## 1. What decides the result

- IEEE `+` and `*` are commutative and exact to swap, so `x*x + y*y` == `y*y + x*x` bit for bit. Only the **grouping** of
  sums decides the rounding. A 2-term sum has one result whatever the order; a 3-term sum has exactly three: `(x+y)+z`,
  `(z+y)+x`, `(z+x)+y` (named by the pair added first: XYZ, ZYX, ZXY; YXZ == XYZ, YZX == ZYX, XZY == ZXY).
- The game's main thread runs the x87 at 24-bit precision (rule 7). Every FADD/FMUL/FDIV/FSQRT then rounds to a float
  significand, so "kept in ST0" and "stored to a float" give the same bits except for the extended exponent range
  (overflow / denormals only). So float arithmetic with the right grouping reproduces the original on the main thread, and
  also in the SSE2 standalone build; the existing `D(x)` / `double` style (82 files under `float_control(precise)`) only
  differs from float maths on 53-bit threads and in the SSE2 build, where it would NOT round per op. FSIN / FCOS / FPATAN /
  the CRT acos ignore the precision control (64-bit results): where the original stores their result to a float, the
  rounding store is required (X3).
- So the helpers only have to fix the grouping, and round after transcendentals.

## 2. Catalog of hand-written patterns (at V1)

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
| matrix x matrix | 2 impl + callers | | matrix_math.cpp (004D9880, 004D9B80) |
| lerp `(b-a)*t + a` | ~20 | 7 | figure.cpp:13, figure_mesh.cpp:100-289, link_figure.cpp:36, clip.cpp (0042E4D0) |
| lerp `(1-t)*a + t*b` | 2 | | unit_client_path.cpp:102, quaternion.cpp:149 (Slerp linear branch) |
| atan2 (FPATAN) | 40 | 13 | ai_motion.cpp:134, combat_sight.cpp:403, core_transform_math.cpp:289 |
| acos / asin (`_CIacos`) | 18 | 10 | quaternion.cpp:143, camera_motion.cpp:43, world_units.cpp:176 |
| sin / cos | 134 | 24 | particle_callbacks.cpp 63, ai_motion.cpp 17 |
| `float a[3]` | 193 | 76 | 71 class members with `// +0x` (map_object_figures.h 17, sound.h 7, world.h 5); ~106 locals |
| `float a[4]` (quaternion / homogeneous) | 31 | 20 | map_object.h:188 orientation, map_object_animation.h:29, ui_camp.h:460 turn_ |
| `float a[16]` / `[4][4]` | 29 | 17 | ui.h:1224 placement_, figure_mesh.h:19, renderer_init.cpp:157; render_mesh.cpp 6 |
| `float a[12]` (3 rows of 4) | 3 | 3 | world.h MatrixRows, camera.h:99 CubicCurve3, figure_part_draw.cpp:530 |
| `float a[2]` | 12 | 10 | renderer_strip.cpp, unit_ai_sight.cpp |
| rounding-store helpers (`Store`, `StoreFloat`, `RoundToFloat`, `Round`, `F`) | 53 definitions, 2481 calls | 102 | particle_callbacks.cpp 935 calls |
| local `D(float)` helpers | 23 | | |
| naked `Fpatan` / `CIacos` / `CIasin` copies | 8 / 5 | | ai_motion, ai_path, camera, combat_sight, core_transform_math, map_object_shadow, ui_hud_map, world_party |

Orders in source were written from the disassembly by the porting workers; VC6's inliner and scheduler produced them, so
they do not follow from the original source (two neighbouring functions differ: unit_ai_memory.cpp NearestEnemy is ZXY,
ForceSee is ZYX).

## 3. Orders checked against the disassembly

| site | what the original does |
|---|---|
| 00401E00 Dot | `(z*z' + y*y') + x*x'`, result unrounded in ST0 (ZYX) |
| 00401E20 Length | loads z, y, x; `(x*x + y*y) + z*z`, FSQRT, unrounded in ST0 (XYZ) |
| 004489D0 Cross | z, y, x components computed on the stack, then 3 FSTPs x, y, z (aliasing safe); each `a*b - c*d` |
| 005CB670 NearestEnemy | dx, dy, dz kept in ST (not stored); `(dz^2 + dx^2) + dy^2` (ZXY); stored only when kept as best |
| 005C7B00 AIPath::Start | `y^2 + x^2`, FSQRT, x / len and y / len (FDIV each), FPATAN; angle unrounded until FSTP |
| 0059FDF0 ExtendWorldBox | inline length `(x^2+y^2)+z^2`; point x row-vector matrix: `((z*m8 + y*m4) + x*m0) + m12`, translation last, one FSTP per coordinate; the 8 corners unrolled |
| 005064C0 SetMorph | Vector3 temporaries: every operator result (`b - a`, `* s`, `+ a`) FSTP'd per component; calls 00401DE0 / 00448A10 / 0040CDC0 out of line |

## 4. Types and hooked helpers

Types (src/lib/vecmath.h; same layouts as before, sizes asserted; aggregates, no constructors, so naked forwarders and
`{x, y, z}` initialisation keep working; global namespace): `Vector2` (x, y), `Vector3` (x, y, z), `Vector4` (x, y, z, w),
`Quaternion` (w, x, y, z: w first), `Matrix4` (float m[16], row-major), `MatrixRows` (float m[12]: 3 rows of a 4x4,
stride 16, `Transform` 00560E10). The hooked members are declared in the structs, their bodies stay in src/game; maths.h
includes vecmath.h; the free out-of-line originals keep their declarations in maths.h / object_maths.h / core_maths.h.
Also: `CubicSegment` / `CubicCurve3` (core_maths.h / camera.h, float c[4] / c[12]). Vertex structs with a position:
`DecalVertex`, render `Vertex`, `GlobalMapPlace` have a `Vector3` position; clip.h TLVertex, renderer_lines.h,
effects.h, lightning.h LightningPoint do not yet.

Two matrix conventions are in use: CTransformObject::WorldMatrix and the figure code use row vectors (`v * M`,
translation m[12..14], D3D); the transform pipeline, clip codes, decals and view code use column vectors (`M * v`,
translation m[3], m[7], m[11]; render_internal.h says so).

Hooked out-of-line originals (none is Rebirth-patched; all stay hooked):

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

## 5. vecmath.h helpers (V2)

All `__forceinline`, by value:

| helper | evaluation |
|---|---|
| `+ - * /`, unary `-`, `+= -= *= /=` (Vector2, Vector3) | one operation per component (`/ s` divides, no reciprocal) |
| `Dot2D`, `LengthSquared2D`, `Length2D`, `DistanceSquared2D`, `Distance2D` | `x*x' + y*y'` |
| `DistanceSquaredXY(Vector3, Vector3)` | `dx*dx + dy*dy` |
| `Cross(a, b)` | `(y*z' - z*y', z*x' - x*z', x*y' - y*x')` |
| `Lerp(a, b, t)` (float, Vector3), `LerpWeighted(a, b, t)` (float) | `(b - a) * t + a`, `(1 - t) * a + t * b` |
| `DotXYZ / DotZYX / DotZXY`, `vecmath::Dot<Sum3>` | the named grouping of the three products |
| `LengthSquared<Sum3>`, `Length<Sum3>` (+ `...XYZ / ZYX / ZXY`) | grouped sum of squares; FSQRT (`x87::fsqrt`) |
| `DistanceSquared<Sum3>`, `Distance<Sum3>` (+ named) | of `a - b` |
| `Normalised<Sum3>`, `NormalisedReciprocal<Sum3>` | `v / len`, `v * (1 / len)` |
| `TransformRow<OX, OY, OZ>`, `TransformRowNormal<...>` | `v * M`: grouped `x*m[i], y*m[4+i], z*m[8+i]`, then `+ m[12+i]`; one grouping per component (OY defaults to OX, OZ to OY) |
| `TransformColumn<OX, OY, OZ>`, `TransformColumnNormal<...>` (Matrix4 or MatrixRows) | `M * v`: grouped `m[4i]*x, m[4i+1]*y, m[4i+2]*z`, then `+ m[4i+3]` |
| `TransformColumnW<Sum3>` | the homogeneous w of `M * (v, 1)`: grouped `m[12]*x, m[13]*y, m[14]*z`, then `+ m[15]` |
| `Conjugate(q)` | `(w, -x, -y, -z)` |
| `RoundToFloat(double)` | volatile float store (replaces the local Store / StoreFloat / Round copies) |
| `Atan2(y, x)` | `x87::fpatan` (include/ei/x87.h), unrounded double |
| `Acos(c)`, `Asin(s)` | the game's CRT `acos` 006E7334 / `asin` 006E83D4 (`gamecrt::`, src/lib/crt.h): the C entries of the bodies behind _CIacos 006E7320 / _CIasin 006E83C0; unrounded double |

Decisions (V1 open questions, settled for V2): no default order: every 3-term helper names its grouping (`enum class Sum3
{ XYZ, ZYX, ZXY }`; commutative re-spellings get no names); code on 53-bit threads stays as written (no `double` helpers);
quaternion products and AxisX/Y/Z stay written out at each site (only `Conjugate` is a helper); one `Matrix4` with
`TransformRow` / `TransformColumn`, no `Matrix4 operator*` for game code (MatrixMultiply 004D9880 has per-element orders;
src/render, not compared bit for bit, may add a plain product). The generic dot is `vecmath::Dot<Sum3>`: a global template
named `Dot` would make `&Dot` (the hooked 00401E00 in `EI_REPLACE`) ambiguous. Atan2 is not `std::atan2`: that becomes
FPATAN only under /fp:fast; inlined into a `float_control(precise)` function it calls the modern CRT's `_CIatan2` (the same
holds for `std::sqrt`, hence `x87::fsqrt`).

**/fp:fast regroups; the helpers are compiled precise.** Measured with cl 14.43 /O2 /arch:IA32: a fast-compiled inline
`(z*z' + y*y') + x*x'` followed by the caller's `* 2.0f + 1.0f` became `(2x*x' + 1) + 2(z*z' + y*y')`-style code (the `* 2`
turned into an add and was distributed); `v * (1 / len)` was rewritten too. Plain uses (`Dot` alone, `* 3`, `* s`, `-
x*x'`) kept their grouping, so the hazard depends on the caller's expression. vecmath.h therefore defines its arithmetic
helpers between `#pragma float_control(precise, on, push)` and `pop` (the one file check_cleanup exempts from that pattern).
The precise body survives inlining into /fp:fast callers (grouping kept), at the cost of rounding each helper's float
result to memory (an FSTP / FLD pair). On the 24-bit main thread that store changes nothing; on a 53-bit thread a helper is
plain float maths, not the original's unrounded register value. **Migration rule:** a helper may replace written-out maths
when the original stores the result to a float (or the result is a single operation per component); a sum the original
keeps unrounded in ST0 and uses further stays written out unless the function is known to run on the main thread only.

**Self-test** (tests/verify/verify_vecmath.cpp, verify build, at DLL load at 24-bit precision, "vecmath self-test" in the
log): every grouped helper in /fp:fast contexts (alone, `* 2 + 1`, two groupings in one expression, transforms with
translation, normalising, distances, lerps) against a reference that rounds each operation separately, on 24 inputs per
helper chosen so that XYZ, ZYX and ZXY give three different results. Standalone: 2088 checks (2448 since V3 part 2: mixed groupings, TransformColumnW), 0 mismatches; with the precise
pragmas removed it reports 116 mismatches (Dot / LengthSquared / DistanceSquared `* 2`, NormalisedReciprocal, LerpWeighted
`* 2`).

**Done in V2**: folded into the vecmath types (layouts unchanged, offsets asserted): world_client_view.cpp's method-only
Matrix4 (now `MatrixMultiplyOriginal` calls), render::detail::Matrix, the render `Vertex` / `DecalVertex` / `GlobalMapPlace`
positions, chunk_stream.h's `Float4` (Quaternion; still copied with memcpy), Point (face_normal, scene_normals, scene_weld),
Vec3 (script_exec, ui_hero_preview), UiVec3 (ui_hud), V3 (figure_mesh, map_object_cage), Quat (ui_hero_preview),
Quaternion4 (ui_item_view). The two method-view forwarders of 0049E800 became one `QuaternionSetAxisAngleOriginal`
(originals_render.h). Local helpers whose names vecmath uses were renamed, maths unchanged: particle_tick `LengthXYZ` ->
`LengthDouble`, ui_hero_preview `Dot` / `Cross` -> `DotUnrounded` / `CrossStored`, figure_mesh `Lerp` -> `LerpStored`.
First migration batch (core_maths.cpp, object_maths.cpp, quaternion.cpp, matrix_math.cpp, maths.cpp): AddVectors,
NegateVector, ScaleVector, AddInPlace, CrossProduct, SubtractVectors, SubtractVectors2, DivideInPlace on the operators /
Cross; PreMultiplyConjugate and MultiplyByConjugate take `Conjugate`; SetAngleAxis / SetAxisAngle scale the axis with
`factor * axis`; matrix_math.cpp and AxisX round through RoundToFloat. Kept written out: Dot 00401E00, Length 00401E20 and
the squared axis lengths of FromAxisAngle / SetAngleAxis / SetAxisAngle (unrounded sums: a version using LengthSquaredXYZ
there differed at 53 and 64 bits, which is where the migration rule comes from), the quaternion products and AxisX/Y/Z,
MatrixMultiply's per-element orders, Slerp. Checked with a scratch harness (old and new bodies, 20k random inputs with
aliased outputs, at 24, 53 and 64 bits): identical bits; the verify wrappers (verify_core_maths, verify_object,
verify_quaternion, verify_maths, the matrix wrappers) cover them in game.

## 6. Migration order (V3, each batch through the full suite)

V3 first adds a check_cleanup column for unrolled vector maths (the x*x + y*y forms and float[3] / [16] members). Not folded
yet: particle_callbacks.cpp's `float x, y, z` locals (particles batch), TLVertex, LightningPoint, the `float [3]` / `[16]`
class members (batch 2).
1. Core: done in V2 (above), except core_transform*.cpp.
2. Fields: the 71 `float [3]` class members, the 3 quaternion `float [4]` members and the `float [16]` members become
   Vector3 / Quaternion / Matrix4 (layout-identical, asserts kept).
3. 2D distances and dots (no order risk): unit_ai_*, combat_*, ai_*, world_client, sector_map, magic_trap, world_sync,
   script_addon / script_eval `Distance`.
4. 3D length / distance / normalise: camera*, lightning*, effects*, map_object_*, unit_hits, world_effect_*, ui_briefing_*.
5. Quaternions: camera*, core_transform*, view_matrices, ui_hero_preview, ui_hud_portraits, ui_item_view, ui_global_map,
   unit_figure_queries, map_spec, scene_bone_frames.
6. Matrix x point: model.cpp, figure_mesh.cpp, world_client*.cpp, ground_decal.cpp.
7. Particles last: particle_callbacks.cpp (27 sums, 935 rounding stores, 63 sin/cos), particle_emitters / create / tick.
src/render can switch at any time (frame-capture checks, no bitwise compare). Out-of-line originals keep their hooks; their
bodies may use the helpers when the order matches (keeping the all-loads-before-stores aliasing); rewritten callers use the
inline helper instead of calling Dot / Length / CrossProduct (same bits).

**V3 progress (2026-10-09).** check_cleanup column "hand-written vector maths" (tools/check_cleanup.py,
`count_vector_maths`): two-product sums whose operand pairs differ only in axis letters / digits (looking through one
`D(...)` / `static_cast<T>(...)`), plus non-const `float n[3]`, `[16]`, `[4][4]`, and `float n[4]` named like a quaternion;
colour / stat names and const tables skipped; vecmath.h exempt. Seeded at 507 (309 sums, 198 arrays); 409 after part 1, 377 after part 2.
- Fields done: map_object_figures.h (FigurePart, FigurePieceRecord, CObject3D: 16), map_object.h (orientation ->
  Quaternion, scale, pointOffset, extent, baseScale), map_object_animation.h (position, rotation -> Quaternion), unit.h
  (modelHeight, center, below, above), combat.h / combat_plan.h / unit_ai.h (point, direction), magic_trap.h, model.h
  ClipBounds, map_object_shadow.h, figure_mesh.h position_, map_spec.h, scene_loader.h MeshFace::normal, unit_db.h,
  map_object_cage.h. Float[3] locals in combat_planning / combat_update / combat_motion / combat_tick / unit_points /
  unit_motion_tick / unit_client_path. Callees that still take `float*` (forwarders such as QuaternionAxisY, Negate,
  MotionSample, PathStart) get `&v.x` until their declarations take Vector3*. Checked by disassembling the release objects
  before / after: identical, or only stack slots / scheduling / commuted FMUL operands (figure_mesh's 12-byte copies became
  FLD / FSTP pairs).
- Helpers done (lengths, distances, dots; grouping per site): unit_ai_fear (ZXY / ZYX / XYZ), unit_ai_sight (Length2D,
  DotZYX, LengthZYX), combat_tick, effects (orbit dot, tendril length), ground_decal (Normalised<XYZ>, DotZYX + offset),
  lightning_draw (its LengthXZY is LengthZXY), object3d_figure_setup radii, figure_mesh ExtendWorldBox, face_normal
  distance, view_matrices (view translation, LightShearMatrix: DotZXY * 2, Cross, LengthSquaredZXY), scene_normals.
  All main-thread code.
- Left for later: world.h / sound.h / script.h / ui*.h / terrain_sector.h members (other tickets were editing those
  files), link_figure.h morph_ (morph coefficients, not a point), figure_mesh.h block[4][4]
  (indexed [i][j]), quaternion axis forms (unit_ai_sight Prepare, combat_actions), particles.
- Callees retyped: Negate 00402200 and QuaternionAxisY 0040F560 (originals_core.h), the local AxisX 0040D940 forwarders
  (unit_points, world_client_draw) take Vector3* / Quaternion*.

**V3 part 2 (2026-10-09), 408 -> 377.** Release objects compared function by function (dumpbin) before / after each
commit: FPU operations unchanged except where noted.
- Matrix x point (groupings read from the disassembly): MatrixRows::Transform 00560E10 (x ZYX, y / z ZXY), ClipTest
  004F16A0 (`ClipCode`: x, y, w ZXY, z ZYX), ExtendWorldBox 0059FDF0 (row ZYX), PlatformSet::AddBox 00560820
  (corners ZYX, normals ZXY), CollectVisible 0056A990 (view position x ZYX, y / z XYZ). ClipTest and ExtendWorldBox
  now round the box corners to float, which the original keeps on the FPU: the same at 24 bits (main thread, render
  code). CObject3D viewPosition / worldMatrix are Vector3 / Matrix4; CollectVisible takes `const Matrix4*`.
- CMapObject and CUnit have `Vector3 position` (+0x1C) instead of positionX / Y / Z, every use converted (pure rename;
  four locals named `position` that would hide the member renamed). One commuted FMUL operand in CEffectMissile::Steer.
- Shared declarations taking Vector3*: AIPathStartOriginal 005C7B00 (originals_ai.h; replaced 5 local forwarders),
  MotionSample 005C7430, QuaternionAxisX / AxisZ 0040D940 / 0040D6B0 (originals_core.h), MarkerDraw 00503A40, the
  CAIMap lookups IsPassableForClass / FindFreeCellNear / CellFlag80At / HasKind3OccluderAt / BlockingUnitAt /
  StampWaterPatch (originals_ai.h); callers use LineOfSightOriginal / SectorMapMove instead of their own copies.
- Next: the 2D distance / dot sums on `position` (unit_ai_*, combat_*; batch 3 above), world.h Platform's
  origin / axisU / axisV / normal (world_client.cpp's plane dots), the remaining `float [3]` members and locals.

## 7. Risks

- **/fp:fast reassociation** (above): inspect the release disassembly of one migrated site per batch.
- **Precise files.** 82 files use `float_control(precise)`; migrating one moves its maths from `double` to `float`: same bits
  at 24 bits, which is what the bitwise wrappers see; different on 53-bit threads.
- **53-bit threads.** Code running off the main thread (verify log "FPU precision ...") is where float helpers and the
  original's register values can differ; leave such functions explicit until checked.
- **Transcendentals.** FPATAN / FSIN / FCOS / acos results are 64-bit regardless of precision; dropping a `RoundToFloat` the
  original performs flips results (X3). Keep every such store.
- **Exponent range.** x87 intermediates do not overflow / underflow at float limits; float helpers do (huge or denormal
  inputs only).
- **Exact checks.** Wrappers using bitwise `Check` must keep passing; no CheckNear widening. A migration that changes a
  grouping is a bug: each batch changes spelling only.
- **Aliasing.** Out-of-line originals load all inputs before storing (out may alias an input); value-returning helpers keep
  that, in-place loops (`v = v * s` per component) do too.
