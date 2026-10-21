#pragma once

namespace ei {

// pi / 180 as the original computes it: the product of two doubles of its .rdata (pi, and 1/180 at 0073BA30),
// rounded to 24 bits by the FPU on the main thread. The compiler folds a product of constants at 53 bits, so
// the rounding is written here (checked against exact arithmetic: S18e; tools/check_fp_fold.py finds a fold
// that was missed). Products with it that the original also does in sequence are rounded again where they are
// written (static_cast<float>(kRadiansPerDegree * 90.0)).
constexpr float kRadiansPerDegree = static_cast<float>(3.141592653589793 /*0073CAE8*/ * 0.005555555555555556 /*0073BA30*/);

} // namespace ei
