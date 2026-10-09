// Check for the interface's saved state written (ui_interface_save.cpp): 005F85C0
// CInterface3D::SaveState. Side by side: the stream's Write (slot 2) is pointed at a recorder while
// the original and ours run, the byte sequences and chunk sizes are compared, then the original's
// bytes go to the real stream. Every save (a new game's "savea": every game scenario).
#include "verify.h"

#include "../../src/game/core_streams.h"
#include "../../src/game/ui.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
void* s_saveState;

struct Recording {
    std::vector<unsigned char> bytes;
    std::vector<std::uint32_t> chunks;
};
Recording* s_recording;

void __fastcall RecordWrite(void*, void*, const void* buffer, std::uint32_t size) {
    auto* p = static_cast<const unsigned char*>(buffer);
    s_recording->bytes.insert(s_recording->bytes.end(), p, p + size);
    s_recording->chunks.push_back(size);
}

// Runs `fn` with `stream`'s writes going into `into` (a copy of its vtable with slot 2 replaced).
template <typename Fn>
void Recorded(CStream* stream, Recording* into, Fn fn) {
    constexpr int kSlots = 8;
    void** real = *reinterpret_cast<void***>(stream);
    void* fake[kSlots];
    std::memcpy(fake, real, sizeof(fake));
    fake[2] = reinterpret_cast<void*>(&RecordWrite);
    s_recording = into;
    *reinterpret_cast<void***>(stream) = fake;
    fn();
    *reinterpret_cast<void***>(stream) = real;
    s_recording = nullptr;
}
} // namespace

static void __fastcall InterfaceSaveStateVerify(CInterface3D* self, void*, CStream* stream) {
    Recording expected, actual;
    Recorded(stream, &expected,
             [&] { reinterpret_cast<void(__fastcall*)(CInterface3D*, void*, CStream*)>(s_saveState)(self, nullptr, stream); });
    Recorded(stream, &actual, [&] { self->SaveState(stream); });
    const bool same = expected.bytes == actual.bytes && expected.chunks == actual.chunks;
    ei::verify::Check("CInterface3D::SaveState", true, same, "%u vs %u bytes", static_cast<unsigned>(expected.bytes.size()),
                      static_cast<unsigned>(actual.bytes.size()));
    std::size_t pos = 0;
    for (std::uint32_t size : expected.chunks) {
        stream->Write(expected.bytes.data() + pos, size);
        pos += size;
    }
}
// 005F85C0: PUSH ECX; PUSH EBX; MOV EBX,ECX; PUSH EBP; PUSH ESI (1 + 1 + 2 + 1 + 1 bytes).
EI_VERIFY(0x005F85C0, InterfaceSaveStateVerify, 6, s_saveState);
