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
| rounding-store helpers (`Store`, `StoreFloat`, `RoundToFloat`, `Round`, `F`) | 53 definitions, 2481 calls | 102 | particle_callbacks.cpp 935 calls; all `static_cast<float>` since SV7d1a1 |
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
| `LengthSquared2DUnrounded<T>(x, y)`, `DistanceSquaredXYUnrounded<T>(a, b)` (V3b; precise off, nothing stored) | `x*x + y*y`; `dy = a.y - b.y`, then `dx`, `dx*dx + dy*dy`, in T (`double` where the site keeps the x87 value, `float` for float locals) |
| `Dot2DUnrounded<T>(x, y, x2, y2)`, `DotUnrounded<Sum3, T>(x, y, z, x2, y2, z2)`, `LengthSquaredUnrounded<Sum3, T>(x, y, z)` (V3c; precise off, on the caller's scalars) | `x*x2 + y*y2`; the named grouping of the three products, in T |
| `DotUnrounded<Sum3, T>(a, b)`, `LengthSquaredUnrounded<Sum3, T>(v)` (V3d; Vector3 forms of the above; T defaults to float since V3e) | the same sums on the members; `double` in a precise region (a float-typed result is rounded on return there) |
| `AxisXUnrounded<T>(q)` (V3c; precise off; returns `AxisUnrounded<T>` {x, y, z}) | 0040D940's X axis, `(w^2 - ((y^2 + z^2) + x^2)) + 2x^2`, `2(wz + xy)`, `2(xz - wy)`, each doubling an addition |
| `RotatedAxisY(q)` (V3c; precise; no intermediate stores since V3e) | AxisY 0040F560 and its inlined copies 00547280 / 005CA410: `2(yx - wz)`, `(w^2 - ((x^2 + z^2) + y^2)) + 2y^2`, `2(yz + wx)`, each component rounded to float |
| `DotWZYXUnrounded<T>(a, b)` (V3e; precise off) | quaternion dot `((w*w' + z*z') + y*y') + x*x'` (Slerp 0049ED20) |
| `RotatedAxisX / Z(q)` (V3e; precise) | Quaternion::AxisX 0040D940 / AxisZ 0040D6B0's orders, each component rounded to float |
| `OffsetDot2DUnrounded<T>(d, x, y, x2, y2)` (V3f; precise off) | `(d + x*x2) + y*y2`, the offset added first (a plane's value at a point, PlaneHeight 006BE3A0); not `d + Dot2DUnrounded(...)` |
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
helper chosen so that XYZ, ZYX and ZXY give three different results (2448 checks, 0 mismatches; without the precise
pragmas 116 mismatch).

**Done in V2**: the old local vector types (render::detail::Matrix, render `Vertex` / `DecalVertex` / `GlobalMapPlace`
positions, chunk_stream.h `Float4` (Quaternion, still memcpy'd), Point, Vec3, UiVec3, V3, Quat, Quaternion4) are the vecmath
types; 0049E800's forwarders are one `QuaternionSetAxisAngleOriginal` (originals_render.h); local helpers renamed where
vecmath uses the name (`LengthDouble`, `DotUnrounded` / `CrossStored`, `LerpStored`). core_maths / object_maths /
quaternion / matrix_math / maths use the operators, `Cross`, `Conjugate`, `static_cast<float>` (was `RoundToFloat`). Kept written out: Dot 00401E00,
Length 00401E20 and the squared axis lengths of FromAxisAngle / SetAngleAxis / SetAxisAngle (unrounded sums: a
LengthSquaredXYZ version differed at 53 and 64 bits, hence the migration rule), quaternion products, AxisX/Y/Z,
MatrixMultiply's per-element orders, Slerp.

## 6. Migration order (V3, each batch through the full suite)

Not folded yet: particle_callbacks.cpp's `float x, y, z` locals (particles batch), TLVertex, LightningPoint.
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

**V3 status.** check_cleanup column "hand-written vector maths" (tools/check_cleanup.py `count_vector_maths`: two-product
sums whose operand pairs differ only in axis letters / digits, looking through one `D(...)` / `static_cast`; non-const
`float n[3]`, `[16]`, `[4][4]`, quaternion-named `float n[4]`; colour / stat names and const tables skipped; vecmath.h
exempt): 507 at the start, 377 after part 2. Each batch compares the release objects function by function (dumpbin):
identical, or only stack slots / scheduling / commuted FMUL operands.
- Fields done: map_object_figures.h, map_object.h (orientation Quaternion), map_object_animation.h, unit.h, combat.h /
  combat_plan.h / unit_ai.h, magic_trap.h, model.h ClipBounds, map_object_shadow.h, figure_mesh.h position_, map_spec.h,
  scene_loader.h, unit_db.h, map_object_cage.h; CObject3D viewPosition / worldMatrix; CMapObject / CUnit `Vector3 position`
  (+0x1C); float[3] locals in combat_* / unit_points / unit_motion_tick / unit_client_path.
- Helpers done (grouping per site, all main-thread code): unit_ai_fear, unit_ai_sight, combat_tick, effects, ground_decal,
  lightning_draw (its LengthXZY is LengthZXY), object3d_figure_setup, face_normal, view_matrices, scene_normals.
- Matrix x point groupings (from the disassembly): MatrixRows::Transform 00560E10 (x ZYX, y / z ZXY), ClipTest 004F16A0
  (x, y, w ZXY, z ZYX), ExtendWorldBox 0059FDF0 (row ZYX), PlatformSet::AddBox 00560820 (corners ZYX, normals ZXY),
  CollectVisible 0056A990 (x ZYX, y / z XYZ). ClipTest and ExtendWorldBox round the box corners to float, which the
  original keeps on the FPU (same at 24 bits).
- Callees taking Vector3* / Quaternion*: Negate 00402200, QuaternionAxisX / Y / Z 0040D940 / 0040F560 / 0040D6B0
  (originals_core.h), AIPathStartOriginal 005C7B00, MotionSample 005C7430, the CAIMap lookups (originals_ai.h), MarkerDraw
  00503A40; callers use LineOfSightOriginal / SectorMapMove. Others still get `&v.x`.
- Left: world.h / sound.h / script.h / ui*.h members, world.h Platform's origin / axisU / axisV / normal
  (world_client.cpp's plane dots), quaternion axis forms (unit_ai_sight Prepare, combat_actions). (link_figure.h morph_,
  figure_mesh.h block, terrain_sector.h and the particles: V3e below.) Next: the 2D distance / dot sums on `position`
  (unit_ai_*, combat_*; batch 3).
- Batch 3 (2D sums in unit_ai_* / combat_*), V3b 2026-10-14: 369 -> 339. The precise 2D helpers added FSTP / FLD round
  trips at these sites (the first try changed nothing), so vecmath.h got unrounded forms compiled with precise off:
  `LengthSquared2DUnrounded<T>(x, y)` and `DistanceSquaredXYUnrounded<T>(a, b)` (dy first, as AIDistance2 005CFE20 loads
  them); T is deduced or `double`, so the double sites stay double and the six float sites stay float. Per site the form
  that compiles to the same code was kept: the Vector3 form where dy, dx are computed in that order and used only in the
  sum (AIDistance2, Score, combat_tick, combat_sight CanActFrom / PlanAction), otherwise the locals stay and
  only the sum is the helper, with its terms in the written order (`dy*dy + dx*dx` -> `(dy, dx)`). Release objects
  compared function by function: all identical except combat_tick ApproachTarget (the volatile `reach` loaded after the
  sum instead of before: scheduling). Left written out: unit_ai.cpp IsNearPath's volatile `segY*segY + segX*segX` and
  combat_sight PlanAction's volatile `delta` sum (the helper reads each volatile once: other code), unit_ai_moves
  UpdateFollow's six sums (any helper there changes the x87 register-stack allocation of unrelated later code, the
  `lengthSq` / side test), unit_ai_moves:150 (a double and a float term), and the sites whose other uses of the same
  forms produced integer register or frame changes (unit_ai_targets IsUsable, combat_tick StartMove, unit_ai_player
  UpdateCombat keep `LengthSquared2DUnrounded(dx, dy)` instead of the Vector3 form). The x87 stackifier is sensitive to
  any inlined call: compare every function, not only the edited lines. The rest of the counted sites in these files are
  3D sums (batch 4) and quaternion axis forms.
- Unit / AI / combat files (unit_*, ai_*, combat_* under src/game), V3c 2026-10-14: 77 -> 0 in those files (tree 332
  -> 255). New forms in vecmath.h (table above): `Dot2DUnrounded`, `DotUnrounded<Sum3, T>`, `LengthSquaredUnrounded<Sum3,
  T>` on scalars (most sites keep differences in double locals or volatile floats, so a Vector3 form would change the
  code), `AxisXUnrounded<T>` and the precise `RotatedAxisY`. float[3] locals / members became Vector3 (ai_motion target,
  combat_planning bestSpot / previous / aim with `AimPointInto(Vector3*)`, combat_sight's volatile facing / delta,
  unit_ai AttackOrder::point, unit_client_animation / _effects / _sounds, unit_create_stats scale, unit_figure_queries
  EffectPoint); forwarders taking `float*` (ObjectPartPositionOriginal, PartWorldPosition, ObjectSetFigureScaleOriginal,
  CWorld::MoveObject) get `&v.x`. Every match was vector maths; nothing went to the counter's skip list.
  Release objects compared function by function (dumpbin, all 114 files): 91 files identical; in the other 23 the
  differences are stack slots, register choice, load order, commuted FADD / FMUL operands, compare operand order (same
  NaN branch), exact float -> double conversions spilled as qwords (unit_ai_fear Evaluate, combat_sight PlanAction), and
  volatiles read once instead of twice (combat_sight SomeoneInTheWay / PlanAction, unit_ai IsNearPath, unit_ai_modes
  GuardMode::Execute, unit_hits IsBehind): same operations, same groupings, same bits. Each was read by hand.
  Three sites needed more than spelling, all quaternion forms in /fp:fast code:
  - unit_figure_queries Rotate: an inlined unrounded dot followed by the caller's `2.0f *` became (2xx' + d) + d with d =
    yy' + zz' (a second rounding; written out it had been (2yy' + 2zz') + 2xx', exact). It uses the precise `DotZYX` /
    `LengthSquaredXYZ` (`2 * round(dot)`, the same bits at 24 bits).
  - combat_update StandStill (00547280) and unit_ai_sight Prepare (005CA410): the previous ports' written-out 2(yz + wx)
    had already become (2wx + yz) + yz under /fp:fast, a second rounding the game does not make (it adds, FADD ST0,ST0,
    then stores), and Prepare's -2(yx - wz) had become (wz - yx) + (wz - yx), +0 where the game's FCHS gives -0. Both now
    use the precise `RotatedAxisY` and negate: the game's operations (rounding stores at 24 bits only). This changes
    those two functions' z component in rare last-bit cases and the sign of a zero x, towards the original. Prepare's
    wrapper compares with the original (CheckNear on the facing, every scenario with AI units); StandStill's checks
    ours only.
  Lesson: a precise-off helper is the written-out expression only while the caller does nothing more with its result;
  `2 *`, doubling and negation of a fast helper's sum regroup (the same hazard the precise helpers exist for). Use the
  unrounded forms for sums that are compared, stored, passed or square-rooted, and the precise forms (rounded result)
  where the caller scales the sum and the function runs on the main thread.
- Interface / camera / world files (ui_*, camera_*, world_* under src/game, other owners' files excluded), V3d
  2026-10-14: 90 -> 0 in those files (tree 255 -> 165 after merging V3c). Fields: CUiMesh placement_ / scaleMatrix_
  Matrix4, BriefingMover target / direction, CWorldServer::startPosition_ and world.h Platform's origin / axisU / axisV /
  normal Vector3; PickGround / PickSurface / HoverPickSurface / AddFace / CCamera::Pan and the battle-command forwarders
  take Vector3*. Sums use the V3c scalar forms or the new Vector3 forms with the original's grouping; world_client's plane
  dots (SurfaceHeight XYZ, Contains / SurfacePlane ZYX) too, though the counter does not see them. ui_hero_panels'
  `stored[3]` (strength / dexterity / intelligence) was reworded to `storedStats` (the counter skips stat names); nothing
  went to the skip list. Member-wise Vector3 copies stay member-wise: a struct copy turns FLD / FSTP into MOV.
  Release objects compared function by function against the base commit with a symbolic x87 comparison (expression trees
  of every store, call argument, compare and return over the control-flow graph; + and * commuted; FSTP/FLD float
  round trips kept as roundings), and the remaining diffs read by hand: values and calls the same everywhere; differences
  are stack slots (HoverUnit shares one Vector3 slot between its two picks), scheduling (DepthLess::Less calls the pure
  MapObject later), register choice, address operand order (BuildMarkers), renamed helpers (Normalised ->
  NormalisedUnlessZero, ui_hero_preview's out-of-line DotUnrounded -> DotCall) and camera::D. camera::D is `inline`, not
  `__forceinline`, and camera.cpp / camera_motion.cpp call it out of line (about 200 calls); a sum moved into a helper
  no longer calls it, which is the same value (D only widens). Two sites needed more than spelling:
  - ui_briefing_dialogue StartPhrases axis X: `Dot2DUnrounded(x, y, z, w) * 2.0` regrouped to xz + (xz + 2wy) (the V3c
    lesson again). The original doubles before multiplying, (2x)z + (2y)w; `Dot2DUnrounded<double>(x * 2.0, y * 2.0, z, w)`
    compiles to x(2z) + (2y)w, the same value (doubling is exact).
  - world_rules DepthLess::Less is in a precise region: `DotUnrounded<Sum3::ZYX, float>` rounded the dot to float before
    the plane offset was added; `double` keeps it on the FPU as the written-out sum did.
- Batch V3e 2026-10-14 (particle, terrain, scene, figure, map object, lightning, magic, sound, face, ground, link,
  sector, sky, lever, quaternion, maths and core_* files in src/game): 126 -> 1 counted sites in these files (tree 332
  -> 207 on its own; 40 after merging V3c / V3d). Helpers: the same unrounded forms as V3c / V3d (written in
  parallel as SumOfProductsUnrounded and merged into V3c's `DotUnrounded` names; the Vector3 forms' T now defaults to
  float), `DotWZYXUnrounded`, and the precise `RotatedAxisX / Z` beside V3c's `RotatedAxisY`. AxisY uses that one, which lost its float
  locals: a precise body rounds them through memory, and 0040F560, 00547280 and 005CA410 keep `yx - wz`, `y^2` and
  `yz + wx` on the FPU until the FADD ST0, ST0 (CUnitServer::StandStill and AIViewer::Prepare lose three FSTP / FLD
  pairs each and are otherwise the same code; nothing changes at 24 bits). Why both kinds: precise helpers inlined
  into /fp:fast code spill scalar or reference-to-float arguments, round a float cast of the result, and push other
  inlining out of line (StoreFloat, fabs became calls in UpdateWorld); the unrounded forms compile to the written-out
  code. But a precise-off body is regrouped with the caller's arithmetic: `2.0f * Dot(...)` became `(2xx' + s) + s`
  (two roundings where the original has one). Sites that double a dot therefore double the operands, `(2u) . v`
  (`DotUnrounded<Sum3::ZXY>(ux * 2.0f, ...)`; what /fp:fast makes of the written-out sum, and exact at 24
  bits): Transform::UpdateWorld, CObject3D::PartWorldPosition. The quaternion axis forms use the precise
  `RotatedAxis*`, which take the quaternion by reference and store floats. A two-term sum's operands commute exactly, so
  `(y + x) + z` is XYZ and `(y + z) + x` is ZYX; there are only the three groupings. Each function changed was compared
  in the release objects (dumpbin) and the grouping traced on the x87 stack: identical, or stack slots / scheduling /
  load order / commuted operands. In the precise particle files the helpers convert float to double inline, so the
  out-of-line `D()` calls around those sums are gone (the same values). Other code on purpose: Quaternion::AxisX / Y / Z and
  CCamera::GroundPoint (core_transform.cpp) go through `RotatedAxis*` (the originals' order, checked against 0040D6B0;
  the same values as the base at 24 bits, but the code differs: precise stores, GroundPoint's fabs out of line and one
  quotient stored), and ground_decal's Project adds the offset last as 004D7BD0 does (/fp:fast had made it
  `(zz' + yy') + (xx' + offset)`).
  Fields: SectorVertex projected / position / normal (Vector4 / Vector3), SectorBox centre / low / high, the terrain
  projection buffers (Vector4), map_object_shadow's light vectors and pixel matrix (Vector3 / Matrix4), magic_trap,
  lever, figure_mesh MorphOldBounds, figure_part_draw locals. Not vectors: VertexPacket::block (figure_mesh.h, the SoA
  rows of four vertices; PositionBlock folded into it) and per-corner `*distances` arrays are skipped by the counter
  (`_VEC_NOT_VECTOR`, reasons in check_cleanup.py); link_figure.h `morph_` was renamed `morphWeights_` (blend
  coefficients; the counter skips weight names). Left: terrain_sector.h `SectorPosition::xyz[3]`, read as `const float*`
  by src/render/render_terrain.cpp (R9a's); after that lands, `struct SectorPosition : Vector3 {}` (terrain.h declares it
  ahead) with `.x / .z` in terrain_animation / terrain_lighting / terrain_lights / terrain_shadow / terrain_sector, the two
  verify files and render_terrain.cpp:346. Self-test: tests/verify/verify_vecmath.cpp checks the unrounded forms alone
  and `(2a) . b`, and `RotatedAxis* * 2`, against one-rounding-per-operation references (not run here).
- Batch V3f 2026-10-14 (every file outside src/render, renderer_init.cpp and device_buffers.h): 40 -> 8 in the tree; the
  8 left are terrain_sector.h `SectorPosition::xyz[3]` (above: tied to render_terrain.cpp:346) and R9a2's renderer files.
  Fields: sound.h SoundSlot / DeferredSound / SoundEmitter / SoundSequence `position`, CSoundManager `listener` /
  `listenerAxis` (Vector3); script.h ScriptPointLight / ScriptParticleSource `step`, ScriptFxSource `position` (Vector3);
  ui_camp.h CGlobalMap `position_` (Vector3) and `turn_` (Quaternion, w first as it was; the local Rotate forwarder to
  0046A4B0 takes `const Quaternion*`). Locals: net_state / player_client / player_sync figure scales and positions,
  script_addon_units' search point, map_spec's chunk quaternion. Sums: CameraViewMatrix 004F0EF0's inlined Y / Z axes
  through `RotatedAxisY / Z` (traced on 004F0EF0: the same orders; the base port's float locals stored the products, which
  changes nothing at 24 bits, and the new code keeps w^2 / z^2 as qwords); LightRotation, FormationOffsets,
  UseOrderDistanceSquared, AttackRoll, GetIntensityAt, wall spell's plane distance, CVisibilityFog::ComputeBounds through
  the V3c scalar forms; QuaternionRotate 0046A4B0's `2(q . v)` doubles the operands (`2.0f * DotUnrounded` became
  s + (2yv + s), s = xv + zv, the V3c hazard; the base had distributed the 2 into the products, exact); PlaneHeight
  006BE3A0's `(d + nx x) + ny y` gets `OffsetDot2DUnrounded` (`d + Dot2DUnrounded` compiled to d + (nx x + ny y)). Release
  objects compared with v3d's symbolic x87 comparison: every changed function the same expressions except CameraViewMatrix
  (float-store roundings of products dropped, same groupings) and CGlobalMap::Update (the sign flip of turn_ written per
  member; FLD / FCHS / FSTP as before, w reuses the compare's load); the rest stack slots and scheduling. No skips added.
  The self-test checks OffsetDot2DUnrounded on inputs that separate it from d + (xx' + yy'). (The "unrounded forms" test
  finds only 1 of 24 inputs separating both its dot and its square: pre-existing, worth splitting.)
- Batch V3g 2026-10-14 (the last 8): 8 -> 0 in the tree. terrain_sector.h `struct SectorPosition : Vector3 {}` (users
  in terrain_animation / lighting / lights / sector / shadow, the verify files and render_terrain.cpp read `.x / .y / .z`;
  the load / record loops step a `SectorPosition*` instead of a float pointer); device_buffers.h `struct ProjectedVertex :
  Vector4 {}`; renderer_init's default light position a Vector3 (LightSet 0068D0A0 takes `const Vector3*`), and IsLit
  006CB020 (originals_effects.h) takes `const Vector3*`, its five callers no longer pass `&v.x`. The new renderer's
  matrices are Matrix4: MeshDraw::matrix (render.h) is `const Matrix4*`, AddMatrix takes `const Matrix4&`,
  ScreenToClip returns one, the shaders' uniform blocks hold a `Matrix4 matrix` (VsParams / Params, same 96 bytes),
  TerrainPage reads the pipeline stack as Matrix4s. No helpers added, no skips. Release objects compared (x87sim): the
  same expressions everywhere; LightVertex no longer calls ToVector (a MOV copy) and reads the position in place
  (`Vector3 at = *position`: member-wise initialisation had become FLD / FSTP round trips), BuildRecords keeps `rec` /
  `vin` in other stack slots, the rest address-operand order and the mangled names of IsLit / LightSet. The self-test's
  unrounded forms run in two loops (dots and `(2a) . b` on inputs separating the three dots, squares on inputs separating
  the three squares): 24 inputs found for each, 2904 checks, 0 mismatches (frames_basic).

## 7. Risks

- **/fp:fast reassociation** (above): inspect the release disassembly of one migrated site per batch. The `...Unrounded` 2D
  helpers are compiled without precise semantics on purpose (V3b): they regroup with the caller like written-out code, so
  they hold only 2-term sums and replace written-out sums only where the object code stays the same.
- **Precise files.** 82 files use `float_control(precise)`; migrating one moves its maths from `double` to `float`: same bits
  at 24 bits, which is what the bitwise wrappers see; different on 53-bit threads.
- **53-bit threads.** Code running off the main thread (verify log "FPU precision ...") is where float helpers and the
  original's register values can differ; leave such functions explicit until checked.
- **Transcendentals.** FPATAN / FSIN / FCOS / acos results are 64-bit regardless of precision; dropping a float store (`static_cast<float>`) the
  original performs flips results (X3). Keep every such store.
- **Exponent range.** x87 intermediates do not overflow / underflow at float limits; float helpers do (huge or denormal
  inputs only).
- **Exact checks.** Wrappers using bitwise `Check` must keep passing; no CheckNear widening. A migration that changes a
  grouping is a bug: each batch changes spelling only.
- **Aliasing.** Out-of-line originals load all inputs before storing (out may alias an input); value-returning helpers keep
  that, in-place loops (`v = v * s` per component) do too.
