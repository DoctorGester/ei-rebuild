// The lever table's records (LeverRecord, lever.h: Levers.ldb, 0x2C bytes; Rebirth patches the start-up code
// that loads them from a chunk archive): the record's fields, its compiler-made members and the list's
// archiving. The list is an STLport vector (its algorithms are library code). See docs/notes/app.md.
#include "chunk_archive.h"
#include "lever.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "record_vectors.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace app {

using LeverRecordVector = stlp::vector<LeverRecord>;
static_assert(sizeof(LeverRecordVector) == 12);

namespace {
// The chunk archive: +0x58 is its loading flag, +0x50 its chunk stack.

void CopyPlain(LeverRecord* to, const LeverRecord* from) {
    std::memcpy(to->spots, from->spots, sizeof(to->spots));
    to->spotCount = from->spotCount;
    to->farScale = from->farScale;
    to->duration = from->duration;
}
} // namespace

void __fastcall RecordDestruct(LeverRecord* self) {
    std::destroy_at(&self->text28);
    std::destroy_at(&self->sound);
    std::destroy_at(&self->text20);
    std::destroy_at(&self->name);
}
EI_REWRITTEN(0x004652A0, RecordDestruct);
} // namespace app

// The record's fields 0 .. 7 (strings 0, 5, 6, 7).
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
EI_REWRITTEN(0x00463030, LeverRecord::Archive);

LeverRecord* LeverRecord::DeleteScalar(unsigned flags) {
    app::RecordDestruct(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x004657E0, LeverRecord::DeleteScalar);

LeverRecord* LeverRecord::CopyConstruct(const LeverRecord* from) {
    std::construct_at(&name, from->name);
    app::CopyPlain(this, from);
    std::construct_at(&text20, from->text20);
    std::construct_at(&sound, from->sound);
    std::construct_at(&text28, from->text28);
    return this;
}
EI_REWRITTEN(0x00465A70, LeverRecord::CopyConstruct);
LeverRecord* LeverRecord::Assign(const LeverRecord* from) {
    name = from->name;
    app::CopyPlain(this, from);
    text20 = from->text20;
    sound = from->sound;
    text28 = from->text28;
    return this;
}
EI_REWRITTEN(0x00465BC0, LeverRecord::Assign);

namespace app {

// STLport's copy for the records: each record of [first, last) assigned to the next of `result`,
// first to last (the assignment inline); returns the end of the copy.
LeverRecord* __fastcall RecordsCopyForward(LeverRecord* first, LeverRecord* last, LeverRecord* result, const void*, int) {
    for (std::ptrdiff_t n = last - first; n > 0; --n, ++first, ++result) result->Assign(first);
    return result;
}
EI_REWRITTEN(0x00465B20, RecordsCopyForward);

} // namespace app

void ChunkArchive::SerializeLevers(int id, stlp::vector<LeverRecord>* list, int /*unused*/) {
    using namespace app;
    ChunkArchive* ar = this;
    if (!ar->EnterChunk(id)) return;
    int n;
    if (ar->loading_) {
        LeverRecord* end = list->end();
        LeverRecord* kept = RecordsCopyForward(end, end, list->begin(), list, 0);
        for (LeverRecord* r = kept; r != list->end(); ++r) RecordDestruct(r);
        list->Finish() = kept;
        n = ar->CountChunks(1);
        list->ResizeDefault<recordvec::LeverVector::Element>(static_cast<unsigned>(n));
    } else {
        n = static_cast<int>(list->size());
    }
    for (int i = 0; i < n; ++i) {
        ar->Current()->occurrence = i + 1;
        LeverRecord* record = list->begin() + i;
        if (!ar->EnterChunk(1)) continue;
        record->Archive(ar);
        ar->LeaveChunk();
    }
    ar->LeaveChunk();
}
EI_REWRITTEN(0x00465090, ChunkArchive::SerializeLevers);
