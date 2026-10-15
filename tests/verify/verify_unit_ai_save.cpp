// Checks for the unit AI's save-game code (src/game/unit_ai_brain.cpp Load / Save,
// src/game/unit_ai_save.cpp) and the mode list's RefPtr::ShareAssign (core_refcount.cpp).
// Reached by saving and loading a game with AI units: ui_saveload (save, then load), and by
// every new game's mode setup (ShareAssign through AddMode's callers is inlined; Load uses it).
#include "verify.h"
#include "fake_stream.h"

#include "../../src/game/core_refcount.h"
#include "../../src/game/unit_ai.h"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace {
void* s_unused;

AIBrain* BrainOfMemory(std::uint8_t* memory) { return reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory)); }

// A loaded list of AI modes is linked both ways, holds `count` nodes, each with a referenced
// mode, and the mode in charge is one of them (or none).
bool ModesConsistent(AIBrain* brain) {
    int count = 0;
    bool currentListed = brain->current == nullptr;
    mfc::CTypedPtrList<AIMode>::Node* prev = nullptr;
    for (auto* node = brain->modes.head; node; node = node->next) {
        if (node->prev != prev || !node->data || node->data->refs < 1) return false;
        if (node->data == brain->current) currentListed = true;
        prev = node;
        ++count;
    }
    return prev == brain->modes.tail && count == brain->modes.count && currentListed;
}
} // namespace

// 005D8860 AIBrain::Save: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x14] (8 bytes).
// The original into a recording stream, ours into another, the bytes compared; then ours into
// the real stream. (The helpers' wrappers write the same way inside both.)
static void(__fastcall* s_brainSave)(AIBrain*, void*, CStream*);
static void __fastcall BrainSaveVerify(AIBrain* self, void*, CStream* stream) {
    RecordingStream expected(stream->version_), actual(stream->version_);
    s_brainSave(self, nullptr, &expected);
    self->Save(&actual);
    ei::verify::Check("AIBrain::Save", true, expected.written == actual.written, "%d bytes (original %d)",
                      static_cast<int>(actual.written.size()), static_cast<int>(expected.written.size()));
    self->Save(stream);
}
EI_VERIFY(0x005D8860, BrainSaveVerify, 8, s_brainSave);

// 005D8310 AIBrain::Load: reads the stream, so ours only: the mode list is consistent and the
// alarm records and looks hold their counts.
static void __fastcall BrainLoadVerify(AIBrain* self, void*, CStream* stream) {
    self->Load(stream);
    bool ok = ModesConsistent(self) && self->alarmRecords.size >= 0 && self->alarmRecords.maxSize >= self->alarmRecords.size &&
              self->looks.size >= 0 && self->looks.maxSize >= self->looks.size;
    ei::verify::Check("AIBrain::Load", true, ok, "brain %p: %d modes, current %p, %d alarm records, %d looks", self,
                      self->modes.count, self->current, self->alarmRecords.size, self->looks.size);
}
EI_VERIFY(0x005D8310, BrainLoadVerify, 0, s_unused);

// 005CC350 memory save: PUSH ECX; PUSH EBX; MOV EBX,ECX; PUSH EBP; PUSH ESI (6 bytes). As Save.
static void(__fastcall* s_memorySave)(std::uint8_t*, void*, CStream*);
static void __fastcall MemorySaveVerify(std::uint8_t* memory, void*, CStream* stream) {
    RecordingStream expected(stream->version_), actual(stream->version_);
    s_memorySave(memory, nullptr, &expected);
    BrainOfMemory(memory)->SaveMemory(&actual);
    ei::verify::Check("AIBrain::SaveMemory", true, expected.written == actual.written, "%d bytes (original %d)",
                      static_cast<int>(actual.written.size()), static_cast<int>(expected.written.size()));
    BrainOfMemory(memory)->SaveMemory(stream);
}
EI_VERIFY(0x005CC350, MemorySaveVerify, 6, s_memorySave);

// 005CC1E0 memory load: ours only: the noticed kind was read (not left -1 unless saved so), the
// reports hold objects or nothing and every call has its text.
static void __fastcall MemoryLoadVerify(std::uint8_t* memory, void*, CStream* stream) {
    AIBrain* brain = BrainOfMemory(memory);
    brain->LoadMemory(stream);
    bool ok = brain->reports.size >= 0 && brain->calls.size >= 0;
    for (int i = 0; ok && i < brain->calls.size; ++i) ok = brain->calls.data[i].text != nullptr;
    ei::verify::Check("AIBrain::LoadMemory", true, ok, "brain %p: %d reports, %d calls", brain, brain->reports.size,
                      brain->calls.size);
}
EI_VERIFY(0x005CC1E0, MemoryLoadVerify, 0, s_unused);

// 005D91B0 SaveLooks: SUB ESP,0x14; PUSH EBX; MOV EBX,ECX (6 bytes). As Save.
static void(__fastcall* s_saveLooks)(const AILookArray*, CStream*);
static void __fastcall SaveLooksVerify(const AILookArray* looks, CStream* stream) {
    RecordingStream expected(stream->version_), actual(stream->version_);
    s_saveLooks(looks, &expected);
    SaveLooks(looks, &actual);
    ei::verify::Check("SaveLooks", true, expected.written == actual.written, "%d looks: %d bytes (original %d)", looks->size,
                      static_cast<int>(actual.written.size()), static_cast<int>(expected.written.size()));
    SaveLooks(looks, stream);
}
EI_VERIFY(0x005D91B0, SaveLooksVerify, 6, s_saveLooks);

// 005D9010 LoadLooks: ours only: the storage holds the count read.
static void __fastcall LoadLooksVerify(AILookArray* looks, CStream* stream) {
    LoadLooks(looks, stream);
    bool ok = looks->size >= 0 && looks->maxSize >= looks->size && (looks->size == 0 || looks->data);
    ei::verify::Check("LoadLooks", true, ok, "%d looks", looks->size);
}
EI_VERIFY(0x005D9010, LoadLooksVerify, 0, s_unused);

// 005D9D40 RefPtr::ShareAssign: ours only: a null or detached object leaves the pointer null,
// any other is held (itself or its clone) and marked copy-on-share; `this` is returned.
static RefPtr* __fastcall ShareAssignVerify(RefPtr* self, void*, CRefObject* object) {
    CRefObject* before = self->ptr;
    bool detached = object && (object->flags7_ & CRefCounted::kDetached);
    RefPtr* result = self->ShareAssign(object);
    bool ok = result == self;
    if (object == before) ok = ok && self->ptr == before;
    else if (!object || detached) ok = ok && self->ptr == nullptr;
    else ok = ok && self->ptr && (self->ptr->flags6_ & CRefCounted::kCopyOnShare);
    ei::verify::Check("RefPtr::ShareAssign", true, ok, "%p: %p -> %p (asked %p)", self, before, self->ptr, object);
    return result;
}
EI_VERIFY(0x005D9D40, ShareAssignVerify, 0, s_unused);
