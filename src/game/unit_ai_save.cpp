// The unit AI's save-game helpers: the looks (005D9010 / 005D91B0) and the perception memory
// (005CC1E0 / 005CC350). The brain's own Load / Save are in unit_ai_brain.cpp.
#include "core_streams.h"
#include "unit_ai.h"

#include "originals_engine.h"
#include "originals_units.h"
#include "world.h"
#include "game_singletons.h"

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
constexpr int kStopFlagVersion = 0x44;      // looks carry their stop flag from this file version on

// The original's Save functions copy each scalar into a stack slot before writing it.
template <typename T>
void WriteCopy(CStream* stream, const T& value) {
    T copy = value;
    stream->Write(&copy, sizeof(T));
}

// Each object's counted reference (its id) of a memory list.
void SaveObjectList(mfc::CTypedPtrList<CEngineObject>& list, CStream* stream) {
    WriteCopy(stream, list.count);
    for (auto* node = list.head; node;) {
        auto* next = node->next;
        reinterpret_cast<AIRef*>(&node->data)->Save(stream);
        node = next;
    }
}
} // namespace

void __fastcall LoadLooks(AILookArray* looks, CStream* stream) {
    int count;
    stream->Read(&count, 4);
    looks->SetSize(count, -1);      // inlined by the original
    for (int i = 0; i < looks->size; ++i) {
        AILook& look = looks->data[i];
        stream->Read(&look.angle, 4);
        stream->Read(&look.time, 4);
        stream->Read(&look.wait, 4);
        if (stream->version_ >= kStopFlagVersion) stream->Read(&look.stopFirst, 1);
    }
}
EI_REPLACE(0x005D9010, LoadLooks);

void __fastcall SaveLooks(const AILookArray* looks, CStream* stream) {
    WriteCopy(stream, looks->size);
    for (int i = 0; i < looks->size; ++i) {
        const AILook& look = looks->data[i];
        WriteCopy(stream, look.angle);
        WriteCopy(stream, look.time);
        WriteCopy(stream, look.wait);
        WriteCopy(stream, look.stopFirst);
    }
}
EI_REPLACE(0x005D91B0, SaveLooks);

void AIMemory::Load(CStream* stream) {
    noticedKind = -1;
    CountedListLoad(&nearby, stream, TheServerWorld());
    CountedListLoad(&visible, stream, TheServerWorld());
    CountedListLoad(&seenCorpses, stream, TheServerWorld());
    CountedListLoad(&corpses, stream, TheServerWorld());
    stream->Read(&noticedX, 8);         // x and y
    stream->Read(&noticedLevel, 4);
    stream->Read(&nearbyCountdown, 4);
    int count;
    stream->Read(&count, 4);
    reports.SetSize(count, -1);
    for (int i = 0; i < count; ++i) {
        reinterpret_cast<AIRef*>(&reports.data[i].object)->Load(stream, TheServerWorld());
        stream->Read(&reports.data[i].what, 4);
    }
    stream->Read(&count, 4);
    calls.SetSize(count, -1);
    for (int i = 0; i < calls.size; ++i) {
        AICallRecord& call = calls.data[i];
        call.hostileMask = 0;
        stream->Read(&call.point, 12);
        stream->Read(&call.kind, 4);
        stream->ReadInto(&call.text);
        stream->Read(&call.hostileMask, 4);
    }
    stream->Read(&noticedKind, 4);
}
EI_REPLACE_METHOD(0x005CC1E0, AIMemory::Load);

void AIMemory::Save(CStream* stream) {
    SaveObjectList(nearby, stream);
    SaveObjectList(visible, stream);
    SaveObjectList(seenCorpses, stream);
    SaveObjectList(corpses, stream);
    stream->Write(&noticedX, 8);        // x and y
    WriteCopy(stream, noticedLevel);
    WriteCopy(stream, nearbyCountdown);
    WriteCopy(stream, reports.size);
    for (int i = 0; i < reports.size; ++i) {
        reinterpret_cast<AIRef*>(&reports.data[i].object)->Save(stream);
        WriteCopy(stream, reports.data[i].what);
    }
    WriteCopy(stream, calls.size);
    for (int i = 0; i < calls.size; ++i) {
        AICallRecord& call = calls.data[i];
        stream->Write(&call.point, 12);
        WriteCopy(stream, call.kind);
        stream->Write(call.text, static_cast<unsigned>(std::strlen(call.text) + 1));
        WriteCopy(stream, call.hostileMask);
    }
    WriteCopy(stream, noticedKind);
}
EI_REPLACE_METHOD(0x005CC350, AIMemory::Save);
