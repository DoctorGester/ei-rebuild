#pragma once

// The original's x87 maths in SSE2 code. From the D3D device's creation the original's main thread ran the FPU at
// 24-bit precision control (PC24): an x87 add, subtract, multiply or divide rounds its exact result once to a float
// significand, which float maths reproduces when both operands are floats. It does not when an operand is wider:
// a double constant (FLD qword keeps its 53 bits), or the result of FSIN, FCOS, FPATAN or FYL2X (64 bits, the precision
// control does not apply to them). These helpers give such an operation the original's float, with no x87 code.
//
// `Unrounded` is a value as the FPU held it before the next operation rounded it: a transcendental's 64-bit result
// (computed to about 100 bits, then rounded to 64 as the FPU does), a double constant, or a float. Round stores one as
// a float (FSTP dword); the operations take two of them, round the exact result once to 24 bits (ties to even), and
// store that:
//
//   FCOS of a, stored as a float                     ->  float c = pc24::Round(pc24::Cos(a))
//   FCOS of a, times radius, plus from.x             ->  pc24::Mul(pc24::Cos(a), radius) + from.x
//   FPATAN of y and x, minus heading                 ->  pc24::Sub(pc24::Atan2(y, x), heading)
//   x times the double constant 0.001                ->  pc24::Mul(x, 0.001)
//   VC6's inline exp of t                            ->  pc24::Exp(t)
//
// Exactness (the equivalence tests in docs/notes/cleanup-tooling.md, "The x87 files to SSE2"): an operation on floats and
// doubles is exact by construction (two-sum / two-product, then one rounding); Exp is VC6's sequence step by step;
// Sin, Cos, Log, Log10 and Exp give the FPU's float for every float argument; a following operation differs only where
// the FPU's own 64-bit result is off by an ulp next to a rounding boundary (9 of 200 M atan2 products tested).
// Arguments are floats (the callers' arguments are PC24 results); Sin, Cos and Atan2 take doubles, which a float
// converts to exactly, for the FPU's loads of a stored double. Results beyond a float's range (the FPU's registers
// keep a 15-bit exponent) give the float the store would.

namespace pc24 {

// hi + lo, |lo| at most half an ulp of hi: an x87 register's value before the precision control rounds it.
struct Unrounded {
    double hi = 0.0;
    double lo = 0.0;

    constexpr Unrounded() = default;
    constexpr Unrounded(double value) : hi(value) {}   // a float or a double constant, exactly
    constexpr Unrounded(double high, double low) : hi(high), lo(low) {}
};

// FSIN / FCOS: the argument reduced by the FPU's 66-bit pi, as the FPU does. |x| >= 2^63 gives x (the FPU's C2 case),
// an infinity the default NaN. A float, or a double loaded whole (FLD qword: the game CRT's arc cosine).
Unrounded Sin(double x);
Unrounded Cos(double x);

// FPATAN: atan2(y, x) in (-pi, pi], the signed zeros and infinities as IEEE's atan2. Floats, or doubles loaded whole
// (FSIN's and FCOS's results stored as doubles: ai_motion's turn); |y| and |x| below about 2^996.
Unrounded Atan2(double y, double x);

// FYL2X after FLDLN2 / FLDLG2: ln x and log10 x as VC6's inline log / log10 (the 64-bit ln 2 / log10 2 times log2 x).
Unrounded Log(float x);
Unrounded Log10(float x);

// VC6's inline exp at PC24: x times FLDL2E rounded to 24 bits, FRNDINT, F2XM1 of the fraction plus 1 rounded to 24
// bits, FSCALE.
float Exp(float x);

// The game CRT's pow at PC24 (_CIpow), in two steps. FYL2X: y log2 x rounded to 64 bits, for a positive finite
// x and a finite y (beyond 2^60 in size the product unrounded).
Unrounded Log2Times(double y, double x);
// 2^t as factor 2^exponent: the exponent t to the nearest integer (FRNDINT), the factor 2^(t - exponent) with the
// fraction and 1 + (2^f - 1) each rounded to 24 bits (FSUBR, F2XM1, FLD1 FADDP); FSCALE is the caller's. |t| < 2^31.
struct PowerOfTwo {
    float factor;
    int exponent;
};
PowerOfTwo TwoToThe(Unrounded t);

// A register stored as a float (FSTP dword: one rounding, also into the denormals).
float Round(Unrounded value);

// One x87 operation at PC24 on two register values, stored as a float.
float Add(Unrounded a, Unrounded b);
float Sub(Unrounded a, Unrounded b);
float Mul(Unrounded a, Unrounded b);
float Div(Unrounded a, Unrounded b);

} // namespace pc24
