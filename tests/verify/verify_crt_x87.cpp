// Start-up battery for the CRT's register-argument x87 entries (src/lib/crt_x87.cpp, S4c), against the
// originals: the ports are registered for the standalone image only, so the hook build still has the
// originals at their addresses. Each case runs under five control words (precision 24 / 53 / 64 bits,
// rounding to nearest, toward zero, up) and checks that the entry leaves the FPU stack depth and the
// control word as it found them.
// - _ftol: 80-bit values of every class (zeros, denormals, the 2^63 edges, infinities, NaNs, random
//   mantissas and exponents): EDX:EAX bit for bit.
// - _CIfmod: bit for bit (FPREM is exact) on random doubles and floats and every special pair.
// - acos, asin, fmod (the stack-argument forms, S16): the same cases as their _CI forms.
// - _hypot (S16a): bit for bit on every special pair, random doubles of every size, floats as the game
//   passes them, the overflow and denormal edges.
// - _CIacos, _CIasin, _CIpow: NaNs, zeros and every result under the main thread's control word (007F, the
//   float its callers store) bit for bit; the rest within a tolerance (CompareResult).
// Every scenario runs it (WinMain entry).
#include "verify.h"

#include "../../src/hook/log.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using ei::verify::Check;

constexpr std::uintptr_t kFtol = 0x006E853C;
constexpr std::uintptr_t kAcos = 0x006E7320;
constexpr std::uintptr_t kAsin = 0x006E83C0;
constexpr std::uintptr_t kPow = 0x006E81A0;
constexpr std::uintptr_t kFmod = 0x006E95AA;
// The stack-argument forms (cdecl; S16).
constexpr std::uintptr_t kStackAcos = 0x006E7334;
constexpr std::uintptr_t kStackAsin = 0x006E83D4;
constexpr std::uintptr_t kStackFmod = 0x006E95A0;
// _hypot (cdecl; S16a).
constexpr std::uintptr_t kHypot = 0x006FD026;

struct Extended {
    unsigned char bytes[10];
};

__declspec(naked) unsigned short __cdecl ControlWord() {
    __asm {
        push eax
        fnstcw word ptr [esp]
        pop eax
        and eax, 0xFFFF
        ret
    }
}

__declspec(naked) void __cdecl SetControlWord(unsigned) {
    __asm {
        fldcw word ptr [esp + 4]
        ret
    }
}

__declspec(naked) unsigned short __cdecl StatusWord() {
    __asm {
        fnstsw ax
        and eax, 0xFFFF
        ret
    }
}

// ST0 = *value, then the entry; EDX:EAX back.
__declspec(naked) std::uint64_t __cdecl CallFtol(void* /*entry*/, const Extended* /*value*/) {
    __asm {
        mov eax, [esp + 8]
        fld tbyte ptr [eax]
        mov eax, [esp + 4]
        call eax
        ret
    }
}

// ST0 = x, then the entry; ST0 into *result.
__declspec(naked) void __cdecl CallUnary(void* /*entry*/, double /*x*/, Extended* /*result*/) {
    __asm {
        fld qword ptr [esp + 8]
        mov eax, [esp + 4]
        call eax
        mov eax, [esp + 16]
        fstp tbyte ptr [eax]
        ret
    }
}

// x on the stack (cdecl), then the function; ST0 into *result.
__declspec(naked) void __cdecl CallStackUnary(void* /*function*/, double /*x*/, Extended* /*result*/) {
    __asm {
        sub esp, 8
        mov eax, [esp + 16]
        mov [esp], eax
        mov eax, [esp + 20]
        mov [esp + 4], eax
        mov eax, [esp + 12]
        call eax
        add esp, 8
        mov eax, [esp + 16]
        fstp tbyte ptr [eax]
        ret
    }
}

// x and y on the stack (cdecl), then the function; ST0 into *result.
__declspec(naked) void __cdecl CallStackBinary(void* /*function*/, double /*x*/, double /*y*/, Extended* /*result*/) {
    __asm {
        sub esp, 16
        mov eax, [esp + 24]
        mov [esp], eax
        mov eax, [esp + 28]
        mov [esp + 4], eax
        mov eax, [esp + 32]
        mov [esp + 8], eax
        mov eax, [esp + 36]
        mov [esp + 12], eax
        mov eax, [esp + 20]
        call eax
        add esp, 16
        mov eax, [esp + 24]
        fstp tbyte ptr [eax]
        ret
    }
}

// ST1 = x, ST0 = y, then the entry; ST0 into *result.
__declspec(naked) void __cdecl CallBinary(void* /*entry*/, double /*x*/, double /*y*/, Extended* /*result*/) {
    __asm {
        fld qword ptr [esp + 8]
        fld qword ptr [esp + 16]
        mov eax, [esp + 4]
        call eax
        mov eax, [esp + 24]
        fstp tbyte ptr [eax]
        ret
    }
}

// An 80-bit value as a caller stores it (FSTP qword / dword, rounding to nearest).
__declspec(naked) void __cdecl StoreExtended(const Extended* /*value*/, double* /*asDouble*/, float* /*asFloat*/) {
    __asm {
        mov eax, [esp + 4]
        fld tbyte ptr [eax]
        mov eax, [esp + 8]
        fst qword ptr [eax]
        mov eax, [esp + 12]
        fstp dword ptr [eax]
        ret
    }
}

std::uint64_t Bits(double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
double FromBits(std::uint64_t bits) {
    double value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}
float FloatFromBits(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

struct Lcg {
    std::uint64_t state;
    std::uint32_t Next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>(state >> 32);
    }
    std::uint64_t Next64() { return static_cast<std::uint64_t>(Next()) << 32 | Next(); }
    // Uniform in [low, high).
    double Uniform(double low, double high) { return low + (high - low) * (Next() / 4294967296.0); }
};

// The control words the cases run under: the game's main thread (24 bits), other threads (53), 64 bits,
// and toward zero / up (the terrain lighting switches to chop).
constexpr unsigned kControlWords[] = {0x007F, 0x027F, 0x037F, 0x0C7F, 0x0A7F};

// The FPU stack depth (TOP) and the control word after a call must be as before.
struct FpuState {
    unsigned short control;
    unsigned short top;
};
FpuState State() { return {ControlWord(), static_cast<unsigned short>((StatusWord() >> 11) & 7)}; }

int s_cases = 0;
int s_nearResults = 0;   // transcendental results compared within a tolerance that are not bit-equal

void CheckState(const char* name, const FpuState& before, const FpuState& original, const FpuState& ours) {
    Check(name, before.control, original.control, "original changed the control word %04X to %04X", before.control,
          original.control);
    Check(name, before.control, ours.control, "control word %04X left as %04X", before.control, ours.control);
    Check(name, original.top, ours.top, "FPU stack top %u, ours %u", original.top, ours.top);
}

void FtolCase(const Extended& value, unsigned controlWord) {
    SetControlWord(controlWord);
    const FpuState before = State();
    const std::uint64_t expected = CallFtol(reinterpret_cast<void*>(kFtol), &value);
    const FpuState afterOriginal = State();
    SetControlWord(controlWord);
    const std::uint64_t actual = CallFtol(reinterpret_cast<void*>(&libcrt::FtolEntry), &value);
    const FpuState afterOurs = State();
    SetControlWord(0x027F);
    std::uint64_t mantissa;
    std::uint16_t top;
    std::memcpy(&mantissa, value.bytes, 8);
    std::memcpy(&top, value.bytes + 8, 2);
    Check("_ftol", expected, actual, "%04X:%016llX cw %04X: %016llX, ours %016llX", top, mantissa, controlWord, expected,
          actual);
    CheckState("_ftol", before, afterOriginal, afterOurs);
    ++s_cases;
}

Extended MakeExtended(bool negative, int exponent, std::uint64_t mantissa) {
    Extended value;
    std::memcpy(value.bytes, &mantissa, 8);
    const std::uint16_t top = static_cast<std::uint16_t>((negative ? 0x8000 : 0) | (exponent & 0x7FFF));
    std::memcpy(value.bytes + 8, &top, 2);
    return value;
}

void FtolBattery() {
    std::vector<Extended> values;
    const std::uint64_t kTop = 0x8000000000000000ull;
    for (bool negative : {false, true}) {
        values.push_back(MakeExtended(negative, 0, 0));                       // zero
        values.push_back(MakeExtended(negative, 0, 0x123456789ull));          // denormal
        values.push_back(MakeExtended(negative, 0x7FFF, kTop));               // infinity
        values.push_back(MakeExtended(negative, 0x7FFF, kTop | 1));           // signalling NaN
        values.push_back(MakeExtended(negative, 0x7FFF, 0xC000000000000000ull)); // quiet NaN
        for (int whole = -3; whole <= 66; ++whole) {
            values.push_back(MakeExtended(negative, 16383 + whole, kTop));            // 2^whole
            values.push_back(MakeExtended(negative, 16383 + whole, ~0ull));           // just below 2^(whole + 1)
            values.push_back(MakeExtended(negative, 16383 + whole, kTop | 1));        // just above 2^whole
            values.push_back(MakeExtended(negative, 16383 + whole, 0xC000000000000000ull)); // 1.5 * 2^whole
        }
    }
    Lcg random{1234};
    for (int i = 0; i < 1500; ++i) {
        const int exponent = 16383 - 4 + static_cast<int>(random.Next() % 72);
        values.push_back(MakeExtended(random.Next() & 1, exponent, random.Next64() | kTop));
    }
    for (double d : {0.5, 0.49999999999999994, 1.5, 2.5, -2.5, 2147483647.5, 4294967295.9, 1e19, -9.2233720368547758e18}) {
        Extended value;
        __asm {
            fld d
            fstp tbyte ptr value
        }
        values.push_back(value);
    }
    for (const Extended& value : values) {
        for (unsigned controlWord : kControlWords) FtolCase(value, controlWord);
    }
}

// The result of a transcendental. Bit for bit: `exact` (fmod), NaNs and zeros, and every result under the
// game's main-thread control word (007F: 24 bits, to nearest), as the caller stores it there (a float).
// Otherwise within `ulps` units in the last place of a double: at 53 and 64 bits the original keeps
// FYL2X / F2XM1 / FPATAN's 64-bit values where the port has doubles (pow's double log2 of the mantissa
// costs it about |y| / 4 units), and in the denormal range the original's underflow handling (_87except)
// may flush the result.
void CompareResult(const char* name, const Extended& expected, const Extended& actual, unsigned controlWord, bool exact,
                   int ulps, const char* arguments) {
    double expectedDouble, actualDouble;
    float expectedFloat, actualFloat;
    StoreExtended(&expected, &expectedDouble, &expectedFloat);
    StoreExtended(&actual, &actualDouble, &actualFloat);
    const std::uint64_t e = Bits(expectedDouble);
    const std::uint64_t a = Bits(actualDouble);
    const auto exponent = [](std::uint64_t bits) { return static_cast<unsigned>(bits >> 52) & 0x7FF; };
    const auto isNan = [&](std::uint64_t bits) { return exponent(bits) == 0x7FF && (bits << 12) != 0; };
    const bool denormal = (exponent(e) == 0 && e << 1 != 0) || (exponent(a) == 0 && a << 1 != 0);
    if (exact || isNan(e) || isNan(a) || ((e << 1 == 0 || a << 1 == 0) && !denormal)) {
        Check(name, e, a, "%s cw %04X: %016llX, ours %016llX", arguments, controlWord, e, a);
        return;
    }
    if (controlWord == 0x007F && !denormal) {
        Check(name, Bits(expectedFloat), Bits(actualFloat), "%s cw %04X: %08X, ours %08X", arguments, controlWord,
              Bits(expectedFloat), Bits(actualFloat));
        return;
    }
    if (e != a) ++s_nearResults;
    const bool near = ei::verify::NearlyEqual(expectedDouble, actualDouble, ulps, denormal ? 1e-300 : 0.0);
    char detail[256] = "";
    if (!near) {
        std::snprintf(detail, sizeof detail, "%s cw %04X: %016llX, ours %016llX (%d units allowed)", arguments,
                      controlWord, e, a, ulps);
    }
    ei::verify::Report(name, near, detail);
}

// A function's code address, as the callers take it.
template <typename F>
void* Code(F function) {
    return reinterpret_cast<void*>(function);
}

using UnaryCaller = void(__cdecl*)(void*, double, Extended*);
using BinaryCaller = void(__cdecl*)(void*, double, double, Extended*);

void UnaryCase(const char* name, std::uintptr_t original, void* ours, double x, unsigned controlWord, bool exact,
               UnaryCaller call = &CallUnary) {
    Extended expected, actual;
    SetControlWord(controlWord);
    const FpuState before = State();
    call(reinterpret_cast<void*>(original), x, &expected);
    const FpuState afterOriginal = State();
    SetControlWord(controlWord);
    call(ours, x, &actual);
    const FpuState afterOurs = State();
    SetControlWord(0x027F);
    char arguments[64];
    std::snprintf(arguments, sizeof arguments, "%016llX (%.17g)", Bits(x), x);
    CompareResult(name, expected, actual, controlWord, exact, 4, arguments);
    CheckState(name, before, afterOriginal, afterOurs);
    ++s_cases;
}

void BinaryCase(const char* name, std::uintptr_t original, void* ours, double x, double y, unsigned controlWord,
                bool exact, BinaryCaller call = &CallBinary) {
    Extended expected, actual;
    SetControlWord(controlWord);
    const FpuState before = State();
    call(reinterpret_cast<void*>(original), x, y, &expected);
    const FpuState afterOriginal = State();
    SetControlWord(controlWord);
    call(ours, x, y, &actual);
    const FpuState afterOurs = State();
    SetControlWord(0x027F);
    char arguments[96];
    std::snprintf(arguments, sizeof arguments, "%016llX %016llX (%.17g, %.17g)", Bits(x), Bits(y), x, y);
    const double size = y < 0.0 ? -y : y;
    const int ulps = size < 1e6 ? 4 + static_cast<int>(size / 2.0) : 500000;
    CompareResult(name, expected, actual, controlWord, exact, ulps, arguments);
    CheckState(name, before, afterOriginal, afterOurs);
    ++s_cases;
}

std::vector<double> SpecialValues() {
    std::vector<double> values;
    for (std::uint64_t bits : {0x0ull, 0x1ull, 0x000FFFFFFFFFFFFFull, 0x7FF0000000000000ull, 0x7FF0000000000001ull,
                               0x7FF8000000000000ull, 0x7FF8000000000123ull, 0x3FF0000000000000ull, 0x3FF0000000000001ull,
                               0x3FEFFFFFFFFFFFFFull, 0x4000000000000000ull, 0x3FE0000000000000ull, 0x7FEFFFFFFFFFFFFFull,
                               0x0010000000000000ull}) {
        values.push_back(FromBits(bits));
        values.push_back(FromBits(bits | 0x8000000000000000ull));
    }
    return values;
}

void InverseTrigBattery() {
    std::vector<double> ordinary;
    Lcg random{99};
    for (int i = 0; i < 600; ++i) ordinary.push_back(random.Uniform(-1.0, 1.0));
    for (int i = 0; i < 600; ++i) ordinary.push_back(static_cast<float>(random.Uniform(-1.0, 1.0)));
    for (int k = 1; k <= 40; ++k) {
        ordinary.push_back(1.0 - k * 5.9604644775390625e-08);   // floats just below 1
        ordinary.push_back(-1.0 + k * 5.9604644775390625e-08);
        ordinary.push_back(k * 1e-5);
    }
    for (unsigned controlWord : kControlWords) {
        for (double x : SpecialValues()) {
            UnaryCase("_CIacos", kAcos, Code(&libcrt::CiAcosEntry), x, controlWord, false);
            UnaryCase("_CIasin", kAsin, Code(&libcrt::CiAsinEntry), x, controlWord, false);
            UnaryCase("acos", kStackAcos, Code(&libcrt::CiAcos), x, controlWord, false, &CallStackUnary);
            UnaryCase("asin", kStackAsin, Code(&libcrt::CiAsin), x, controlWord, false, &CallStackUnary);
        }
        for (double x : ordinary) {
            UnaryCase("_CIacos", kAcos, Code(&libcrt::CiAcosEntry), x, controlWord, false);
            UnaryCase("_CIasin", kAsin, Code(&libcrt::CiAsinEntry), x, controlWord, false);
            UnaryCase("acos", kStackAcos, Code(&libcrt::CiAcos), x, controlWord, false, &CallStackUnary);
            UnaryCase("asin", kStackAsin, Code(&libcrt::CiAsin), x, controlWord, false, &CallStackUnary);
        }
    }
}

void PowBattery() {
    const std::vector<double> specials = SpecialValues();
    std::vector<double> bases = specials;
    std::vector<double> exponents = specials;
    for (double v : {2.0, 3.0, -3.0, 0.5, -0.5, 2.5, 1e300, -7.0, 1.0000001, 4503599627370497.0, -4503599627370497.0}) {
        bases.push_back(v);
        exponents.push_back(v);
    }
    for (unsigned controlWord : kControlWords) {
        for (double x : bases) {
            for (double y : exponents) BinaryCase("_CIpow", kPow, Code(&libcrt::CiPowEntry), x, y, controlWord, false);
        }
    }
    Lcg random{4321};
    for (unsigned controlWord : kControlWords) {
        for (int i = 0; i < 300; ++i) {
            const double x = static_cast<float>(random.Uniform(0.0, 100.0));
            const double y = static_cast<float>(random.Uniform(-10.0, 10.0));
            BinaryCase("_CIpow", kPow, Code(&libcrt::CiPowEntry), x, y, controlWord, false);
        }
        for (int i = 0; i < 200; ++i) {
            const double x = random.Uniform(-50.0, 50.0);
            const double y = static_cast<double>(static_cast<int>(random.Next() % 41) - 20);
            BinaryCase("_CIpow", kPow, Code(&libcrt::CiPowEntry), x, y, controlWord, false);
        }
        for (int i = 0; i < 100; ++i) {   // overflow, underflow and denormal results
            const double x = random.Uniform(0.5, 4.0);
            const double y = random.Uniform(-1200.0, 1200.0);
            BinaryCase("_CIpow", kPow, Code(&libcrt::CiPowEntry), x, y, controlWord, false);
        }
        for (int i = 0; i < 100; ++i) {   // the camera's zoom steps and the map's 2.5^(delta / 1200)
            BinaryCase("_CIpow", kPow, Code(&libcrt::CiPowEntry), 2.5, static_cast<float>(random.Uniform(-3.0, 3.0)),
                       controlWord, false);
        }
    }
}

void FmodBattery() {
    const std::vector<double> specials = SpecialValues();
    Lcg random{555};
    for (unsigned controlWord : kControlWords) {
        for (double x : specials) {
            for (double y : specials) {
                BinaryCase("_CIfmod", kFmod, Code(&libcrt::CiFmodEntry), x, y, controlWord, true);
                BinaryCase("fmod", kStackFmod, Code(&libcrt::StackFmod), x, y, controlWord, true, &CallStackBinary);
            }
        }
        for (int i = 0; i < 400; ++i) {
            const double x = random.Uniform(-1e6, 1e6);
            const double y = random.Uniform(-100.0, 100.0);
            BinaryCase("_CIfmod", kFmod, Code(&libcrt::CiFmodEntry), x, y, controlWord, true);
            BinaryCase("fmod", kStackFmod, Code(&libcrt::StackFmod), x, y, controlWord, true, &CallStackBinary);
            BinaryCase("_CIfmod", kFmod, Code(&libcrt::CiFmodEntry), static_cast<float>(x), static_cast<float>(y) * 0.01,
                       controlWord, true);
            BinaryCase("_CIfmod", kFmod, Code(&libcrt::CiFmodEntry), FromBits(random.Next64()), FromBits(random.Next64()),
                       controlWord, true);
        }
    }
}

void HypotBattery() {
    std::vector<double> specials = SpecialValues();
    for (double v : {3.0, -4.0, 1e300, -1e-300, 1.3407807929942596e154, 1e-310, 2.2250738585072014e-308})
        specials.push_back(v);
    Lcg random{8080};
    for (unsigned controlWord : kControlWords) {
        for (double x : specials) {
            for (double y : specials)
                BinaryCase("_hypot", kHypot, Code(&libcrt::Hypot), x, y, controlWord, true, &CallStackBinary);
        }
        for (int i = 0; i < 300; ++i) {
            const double x = static_cast<float>(random.Uniform(-5000.0, 5000.0));
            const double y = static_cast<float>(random.Uniform(-5000.0, 5000.0));
            BinaryCase("_hypot", kHypot, Code(&libcrt::Hypot), x, y, controlWord, true, &CallStackBinary);
            BinaryCase("_hypot", kHypot, Code(&libcrt::Hypot), x * 1e-3, y, controlWord, true, &CallStackBinary);
            BinaryCase("_hypot", kHypot, Code(&libcrt::Hypot), FromBits(random.Next64()), FromBits(random.Next64()),
                       controlWord, true, &CallStackBinary);
        }
        for (int i = 0; i < 100; ++i) {   // results near the largest double and in the denormal range
            const double scale = (i & 1) ? 1.7e308 : 2.5e-308;
            BinaryCase("_hypot", kHypot, Code(&libcrt::Hypot), random.Uniform(-1.0, 1.0) * scale,
                       random.Uniform(-1.0, 1.0) * scale, controlWord, true, &CallStackBinary);
            BinaryCase("_hypot", kHypot, Code(&libcrt::Hypot), random.Uniform(0.0, 1.0) * 4.9e-322,
                       random.Uniform(0.0, 1.0) * 1e-320, controlWord, true, &CallStackBinary);
        }
    }
}

void CrtX87StartupCheck() {
    const unsigned saved = ControlWord();
    s_cases = 0;
    s_nearResults = 0;
    FtolBattery();
    FmodBattery();
    InverseTrigBattery();
    PowBattery();
    HypotBattery();
    SetControlWord(saved);
    ei::Log("crt x87: %d case(s) against the originals; %d result(s) at 53 / 64 bits or denormal within the "
            "tolerance but not bit-equal", s_cases, s_nearResults);
}
EI_STARTUP_CHECK(CrtX87StartupCheck);

} // namespace
