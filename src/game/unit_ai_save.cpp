// The unit AI's save-game helpers: the looks and the perception memory. The brain's own Load / Save are in
// unit_ai_brain.cpp.
#include "core_streams.h"
#include "unit_ai.h"

#include "world.h"
#include "game_singletons.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>

namespace {
constexpr int kStopFlagVersion = 0x44;      // looks carry their stop flag from this file version on

// Each scalar is copied into a stack slot before it is written.
template <typename T>
void WriteCopy(CStream* stream, const T& value) {
    T copy = value;
    stream->Write(&copy, sizeof(T));
}

// Each object's counted reference (its id) of a memory list.
void SaveObjectList(std::list<CUnit*>& list, CStream* stream) {
    WriteCopy(stream, std::ssize(list));
    for (auto node = list.begin(); node != list.end();) {
        auto next = std::next(node);
        AIRefTo<CUnit>::Of(&*node)->Save(stream);
        node = next;
    }
}
} // namespace

void LoadLooks(std::vector<AILook>* looks, CStream* stream) {
    int count;
    stream->Read(&count, 4);
    looks->resize(count);
    for (int i = 0; i < std::ssize(*looks); ++i) {
        AILook& look = (*looks)[i];
        stream->Read(&look.angle, 4);
        stream->Read(&look.time, 4);
        stream->Read(&look.wait, 4);
        if (stream->version_ >= kStopFlagVersion) stream->Read(&look.stopFirst, 1);
    }
}

void SaveLooks(const std::vector<AILook>* looks, CStream* stream) {
    WriteCopy(stream, std::ssize(*looks));
    for (int i = 0; i < std::ssize(*looks); ++i) {
        const AILook& look = (*looks)[i];
        WriteCopy(stream, look.angle);
        WriteCopy(stream, look.time);
        WriteCopy(stream, look.wait);
        WriteCopy(stream, look.stopFirst);
    }
}

void AIMemory::Load(CStream* stream) {
    noticedKind = -1;
    CountedListLoad(&nearby, stream, &g_serverWorld.Get());
    CountedListLoad(&visible, stream, &g_serverWorld.Get());
    CountedListLoad(&seenCorpses, stream, &g_serverWorld.Get());
    CountedListLoad(&corpses, stream, &g_serverWorld.Get());
    stream->Read(&noticedX, 8);         // x and y
    stream->Read(&noticedLevel, 4);
    stream->Read(&nearbyCountdown, 4);
    int count;
    stream->Read(&count, 4);
    reports.resize(count);
    for (int i = 0; i < count; ++i) {
        AIRefTo<CMapObject> loaded{nullptr};
        loaded.Load(stream, &g_serverWorld.Get());
        reports[i].object = loaded.object;   // the report takes its own reference
        loaded.Reset();
        stream->Read(&reports[i].what, 4);
    }
    stream->Read(&count, 4);
    calls.resize(count);
    for (int i = 0; i < std::ssize(calls); ++i) {
        AICallRecord& call = calls[i];
        call.hostileMask = 0;
        stream->Read(&call.point, 12);
        stream->Read(&call.kind, 4);
        stream->ReadInto(&call.text);
        stream->Read(&call.hostileMask, 4);
    }
    stream->Read(&noticedKind, 4);
}

void AIMemory::Save(CStream* stream) {
    SaveObjectList(nearby, stream);
    SaveObjectList(visible, stream);
    SaveObjectList(seenCorpses, stream);
    SaveObjectList(corpses, stream);
    stream->Write(&noticedX, 8);        // x and y
    WriteCopy(stream, noticedLevel);
    WriteCopy(stream, nearbyCountdown);
    WriteCopy(stream, std::ssize(reports));
    for (int i = 0; i < std::ssize(reports); ++i) {
        AIRefTo<CMapObject>{reports[i].object}.Save(stream);
        WriteCopy(stream, reports[i].what);
    }
    WriteCopy(stream, std::ssize(calls));
    for (int i = 0; i < std::ssize(calls); ++i) {
        AICallRecord& call = calls[i];
        stream->Write(&call.point, 12);
        WriteCopy(stream, call.kind);
        stream->WriteString(call.text);
        WriteCopy(stream, call.hostileMask);
    }
    WriteCopy(stream, noticedKind);
}
