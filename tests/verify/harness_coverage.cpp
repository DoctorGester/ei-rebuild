// Source coverage of a run (harness_coverage.h; docs/notes/harness.md, "Source coverage").
#include "harness_coverage.h"

#if defined(EI_COVERAGE)

#include "system/harness_system.h"
#include "platform/log.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <windows.h>   // VirtualProtect: this build is MSVC's only

// What MSVC's /fsanitize-coverage objects need from a runtime: each object refers to this symbol to pull in LLVM's
// sancov runtime, which would find the flags below and hand them to a fuzzer; this file reads them itself.
extern "C" {
int _SancovBoolFlagUsed = 0;
// The linker merges every object's .sancov$G..$I: the flag bytes, one per instrumented edge, each set by a
// `mov byte ptr [flag], 1` in the edge's code (tools/coverage.py finds those to place the flags). The compiler puts
// these two symbols (COMDAT, one kept) at the ends; the first 8 bytes are the start symbol's own section. (Its
// pc-table option, the edges' addresses, crashed the compiler on most files at /O2.)
extern unsigned char __sancov$BoolFlagStart;
extern unsigned char __sancov$BoolFlagEnd;
// The image's first byte (the linker's).
extern const unsigned char __ImageBase;
// One byte per 16 bytes of the image, set by _penter (harness_coverage_entry.asm) for the function that called it.
unsigned char g_coverageEntries[1u << 22];
// Nonzero once the image's code is writable: _penter then jumps each call of it over after the first.
unsigned char g_coveragePatch = 0;
}

namespace ei::verify::coverage {

namespace {

constexpr std::uint32_t kEntrySlots = 1u << 22;   // harness_coverage_entry.asm's ENTRY_SLOTS

char g_path[1024];   // the dump's path: kept in a plain array, which the exit handler can still read

std::uint32_t Rva(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(&__ImageBase));
}

std::uint32_t Read32(const unsigned char* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void WriteAtExit() { Write(); }

// The image's .text made writable for _penter's patches (until then each entry calls it every time).
void MakeCodeWritable() {
    const unsigned char* image = &__ImageBase;
    const unsigned char* nt = image + Read32(image + 0x3C);
    const unsigned sections = nt[6] | nt[7] << 8;                  // IMAGE_FILE_HEADER.NumberOfSections
    const unsigned char* section = nt + 24 + (nt[20] | nt[21] << 8);   // after the optional header
    for (unsigned i = 0; i < sections; ++i, section += 40) {
        if (std::memcmp(section, ".text\0\0\0", 8) != 0) continue;
        DWORD old = 0;
        if (VirtualProtect(const_cast<unsigned char*>(image) + Read32(section + 12), Read32(section + 8),
                           PAGE_EXECUTE_READWRITE, &old))
            g_coveragePatch = 1;
    }
}

const bool g_registered = [] {
    MakeCodeWritable();
    std::atexit(WriteAtExit);
    return true;
}();

} // namespace

// coverage.dat, little-endian 32-bit words after the 8-byte magic "EICOV1\0\0": the image's link time stamp and size
// (the PE header's: tools/coverage.py takes only dumps of the exe it reads), the RVAs of the two sancov symbols, the
// count of set flag bytes and each one's offset from __sancov$BoolFlagStart, the count of marked entry slots and each
// slot (an RVA / 16).
void Write() {
    if (!g_path[0]) {
        const std::string path = ei::OutputPath("coverage.dat");
        if (path.size() >= sizeof(g_path)) return;
        std::memcpy(g_path, path.c_str(), path.size() + 1);
    }
    const unsigned char* image = &__ImageBase;
    const unsigned char* nt = image + Read32(image + 0x3C);
    std::vector<std::uint32_t> words;
    words.push_back(Read32(nt + 8));         // IMAGE_FILE_HEADER.TimeDateStamp
    words.push_back(Read32(nt + 24 + 56));   // IMAGE_OPTIONAL_HEADER64.SizeOfImage
    words.push_back(Rva(&__sancov$BoolFlagStart));
    words.push_back(Rva(&__sancov$BoolFlagEnd));
    const unsigned char* flags = &__sancov$BoolFlagStart;
    const std::uint32_t flagCount = static_cast<std::uint32_t>(&__sancov$BoolFlagEnd - flags);
    const std::size_t flagsAt = words.size();
    words.push_back(0);
    for (std::uint32_t i = 0; i < flagCount; ++i)
        if (flags[i]) words.push_back(i);
    words[flagsAt] = static_cast<std::uint32_t>(words.size() - flagsAt - 1);
    const std::size_t entriesAt = words.size();
    words.push_back(0);
    for (std::uint32_t i = 0; i < kEntrySlots; ++i)
        if (g_coverageEntries[i]) words.push_back(i);
    words[entriesAt] = static_cast<std::uint32_t>(words.size() - entriesAt - 1);
    FILE* f = sys::OpenFile(g_path, "wb");
    if (!f) return;
    std::fwrite("EICOV1\0\0", 1, 8, f);
    std::fwrite(words.data(), sizeof(std::uint32_t), words.size(), f);
    std::fclose(f);
}

} // namespace ei::verify::coverage

#else

namespace ei::verify::coverage {

void Write() {}

} // namespace ei::verify::coverage

#endif
