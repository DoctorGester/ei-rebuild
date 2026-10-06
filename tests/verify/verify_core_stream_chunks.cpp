// Checks for core_stream_chunks.cpp, each against the original: the original runs first (into
// scratch outputs), the stream is sought back to where it was, then ours runs on the real
// outputs. Scenarios: world_load / ui_saveload (the save's tagged blocks, FindTagged), newgame and
// world_client (CPlayerClient::OnNewObject reads names with ReadText).
#include "verify.h"

#include "../../src/game/core_streams.h"

#include <cstring>
#include <vector>

namespace {

struct ScratchStream {
    alignas(CMemoryStream) unsigned char storage[sizeof(CMemoryStream)];
    CMemoryStream* stream;
    ScratchStream() {
        std::memset(storage, 0, sizeof(storage));   // Construct leaves some fields as they are
        stream = reinterpret_cast<CMemoryStream*>(storage)->Construct();
    }
    ~ScratchStream() { stream->Destruct(); }
};

bool IsMemoryStream(CStream* s) { return *reinterpret_cast<std::uintptr_t*>(s) == CMemoryStream::kVtableAddress; }

// The memory stream `actual` holds what `expected` holds.
void CompareOutput(const char* name, CMemoryStream* expected, CStream* actual) {
    ei::verify::Check(name, expected->Size(), actual->Size(), "size %u");
    if (!IsMemoryStream(actual) || expected->Size() != actual->Size()) return;
    auto* m = static_cast<CMemoryStream*>(actual);
    bool same = expected->Size() == 0 || std::memcmp(expected->data_, m->data_, expected->Size()) == 0;
    ei::verify::Check(name, true, same, "bytes");
}

} // namespace

// 004A4940: PUSH ECX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,ECX (6 bytes).
static bool (__fastcall* s_readTagged)(CStream*, void*, std::uint32_t*, CStream*);
static bool __fastcall ReadTaggedVerify(CStream* stream, void*, std::uint32_t* tag, CStream* out) {
    std::uint32_t start = stream->Position();
    ScratchStream scratch;
    std::uint32_t expectedTag = *tag;
    bool expected = s_readTagged(stream, nullptr, &expectedTag, scratch.stream);
    std::uint32_t expectedEnd = stream->Position();
    stream->Seek(start);
    bool actual = stream->ReadTagged(tag, out);
    ei::verify::Check("ReadTagged", expected, actual, "%d");
    ei::verify::Check("ReadTagged tag", expectedTag, *tag, "%08X");
    ei::verify::Check("ReadTagged end", expectedEnd, stream->Position(), "%u");
    if (expected && actual) CompareOutput("ReadTagged out", scratch.stream, out);
    return actual;
}
EI_VERIFY(0x004A4940, ReadTaggedVerify, 6, s_readTagged);

// 004A4A90: PUSH ECX; PUSH EBX; PUSH ESI; MOV ESI,ECX (5 bytes). Both start from the stream's start.
static bool (__fastcall* s_findTagged)(CStream*, void*, std::uint32_t, CStream*);
static bool __fastcall FindTaggedVerify(CStream* stream, void*, std::uint32_t id, CStream* out) {
    ScratchStream scratch;
    bool expected = s_findTagged(stream, nullptr, id, scratch.stream);
    std::uint32_t expectedEnd = stream->Position();
    bool actual = stream->FindTagged(id, out);
    ei::verify::Check("FindTagged", expected, actual, "%d");
    ei::verify::Check("FindTagged end", expectedEnd, stream->Position(), "%u");
    CompareOutput("FindTagged out", scratch.stream, out);
    return actual;
}
EI_VERIFY(0x004A4A90, FindTaggedVerify, 5, s_findTagged);

// 004A48F0: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,[ESP+0x18] (8 bytes).
static void (__fastcall* s_readText)(CStream*, void*, char*, int);
static void __fastcall ReadTextVerify(CStream* stream, void*, char* buffer, int capacity) {
    if (capacity <= 0) {
        stream->ReadText(buffer, capacity);
        return;
    }
    std::uint32_t start = stream->Position();
    std::vector<char> expected(buffer, buffer + capacity);
    s_readText(stream, nullptr, expected.data(), capacity);
    std::uint32_t expectedEnd = stream->Position();
    stream->Seek(start);
    stream->ReadText(buffer, capacity);
    ei::verify::Check("ReadText end", expectedEnd, stream->Position(), "%u");
    ei::verify::Check("ReadText", 0, std::strcmp(expected.data(), buffer), "%d");
}
EI_VERIFY(0x004A48F0, ReadTextVerify, 8, s_readText);
