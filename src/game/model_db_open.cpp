// The loads of prints.db and acks.db from database.res, once at startup (006D0630 / 006D7620, from
// WinMain 00456440). A-H26.
// Rebirth patches both loads (it redirects their file lookup to a mod folder's copy): these are the
// vanilla functions and install in the vanilla launch only (rule 9). As unit_db_load.cpp; see
// docs/notes/models.md.
#include "acks_db.h"
#include "model.h"

#include "database_file.h"
#include "originals_engine.h"   // FatalErrorOriginal

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

namespace {

using dbfile::FillStream;
using dbfile::LoadTable;
using dbfile::LocalResource;
using dbfile::LocalStream;

// The chunks of prints.db and acks.db.
constexpr int kBloodChunk = 1;
constexpr int kFootprintsChunk = 2;
constexpr int kScorchChunk = 3;
constexpr int kUnitAcksChunk = 1;
constexpr int kModelAcksChunk = 2;
constexpr int kExtraAcksChunk = 3;

// The tables' vector serialisers (RET 0xC: id, vector, unused; rewritten in model_db.cpp), through
// their addresses so that their verify wrappers run.
__declspec(naked) void __fastcall SerializePrintsAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x006D23A0)
__declspec(naked) void __fastcall SerializePrints2At(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x006D25F0)
__declspec(naked) void __fastcall SerializeAcksAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x006DB430)
__declspec(naked) void __fastcall SerializeAcks2At(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x006DB680)
__declspec(naked) void __fastcall SerializeAcks3At(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x006DB8D0)
// AckTable<ExtraAcks>::RecordAt (RET 4; rewritten in acks_db.cpp), likewise.
__declspec(naked) acks::ExtraAcks* __fastcall ExtraAcksAt(acks::AckTable<acks::ExtraAcks>*, void*, std::uint32_t)
    EI_ORIGINAL(0x006D8E30)

// The archive `folder` + "database.res" opened into `resource`; false when it cannot be.
bool OpenDatabase(LocalResource& resource, const mfc::CString& folder) {
    return resource.Open(folder + "database.res") != 0;
}
// The report of a failed open. The original names "databases.res" here.
int ReportNoDatabase(const mfc::CString& folder) {
    return FatalErrorOriginal("Can't open database file \"%s\"\n", static_cast<const char*>(folder + "databases.res"));
}

// Every record of `records` filed in `index` under its name (the index's operator[]: a record of
// a name seen before replaces the earlier one).
template <typename Record, typename Index, typename Name>
void FileRecords(const stlp::vector<Record>& records, Index& index, Name name) {
    for (Record* record = records.begin(); record != records.end(); ++record) index[name(*record)] = record;
}

// The speech lists of a unit's record by ack kind, in the order the original enters them: the
// list's index, the named speeches, or no list at all.
constexpr int kNamedSpeeches = -1;
constexpr int kNoList = -2;
struct KindEntry {
    std::uint8_t kind;
    std::int8_t list;
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

acks::SpeechList* UnitList(acks::UnitAcks& record, int list) {
    if (list == kNoList) return nullptr;
    return list == kNamedSpeeches ? &record.namedSpeeches : &record.lists[list];
}

} // namespace

// --- prints.db ------------------------------------------------------------------------------------

int PrintsDatabase::Load(const char* folder) {
    mfc::CString base(folder);
    LocalResource resource;
    if (!OpenDatabase(resource, base)) return ReportNoDatabase(base);
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("prints.db", &size);
    if (!data) return FatalErrorOriginal("Can't open terrain prints database\n");

    LocalStream local;
    CMemoryStream* stream = &local.stream;
    FillStream(*stream, data, size);

    // The serialisers' unused third argument is the file's data.
    const auto name = [](auto& record) -> const stlp::string& { return record.name; };
    LoadTable(stream, kBloodChunk, SerializePrintsAt, &blood, data,
              [&] { FileRecords(blood, bloodByName, name); });
    LoadTable(stream, kFootprintsChunk, SerializePrints2At, &footprints, data,
              [&] { FileRecords(footprints, footprintsByName, name); });
    LoadTable(stream, kScorchChunk, SerializePrintsAt, &scorch, data,
              [&] { FileRecords(scorch, scorchByName, name); });

    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();
    return 1;
}
EI_REPLACE_METHOD(0x006D0630, PrintsDatabase::Load);

// --- acks.db --------------------------------------------------------------------------------------

namespace acks {

int AckDatabase::Load(const char* folder) {
    mfc::CString base(folder);
    LocalResource resource;
    if (!OpenDatabase(resource, base)) return ReportNoDatabase(base);
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("acks.db", &size);
    if (!data) return FatalErrorOriginal("Can't open acks database\n");

    LocalStream local;
    CMemoryStream* stream = &local.stream;
    FillStream(*stream, data, size);

    // The serialisers' unused third argument: a stack slot the original has not written yet for the
    // first table, the model table's address for the other two.
    const auto name = [](auto& record) -> const stlp::string& { return record.head.name; };
    LoadTable(stream, kUnitAcksChunk, SerializeAcksAt, &units, nullptr,
              [&] { FileRecords(units.records, units.byName, name); });
    LoadTable(stream, kModelAcksChunk, SerializeAcks2At, &models, &models,
              [&] { FileRecords(models.records, models.byName, name); });
    LoadTable(stream, kExtraAcksChunk, SerializeAcks3At, &extras, &models,
              [&] { FileRecords(extras.records, extras.byName, name); });

    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();

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
        ExtraAcks* record = ExtraAcksAt(&extras, nullptr, static_cast<std::uint32_t>(i));
        for (int list = 0; list < 2; ++list) record->head.speeches[kFirstExtraKind + list] = &record->lists[list];
    }
    return 1;
}
EI_REPLACE_METHOD(0x006D7620, AckDatabase::Load);

} // namespace acks
