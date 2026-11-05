# Floating point and vector maths

The game code is plain float maths: SSE code with `/fp:precise` (x64, no x87 anywhere; gcc and clang with
`-ffp-contract=off`), lib/maths.h's functions on floats. The goldens are our build's own, so nothing reproduces the
original's x87 rounding any more, and one set of goldens holds on Windows and Linux.

- **Grouping decides the rounding.** `+` and `*` commute exactly; only the grouping of a sum matters. The grouping
  written is the grouping compiled under `/fp:precise`; `/fp:fast` regroups sums and distributes products (FearMode's
  `(30x + d) * (1/31)` settled on another fixed point), so never use it. The vector helpers sum left to right.
- **Floats, with float constants** (`0.001f`, `1.0f / 15.0f`, `6.3f * 0x1p-32f` for a random word to [0, 6.3)). A
  double constant or a double-returning function in float maths is a C4244 warning, which fails the build.
- **Double only where precision matters:** millisecond and tick clocks (`platform::FrameClock`, the animation frames
  of the hero and camp screens, the connections' clocks), free disk space and file sizes, the performance timers,
  printf's arguments, the registry's `GetFloat` (an int value exact), script values (`GetVariable`, addon's
  `CrtSin` / `CrtCos` / `CrtPow`), the shadow camera's matrix inverse (render_shadows.cpp) and the Bink audio
  decoder.

## Maths functions (src/lib/maths.h)

The game code calls `lib::Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan2`, `Exp`, `Log`, `Log10`, `Pow` and `Hypot`, never the
system's float functions: those are not correctly rounded and each system rounds its own way, so the same game left
Windows' goldens on Linux at its first turning animal (H47c: the main menu's unanwibo, `rot.w` an ulp apart at world
update 5). Measured over every float (2026-11-03, MSVC 14.43's static UCRT and glibc 2.39), the float function against
the double one rounded to float: sinf off for 0.07% of the inputs on UCRT and 0.7% on glibc, cosf alike, acosf 0.03% /
0.25%, log10f 0.06% / 0.7%, expf 0.0005% / 0.004%, and the two sets' results differ. The double functions rounded to
float agree on both for every float (the same hash of all results) (sin, cos, tan, asin, acos, atan, exp, log, log10) and on 2^28 samples each of atan2 and
pow; so each lib:: function is the double function on the float, rounded once. `Hypot` calls no library: two floats'
squares are exact in double, the sum and the square root are IEEE's (glibc's `(float)hypot(double)` differed once in
2^28 samples). tools/check_cleanup.py counts `std::sin` and the rest per file ("system maths function"); outside the
game state they stay (the movie decoder's double `pow`, the scripts' functions in
script_addon_math.cpp). The doubles of the scripts (`CrtSin` / `CrtCos` / `CrtPow`) are the systems' and not
checked across them.

## Conversions and copies

- FISTP rounds to nearest, ties to even: `lib::RoundEven` (0x80000000 for NaN or out of range). Not `std::lrint`
  (reads the FP environment per call) nor, in game state, `std::lround` (halves away from zero).
- `std::lround` is allowed in client-only code (user, 2026-11-05): a value that only the picture, the sound or the
  window sees (a colour byte, a volume, a particle count, a window size, a number shown on a panel) differs from the
  even rounding at exact halves only, by one step. Every site was read on that day; the files `CLIENT_ONLY_ROUNDING`
  lists in `tools/check_cleanup.py` hold them, and its column "std::lround in game-affecting code" is 0. The two that
  decided something use `lib::RoundEven`, as the original's FISTP (C155a, read in game.exe 1.06): `CampPages::CanEquip`
  (006100E0: a worn-out weapon or armour, `FISTP(condition) <= 0`, is not given to a hero, as combat and armour test it)
  and `HeroSchoolSkill` (00610020 and CanEquip's trade page: the float skill is stored, reloaded and FISTP'd before the
  spell's requirement is compared).
- VC6's `_ftol` truncates and keeps the low half of the 64-bit integer (0 for NaN):
  `static_cast<int>(static_cast<std::int64_t>(x))` where a NaN or a value beyond int can arrive, else
  `static_cast<int>`.
- An unsigned 32-bit value (random words, tick counts) converts to float with `static_cast<float>` (rounded to 24
  bits).
- Float copies are plain assignments (SSE copies keep a signalling NaN's bits, where FLD / FSTP set the quiet bit);
  a float seen as its bits is `std::bit_cast`.
- The original truncated the terrain's edge fade under chop rounding; ours rounds to nearest (the speculars are shader
  input, not game state).
- Hit tests are integer: pixels and cell widths with a float position snapped once (`UiItemStrip::SlotAt`); a held
  button's repeat counts whole milliseconds (`UiContainer::FrameTicks`). A float hit test picked the neighbouring
  figure where `200 * 0.01f` is 1.99999996 in double and 2.0 at 24 bits.
- Accepted rounding differences: ui_hud_corner.cpp's `OnArc` and the round map's outline (a region corner one pixel off),
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
and none reaches them in the suite; nothing marks them. Where an equally plain form keeps the original's NaN result, it is used instead (`a > b`; `x < low ?
x : low` for a bound a NaN never replaces; `if (!(gap > limit)) break;` for a loop a NaN must still end). Kept as the
original's test, with the reason in a comment: ai_path's free-cell scale (a NaN step occurs in the suite),
`UnitStats::HealSlots` (LearnSkill passes a NaN heal while a new unit's stats are set up), `unit_skill_price`'s
below-4 branch, the client turn loop, addon's IEEE tests. Loops that spun for ever on a NaN in the original (the
modifier ring's rejection loop, the angle wraps, the weather band step, the cloud phase) now end.

A NaN does reach the unit sort in the original: an object listed only for its shadow before it was ever in view had a
NaN view depth from uninitialised memory, `Less(x, x)` became true and STLport's unguarded partition walked off the
array (a rare Rebirth crash). Ours zeroes the depth.

## The control word

x64 code has no x87 arithmetic and no precision control. `render.cpp`'s `CheckFloatControl` compares MXCSR's control
bits once a frame with those at the device's start and puts them back when a driver or library changed them (verify
builds log it). No game code loads or resets the floating-point state.

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
- Quaternions: `a * b` (Hamilton), `Dot`, `AxisX / AxisY / AxisZ` (the rotation's columns), `Rotate(v)`, `ToMatrix`;
  `ToMatrix`, `AxisAngle`, `Slerp`, `SetIdentity` and `PreMultiply` are out of line in src/lib/vecmath.cpp.

`tools/check_cleanup.py` counts hand-written vector maths (two-product sums whose operands differ only in axis,
`float n[3]`-style members) and keeps it at 0; exceptions are listed in it (`_VEC_NOT_VECTOR`).
