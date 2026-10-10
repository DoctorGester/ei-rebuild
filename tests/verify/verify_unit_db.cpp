// Checks for unit_db.cpp, the units database records' serialisers: prototypes, models and NPCs
// (startup: units.udb is loaded by 0053A200 in every scenario).
//
// Loading reads fields by id and leaves the archive as it was, so reading twice gives the same
// record: ours reads into the real record, its fields are taken (the bytes with the CString and
// vector slots left out, and a digest of every string's text), then the original reads into the
// same record again and the two are compared. The record keeps the original's reading. Writing
// appends to the archive: ours only.
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/unit_db.h"

#include <cstdint>
#include <cstring>

namespace {

struct Layout {
    int size;
    const int* strings;      // offsets of the CStrings
    int stringCount;
    const int* vectors;      // offsets of the vector<CString>s
    int vectorCount;
};

struct Snapshot {
    unsigned char bytes[0x168];
    std::uint64_t digest;
};

const char* TextAt(const unsigned char* base, int offset) { return *reinterpret_cast<const char* const*>(base + offset); }

void Mix(std::uint64_t& digest, const char* text) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) digest = (digest ^ *p) * 0x100000001B3ull;
    digest = (digest ^ 0x100) * 0x100000001B3ull;   // separator
}

Snapshot Take(const void* record, const Layout& layout) {
    Snapshot s{};
    const auto* base = static_cast<const unsigned char*>(record);
    std::memcpy(s.bytes, base, static_cast<std::size_t>(layout.size));
    s.digest = 0xCBF29CE484222325ull;
    for (int i = 0; i < layout.stringCount; ++i) {
        Mix(s.digest, TextAt(base, layout.strings[i]));
        std::memset(s.bytes + layout.strings[i], 0, 4);
    }
    for (int i = 0; i < layout.vectorCount; ++i) {
        const auto* v = reinterpret_cast<const CStringVector*>(base + layout.vectors[i]);
        s.digest = (s.digest ^ static_cast<std::uint64_t>(v->end - v->begin)) * 0x100000001B3ull;
        for (const mfc::CString* text = v->begin; text != v->end; ++text) Mix(s.digest, *text);
        std::memset(s.bytes + layout.vectors[i], 0, sizeof(CStringVector));
    }
    return s;
}

template <typename Record>
void SerializeVerify(const char* name, void(__fastcall* original)(Record*, void*, ChunkArchive*), Record* self,
                     ChunkArchive* ar, const Layout& layout) {
    self->Serialize(ar);
    if (!ar->loading_) return;
    Snapshot ours = Take(self, layout);
    original(self, nullptr, ar);
    Snapshot theirs = Take(self, layout);
    int diff = -1;
    for (int i = 0; i < layout.size && diff < 0; ++i)
        if (ours.bytes[i] != theirs.bytes[i]) diff = i;
    bool same = diff < 0 && ours.digest == theirs.digest;
    ei::verify::Check(name, true, same, "\"%s\": %s +0x%X", static_cast<const char*>(self->name),
                      diff < 0 ? "strings differ" : "first difference at", diff < 0 ? 0 : diff);
}

constexpr int kPrototypeStrings[] = {0x00, 0x04, 0x34, 0x118, 0x11C};
constexpr int kPrototypeVectors[] = {0xE8, 0x100, 0x10C};
constexpr Layout kPrototypeLayout{sizeof(UnitPrototypeRecord), kPrototypeStrings, 5, kPrototypeVectors, 3};

constexpr int kModelStrings[] = {0x00, 0x88, 0x98, 0xA8, 0xB8, 0xC8, 0xD8, 0xE8, 0x118, 0x124};
constexpr int kModelVectors[] = {0xEC, 0xF8, 0x138};
constexpr Layout kModelLayout{sizeof(UnitModelRecord), kModelStrings, 10, kModelVectors, 3};

} // namespace

// 0052D740: PUSH EBX; PUSH ESI; MOV ESI,[ESP+0xC]; PUSH EDI (7 bytes).
static void(__fastcall* s_prototypeSerialize)(UnitPrototypeRecord*, void*, ChunkArchive*);
static void __fastcall PrototypeSerializeVerify(UnitPrototypeRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("UnitPrototypeRecord::Serialize", s_prototypeSerialize, self, ar, kPrototypeLayout);
}
EI_VERIFY(0x0052D740, PrototypeSerializeVerify, 7, s_prototypeSerialize);

// 0052DC70: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,[ESP+0x14] (8 bytes).
static void(__fastcall* s_modelSerialize)(UnitModelRecord*, void*, ChunkArchive*);
static void __fastcall ModelSerializeVerify(UnitModelRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("UnitModelRecord::Serialize", s_modelSerialize, self, ar, kModelLayout);
}
EI_VERIFY(0x0052DC70, ModelSerializeVerify, 8, s_modelSerialize);

namespace {
constexpr int kNpcStrings[] = {0x00};
constexpr int kNpcVectors[] = {0x24, 0x30, 0x3C, 0x48};
constexpr Layout kNpcLayout{sizeof(UnitNpcRecord), kNpcStrings, 1, kNpcVectors, 4};
} // namespace

// 0052DA60: PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x10] (7 bytes).
static void(__fastcall* s_npcSerialize)(UnitNpcRecord*, void*, ChunkArchive*);
static void __fastcall NpcSerializeVerify(UnitNpcRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("UnitNpcRecord::Serialize", s_npcSerialize, self, ar, kNpcLayout);
}
EI_VERIFY(0x0052DA60, NpcSerializeVerify, 7, s_npcSerialize);

// The skill database (A-U10; startup: perks.pdb is loaded by 00520220 in every scenario).
// - SkillRecord / SkillLevelRecord::Serialize (0051F3A0 / 0051F480): as the records above (ours,
//   then the original reads the same record again; compared).
// - FindLevel / IndexOf / IndexOfLevel (0051FE00 / 0051FE80 / 0051FF00): pure, original vs ours
//   (the trade panel, item descriptions and the hero statistics look levels up by name).
// - SerializeSkills / SerializeSkillLevels / SkillLevelChunk (005207D0 / 005208C0 / 00521480):
//   ours only; after a load the vector holds as many records as the chunk has record chunks.
namespace {
constexpr int kSkillStrings[] = {0x00, 0x04};
constexpr int kSkillVectors[] = {0x0C};
constexpr Layout kSkillLayout{sizeof(SkillRecord), kSkillStrings, 2, kSkillVectors, 1};
constexpr int kSkillLevelStrings[] = {0x00, 0x04, 0x0C, 0x14};
constexpr Layout kSkillLevelLayout{sizeof(SkillLevelRecord), kSkillLevelStrings, 4, nullptr, 0};

// The record chunks of chunk `id` of the current chunk (re-entered after the call; reading leaves
// the parent as it was).
int RecordChunks(ChunkArchive* ar, int id) {
    if (!ar->EnterChunk(id)) return 0;
    int count = ar->CountChunks(1);
    ar->LeaveChunk();
    return count;
}

void* s_unusedSkills;
void* s_unusedSkillLevels;
void* s_unusedSkillLevelChunk;
} // namespace

// 0051F3A0 / 0051F480: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes).
static void(__fastcall* s_skillSerialize)(SkillRecord*, void*, ChunkArchive*);
static void __fastcall SkillSerializeVerify(SkillRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("SkillRecord::Serialize", s_skillSerialize, self, ar, kSkillLayout);
}
EI_VERIFY(0x0051F3A0, SkillSerializeVerify, 6, s_skillSerialize);

static void(__fastcall* s_skillLevelSerialize)(SkillLevelRecord*, void*, ChunkArchive*);
static void __fastcall SkillLevelSerializeVerify(SkillLevelRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("SkillLevelRecord::Serialize", s_skillLevelSerialize, self, ar, kSkillLevelLayout);
}
EI_VERIFY(0x0051F480, SkillLevelSerializeVerify, 6, s_skillLevelSerialize);

static void __fastcall SerializeSkillsVerify(ChunkArchive* ar, void*, int id, stlp::vector<SkillRecord>* skills,
                                             void* unused) {
    ar->SerializeSkills(id, skills, unused);
    if (!ar->loading_) return;
    int chunks = RecordChunks(ar, id);
    ei::verify::Check("ChunkArchive::SerializeSkills", chunks, static_cast<int>(skills->size()), "%d records", chunks);
}
EI_VERIFY(0x005207D0, SerializeSkillsVerify, 0, s_unusedSkills);

static void __fastcall SerializeSkillLevelsVerify(ChunkArchive* ar, void*, int id,
                                                  stlp::vector<SkillLevelRecord>* levels, void* unused) {
    ar->SerializeSkillLevels(id, levels, unused);
    if (!ar->loading_) return;
    int chunks = RecordChunks(ar, id);
    ei::verify::Check("ChunkArchive::SerializeSkillLevels", chunks, static_cast<int>(levels->size()), "%d records",
                      chunks);
}
EI_VERIFY(0x005208C0, SerializeSkillLevelsVerify, 0, s_unusedSkillLevels);

static void __fastcall SkillLevelChunkVerify(ChunkArchive* ar, void*, int id, SkillLevelRecord* record) {
    ar->SkillLevelChunk(id, record);
    ei::verify::Check("ChunkArchive::SkillLevelChunk", true, record != nullptr, "chunk %d", id);
}
EI_VERIFY(0x00521480, SkillLevelChunkVerify, 0, s_unusedSkillLevelChunk);

// 0051FE00 / 0051FE80: PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,ECX; PUSH EDI (6 bytes).
static SkillLevelRecord*(__fastcall* s_findLevel)(SkillDatabase*, void*, const char*);
static SkillLevelRecord* __fastcall FindLevelVerify(SkillDatabase* self, void*, const char* name) {
    SkillLevelRecord* expected = s_findLevel(self, nullptr, name);
    SkillLevelRecord* actual = self->FindLevel(name);
    ei::verify::Check("SkillDatabase::FindLevel", static_cast<void*>(expected), static_cast<void*>(actual), "%s", name);
    return expected;
}
EI_VERIFY(0x0051FE00, FindLevelVerify, 6, s_findLevel);

static int(__fastcall* s_indexOf)(SkillDatabase*, void*, const char*);
static int __fastcall IndexOfVerify(SkillDatabase* self, void*, const char* name) {
    int expected = s_indexOf(self, nullptr, name);
    int actual = self->IndexOf(name);
    ei::verify::Check("SkillDatabase::IndexOf", expected, actual, "%s", name);
    return expected;
}
EI_VERIFY(0x0051FE80, IndexOfVerify, 6, s_indexOf);

// 0051FF00: MOV EDX,[ECX+0xC]; MOV ECX,[ECX+0x10] (6 bytes).
static int(__fastcall* s_indexOfLevel)(SkillDatabase*, void*, const SkillLevelRecord*);
static int __fastcall IndexOfLevelVerify(SkillDatabase* self, void*, const SkillLevelRecord* record) {
    int expected = s_indexOfLevel(self, nullptr, record);
    int actual = self->IndexOfLevel(record);
    ei::verify::Check("SkillDatabase::IndexOfLevel", expected, actual, "%p", static_cast<const void*>(record));
    return expected;
}
EI_VERIFY(0x0051FF00, IndexOfLevelVerify, 6, s_indexOfLevel);

// A-U11: the unit database's pieces and vectors (startup, 0053A200 in every scenario).
// - UnitLevelRecord::Serialize (0052DEE0), the model slot (0052D6F0) and the four dwords (0052D6A0):
//   ours, then the original reads the same again; compared (strings by content).
// - SerializePrototypes / Npcs / Models / UnitLevels (0053DC10 / 0053DD00 / 0053DF50 / 0053E1B0) and
//   SerializeCStrings (0052DF40): ours only; after a load the vector holds as many records as the chunk
//   has record chunks (strings: fields 1).
namespace {
constexpr int kLevelStrings[] = {0x00};
constexpr Layout kLevelLayout{sizeof(UnitLevelRecord), kLevelStrings, 1, nullptr, 0};
void* s_unusedUnitVectors;
} // namespace

// 0052DEE0 / 0052D6F0 / 0052D6A0: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes).
static void(__fastcall* s_levelSerialize)(UnitLevelRecord*, void*, ChunkArchive*);
static void __fastcall LevelSerializeVerify(UnitLevelRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("UnitLevelRecord::Serialize", s_levelSerialize, self, ar, kLevelLayout);
}
EI_VERIFY(0x0052DEE0, LevelSerializeVerify, 6, s_levelSerialize);

static void(__fastcall* s_slotSerialize)(UnitModelSlot*, void*, ChunkArchive*);
static void __fastcall SlotSerializeVerify(UnitModelSlot* self, void*, ChunkArchive* ar) {
    self->Serialize(ar);
    if (!ar->loading_) return;
    unsigned char ours[12];
    std::memcpy(ours, &self->levelIndex, sizeof(ours));
    std::uint64_t oursText = 0xCBF29CE484222325ull;
    Mix(oursText, self->levelName);
    s_slotSerialize(self, nullptr, ar);
    std::uint64_t theirsText = 0xCBF29CE484222325ull;
    Mix(theirsText, self->levelName);
    bool same = std::memcmp(ours, &self->levelIndex, sizeof(ours)) == 0 && oursText == theirsText;
    ei::verify::Check("UnitModelSlot::Serialize", true, same, "\"%s\"", static_cast<const char*>(self->levelName));
}
EI_VERIFY(0x0052D6F0, SlotSerializeVerify, 6, s_slotSerialize);

static void(__fastcall* s_fourDwords)(std::uint32_t*, void*, ChunkArchive*);
static void __fastcall FourDwordsVerify(std::uint32_t* values, void*, ChunkArchive* ar) {
    SerializeFourDwords(values, nullptr, ar);
    if (!ar->loading_) return;
    std::uint32_t ours[4];
    std::memcpy(ours, values, sizeof(ours));
    s_fourDwords(values, nullptr, ar);
    ei::verify::Check("SerializeFourDwords", true, std::memcmp(ours, values, sizeof(ours)) == 0, "%u %u %u %u",
                      values[0], values[1], values[2], values[3]);
}
EI_VERIFY(0x0052D6A0, FourDwordsVerify, 6, s_fourDwords);

template <typename Record>
void CheckRecords(const char* name, ChunkArchive* ar, int id, const stlp::vector<Record>* records) {
    if (!ar->loading_) return;
    int chunks = RecordChunks(ar, id);
    ei::verify::Check(name, chunks, static_cast<int>(records->size()), "%d records", chunks);
}

static void __fastcall PrototypesVerify(ChunkArchive* ar, void*, int id, stlp::vector<UnitPrototypeRecord>* records,
                                        void* unused) {
    ar->SerializePrototypes(id, records, unused);
    CheckRecords("ChunkArchive::SerializePrototypes", ar, id, records);
}
EI_VERIFY(0x0053DC10, PrototypesVerify, 0, s_unusedUnitVectors);

static void __fastcall NpcsVerify(ChunkArchive* ar, void*, int id, stlp::vector<UnitNpcRecord>* records, void* unused) {
    ar->SerializeNpcs(id, records, unused);
    CheckRecords("ChunkArchive::SerializeNpcs", ar, id, records);
}
EI_VERIFY(0x0053DD00, NpcsVerify, 0, s_unusedUnitVectors);

static void __fastcall ModelsVerify(ChunkArchive* ar, void*, int id, stlp::vector<UnitModelRecord>* records,
                                    void* unused) {
    ar->SerializeModels(id, records, unused);
    CheckRecords("ChunkArchive::SerializeModels", ar, id, records);
}
EI_VERIFY(0x0053DF50, ModelsVerify, 0, s_unusedUnitVectors);

static void __fastcall UnitLevelsVerify(ChunkArchive* ar, void*, int id, stlp::vector<UnitLevelRecord>* records,
                                        void* unused) {
    ar->SerializeUnitLevels(id, records, unused);
    CheckRecords("ChunkArchive::SerializeUnitLevels", ar, id, records);
}
EI_VERIFY(0x0053E1B0, UnitLevelsVerify, 0, s_unusedUnitVectors);

static void __fastcall CStringsVerify(ChunkArchive* ar, void*, int id, CStringVector* strings, void* unused) {
    ar->SerializeCStrings(id, strings, unused);
    if (!ar->loading_) return;
    int fields = RecordChunks(ar, id);
    ei::verify::Check("ChunkArchive::SerializeCStrings", fields, static_cast<int>(strings->end - strings->begin),
                      "%d strings", fields);
}
EI_VERIFY(0x0052DF40, CStringsVerify, 0, s_unusedUnitVectors);
