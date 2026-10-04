// CInterface3D's saved state (005F8420). See ui.h.
#include "ui.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstdint>

namespace {

// The save stream (thiscall): slot 1 reads `size` bytes; +0x08 is the save's version.
void Read(void* stream, void* to, int size) {
    using Fn = void(__thiscall*)(void*, void*, int);
    reinterpret_cast<Fn>((*static_cast<void***>(stream))[1])(stream, to, size);
}
int Version(const void* stream) { return *reinterpret_cast<const int*>(static_cast<const std::uint8_t*>(stream) + 8); }

// 00461190 (thiscall on the stream, RET 4): reads a CString into `out` and returns it.
__declspec(naked) mfc::CString* __fastcall ReadString(void*, void*, mfc::CString*) EI_ORIGINAL(0x00461190)

// A string read into a temporary (ReadString constructs it), assigned to `field`
// (CString::operator=, 00700AA9), then destroyed.
void ReadInto(void* stream, mfc::CString* field) {
    alignas(4) std::uint8_t storage[sizeof(mfc::CString)];
    auto* text = reinterpret_cast<mfc::CString*>(storage);
    *field = *ReadString(stream, nullptr, text);
    text->~CString();
}

} // namespace

void CInterface3D::LoadState(void* stream) {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    Read(stream, self + 0x89, 1);
    Read(stream, self + 0x8A, 1);
    Read(stream, self + 0x8B, 1);
    Read(stream, self + 0x8C, 4);
    Read(stream, self + 0x90, 4);
    if (Version(stream) < 0x47) return;
    Read(stream, self + 0x94, 4);
    if (Version(stream) < 0x56) return;
    // An obsolete list: each name and value read and dropped (the last name is kept until the
    // end, as the original's local CString).
    mfc::CString last;
    int count;
    Read(stream, &count, 4);
    for (int i = 0; i < count; ++i) {
        ReadInto(stream, &last);
        int value;
        Read(stream, &value, 4);
    }
    ReadInto(stream, reinterpret_cast<mfc::CString*>(self + 0xA4));
    if (Version(stream) >= 0x5D) {
        Read(stream, self + 0xA8, 4);
        if (Version(stream) >= 0x5F) ReadInto(stream, reinterpret_cast<mfc::CString*>(self + 0xAC));
    }
}

namespace {
void __fastcall InterfaceLoadState(CInterface3D* self, void*, void* stream) { self->LoadState(stream); }
} // namespace
EI_REPLACE(0x005F8420, InterfaceLoadState);
