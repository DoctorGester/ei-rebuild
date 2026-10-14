// The list of 0x2C-byte device records WinMain's start-up (00463390, patched by Rebirth) keeps in
// a chunk archive (a name, a GUID, three dwords, three more strings): the record's fields, its
// compiler-made members and the list's archiving. The list is an STLport vector (its algorithms
// are library code). See docs/notes/app.md.
#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace app {

struct DeviceRecord {
    mfc::CString name;           // +0x00
    std::uint32_t guid[4];       // +0x04
    std::uint32_t unknown_14;    // +0x14
    std::uint32_t unknown_18;    // +0x18
    std::uint32_t unknown_1C;    // +0x1C
    mfc::CString text[3];        // +0x20
};
static_assert(sizeof(DeviceRecord) == 0x2C);

struct DeviceVector {            // STLport vector<DeviceRecord>
    DeviceRecord* begin;
    DeviceRecord* end;
    DeviceRecord* eos;
};

namespace {
// The chunk archive (originals_engine.h): +0x58 is its loading flag, +0x50 its chunk stack.
// STLport (library): copy (first, last, dest), resize.
__declspec(naked) DeviceRecord* __fastcall RecordsCopy(DeviceRecord*, DeviceRecord*, DeviceRecord*, const void*, int) EI_ORIGINAL(0x00465B20)
__declspec(naked) void __fastcall RecordsResize(DeviceVector*, void*, unsigned) EI_ORIGINAL(0x00465180)
// The record's own functions through their addresses (rewritten below).
__declspec(naked) void __fastcall RecordDestructAt(DeviceRecord*) EI_ORIGINAL(0x004652A0)
__declspec(naked) void __fastcall RecordArchiveAt(DeviceRecord*, void*, void*) EI_ORIGINAL(0x00463030)

void CopyPlain(DeviceRecord* to, const DeviceRecord* from) {
    std::memcpy(to->guid, from->guid, sizeof(to->guid));
    to->unknown_14 = from->unknown_14;
    to->unknown_18 = from->unknown_18;
    to->unknown_1C = from->unknown_1C;
}
} // namespace

// 00463030: the record's fields 0 .. 7 (strings 0, 5, 6, 7).
void __fastcall RecordArchive(DeviceRecord* self, void*, ChunkArchive* archive) {
    ArchiveString(archive, nullptr, 0, &self->name);
    ArchiveField(archive, nullptr, 1, self->guid, 0x10);
    ArchiveField(archive, nullptr, 2, &self->unknown_14, 4);
    ArchiveField(archive, nullptr, 3, &self->unknown_18, 4);
    ArchiveField(archive, nullptr, 4, &self->unknown_1C, 4);
    ArchiveString(archive, nullptr, 5, &self->text[0]);
    ArchiveString(archive, nullptr, 6, &self->text[1]);
    ArchiveString(archive, nullptr, 7, &self->text[2]);
}
EI_REPLACE(0x00463030, RecordArchive);

// 004652A0 / 004657E0: destructor, deleting destructor.
void __fastcall RecordDestruct(DeviceRecord* self) {
    self->text[2].~CString();
    self->text[1].~CString();
    self->text[0].~CString();
    self->name.~CString();
}
EI_REPLACE(0x004652A0, RecordDestruct);
DeviceRecord* __fastcall RecordDelete(DeviceRecord* self, void*, unsigned flags) {
    RecordDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004657E0, RecordDelete);

// 00465A70 / 00465BC0: copy constructor and assignment; both return this.
DeviceRecord* __fastcall RecordCopyConstruct(DeviceRecord* self, void*, const DeviceRecord* from) {
    new (&self->name) mfc::CString(from->name);
    CopyPlain(self, from);
    for (int i = 0; i < 3; ++i) new (&self->text[i]) mfc::CString(from->text[i]);
    return self;
}
EI_REPLACE(0x00465A70, RecordCopyConstruct);
DeviceRecord* __fastcall RecordAssign(DeviceRecord* self, void*, const DeviceRecord* from) {
    self->name = from->name;
    CopyPlain(self, from);
    for (int i = 0; i < 3; ++i) self->text[i] = from->text[i];
    return self;
}
EI_REPLACE(0x00465BC0, RecordAssign);

// 00465090: the list in chunk `id` of the archive, one chunk 1 per record (emptied and resized
// to the number of chunks when loading).
void __fastcall DeviceListArchive(void* archive, void*, int id, DeviceVector* list, int /*unused*/) {
    ChunkArchive* ar = static_cast<ChunkArchive*>(archive);
    if (!ArchiveEnter(ar, nullptr, id)) return;
    int n;
    if (*(static_cast<std::uint8_t*>(archive) + 0x58)) {
        DeviceRecord* end = list->end;
        DeviceRecord* kept = RecordsCopy(end, end, list->begin, list, 0);
        for (DeviceRecord* r = kept; r != list->end; ++r) RecordDestructAt(r);
        list->end = kept;
        n = ArchiveCount(ar, nullptr, 1);
        RecordsResize(list, nullptr, static_cast<unsigned>(n));
    } else {
        n = static_cast<int>(list->end - list->begin);
    }
    for (int i = 0; i < n; ++i) {
        void* chunk = *reinterpret_cast<void**>(*reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(archive) + 0x50) + 4);
        *reinterpret_cast<int*>(static_cast<std::uint8_t*>(chunk) + 0x14) = i + 1;
        DeviceRecord* record = list->begin + i;
        if (!ArchiveEnter(ar, nullptr, 1)) continue;
        RecordArchiveAt(record, nullptr, archive);
        ArchiveLeave(ar);
    }
    ArchiveLeave(ar);
}
EI_REPLACE(0x00465090, DeviceListArchive);

} // namespace app
