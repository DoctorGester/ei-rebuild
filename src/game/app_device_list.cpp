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
// STLport (library): resize.
__declspec(naked) void __fastcall RecordsResize(LeverRecordVector*, void*, unsigned) EI_ORIGINAL(0x00465180)
__declspec(naked) void __fastcall RecordArchiveAt(LeverRecord*, void*, void*) EI_ORIGINAL(0x00463030)

void CopyPlain(LeverRecord* to, const LeverRecord* from) {
    std::memcpy(to->spots, from->spots, sizeof(to->spots));
    to->spotCount = from->spotCount;
    to->farScale = from->farScale;
    to->duration = from->duration;
}
} // namespace

// 004652A0: the destructor.
void __fastcall RecordDestruct(LeverRecord* self) {
    self->text28.~CString();
    self->sound.~CString();
    self->text20.~CString();
    self->name.~CString();
}
EI_REPLACE(0x004652A0, RecordDestruct);
} // namespace app

// 00463030: the record's fields 0 .. 7 (strings 0, 5, 6, 7).
void LeverRecord::Archive(ChunkArchive* archive) {
    archive->CStringField(0, &name);
    archive->Field(1, spots, 0x10);
    archive->Field(2, &spotCount, 4);
    archive->Field(3, &farScale, 4);
    archive->Field(4, &duration, 4);
    archive->CStringField(5, &text20);
    archive->CStringField(6, &sound);
    archive->CStringField(7, &text28);
}
EI_REPLACE_METHOD(0x00463030, LeverRecord::Archive);

// 004657E0: the deleting destructor.
LeverRecord* LeverRecord::DeleteScalar(unsigned flags) {
    app::RecordDestruct(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004657E0, LeverRecord::DeleteScalar);

// 00465A70 / 00465BC0: copy constructor and assignment; both return this.
LeverRecord* LeverRecord::CopyConstruct(const LeverRecord* from) {
    new (&name) mfc::CString(from->name);
    app::CopyPlain(this, from);
    new (&text20) mfc::CString(from->text20);
    new (&sound) mfc::CString(from->sound);
    new (&text28) mfc::CString(from->text28);
    return this;
}
EI_REPLACE_METHOD(0x00465A70, LeverRecord::CopyConstruct);
LeverRecord* LeverRecord::Assign(const LeverRecord* from) {
    name = from->name;
    app::CopyPlain(this, from);
    text20 = from->text20;
    sound = from->sound;
    text28 = from->text28;
    return this;
}
EI_REPLACE_METHOD(0x00465BC0, LeverRecord::Assign);

namespace app {

// 00465B20 (library, STLport's copy for the records; RET 0xC, the last two arguments its tags):
// each record of [first, last) assigned to the next of `result`, first to last (the assignment
// inline); returns the end of the copy.
LeverRecord* __fastcall RecordsCopyForward(LeverRecord* first, LeverRecord* last, LeverRecord* result, const void*, int) {
    for (std::ptrdiff_t n = last - first; n > 0; --n, ++first, ++result) result->Assign(first);
    return result;
}
EI_REPLACE(0x00465B20, RecordsCopyForward);

// 00465090: the list in chunk `id` of the archive, one chunk 1 per record (emptied and resized
// to the number of chunks when loading).
void __fastcall DeviceListArchive(void* archive, void*, int id, LeverRecordVector* list, int /*unused*/) {
    ChunkArchive* ar = static_cast<ChunkArchive*>(archive);
    if (!ar->EnterChunk(id)) return;
    int n;
    if (ar->loading_) {
        LeverRecord* end = list->end();
        LeverRecord* kept = RecordsCopyForward(end, end, list->begin(), list, 0);
        for (LeverRecord* r = kept; r != list->end(); ++r) RecordDestruct(r);
        list->Finish() = kept;
        n = ar->CountChunks(1);
        RecordsResize(list, nullptr, static_cast<unsigned>(n));
    } else {
        n = static_cast<int>(list->size());
    }
    for (int i = 0; i < n; ++i) {
        ar->Current()->occurrence = i + 1;
        LeverRecord* record = list->begin() + i;
        if (!ar->EnterChunk(1)) continue;
        RecordArchiveAt(record, nullptr, archive);
        ar->LeaveChunk();
    }
    ar->LeaveChunk();
}
EI_REPLACE(0x00465090, DeviceListArchive);

} // namespace app
