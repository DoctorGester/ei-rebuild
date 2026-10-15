#include "random.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"

#include <bit>
#include <cstring>

std::uint32_t IsaacRandom::Next() {
    if (count_-- == 0) {
        Refill();
        count_ = 255;
    }
    return results_[count_];
}
EI_REPLACE_METHOD(0x004667F0, IsaacRandom::Next);

namespace {
// The 2^-32 scale of Range (0073C468); its last bit is set (sic).
constexpr double kRangeScale = std::bit_cast<double>(0x3DF0000000100000ull);

// A zero-extended dword loaded with FILD qword (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}
} // namespace

// 00488970 (moved here from effects.cpp so that the function vectors link it).
float IsaacRandom::Range(float min, float max) {
    return static_cast<float>(Unsigned(Next()) * (max - min) * kRangeScale + min);
}
EI_REPLACE_METHOD(0x00488970, IsaacRandom::Range);

void IsaacRandom::Refill() {
    // One ISAAC step: mix a, produce memory and result words. `ind` reads memory_ at the byte
    // offset (x & 0x3FC), i.e. index (x >> 2) & 255.
    auto ind = [this](std::uint32_t x) { return memory_[(x >> 2) & 0xFF]; };

    std::uint32_t a = a_;
    std::uint32_t b = b_ + ++c_;
    std::uint32_t* m = memory_;
    std::uint32_t* r = results_;

    auto step = [&](std::uint32_t mix, std::uint32_t*& m2) {
        std::uint32_t x = *m;
        a = mix + *m2++;
        std::uint32_t y = ind(x) + a + b;
        *m++ = y;
        b = ind(y >> 8) + x;
        *r++ = b;
    };

    std::uint32_t* m2 = memory_ + 128;
    while (m < memory_ + 128) {
        step(a ^ (a << 13), m2);
        step(a ^ (a >> 6), m2);
        step(a ^ (a << 2), m2);
        step(a ^ (a >> 16), m2);
    }
    m2 = memory_;
    while (m2 < memory_ + 128) {
        step(a ^ (a << 13), m2);
        step(a ^ (a >> 6), m2);
        step(a ^ (a << 2), m2);
        step(a ^ (a >> 16), m2);
    }
    b_ = b;
    a_ = a;
}
EI_REPLACE_METHOD(0x0049EE70, IsaacRandom::Refill);

namespace {

// The exe's search strings: the pointer at 0073D080 ("C:\*.*") and the sub-directory suffix 0078B054
// ("\*.*").
const char* SeedSearchPattern() { return *reinterpret_cast<const char* const*>(0x0073D080); }
const char* SubdirectoryPattern() { return reinterpret_cast<const char*>(0x0078B054); }

constexpr unsigned long kAttributeHidden = 0x02;      // FILE_ATTRIBUTE_HIDDEN
constexpr unsigned long kAttributeSystem = 0x04;      // FILE_ATTRIBUTE_SYSTEM
constexpr unsigned long kAttributeDirectory = 0x10;   // FILE_ATTRIBUTE_DIRECTORY
constexpr unsigned long kMinimumFileSize = 0x800;
constexpr unsigned kOpenReadShared = 0x8040;          // CFile modeRead | shareDenyNone | typeBinary
constexpr int kSeedBytes = 0x400;

// The original's 284-byte path buffer on its stack.
constexpr int kPathBuffer = 0x11C;

// rand() % 512 as the original computes it (rand() is never negative).
int RandomBelow512() { return gamecrt::rand() & 0x1FF; }

// CFileFind::MatchesMask through the vtable, as the original calls it.
BOOL Matches(const mfc::CFileFind& finder, unsigned long mask) {
    return reinterpret_cast<const mfc::CFileFindVirtuals*>(&finder)->MatchesMask(mask);
}

// MFC's result-slot calls (GetFilePath, operator+) write a CString into raw storage.
struct CStringSlot {
    alignas(mfc::CString) unsigned char bytes[sizeof(mfc::CString)];
    mfc::CString* Get() { return reinterpret_cast<mfc::CString*>(bytes); }
};

// 0049F660 and 0049F4B0 through their addresses, so their verify wrappers are reached from Init /
// Seed (the search's own recursion calls itself directly).
__declspec(naked) void __fastcall SeedAt(IsaacRandom*) EI_ORIGINAL(0x0049F660)
__declspec(naked) int __fastcall FindSeedFileAt(IsaacRandom*, void*, char* /*out*/, const char* /*pattern*/,
                                                int /*target*/, int* /*counter*/) EI_ORIGINAL(0x0049F4B0)

} // namespace

int IsaacRandom::FindSeedFile(char* out, const char* pattern, int target, int* counter) {
    mfc::CFileFind finder;
    BOOL more = finder.FindFile(pattern, 0);
    while (more) {
        more = CFileFindNext(&finder);
        if (CFileFindIsDotsOriginal(&finder)) continue;
        if (Matches(finder, kAttributeSystem) || Matches(finder, kAttributeHidden)) continue;
        if (Matches(finder, kAttributeDirectory)) {
            CStringSlot path, sub;
            CFileFindPathOriginal(&finder, nullptr, path.Get());
            CStringConcatTextOriginal(sub.Get(), path.Get(), SubdirectoryPattern());
            int found = FindSeedFile(out, *sub.Get(), target, counter);
            sub.Get()->~CString();
            path.Get()->~CString();
            if (found == 1) return 1;
            continue;
        }
        if (*counter >= target) {
            if (finder.GetLength() >= kMinimumFileSize) {
                CStringSlot path;
                CFileFindPathOriginal(&finder, nullptr, path.Get());
                const char* text = *path.Get();
                std::memcpy(out, text, std::strlen(text) + 1);   // strcpy, unbounded as in the original
                path.Get()->~CString();
                return 1;
            }
            --*counter;
        }
        ++*counter;
    }
    return 0;
}
EI_REPLACE_METHOD(0x0049F4B0, IsaacRandom::FindSeedFile);

// The original opens the file with CFile's throwing constructor inside TRY / CATCH(CFileException):
// a failed open, seek or read clears `ok`. Here Open reports the failure and the seek and read go to
// the handle directly (as CFile::Seek / Read do), so nothing throws. Kept: when the first search
// counts exactly one file, rand() % 0 faults as in the original.
void IsaacRandom::Seed() {
    char path[kPathBuffer];
    gamecrt::srand(gameapi::timeGetTime());
    int ok = 0;
    while (!ok) {
        int counter = 0;
        if (!FindSeedFileAt(this, nullptr, path, SeedSearchPattern(), RandomBelow512() + 1, &counter)) {
            int r = gamecrt::rand();
            int files = counter;
            counter = 0;
            if (!FindSeedFileAt(this, nullptr, path, SeedSearchPattern(), r % (files - 1) + 1, &counter)) continue;
        }
        ok = 1;
        mfc::CFile file;
        if (file.Open(path, kOpenReadShared, nullptr)) {
            gamecrt::srand(gameapi::timeGetTime());
            long offset = kSeedBytes - RandomBelow512();
            DWORD read = 0;
            if (gameapi::SetFilePointer(file.Handle(), offset, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
                !gameapi::ReadFile(file.Handle(), results_, kSeedBytes, &read, nullptr) || read != kSeedBytes)
                ok = 0;
            file.Close();
        } else {
            ok = 0;
        }
        bool zero = true;
        for (std::uint32_t word : results_)
            if (word != 0) {
                zero = false;
                break;
            }
        if (zero) {
            ok = 0;
            gameapi::Sleep(10);
        }
        for (std::uint32_t& word : results_) word ^= static_cast<std::uint32_t>(gamecrt::rand());
    }
}
EI_REPLACE_METHOD(0x0049F660, IsaacRandom::Seed);

namespace {

// The reference ISAAC mix of the eight randinit words.
struct InitMix {
    std::uint32_t a, b, c, d, e, f, g, h;

    void Mix() {
        a ^= b << 11; d += a; b += c;
        b ^= c >> 2;  e += b; c += d;
        c ^= d << 8;  f += c; d += e;
        d ^= e >> 16; g += d; e += f;
        e ^= f << 10; h += e; f += g;
        f ^= g >> 4;  a += f; g += h;
        g ^= h << 8;  b += g; h += a;
        h ^= a >> 9;  c += h; a += b;
    }

    // One pass over memory: add eight words of `source`, mix, store into memory.
    void Pass(const std::uint32_t* source, std::uint32_t* memory) {
        for (int i = 0; i < 256; i += 8) {
            a += source[i]; b += source[i + 1]; c += source[i + 2]; d += source[i + 3];
            e += source[i + 4]; f += source[i + 5]; g += source[i + 6]; h += source[i + 7];
            Mix();
            memory[i] = a; memory[i + 1] = b; memory[i + 2] = c; memory[i + 3] = d;
            memory[i + 4] = e; memory[i + 5] = f; memory[i + 6] = g; memory[i + 7] = h;
        }
    }
};

} // namespace

void IsaacRandom::Init() {
    if (seeded_ != 0) return;
    SeedAt(this);
    c_ = 0;
    b_ = 0;
    a_ = 0;
    constexpr std::uint32_t kGolden = 0x9E3779B9;
    InitMix m{kGolden, kGolden, kGolden, kGolden, kGolden, kGolden, kGolden, kGolden};
    for (int i = 0; i < 4; ++i) m.Mix();
    m.Pass(results_, memory_);
    m.Pass(memory_, memory_);
    Refill();
    count_ = 256;
    seeded_ = 1;
}
EI_REPLACE_METHOD(0x0049F150, IsaacRandom::Init);
