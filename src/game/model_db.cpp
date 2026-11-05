// Databases read at startup from database.res (prints.db, acks.db) and the world's water level changes.
#include "game/acks_db.h"

#include "game/chunk_archive.h"
#include "game/database_file.h"
#include "game/model.h"
#include "game/terrain.h"
#include "game/water_tweens.h"
#include "lib/bytes.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// The rest of the archive is in chunk_archive.cpp.

namespace {

// Fields 1-3 of chunk `id`, four bytes each, if the chunk is present.
void TripleChunk(ChunkArchive* ar, int id, void* first, void* second, void* third) {
    if (!ar->EnterChunk(id)) return;
    ar->Field(1, first, 4);
    ar->Field(2, second, 4);
    ar->Field(3, third, 4);
    ar->LeaveChunk();
}
void TripleChunk(ChunkArchive* ar, int id, std::uint32_t* values) { TripleChunk(ar, id, &values[0], &values[1], &values[2]); }
void TripleChunk(ChunkArchive* ar, int id, PrintLook* look) { TripleChunk(ar, id, &look->alpha, &look->life, &look->lifetime); }

} // namespace

void ChunkArchive::SerializePrints(int id, std::vector<PrintRecord>* records) {
    RecordChunks(this, id, records, &PrintRecord::Serialize);
}
void ChunkArchive::SerializeFootprints(int id, std::vector<PrintRecord2>* records) {
    RecordChunks(this, id, records, &PrintRecord2::Serialize);
}
void ChunkArchive::SerializeUnitAcks(int id, std::vector<acks::UnitAcks>* records) {
    RecordChunks(this, id, records, &acks::UnitAcks::Serialize);
}
void ChunkArchive::SerializeModelAcks(int id, std::vector<acks::ModelAcks>* records) {
    RecordChunks(this, id, records, &acks::ModelAcks::Serialize);
}
void ChunkArchive::SerializeExtraAcks(int id, std::vector<acks::ExtraAcks>* records) {
    RecordChunks(this, id, records, &acks::ExtraAcks::Serialize);
}

// ---- prints.db records ----------------------------------------------------------------------------

PrintRecord::PrintRecord() : dryLook{}, wetLook{} {}
PrintRecord::PrintRecord(const PrintRecord& from) : name(from.name), dryLook(from.dryLook), wetLook(from.wetLook) {}
PrintRecord2::PrintRecord2() : dryLook{}, wetLook{}, unreadChunk{} {}
PrintRecord2::PrintRecord2(const PrintRecord2& from) : name(from.name), dryLook(from.dryLook), wetLook(from.wetLook) {
    std::memcpy(unreadChunk, from.unreadChunk, sizeof(unreadChunk));
}

void PrintRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, &dryLook);
    TripleChunk(ar, 3, &wetLook);
}

void PrintRecord2::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, &dryLook);
    TripleChunk(ar, 3, &wetLook);
    TripleChunk(ar, 10, unreadChunk);
}

void WaterTweens::SetTarget(int key, float target, int steps) {
    if (steps < 1) steps = 1;
    for (TweenEntry& e : entries) {
        if (e.key != key) continue;
        e.step = (target - e.value) / steps;
        e.steps = steps;
        return;
    }
    // A new entry, from 0.
    entries.push_back(TweenEntry{key, steps, 0.0f, target / steps});
}

bool WaterTweens::Tick() {
    bool moving = false;
    for (auto e = entries.begin(); e != entries.end();) {
        if (e->steps > 0) {
            --e->steps;
            moving = true;
            e->value = e->step + e->value;
        } else if (e->value == 0.0f) {
            // Finished and back at zero: removed.
            e = entries.erase(e);
            continue;
        }
        ++e;
    }
    return moving;
}

void WaterTweens::Save(ByteStream* stream) const {
    stream->WriteCount(entries.size());
    for (const TweenEntry& e : entries) stream->WriteValue(e);
}

void WaterTweens::Load(ByteStream* stream) {
    entries.resize(stream->ReadCount());
    for (TweenEntry& e : entries) stream->ReadValue(e);
}

void WaterTweens::ApplyTo(terrain::Terrain* terrain) const {
    for (const TweenEntry& e : entries) terrain->SetWaterLevel(e.key, e.value);
}

// --- Open --------------------------------------------------------------------------------------------

// The loads of prints.db and acks.db from database.res, once at startup. As unit_db.cpp; see docs/notes/models.md.

namespace {

using dbfile::LoadTable;
using dbfile::OpenDatabase;

// The chunks of prints.db and acks.db.
constexpr int kBloodChunk = 1;
constexpr int kFootprintsChunk = 2;
constexpr int kScorchChunk = 3;
constexpr int kUnitAcksChunk = 1;
constexpr int kModelAcksChunk = 2;
constexpr int kExtraAcksChunk = 3;

// Every record of `records` filed in `index` under its name (the index's operator[]: a record of
// a name seen before replaces the earlier one).
template <typename Record, typename Index, typename Name>
void FileRecords(std::vector<Record>& records, Index& index, Name name) {
    for (Record& record : records) index[name(record)] = &record;
}

// The speech lists of a unit's record by ack kind, in the order they are entered: the
// list's index, the named speeches, or no list at all.
constexpr int kNamedSpeeches = -1;
constexpr int kNoList = -2;
struct KindEntry {
    std::uint8_t kind = 0;
    std::int8_t list = 0;
};
constexpr KindEntry kUnitKinds[] = {
    {0x00, 0},  {0x01, 1},  {0x02, 2},  {0x03, 3},  {0x04, 4},  {0x05, 5},  {0x06, 6},  {0x07, 7},
    {0x08, 8},  {0x09, 9},  {0x0A, 10}, {0x0B, 11}, {0x0C, 12}, {0x0D, 13}, {0x0E, 14}, {0x0F, 15},
    {0x10, 16}, {0x11, 17}, {0x12, 18}, {0x13, 19}, {0x14, 20}, {0x15, 21},
    {0x16, kNoList}, {0x17, kNoList}, {0x18, kNoList}, {0x19, kNoList}, {0x1A, kNoList}, {0x1B, kNoList},
    {0x1C, 22}, {0x1D, 23}, {0x1E, 24}, {0x1F, 25},
    {0x20, 28}, {0x21, 29}, {0x22, 30}, {0x23, 26}, {0x24, 27},
    {0x27, 31}, {0x28, 32}, {0x2A, kNamedSpeeches},
};
constexpr int kFirstModelKind = 0x2E;
constexpr int kFirstExtraKind = 0x2C;

acks::Speeches* UnitList(acks::UnitAcks& record, int list) {
    if (list == kNoList) return nullptr;
    return list == kNamedSpeeches ? &record.namedSpeeches : &record.lists[list];
}

} // namespace

// --- prints.db ------------------------------------------------------------------------------------

int PrintsDatabase::Load(const std::string& folder) {
    std::string base = folder;
    MappedResource resource;
    OpenDatabase(resource, base);
    std::uint32_t size;
    void* data = resource.GetFile("prints.db", &size);
    if (!data) game::FatalExit("Can't open terrain prints database\n");

    MemoryStream stream;
    stream.Fill(data, size);

    const auto name = [](auto& record) -> const std::string& { return record.name; };
    LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializePrints(kBloodChunk, &blood); });
    FileRecords(blood, bloodByName, name);
    LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeFootprints(kFootprintsChunk, &footprints); });
    FileRecords(footprints, footprintsByName, name);
    LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializePrints(kScorchChunk, &scorch); });
    FileRecords(scorch, scorchByName, name);

    resource.CloseFile(data);
    resource.CloseResource();
    return 1;
}

// --- acks.db --------------------------------------------------------------------------------------

namespace acks {

int AckDatabase::Load(const std::string& folder) {
    std::string base = folder;
    MappedResource resource;
    OpenDatabase(resource, base);
    std::uint32_t size;
    void* data = resource.GetFile("acks.db", &size);
    if (!data) game::FatalExit("Can't open acks database\n");

    MemoryStream stream;
    stream.Fill(data, size);

    const auto name = [](auto& record) -> const std::string& { return record.head.name; };
    LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeUnitAcks(kUnitAcksChunk, &units.records); });
    FileRecords(units.records, units.byName, name);
    LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeModelAcks(kModelAcksChunk, &models.records); });
    FileRecords(models.records, models.byName, name);
    LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeExtraAcks(kExtraAcksChunk, &extras.records); });
    FileRecords(extras.records, extras.byName, name);

    resource.CloseFile(data);
    resource.CloseResource();

    // Every record's map from the ack kind to the list it says it from.
    for (std::size_t i = 0; i < units.records.size(); ++i) {
        UnitAcks& record = units.records[i];
        for (const KindEntry& entry : kUnitKinds) record.head.speeches[entry.kind] = UnitList(record, entry.list);
    }
    for (std::size_t i = 0; i < models.records.size(); ++i) {
        ModelAcks& record = models.records[i];
        for (int list = 0; list < 5; ++list) record.head.speeches[kFirstModelKind + list] = &record.lists[list];
    }
    for (std::size_t i = 0; i < extras.records.size(); ++i) {
        ExtraAcks* record = extras.RecordAt(static_cast<std::uint32_t>(i));
        for (int list = 0; list < 2; ++list) record->head.speeches[kFirstExtraKind + list] = &record->lists[list];
    }
    return 1;
}

} // namespace acks

// --- Model names -------------------------------------------------------------------------------------

// A length-prefixed name in a model file's data. See model.h.

void ReadPrefixedName(std::string* out, const std::uint8_t** cursor) {
    std::int32_t length = lib::Load<std::int32_t>(*cursor);
    *cursor += 4;
    if (length == 0) {
        out->clear();
        return;
    }
    // Up to the first zero byte, as the original's copy through a C string.
    const auto* text = reinterpret_cast<const char*>(*cursor);
    out->assign(text, strnlen(text, static_cast<std::size_t>(length)));
    *cursor += length;
}
