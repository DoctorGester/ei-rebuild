// lib/maths.h's functions at inputs where a system's own float function rounds otherwise (docs/notes/float-maths.md,
// "Maths functions"): the first inputs of an exhaustive comparison at which Windows' UCRT (first of each pair) or
// glibc (second) is an ulp off; the answers are the double functions' rounded to float, the same on both systems.
#include "../../src/lib/maths.h"

#include <bit>
#include <cstdint>
#include <cstdio>

namespace {

int g_failures = 0;

void Expect(const char* what, float got, float want) {
    if (std::bit_cast<std::uint32_t>(got) == std::bit_cast<std::uint32_t>(want)) return;
    std::printf("maths: %s gave %a, not %a\n", what, static_cast<double>(got), static_cast<double>(want));
    ++g_failures;
}

} // namespace

int RunMathsChecks() {
    Expect("Sin", lib::Sin(0x1.0a4030p-10f), 0x1.0a402ep-10f);
    Expect("Sin", lib::Sin(0x1.d12ed2p-12f), 0x1.d12ed0p-12f);
    Expect("Cos", lib::Cos(0x1.a8872ap-11f), 0x1.fffff6p-1f);
    Expect("Cos", lib::Cos(0x1.1e377ap-11f), 0x1.fffffap-1f);
    Expect("Acos", lib::Acos(-0x1.80d99ap-7f), 0x1.95216cp+0f);
    Expect("Acos", lib::Acos(0x1.110b3cp-26f), 0x1.921fb6p+0f);
    Expect("Asin", lib::Asin(0x1.d12ed0p-12f), 0x1.d12ed2p-12f);
    Expect("Asin", lib::Asin(0x1.d12e9ep-12f), 0x1.d12e9ep-12f);
    Expect("Exp", lib::Exp(0x1.8d7cb6p-12f), 0x1.0018dap+0f);
    Expect("Exp", lib::Exp(0x1.fefe02p-16f), 0x1.0001fep+0f);
    Expect("Log", lib::Log(0x1.16e480p-132f), -0x1.6da39ep+6f);
    Expect("Log", lib::Log(0x1.223800p-136f), -0x1.7891fcp+6f);
    Expect("Log10", lib::Log10(0x1.420000p-141f), -0x1.52c3d2p+5f);
    Expect("Atan2", lib::Atan2(0x1.12f2eap-10f, 0x1.6a956ep+3f), 0x1.84407ep-14f);
    Expect("Atan2", lib::Atan2(-0x1.8cef28p-12f, -0x1.785648p-15f), -0x1.b05290p+0f);
    Expect("Pow", lib::Pow(0x1.38edecp-7f, 0x1.3e1a96p+2f), 0x1.90f2f4p-34f);
    Expect("Pow", lib::Pow(0x1.b663a4p+4f, -0x1.6181e8p-6f), 0x1.dcb434p-1f);
    // Correctly rounded (the true value is 9.6e-15 nearer the lower float); glibc's hypot rounded to float is not.
    Expect("Hypot", lib::Hypot(-0x1.eee7ecp-6f, -0x1.de61f2p+6f), 0x1.de61f2p+6f);
    std::printf("ei_unit_check: maths: 18 checks, %d failed\n", g_failures);
    return g_failures;
}
