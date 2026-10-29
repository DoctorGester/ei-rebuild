// The unit AI's save-game helpers: the looks and the perception memory. The brain's own Load / Save are in
// unit_ai_brain.cpp.
#include "core_streams.h"
#include "unit_ai.h"

#include "world.h"
#include "game_singletons.h"
#include "save_versions.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>

namespace {

// Each object's counted reference (its id) of a memory list.
void SaveObjectList(const HeldUnits& list, CStream* stream) {
    stream->WriteValue(static_cast<std::int32_t>(list.size()));
    for (const HeldRef<CUnit>& unit : list) SaveRef(unit, stream);
}
} // namespace

void LoadLooks(std::vector<AILook>* looks, CStream* stream) {
    int count;
    stream->ReadValue(count);
    looks->resize(count);
    for (AILook& look : *looks) {
        stream->ReadValue(look.angle);
        stream->ReadValue(look.time);
        stream->ReadValue(look.wait);
        if (stream->version >= kVersionLookStopFlag) stream->ReadValue(look.stopFirst);
    }
}

void SaveLooks(const std::vector<AILook>* looks, CStream* stream) {
    stream->WriteValue(static_cast<std::int32_t>(looks->size()));
    for (const AILook& look : *looks) {
        stream->WriteValue(look.angle);
        stream->WriteValue(look.time);
        stream->WriteValue(look.wait);
        stream->WriteValue(look.stopFirst);
    }
}

void AIMemory::Load(CStream* stream) {
    noticedDecay = -1;
    CountedListLoad(&nearby, stream, &g_serverWorld);
    CountedListLoad(&visible, stream, &g_serverWorld);
    CountedListLoad(&seenCorpses, stream, &g_serverWorld);
    CountedListLoad(&corpses, stream, &g_serverWorld);
    stream->Read(&noticedX, 8);         // x and y
    stream->ReadValue(noticedLevel);
    stream->ReadValue(nearbyCountdown);
    int count;
    stream->ReadValue(count);
    reports.resize(count);
    for (int i = 0; i < count; ++i) {
        LoadRef(reports[i].object, stream, &g_serverWorld);
        stream->ReadValue(reports[i].ticksLeft);
    }
    stream->ReadValue(count);
    calls.resize(count);
    for (int i = 0; i < std::ssize(calls); ++i) {
        AICallRecord& call = calls[i];
        call.hostileMask = 0;
        stream->ReadValue(call.point);
        stream->ReadValue(call.delay);
        call.text = stream->ReadString();
        stream->ReadValue(call.hostileMask);
    }
    stream->ReadValue(noticedDecay);
}

void AIMemory::Save(CStream* stream) {
    SaveObjectList(nearby, stream);
    SaveObjectList(visible, stream);
    SaveObjectList(seenCorpses, stream);
    SaveObjectList(corpses, stream);
    stream->Write(&noticedX, 8);        // x and y
    stream->WriteValue(noticedLevel);
    stream->WriteValue(nearbyCountdown);
    stream->WriteValue(static_cast<std::int32_t>(reports.size()));
    for (const AIReport& report : reports) {
        SaveRef(report.object, stream);
        stream->WriteValue(report.ticksLeft);
    }
    stream->WriteValue(static_cast<std::int32_t>(calls.size()));
    for (const AICallRecord& call : calls) {
        stream->WriteValue(call.point);
        stream->WriteValue(call.delay);
        stream->WriteString(call.text);
        stream->WriteValue(call.hostileMask);
    }
    stream->WriteValue(noticedDecay);
}
