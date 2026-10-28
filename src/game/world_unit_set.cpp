// The unit set's writer, used by the server's SendZone (world_sync.cpp), and the client's reader of the unit
// looks. See docs/notes/world.md ("Client maps", the unit set). The set's three hash maps walk in the game's
// order (lib::HashMap).
#include "core_streams.h"
#include "world.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

void Put(CStream* stream, const void* data, std::uint32_t size) { stream->Write(data, size); }

void PutWord(CStream* stream, std::uint32_t value) { stream->WriteValue(value); }

// vector<int>::resize(n, value), vector<string>::resize, the stream's string reader.

void Get(CStream* stream, void* data, std::uint32_t size) { stream->Read(data, size); }

// A counted dword array: the count, the vector resized (new entries 0), the data.
void GetIds(CStream* stream, std::vector<std::uint32_t>& list) {
    std::uint32_t n;
    stream->ReadValue(n);
    std::uint32_t zero = 0;
    list.resize(n, zero);
    Get(stream, list.data(), static_cast<std::uint32_t>(list.size()) << 2);
}

} // namespace

void DressedUnitRecord::Write(CStream* stream) {
    std::int16_t word = static_cast<std::int16_t>(prototype);
    stream->WriteValue(word);
    PutWord(stream, static_cast<std::uint32_t>(items.size()));
    Put(stream, items.data(), static_cast<std::uint32_t>(items.size() * sizeof(int)));
    PutWord(stream, static_cast<std::uint32_t>(extraWeapons.size()));
    Put(stream, extraWeapons.data(), static_cast<std::uint32_t>(extraWeapons.size() * sizeof(int)));
    std::uint8_t byte = static_cast<std::uint8_t>(extraWeapon);
    stream->WriteValue(byte);
    PutWord(stream, static_cast<std::uint32_t>(modelNumber));
}

void UnitSet::Write(CStream* stream) {
    // The number of figured records over every group.
    std::uint32_t count = 0;
    for (const auto& [prototype, group] : figured) count += static_cast<std::uint32_t>(group.size());
    PutWord(stream, count);

    for (auto& [prototype, group] : figured) {
        for (auto& [scaleKey, record] : group) {
            record.Write(stream);
            PutWord(stream, std::bit_cast<std::uint32_t>(record.morphS));
            PutWord(stream, std::bit_cast<std::uint32_t>(record.morphT));
            PutWord(stream, std::bit_cast<std::uint32_t>(record.morphU));
            auto names = [&record](std::uint32_t i) -> const char* { return record.names[i].c_str(); };
            auto nameCount = [&record] { return static_cast<std::uint32_t>(record.names.size()); };
            PutWord(stream, nameCount());
            for (std::uint32_t i = 0; i < nameCount(); ++i) {
                const char* text = names(i);
                Put(stream, text, static_cast<std::uint32_t>(std::strlen(text) + 1));
            }
        }
    }
    PutWord(stream, static_cast<std::uint32_t>(dressed.size()));

    for (auto& [prototype, looks] : dressed) {
        DressedUnitRecord* record = &looks;
        std::uint32_t key = record->prototype;   // two bytes written
        Put(stream, &key, 2);
        auto counted = [stream](const std::vector<std::uint32_t>& list) {
            std::uint32_t n = static_cast<std::uint32_t>(list.size());
            PutWord(stream, n);
            n = static_cast<std::uint32_t>(list.size());
            Put(stream, list.data(), n << 2);
        };
        counted(record->items);
        counted(record->extraWeapons);
        std::uint32_t flag = static_cast<std::uint8_t>(record->extraWeapon);
        Put(stream, &flag, 1);
        PutWord(stream, static_cast<std::uint32_t>(record->modelNumber));
    }
}

// ---- The unit look lists' reader ------------------------------------------------------

void ReadUnitLooks(CStream* stream, std::vector<FiguredUnitRecord>* figured, std::vector<DressedUnitRecord>* dressed) {
    std::uint32_t count;
    stream->ReadValue(count);
    figured->resize(count);
    for (FiguredUnitRecord& looks : *figured) {
        FiguredUnitRecord* record = &looks;
        stream->ReadValue(record->prototype);
        GetIds(stream, record->items);
        GetIds(stream, record->extraWeapons);
        stream->ReadValue(record->extraWeapon);
        stream->ReadValue(record->modelNumber);
        stream->ReadValue(record->morphS);
        stream->ReadValue(record->morphT);
        stream->ReadValue(record->morphU);
        std::uint32_t names;
        stream->ReadValue(names);
        record->names.resize(names);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(record->names.size()); ++i)
            stream->ReadInto(&record->names[i]);
    }
    stream->ReadValue(count);
    dressed->resize(count);
    for (DressedUnitRecord& looks : *dressed) {
        DressedUnitRecord* record = &looks;
        stream->ReadValue(record->prototype);
        GetIds(stream, record->items);
        GetIds(stream, record->extraWeapons);
        stream->ReadValue(record->extraWeapon);
        stream->ReadValue(record->modelNumber);
    }
}
