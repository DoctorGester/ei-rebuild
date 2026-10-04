// Checks for the unit set's writer (src/game/world_unit_set.cpp): the original and ours each write
// into a recording stream (slot 2 appends the bytes), the bytes are compared, then ours writes
// to the real stream.
#include "verify.h"

#include "../../src/game/world.h"

#include <cstdint>
#include <vector>

namespace {

struct RecordingStream;
void __fastcall RecordWrite(RecordingStream* self, void*, const void* data, std::uint32_t size);

void* const kRecordingVtable[3] = {nullptr, nullptr, reinterpret_cast<void*>(&RecordWrite)};

struct RecordingStream {
    void* const* vtable = kRecordingVtable;
    std::vector<std::uint8_t> bytes;
};

void __fastcall RecordWrite(RecordingStream* self, void*, const void* data, std::uint32_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    self->bytes.insert(self->bytes.end(), p, p + size);
}

} // namespace

// 00559E60: SUB ESP,0x30; PUSH EBX; PUSH EBP (5 bytes).
using WriteFn = void(__fastcall*)(UnitSet*, void*, void*);
static WriteFn s_write;
static void __fastcall WriteVerify(UnitSet* set, void*, void* stream) {
    RecordingStream theirs;
    s_write(set, nullptr, &theirs);
    RecordingStream mine;
    set->Write(&mine);
    ei::verify::Check("UnitSet::Write", true, theirs.bytes == mine.bytes, "%zu vs %zu bytes", theirs.bytes.size(),
                      mine.bytes.size());
    set->Write(stream);
}
EI_VERIFY(0x00559E60, WriteVerify, 5, s_write);
