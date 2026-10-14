// The unit AI's save-game helpers: the looks (005D9010 / 005D91B0) and the perception memory
// (005CC1E0 / 005CC350). The brain's own Load / Save are in unit_ai_brain.cpp.
#include "unit_ai.h"

#include "originals_engine.h"
#include "originals_units.h"
#include "world.h"

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
constexpr int kStopFlagVersion = 0x44;      // looks carry their stop flag from this file version on

// The original's Save functions copy each scalar into a stack slot before writing it.
template <typename T>
void WriteCopy(AIStream* stream, const T& value) {
    T copy = value;
    stream->Write(&copy, sizeof(T));
}

AIBrain* BrainOfMemory(std::uint8_t* memory) { return reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory)); }

// Each object's counted reference (its id) of a memory list.
void SaveObjectList(mfc::CTypedPtrList<CEngineObject>& list, AIStream* stream) {
    WriteCopy(stream, list.count);
    for (auto* node = list.head; node;) {
        auto* next = node->next;
        reinterpret_cast<AIRef*>(&node->data)->Save(stream);
        node = next;
    }
}
} // namespace

void __fastcall LoadLooks(AILookArray* looks, AIStream* stream) {
    int count;
    stream->Read(&count, 4);
    looks->SetSize(count, -1);      // inlined by the original
    for (int i = 0; i < looks->size; ++i) {
        AILook& look = looks->data[i];
        stream->Read(&look.angle, 4);
        stream->Read(&look.time, 4);
        stream->Read(&look.wait, 4);
        if (stream->version >= kStopFlagVersion) stream->Read(&look.stopFirst, 1);
    }
}
EI_REPLACE(0x005D9010, LoadLooks);

void __fastcall SaveLooks(const AILookArray* looks, AIStream* stream) {
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

void AIBrain::LoadMemory(AIStream* stream) {
    noticedKind = -1;
    CountedListLoadOriginal(&nearby, stream, WorldServer());
    CountedListLoadOriginal(&visible, stream, WorldServer());
    CountedListLoadOriginal(&seenCorpses, stream, WorldServer());
    CountedListLoadOriginal(&corpses, stream, WorldServer());
    stream->Read(&noticedX, 8);         // x and y
    stream->Read(&noticedLevel, 4);
    stream->Read(&nearbyCountdown, 4);
    int count;
    stream->Read(&count, 4);
    ReportsSetSizeAt(&reports, nullptr, count, -1);
    for (int i = 0; i < count; ++i) {
        reinterpret_cast<AIRef*>(&reports.data[i].object)->Load(stream, WorldServer());
        stream->Read(&reports.data[i].what, 4);
    }
    stream->Read(&count, 4);
    CallsSetSizeAt(&calls, nullptr, count, -1);
    for (int i = 0; i < calls.size; ++i) {
        AICallRecord& call = calls.data[i];
        call.hostileMask = 0;
        stream->Read(&call.point, 12);
        stream->Read(&call.kind, 4);
        StreamReadCStringOriginal(stream, nullptr, &call.text);
        stream->Read(&call.hostileMask, 4);
    }
    stream->Read(&noticedKind, 4);
}

void AIBrain::SaveMemory(AIStream* stream) {
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

// The original's `this` is the memory at +0xD0.
void __fastcall LoadMemoryHook(std::uint8_t* memory, void*, AIStream* stream) { BrainOfMemory(memory)->LoadMemory(stream); }
EI_REPLACE(0x005CC1E0, LoadMemoryHook);
void __fastcall SaveMemoryHook(std::uint8_t* memory, void*, AIStream* stream) { BrainOfMemory(memory)->SaveMemory(stream); }
EI_REPLACE(0x005CC350, SaveMemoryHook);
