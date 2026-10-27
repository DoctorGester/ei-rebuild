// The unit set's writer, used by the server's SendZone (world_sync.cpp), and the client's reader of the unit
// looks. See docs/notes/world.md ("Client maps", the unit set). The set's three STLport hash_maps are walked
// with the game's iterator helpers, so the order matches theirs.
#include "core_streams.h"
#include "record_vectors.h"
#include "world.h"

#include "ei/hook.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

using FiguredGroups = UnitSet::FiguredGroups;
using FiguredTable = UnitSet::FiguredTable;
using DressedTable = UnitSet::DressedTable;

// The maps are walked as their iterators do: begin() (the groups, a group's figured map, the dressed map), then
// ++ (the chain's next node, else the next bucket's head; the dressed map's is inline with its hash).

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
EI_REWRITTEN(0x00558FA0, DressedUnitRecord::Write);

void UnitSet::Write(CStream* stream) {
    // The number of figured records over every group.
    std::uint32_t count = 0;
    for (FiguredGroups::Node* group = figured.First(); group; group = figured.Next(group)) {
        FiguredTable& units = group->value.mapped;
        for (FiguredTable::Node* unit = units.First(); unit; unit = units.Next(unit)) ++count;
    }
    PutWord(stream, count);

    for (FiguredGroups::Node* group = figured.First(); group; group = figured.Next(group)) {
        FiguredTable& units = group->value.mapped;
        for (FiguredTable::Node* unit = units.First(); unit; unit = units.Next(unit)) {
            FiguredUnitRecord& record = unit->value.mapped;
            record.Write(stream);
            PutWord(stream, std::bit_cast<std::uint32_t>(record.morphS));
            PutWord(stream, std::bit_cast<std::uint32_t>(record.morphT));
            PutWord(stream, std::bit_cast<std::uint32_t>(record.morphU));
            auto names = [&record](std::uint32_t i) -> const char* { return record.names[i]; };
            auto nameCount = [&record] { return static_cast<std::uint32_t>(record.names.size()); };
            PutWord(stream, nameCount());
            for (std::uint32_t i = 0; i < nameCount(); ++i) {
                const char* text = names(i);
                Put(stream, text, static_cast<std::uint32_t>(std::strlen(text) + 1));
            }
        }
    }
    PutWord(stream, static_cast<std::uint32_t>(dressed.Count()));

    for (DressedTable::Node* node = dressed.First(); node; node = dressed.Next(node)) {
        DressedUnitRecord* record = &node->value.mapped;
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

// ---- The unit look lists' reader ------------------------------------------------------

namespace {

// vector<int>::resize(n, value), vector<CString>::resize, the stream's CString reader.

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
    figured->ResizeDefault<recordvec::FiguredUnitVector::Element>(count);
    for (FiguredUnitRecord* record = figured->begin(); record != figured->end(); ++record) {
        Get(stream, &record->prototype, 2);
        GetIds(stream, record->items);
        GetIds(stream, record->extraWeapons);
        Get(stream, &record->extraWeapon, 1);
        Get(stream, &record->modelNumber, 4);
        Get(stream, &record->morphS, 4);
        Get(stream, &record->morphT, 4);
        Get(stream, &record->morphU, 4);
        std::uint32_t names;
        Get(stream, &names, 4);
        static_cast<stlp::VectorAbi<mfc::CString, stlp::ObjectElement<mfc::CString>>*>(&record->names)->ResizeDefault(names);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(record->names.size()); ++i)
            stream->ReadInto(&record->names[i]);
    }
    Get(stream, &count, 4);
    dressed->ResizeDefault<recordvec::DressedUnitVector::Element>(count);
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
