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
void SaveObjectList(std::list<CUnit*>& list, CStream* stream) {
    stream->WriteValue(std::ssize(list));
    for (auto node = list.begin(); node != list.end();) {
        auto next = std::next(node);
        SaveRef(*node, stream);
        node = next;
    }
}
} // namespace

void LoadLooks(std::vector<AILook>* looks, CStream* stream) {
    int count;
    stream->ReadValue(count);
    looks->resize(count);
    for (int i = 0; i < std::ssize(*looks); ++i) {
        AILook& look = (*looks)[i];
        stream->ReadValue(look.angle);
        stream->ReadValue(look.time);
        stream->ReadValue(look.wait);
        if (stream->version_ >= kVersionLookStopFlag) stream->ReadValue(look.stopFirst);
    }
}

void SaveLooks(const std::vector<AILook>* looks, CStream* stream) {
    stream->WriteValue(std::ssize(*looks));
    for (int i = 0; i < std::ssize(*looks); ++i) {
        const AILook& look = (*looks)[i];
        stream->WriteValue(look.angle);
        stream->WriteValue(look.time);
        stream->WriteValue(look.wait);
        stream->WriteValue(look.stopFirst);
    }
}

void AIMemory::Load(CStream* stream) {
    noticedKind = -1;
    CountedListLoad(&nearby, stream, &g_serverWorld.Get());
    CountedListLoad(&visible, stream, &g_serverWorld.Get());
    CountedListLoad(&seenCorpses, stream, &g_serverWorld.Get());
    CountedListLoad(&corpses, stream, &g_serverWorld.Get());
    stream->Read(&noticedX, 8);         // x and y
    stream->ReadValue(noticedLevel);
    stream->ReadValue(nearbyCountdown);
    int count;
    stream->ReadValue(count);
    reports.resize(count);
    for (int i = 0; i < count; ++i) {
        LoadRef(reports[i].object, stream, &g_serverWorld.Get());
        stream->ReadValue(reports[i].what);
    }
    stream->ReadValue(count);
    calls.resize(count);
    for (int i = 0; i < std::ssize(calls); ++i) {
        AICallRecord& call = calls[i];
        call.hostileMask = 0;
        stream->ReadValue(call.point);
        stream->ReadValue(call.kind);
        stream->ReadInto(&call.text);
        stream->ReadValue(call.hostileMask);
    }
    stream->ReadValue(noticedKind);
}

void AIMemory::Save(CStream* stream) {
    SaveObjectList(nearby, stream);
    SaveObjectList(visible, stream);
    SaveObjectList(seenCorpses, stream);
    SaveObjectList(corpses, stream);
    stream->Write(&noticedX, 8);        // x and y
    stream->WriteValue(noticedLevel);
    stream->WriteValue(nearbyCountdown);
    stream->WriteValue(std::ssize(reports));
    for (int i = 0; i < std::ssize(reports); ++i) {
        SaveRef(reports[i].object, stream);
        stream->WriteValue(reports[i].what);
    }
    stream->WriteValue(std::ssize(calls));
    for (int i = 0; i < std::ssize(calls); ++i) {
        AICallRecord& call = calls[i];
        stream->WriteValue(call.point);
        stream->WriteValue(call.kind);
        stream->WriteString(call.text);
        stream->WriteValue(call.hostileMask);
    }
    stream->WriteValue(noticedKind);
}
