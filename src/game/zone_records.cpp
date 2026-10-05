// Zone exits (world.h ZoneExit, 0x3C bytes) and the map database's zone records that hold them
// (eight CStrings, then a CArray<ZoneExit> at +0x48): their destructors. See
// docs/notes/app.md.
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace zone {

namespace {
template <typename T>
T& At(void* base, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }
void Destroy(void* base, std::size_t offset) { reinterpret_cast<mfc::CString*>(static_cast<std::uint8_t*>(base) + offset)->~CString(); }
__declspec(naked) void __fastcall ExitDestructAt(void*) EI_ORIGINAL(0x00460460)
} // namespace

// 00460460: a zone exit's destructor (its CString at +0x28).
void __fastcall ExitDestruct(void* self) { Destroy(self, 0x28); }
EI_REPLACE(0x00460460, ExitDestruct);

// 004608B0: the zone exit's scalar deleting destructor.
void* __fastcall ExitDelete(void* self, void*, unsigned flags) {
    ExitDestructAt(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004608B0, ExitDelete);

// 00460970: a zone record's destructor: the exits' array (vtable 0073C3F4; each exit's string
// destroyed, the block freed), then the strings at +0x1C ... +0.
void __fastcall ZoneRecordDestruct(void* self) {
    void* array = static_cast<std::uint8_t*>(self) + 0x48;
    std::uint32_t vtable = 0x0073C3F4;
    std::memcpy(array, &vtable, 4);
    if (std::uint8_t* data = At<std::uint8_t*>(array, 4)) {
        for (int i = 0, n = At<int>(array, 8); i < n; ++i) Destroy(data + i * 0x3C, 0x28);
        gamecrt::operator_delete(At<void*>(array, 4));
    }
    for (std::size_t offset = 0x1C;; offset -= 4) {
        Destroy(self, offset);
        if (offset == 0) break;
    }
}
EI_REPLACE(0x00460970, ZoneRecordDestruct);

} // namespace zone
