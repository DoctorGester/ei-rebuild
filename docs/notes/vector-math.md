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

The V1 survey counted the patterns the porting workers had written out by hand (before V3 replaced them with the helpers of
section 5; the counts are gone, the kinds are what matters): sums of squares (length / distance squared; 2-term in any order,
3-term ZYX `(z+y)+x`, XYZ `(x+y)+z`, ZXY `(z+x)+y`), 3D and 2D dot products (ZYX most), sqrt calls (`x87::fsqrt` / `std::sqrt`),
normalise (divide by the length, or times 1/length), cross products, quaternion products, quaternion rotated axes
(`w*w - (..) + 2zz`), rotate vector by quaternion, matrix x point / normal, matrix x matrix (004D9880, 004D9B80), lerps
(`(b-a)*t + a`; `(1-t)*a + t*b` in unit_client_path.cpp and Slerp's linear branch), atan2 (FPATAN), acos / asin (`_CIacos`),
sin / cos (particle_callbacks.cpp most), `float a[3]` / `[4]` / `[16]` / `[12]` / `[2]` members and locals, rounding-store helpers
(`Store`, `StoreFloat`, `RoundToFloat`; all `static_cast<float>` since SV7d1a1), local `D(float)` helpers and naked `Fpatan` /
`CIacos` / `CIasin` copies. Orders in source were written from the disassembly by the porting workers; VC6's inliner and
scheduler produced them, so they do not follow from the original source (two neighbouring functions differ:
unit_ai_memory.cpp NearestEnemy is ZXY, ForceSee is ZYX).

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

Status: finished. The check_cleanup column "hand-written vector maths" (tools/check_cleanup.py `count_vector_maths`: two-product
sums whose operand pairs differ only in axis letters / digits, looking through one `D(...)` / `static_cast`; non-const `float
n[3]`, `[16]`, `[4][4]`, quaternion-named `float n[4]`; colour / stat names and const tables skipped; vecmath.h exempt) went from
507 to 0 in batches V3a-V3g (git log has them). Order that was followed: core (V2), fields (the 71 `float [3]` class members,
the 3 quaternion `float [4]` members and the `float [16]` members became Vector3 / Quaternion / Matrix4, layout-identical, asserts
kept), 2D distances and dots, 3D lengths, quaternions, matrix x point, particles last. `src/render` may switch at any time
(frame-capture checks, no bitwise compare). Out-of-line originals keep their hooks; their bodies may use the helpers when the
order matches (keeping the all-loads-before-stores aliasing); rewritten callers use the inline helper instead of calling Dot /
Length / CrossProduct (same bits). Not vectors, skipped by the counter (`_VEC_NOT_VECTOR`, reasons in check_cleanup.py):
`VertexPacket::block` (the SoA rows of four vertices; PositionBlock folded into it) and per-corner `*distances` arrays; stat
and weight names (`link_figure.h` `morph_` is `morphWeights_`).

**Rules that came out of the batches** (each batch compared the release objects function by function with dumpbin, later with a
symbolic x87 comparison of every store, call argument, compare and return; the differences left were stack slots, scheduling,
register choice, commuted FADD / FMUL operands, compare operand order with the same NaN branch, and volatiles read once):
- The x87 stackifier is sensitive to any inlined call: compare every function of the file, not only the edited lines.
- Precise helpers (float_control(precise)) inlined into /fp:fast code spill scalar or reference-to-float arguments, round a
  float cast of the result and push other inlining out of line (StoreFloat, fabs became calls in UpdateWorld). The unrounded
  forms (`LengthSquared2DUnrounded<T>`, `DistanceSquaredXYUnrounded<T>` (dy first, as AIDistance2 005CFE20 loads them),
  `Dot2DUnrounded`, `DotUnrounded<Sum3, T>`, `LengthSquaredUnrounded<Sum3, T>`, `DotWZYXUnrounded`, `AxisXUnrounded<T>`,
  `OffsetDot2DUnrounded`; T is deduced or `double`, so double sites stay double and float sites float) are compiled with precise
  off and compile to the written-out code, so they are used for sums that are compared, stored, passed or square-rooted.
- But a precise-off body regroups with the caller's arithmetic: `2.0f * Dot(...)` became `(2xx' + s) + s` (two roundings where the
  original has one), and negation of a fast helper's sum regroups too (`-2(yx - wz)` became `(wz - yx) + (wz - yx)`: +0 where
  the game's FCHS gives -0). So: where a caller doubles a dot, double the operands, `(2u) . v` (`Dot2DUnrounded<double>(x * 2.0,
  y * 2.0, z, w)`; doubling is exact and the same value as the original's `(2x)z + (2y)w`); where the caller scales or negates
  the sum, use the precise forms (rounded result) and only on main-thread code: `DotZYX`, `LengthSquaredXYZ`, and the quaternion
  axis forms `RotatedAxisX / Y / Z` (they take the quaternion by reference and store floats; the previous written-out 2(yz + wx)
  had become (2wx + yz) + yz under /fp:fast, a second rounding the game does not make: it adds with FADD ST0, ST0, then stores).
  `Quaternion::AxisX / Y / Z` and `CCamera::GroundPoint` (core_transform.cpp) go through `RotatedAxis*` (the originals' order,
  checked against 0040D6B0). A two-term sum's operands commute exactly, so `(y + x) + z` is XYZ and `(y + z) + x` is ZYX; there
  are only the three groupings.
- A site that dereferences or stores in a precise region (`DepthLess::Less` in world_rules): `DotUnrounded<Sum3::ZYX, float>` rounded
  the dot to float before the plane offset was added; `double` keeps it on the FPU as the written-out sum did. `PlaneHeight`
  006BE3A0's `(d + nx x) + ny y` needs `OffsetDot2DUnrounded` (`d + Dot2DUnrounded` compiled to d + (nx x + ny y)). ground_decal's
  Project adds the offset last as 004D7BD0 does (/fp:fast had made it `(zz' + yy') + (xx' + offset)`).
- Member-wise Vector3 copies stay member-wise: a struct copy turns FLD / FSTP into MOV; `Vector3 at = *position` was an FLD / FSTP
  round trip where the original read in place.
- Left written out on purpose (any helper changes the x87 register-stack allocation of unrelated later code or reads a volatile
  once instead of twice): unit_ai.cpp IsNearPath's volatile `segY*segY + segX*segX`, combat_sight PlanAction's volatile `delta` sum,
  unit_ai_moves UpdateFollow's six sums (the `lengthSq` / side test) and :150 (a double and a float term), the sites whose other
  uses of the same forms produced integer register or frame changes (unit_ai_targets IsUsable, combat_tick StartMove, unit_ai_player
  UpdateCombat keep `LengthSquared2DUnrounded(dx, dy)`), Dot 00401E00, Length 00401E20 and the squared axis lengths of
  FromAxisAngle / SetAngleAxis / SetAxisAngle (unrounded sums: a LengthSquaredXYZ version differed at 53 and 64 bits, hence the
  migration rule), quaternion products, AxisX/Y/Z, MatrixMultiply's per-element orders, Slerp.
- Matrix x point groupings (from the disassembly): MatrixRows::Transform 00560E10 (x ZYX, y / z ZXY), ClipTest 004F16A0 (x, y, w
  ZXY, z ZYX), ExtendWorldBox 0059FDF0 (row ZYX), PlatformSet::AddBox 00560820 (corners ZYX, normals ZXY), CollectVisible 0056A990
  (x ZYX, y / z XYZ). ClipTest and ExtendWorldBox round the box corners to float, which the original keeps on the FPU (same at 24
  bits). world_client's plane dots: SurfaceHeight XYZ, Contains / SurfacePlane ZYX.
- Callees taking Vector3* / Quaternion*: Negate 00402200, QuaternionAxisX / Y / Z 0040D940 / 0040F560 / 0040D6B0 (originals_core.h),
  AIPathStartOriginal 005C7B00, MotionSample 005C7430, the CAIMap lookups (originals_ai.h), MarkerDraw 00503A40, IsLit 006CB020
  (originals_effects.h), LightSet 0068D0A0; callers use LineOfSightOriginal / SectorMapMove. Others still get `&v.x`.
- Field types now vecmath: `SectorPosition : Vector3`, `ProjectedVertex : Vector4` (device_buffers.h), `CObject3D`
  viewPosition / worldMatrix, CMapObject / CUnit `Vector3 position` (+0x1C), CUiMesh placement_ / scaleMatrix_ Matrix4, CGlobalMap
  `position_` Vector3 and `turn_` Quaternion (w first), sound.h positions, script.h steps, the new renderer's matrices
  (`MeshDraw::matrix` is `const Matrix4*`, shaders' uniform blocks hold a `Matrix4 matrix`).
- tests/verify/verify_vecmath.cpp checks the unrounded forms alone, `(2a) . b`, `RotatedAxis* * 2` and `OffsetDot2DUnrounded`
  against one-rounding-per-operation references on inputs that separate the three dots / squares (24 each, 2904 checks).

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
