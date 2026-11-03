# Floating point and vector maths

The game code builds as SSE code with `/fp:precise` (x64, no x87 anywhere). `python tools/check_x87.py` fails on an
`x87::` helper, a `libcrt::Ftol` or a `D()` register in src/game, src/lib or src/render.

## Why plain float maths reproduces the original

The original's main thread ran its x87 at 24-bit precision (the control word Direct3D left): every add, subtract,
multiply, divide and square root rounded to a float significand, as the float operation does. So float maths in the
original's order and grouping gives the original's results; a "double register" of the original is a float.
- **Grouping decides the rounding.** `+` and `*` commute exactly; only the grouping of a sum matters. The grouping
  written is the grouping compiled under `/fp:precise`; `/fp:fast` regrouped sums and distributed products
  (FearMode's `(30x + d) * (1/31)` settled on another fixed point), so never use it. The vector helpers sum left to
  right; the original's per-site groupings are gone: the goldens are our build's own.
- **Constant products.** A product of two constants the original computed at run time rounds to 24 bits there; the
  compiler folds it at double precision. Write it as the float (`static_cast<float>(kPi * kPerDegree)`, checking the
  rounding) or keep the operation. The build inlines at /Ob2, so a constant behind a helper folds too.
- **53-bit code.** Before the device existed (start-up: options, registry, settings, the first files) the original
  ran at 53 bits, where its double maths is SSE's double maths: such code stays double (options' loader and saver
  have their own helpers, `LoadedTimes255` / `SavedTimes255`). Other threads (the MP3 stream, the cursor, the master
  lookup) do no float maths that matters.
- **Kept double:** values that never were 24-bit results: timeGetTime counts, file sizes, doubles read from data or
  messages, performance timers. Stored doubles (members, saves, messages) keep their type and receive float values.

## Wider operands (lib/pc24.h)

FSIN, FCOS, FPATAN, FYL2X and F2XM1 ignore the precision control (64-bit results); a double constant or an int above
2^24 keeps all its bits until the one operation rounds. `pc24::Mul / Add / Sub / Div` do one 24-bit-rounded operation
on two such values exactly (double-double, then one rounding); `pc24::Sin / Cos / Atan2 / Log / Log10` return the
64-bit result as an `Unrounded`, `pc24::Round` stores it; `pc24::Exp` is VC6's inline exp step by step. The game CRT's
pow / acos / asin / hypot are `libcrt::CiPow` / `CiAcos` / `CiAsin` / `Hypot` (src/lib/crt_maths.cpp). Examples:
`x87::fcos(a) * r + x` is `pc24::Mul(pc24::Cos(a), r) + x`; `x * 0.001` (a double constant) is `pc24::Mul(x, 0.001)`;
a float times the double 1 / (2^32 - 1) rounds as `* 0x1p-32f`; `pc24::Mul(x, word * 0x1p-32)` keeps the result where
`x * word` alone would overflow. A constant exact in 24 bits (0.5, 100) is a float constant. Each helper was checked
against the original's instruction sequences on every float or hundreds of millions of cases; the differences left are
the exponent range (x87 registers kept a 15-bit exponent: products beyond 3.4e38 or below 1.2e-38 inside an
expression), which NaN of two comes out, and FPATAN cases within hundredths of an ulp of a 64-bit tie (about 2 in 10^7
of the AI's kind-4 turn orders). Accepted.

## Conversions and copies

- FISTP rounds to nearest, ties to even: `lib::RoundEven` (0x80000000 for NaN or out of range). Not `std::lrint`
  (reads the FP environment per call) nor `std::lround` (halves away from zero).
- VC6's `_ftol` truncates and keeps the low half of the 64-bit integer (0 for NaN):
  `static_cast<int>(static_cast<std::int64_t>(x))` where a NaN or a value beyond int can arrive, else
  `static_cast<int>`.
- An unsigned 32-bit value (random words, tick counts) converts exactly with `static_cast<double>`.
- Float copies are plain assignments (SSE copies keep a signalling NaN's bits, where FLD / FSTP set the quiet bit);
  a float seen as its bits is `std::bit_cast`.
- The original truncated the terrain's edge fade under chop rounding; ours rounds to nearest (the speculars are shader
  input, not game state).
- Hit tests are integer: pixels and cell widths with a float position snapped once (`UiItemStrip::SlotAt`); a held
  button's repeat counts whole milliseconds (`UiContainer::FrameTicks`). A float hit test picked the neighbouring
  figure where `200 * 0.01f` is 1.99999996 in double and 2.0 at 24 bits.
- Accepted rounding differences: ui_hud_setup's `OnArc` and the round map's outline (a region corner one pixel off),
  colour bytes and wave entries at an exact half, terrain_animation's waves and `EdgeFade`.

## Selects on `>=` / `<=` (lib/float_select.h)

MSVC compiles `a >= b ? a : b` to MAXSS and `a >= b ? b : a` to MINSS (the `<=` forms likewise), which return the
second operand on a tie: where +0 meets -0 the zero's sign differs from the original's compare and branch. The strict
forms (`a < b ? a : b`, `std::min` / `std::max`) are exact. So a select of two floats on `>=` / `<=` goes through
float_select.h, never as a conditional or a one-line if: `a >= b ? a : b` -> `MaxKeepB(a, b)`, `a >= b ? b : a` ->
`MinKeepA(a, b)`, `a <= b ? a : b` -> `MinKeepA(b, a)`, `a <= b ? b : a` -> `MaxKeepB(b, a)`; `SelectLess(a, b,
ifLess, otherwise)` is the general one. Their deleted overloads make an int or double pair a compile error.

## NaN in comparisons

VC6 compared with FCOM / FNSTSW / TEST AH, where a NaN counted as equal and less. Ours are the plain comparisons the
source had (C3 `==`, C0 `<`, C0|C3 `<=`, a C0 mask `a < b ? x : y`), IEEE for NaN: a behaviour change for NaN only,
and none reaches them in the suite. Next to a comparison whose result changes for a NaN, `EI_NAN_AUDIT(values...)`
(src/lib/nan_audit.h) logs `NaN audit: a NaN compared at <file>:<line>` to ei-rebuild.log once per site in the verify
and debug builds. Where an equally plain form keeps the original's NaN result, it is used instead (`a > b`; `x < low ?
x : low` for a bound a NaN never replaces; `if (!(gap > limit)) break;` for a loop a NaN must still end). Kept as the
original's test, with the reason in a comment: ai_path's free-cell scale (a NaN step occurs in the suite),
`UnitStats::HealSlots` (LearnSkill passes a NaN heal while a new unit's stats are set up), `unit_skill_price`'s
below-4 branch, the client turn loop, addon's IEEE tests. Loops that spun for ever on a NaN in the original (the
modifier ring's rejection loop, the angle wraps, the weather band step, the cloud phase) now end.

A NaN does reach the unit sort in the original: an object listed only for its shadow before it was ever in view had a
NaN view depth from uninitialised memory, `Less(x, x)` became true and STLport's unguarded partition walked off the
array (a rare Rebirth crash). Ours zeroes the depth.

## The control word

x64 code has no x87 arithmetic and no precision control. `render.cpp`'s `FpuGuard` (platform::FloatingPointGuard)
puts MXCSR's control bits back around the renderer's library calls, only when they changed. No game code loads or
resets the floating-point state.

## Vector helpers (src/lib/vecmath.h)

Types: `Vector2`, `Vector3`, `Vector4`, `Quaternion` (w first), `Matrix4` (float m[16], row-major), `MatrixRows` (3
rows of a 4x4): aggregates, so `{x, y, z}` works. Two matrix conventions: `TransformObject::WorldMatrix` and the figure
code use row vectors (`v * M`, translation m[12..14]); the transform pipeline, clip codes, decals and view code column
vectors (`M * v`, translation m[3], m[7], m[11]).

- `+ - * /`, `+= -= *= /=`: one operation per component (`/ s` divides, no reciprocal); `Min`, `Max`, `Abs` per
  component (float_select.h's `MinKeepA` / `MaxKeepB` where a NaN must keep the bound).
- `Dot`, `LengthSquared`, `Length`, `Distance`, `DistanceSquared`, `Normalised`, `NormalisedUnlessZero`, `Cross`,
  `Lerp` (`(b - a) * t + a`); the 2D forms `Dot2D`, `LengthSquared2D`, `Length2D`, `DistanceSquaredXY` (also on
  scalars).
- `TransformRow` / `TransformRowNormal` (`v * M`), `TransformColumn` / `TransformColumnNormal` / `TransformColumnW`
  (`M * v`), `Matrix4::Multiply`.
- Quaternions: `a * b` (Hamilton), `Dot`, `AxisX / AxisY / AxisZ` (the rotation's columns), `Rotate(v)`, `ToMatrix`.

`tools/check_cleanup.py` counts hand-written vector maths (two-product sums whose operands differ only in axis,
`float n[3]`-style members) and keeps it at 0; exceptions are listed in it (`_VEC_NOT_VECTOR`).
