// The lever table's records (LeverRecord, lever.h: Levers.ldb, 0x2C bytes; WinMain's start-up
// 00463390, patched by Rebirth, loads them from a chunk archive): the record's fields, its
// compiler-made members and the list's archiving. The list is an STLport vector (its algorithms
// are library code). See docs/notes/app.md.
#include "chunk_archive.h"
#include "lever.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace app {

using LeverRecordVector = stlp::vector<LeverRecord>;
static_assert(sizeof(LeverRecordVector) == 12);

namespace {
// The chunk archive (originals_engine.h): +0x58 is its loading flag, +0x50 its chunk stack.
// STLport (library): copy (first, last, dest), resize.
__declspec(naked) LeverRecord* __fastcall RecordsCopy(LeverRecord*, LeverRecord*, LeverRecord*, const void*, int) EI_ORIGINAL(0x00465B20)
__declspec(naked) void __fastcall RecordsResize(LeverRecordVector*, void*, unsigned) EI_ORIGINAL(0x00465180)
// The record's own functions through their addresses (rewritten below).
__declspec(naked) void __fastcall RecordDestructAt(LeverRecord*) EI_ORIGINAL(0x004652A0)
__declspec(naked) void __fastcall RecordArchiveAt(LeverRecord*, void*, void*) EI_ORIGINAL(0x00463030)

void CopyPlain(LeverRecord* to, const LeverRecord* from) {
    std::memcpy(to->spots, from->spots, sizeof(to->spots));
    to->spotCount = from->spotCount;
    to->farScale = from->farScale;
    to->duration = from->duration;
}
} // namespace

// 00463030: the record's fields 0 .. 7 (strings 0, 5, 6, 7).
void __fastcall RecordArchive(LeverRecord* self, void*, ChunkArchive* archive) {
    ArchiveString(archive, nullptr, 0, &self->name);
    ArchiveField(archive, nullptr, 1, self->spots, 0x10);
    ArchiveField(archive, nullptr, 2, &self->spotCount, 4);
    ArchiveField(archive, nullptr, 3, &self->farScale, 4);
    ArchiveField(archive, nullptr, 4, &self->duration, 4);
    ArchiveString(archive, nullptr, 5, &self->text20);
    ArchiveString(archive, nullptr, 6, &self->sound);
    ArchiveString(archive, nullptr, 7, &self->text28);
}
EI_REPLACE(0x00463030, RecordArchive);

// 004652A0 / 004657E0: destructor, deleting destructor.
void __fastcall RecordDestruct(LeverRecord* self) {
    self->text28.~CString();
    self->sound.~CString();
    self->text20.~CString();
    self->name.~CString();
}
EI_REPLACE(0x004652A0, RecordDestruct);
LeverRecord* __fastcall RecordDelete(LeverRecord* self, void*, unsigned flags) {
    RecordDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004657E0, RecordDelete);

// 00465A70 / 00465BC0: copy constructor and assignment; both return this.
LeverRecord* __fastcall RecordCopyConstruct(LeverRecord* self, void*, const LeverRecord* from) {
    new (&self->name) mfc::CString(from->name);
    CopyPlain(self, from);
    new (&self->text20) mfc::CString(from->text20);
    new (&self->sound) mfc::CString(from->sound);
    new (&self->text28) mfc::CString(from->text28);
    return self;
}
EI_REPLACE(0x00465A70, RecordCopyConstruct);
LeverRecord* __fastcall RecordAssign(LeverRecord* self, void*, const LeverRecord* from) {
    self->name = from->name;
    CopyPlain(self, from);
    self->text20 = from->text20;
    self->sound = from->sound;
    self->text28 = from->text28;
    return self;
}
EI_REPLACE(0x00465BC0, RecordAssign);

// 00465090: the list in chunk `id` of the archive, one chunk 1 per record (emptied and resized
// to the number of chunks when loading).
void __fastcall DeviceListArchive(void* archive, void*, int id, LeverRecordVector* list, int /*unused*/) {
    ChunkArchive* ar = static_cast<ChunkArchive*>(archive);
    if (!ArchiveEnter(ar, nullptr, id)) return;
    int n;
    if (ar->loading_) {
        LeverRecord* end = list->end();
        LeverRecord* kept = RecordsCopy(end, end, list->begin(), list, 0);
        for (LeverRecord* r = kept; r != list->end(); ++r) RecordDestructAt(r);
        list->Finish() = kept;
        n = ArchiveCount(ar, nullptr, 1);
        RecordsResize(list, nullptr, static_cast<unsigned>(n));
    } else {
        n = static_cast<int>(list->size());
    }
    for (int i = 0; i < n; ++i) {
        ar->Current()->occurrence = i + 1;
        LeverRecord* record = list->begin() + i;
        if (!ArchiveEnter(ar, nullptr, 1)) continue;
        RecordArchiveAt(record, nullptr, archive);
        ArchiveLeave(ar);
    }
    ArchiveLeave(ar);
}
EI_REPLACE(0x00465090, DeviceListArchive);

} // namespace app
