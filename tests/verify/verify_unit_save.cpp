// Checks for CUnit's Load / Save (src/game/unit_save.cpp).
//
// Save: the original and ours write into capture streams (the version copied from the real one);
// bytes and write sizes are compared and the original's writes go on to the real stream. Load
// reads the real stream and changes the world (players, references), so only ours runs, followed
// by consistency checks.
#include "verify.h"

#include "unit_capture_stream.h"

#include <cstring>
#include <vector>

namespace {
using unitverify::CaptureStream;
using SaveFn = void(__fastcall*)(CUnit*, void*, UnitStream*);
SaveFn s_originalSave;
void* s_unused;

bool SlotsReferenced(CEngineObject* const* slots, int count) {
    for (int i = 0; i < count; ++i)
        if (slots[i] && slots[i]->refCount_ < 1) return false;
    return true;
}
} // namespace

static void __fastcall SaveUnitVerify(CUnit* self, void*, UnitStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_originalSave(self, nullptr, &expected);
    self->SaveUnit(&actual);
    ei::verify::Check("CUnit::SaveUnit", true, expected.Same(actual), "unit %d: %u vs %u bytes, first difference at %d",
                      self->id_, static_cast<unsigned>(expected.bytes.size()),
                      static_cast<unsigned>(actual.bytes.size()), expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x005321C0, SaveUnitVerify, 7, s_originalSave);

// Load: ours only. Every item slot is empty or referenced, the carried array is consistent, and
// the prototype indices are -1 or in the table.
static void __fastcall LoadUnitVerify(CUnit* self, void*, UnitStream* stream) {
    self->LoadUnit(stream);
    char* begin = *reinterpret_cast<char**>(0x007AF358);
    char* end = *reinterpret_cast<char**>(0x007AF35C);
    int prototypes = static_cast<int>((end - begin) / 0x120);
    int index = *reinterpret_cast<int*>(reinterpret_cast<char*>(self) + 0x18);
    bool ok = SlotsReferenced(self->weapons, 4) && SlotsReferenced(self->quickItems, 4) &&
              SlotsReferenced(self->spells, 8) && SlotsReferenced(self->armor, 7) &&
              self->carriedCount >= 0 && self->carriedCount <= self->carriedCapacity &&
              (self->carriedCount == 0 || self->carried != nullptr) &&
              SlotsReferenced(self->carried, self->carriedCount);
    if (stream->version >= 0x4D)
        ok = ok && index >= -1 && index < prototypes && self->prototype23C >= -1 && self->prototype23C < prototypes;
    ei::verify::Check("CUnit::LoadUnit", true, ok, "unit %d version %d: carried %d/%d, prototypes %d/%d", self->id_,
                      stream->version, self->carriedCount, self->carriedCapacity, index, self->prototype23C);
}
EI_VERIFY(0x00531A80, LoadUnitVerify, 0, s_unused);
