// The unit set's writer (00559E60), used by the server's SendZone (world_sync.cpp), and the
// client's reader of the unit looks (0055A190). See docs/notes/world.md ("Client maps", the unit
// set). The set's three STLport hash_maps are walked with the original's iterator helpers, so the
// order matches theirs.
#include "core_streams.h"
#include "record_vectors.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_engine.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>

namespace {

using FiguredGroups = stlp::hash_map<unsigned, FiguredUnitMap, stlp::IntHash>;
using DressedMap = stlp::hash_map<unsigned, DressedUnitRecord, stlp::IntHash>;

// The maps are walked as their iterators do: begin() (0055AB50 the groups, 0055ADF0 a group's
// figured map, 0055AE50 the dressed map), then ++ (the chain's next node, else the next bucket's
// head: 0055B9D0 groups, 0055B500 figured, the dressed map's inline with its hash 0055C5C0).
// A record's head written to the stream [00558FA0, on the record].
__declspec(naked) void __fastcall WriteRecordHead(void* /*record*/, void*, CStream*) EI_ORIGINAL(0x00558FA0)

void Put(CStream* stream, const void* data, std::uint32_t size) { stream->Write(data, size); }

void PutWord(CStream* stream, std::uint32_t value) { Put(stream, &value, 4); }

} // namespace

void DressedUnitRecord::Write(CStream* stream) {
    std::int16_t word = static_cast<std::int16_t>(prototype);
    Put(stream, &word, 2);
    PutWord(stream, static_cast<std::uint32_t>(items.size()));
    Put(stream, items.begin(), static_cast<std::uint32_t>(items.size() * sizeof(int)));
    PutWord(stream, static_cast<std::uint32_t>(extraWeapons.size()));
    Put(stream, extraWeapons.begin(), static_cast<std::uint32_t>(extraWeapons.size() * sizeof(int)));
    std::uint8_t byte = static_cast<std::uint8_t>(extraWeapon);
    Put(stream, &byte, 1);
    PutWord(stream, static_cast<std::uint32_t>(modelNumber));
}
EI_REPLACE_METHOD(0x00558FA0, DressedUnitRecord::Write);

void UnitSet::Write(CStream* stream) {
    // The number of figured records over every group.
    std::uint32_t count = 0;
    for (FiguredGroups::Node* group = figured.First(); group; group = figured.After(group)) {
        FiguredUnitMap& units = group->value;
        for (FiguredUnitMap::Node* unit = units.First(); unit; unit = units.After(unit)) ++count;
    }
    PutWord(stream, count);

    for (FiguredGroups::Node* group = figured.First(); group; group = figured.After(group)) {
        FiguredUnitMap& units = group->value;
        for (FiguredUnitMap::Node* unit = units.First(); unit; unit = units.After(unit)) {
            FiguredUnitRecord& record = unit->value;
            WriteRecordHead(&record, nullptr, stream);
            PutWord(stream, reinterpret_cast<const std::uint32_t&>(record.morphS));
            PutWord(stream, reinterpret_cast<const std::uint32_t&>(record.morphT));
            PutWord(stream, reinterpret_cast<const std::uint32_t&>(record.morphU));
            auto names = [&record](std::uint32_t i) { return record.names[i]; };
            auto nameCount = [&record] { return static_cast<std::uint32_t>(record.names.size()); };
            PutWord(stream, nameCount());
            for (std::uint32_t i = 0; i < nameCount(); ++i) {
                const char* text = names(i);
                Put(stream, text, static_cast<std::uint32_t>(std::strlen(text) + 1));
            }
        }
    }
    PutWord(stream, static_cast<std::uint32_t>(dressed.Size()));

    for (DressedMap::Node* node = dressed.First(); node; node = dressed.After(node)) {
        DressedUnitRecord* record = &node->value;
        std::uint32_t key = record->prototype;   // two bytes written
        Put(stream, &key, 2);
        auto counted = [stream](const UnitIdList& list) {
            std::uint32_t n = static_cast<std::uint32_t>(list.size());
            PutWord(stream, n);
            n = static_cast<std::uint32_t>(list.size());
            Put(stream, list.begin(), n << 2);
        };
        counted(record->items);
        counted(record->extraWeapons);
        std::uint32_t flag = static_cast<std::uint8_t>(record->extraWeapon);
        Put(stream, &flag, 1);
        PutWord(stream, static_cast<std::uint32_t>(record->modelNumber));
    }
}

// ---- The unit look lists' reader (0055A190) ------------------------------------------------------

namespace {

// vector<int>::resize(n, value) [004FE0C0], vector<CString>::resize [0051F5B0], the stream's
// CString reader [004E5840, on the stream].

void Get(CStream* stream, void* data, std::uint32_t size) { stream->Read(data, size); }

// A counted dword array: the count, the vector resized (new entries 0), the data.
void GetIds(CStream* stream, UnitIdList& list) {
    std::uint32_t n;
    Get(stream, &n, 4);
    std::uint32_t zero = 0;
    list.Resize<stlp::PlainElement<unsigned int>>(n, zero);
    Get(stream, list.begin(), static_cast<std::uint32_t>(list.size()) << 2);
}

} // namespace

void __fastcall ReadUnitLooks(CStream* stream, stlp::vector<FiguredUnitRecord>* figured,
                              stlp::vector<DressedUnitRecord>* dressed) {
    std::uint32_t count;
    Get(stream, &count, 4);
    recordvec::FiguredUnitVector::ResizeDefault(figured, nullptr, count);
    for (FiguredUnitRecord* record = figured->begin(); record != figured->end(); ++record) {
        Get(stream, &record->prototype, 2);
        GetIds(stream, record->armourIds);
        GetIds(stream, record->weaponIds);
        Get(stream, &record->extraWeapon, 1);
        Get(stream, &record->modelNumber, 4);
        Get(stream, &record->morphS, 4);
        Get(stream, &record->morphT, 4);
        Get(stream, &record->morphU, 4);
        std::uint32_t names;
        Get(stream, &names, 4);
        stlp::VectorAbi<mfc::CString, stlp::ObjectElement<mfc::CString>>::ResizeDefault(&recordvec::FiguredUnitElement::NamesOf(*record), nullptr, names);   // 0051F5B0
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(record->names.size()); ++i)
            stream->ReadInto(reinterpret_cast<mfc::CString*>(&record->names[i]));
    }
    Get(stream, &count, 4);
    recordvec::DressedUnitVector::ResizeDefault(dressed, nullptr, count);
    for (DressedUnitRecord* record = dressed->begin(); record != dressed->end(); ++record) {
        Get(stream, &record->prototype, 2);
        GetIds(stream, record->items);
        GetIds(stream, record->extraWeapons);
        Get(stream, &record->extraWeapon, 1);
        Get(stream, &record->modelNumber, 4);
    }
}

EI_REWRITTEN(0x0055A190, ReadUnitLooks);
EI_REWRITTEN(0x00559E60, UnitSet::Write);
