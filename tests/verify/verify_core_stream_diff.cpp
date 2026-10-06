// Checks for the save game's difference coding (core_stream_diff.cpp). Scenarios: world_load and
// ui_saveload (saving writes the AI graph's difference, loading applies it).
#include "verify.h"

#include "../../src/game/core_streams.h"

#include <cstring>
#include <new>

namespace {

// A scratch memory stream (the game's constructor and destructor bodies).
struct ScratchStream {
    alignas(CMemoryStream) unsigned char storage[sizeof(CMemoryStream)];
    CMemoryStream* stream;
    ScratchStream() {
        std::memset(storage, 0, sizeof(storage));   // Construct leaves some fields as they are
        stream = reinterpret_cast<CMemoryStream*>(storage)->Construct();
    }
    ~ScratchStream() { stream->Destruct(); }
};

bool SameBytes(const CMemoryStream* a, const CMemoryStream* b, std::uint32_t n) {
    return n == 0 || std::memcmp(a->data_, b->data_, n) == 0;
}

} // namespace

// 004A4DA0: SUB ESP,8; PUSH EBX; PUSH EBP (5 bytes). The original and ours into two scratch
// streams (both re-read `base` and `current` from the start), compared; then ours into the real one.
static void (__fastcall* s_writeDiff)(CStream*, void*, CStream*, CStream*);
static void __fastcall WriteDiffVerify(CStream* out, void*, CStream* base, CStream* current) {
    {
        ScratchStream expected, actual;
        s_writeDiff(expected.stream, nullptr, base, current);
        actual.stream->WriteDiff(base, current);
        std::uint32_t n = expected.stream->Position();
        ei::verify::Check("WriteDiff length", n, actual.stream->Position(), "%u");
        if (n == actual.stream->Position())
            ei::verify::Check("WriteDiff bytes", true, SameBytes(expected.stream, actual.stream, n), "%u bytes", n);
    }
    out->WriteDiff(base, current);
}
EI_VERIFY(0x004A4DA0, WriteDiffVerify, 5, s_writeDiff);

// 004A50D0: SUB ESP,0x108 (6 bytes). The original into a scratch stream, the difference stream put
// back, then ours into the real one; the rebuilt bytes and where the difference stream ends compared.
static void (__fastcall* s_applyDiff)(CMemoryStream*, void*, CStream*, CStream*);
static void __fastcall ApplyDiffVerify(CMemoryStream* out, void*, CStream* base, CStream* diff) {
    ScratchStream expected;
    std::uint32_t start = diff->Position();
    s_applyDiff(expected.stream, nullptr, base, diff);
    std::uint32_t expectedEnd = diff->Position();
    diff->Seek(start);
    out->ApplyDiff(base, diff);
    std::uint32_t n = expected.stream->Position();
    ei::verify::Check("ApplyDiff length", n, out->Position(), "%u");
    ei::verify::Check("ApplyDiff diff end", expectedEnd, diff->Position(), "%u");
    if (n == out->Position()) ei::verify::Check("ApplyDiff bytes", true, SameBytes(expected.stream, out, n), "%u bytes", n);
}
EI_VERIFY(0x004A50D0, ApplyDiffVerify, 6, s_applyDiff);
