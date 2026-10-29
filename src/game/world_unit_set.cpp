// The unit set's writer, used by the server's SendZone (world_sync.cpp), and the client's reader of the unit
// looks. See docs/notes/world.md ("Client maps", the unit set). The set's three hash maps walk in the game's
// order (lib::HashMap).
#include "core_streams.h"
#include "world.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

void Put(ByteStream* stream, const void* data, std::uint32_t size) { stream->Write(data, size); }

// vector<int>::resize(n, value), vector<string>::resize, the stream's string reader.

void Get(ByteStream* stream, void* data, std::uint32_t size) { stream->Read(data, size); }

// A counted dword array: the count, the vector resized (new entries 0), the data.
void GetIds(ByteStream* stream, std::vector<std::uint32_t>& list) {
    std::uint32_t n;
    stream->ReadValue(n);
    std::uint32_t zero = 0;
    list.resize(n, zero);
    Get(stream, list.data(), static_cast<std::uint32_t>(list.size()) << 2);
}

} // namespace

void DressedUnitRecord::Write(ByteStream* stream) {
    std::int16_t word = static_cast<std::int16_t>(prototype);
    stream->WriteValue(word);
    stream->WriteValue(static_cast<std::uint32_t>(items.size()));
    Put(stream, items.data(), static_cast<std::uint32_t>(items.size() * sizeof(int)));
    stream->WriteValue(static_cast<std::uint32_t>(extraWeapons.size()));
    Put(stream, extraWeapons.data(), static_cast<std::uint32_t>(extraWeapons.size() * sizeof(int)));
    std::uint8_t byte = static_cast<std::uint8_t>(extraWeapon);
    stream->WriteValue(byte);
    stream->WriteValue(static_cast<std::uint32_t>(modelNumber));
}

void UnitSet::Write(ByteStream* stream) {
    // The number of figured records over every group.
    std::uint32_t count = 0;
    for (const auto& [prototype, group] : figured) count += static_cast<std::uint32_t>(group.size());
    stream->WriteValue(count);

    for (auto& [prototype, group] : figured) {
        for (auto& [scaleKey, record] : group) {
            record.Write(stream);
            stream->WriteValue(record.morphS);
            stream->WriteValue(record.morphT);
            stream->WriteValue(record.morphU);
            auto names = [&record](std::uint32_t i) -> const char* { return record.names[i].c_str(); };
            auto nameCount = [&record] { return static_cast<std::uint32_t>(record.names.size()); };
            stream->WriteValue(nameCount());
            for (std::uint32_t i = 0; i < nameCount(); ++i) {
                const char* text = names(i);
                Put(stream, text, static_cast<std::uint32_t>(std::strlen(text) + 1));
            }
        }
    }
    stream->WriteValue(static_cast<std::uint32_t>(dressed.size()));

    for (auto& [prototype, looks] : dressed) {
        DressedUnitRecord* record = &looks;
        std::uint32_t key = record->prototype;   // two bytes written
        Put(stream, &key, 2);
        auto counted = [stream](const std::vector<std::uint32_t>& list) {
            std::uint32_t n = static_cast<std::uint32_t>(list.size());
            stream->WriteValue(n);
            n = static_cast<std::uint32_t>(list.size());
            Put(stream, list.data(), n << 2);
        };
        counted(record->items);
        counted(record->extraWeapons);
        std::uint32_t flag = static_cast<std::uint8_t>(record->extraWeapon);
        Put(stream, &flag, 1);
        stream->WriteValue(static_cast<std::uint32_t>(record->modelNumber));
    }
}

// ---- The unit look lists' reader ------------------------------------------------------

void ReadUnitLooks(ByteStream* stream, std::vector<FiguredUnitRecord>* figured, std::vector<DressedUnitRecord>* dressed) {
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
            record->names[i] = stream->ReadString();
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
