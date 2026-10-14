// Checks for quest_records.cpp (the quest database's records in an archive: 006AD7D0, 006AD900
// and the vectors 006B1F20 / 006B2010; reached when the database is saved with a game and read
// back: ui_saveload, world_load).
//
// A record's loading reads fields by id and leaves the archive as it was, so reading twice gives
// the same record (as verify_unit_db.cpp does): ours reads into the real record, its values are
// taken (the plain fields and a digest of the strings, lists and places), then the original reads
// into the same record again and the two are compared; the record keeps the original's reading.
// Writing appends to the archive, and the vectors allocate records: ours only (the current chunk
// is the same afterwards; a loaded vector's records each went through the record's wrapper).
#include "verify.h"

#include "../../src/game/chunk_archive.h"
#include "../../src/game/quest_records.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

struct Snapshot {
    std::uint32_t values[6];
    std::uint64_t digest;
};

void Mix(std::uint64_t& digest, const void* bytes, std::size_t length) {
    const auto* p = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < length; ++i) digest = (digest ^ p[i]) * 0x100000001B3ull;
    digest = (digest ^ 0x100) * 0x100000001B3ull;   // separator
}

std::uint32_t Bits(const void* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

Snapshot Take(const QuestRecord* r) {
    Snapshot s{};
    s.values[0] = Bits(&r->exp);
    s.values[1] = r->field2;
    s.values[2] = Bits(&r->money);
    s.values[3] = static_cast<std::uint32_t>(r->zone);
    s.values[4] = static_cast<std::uint32_t>(r->order);
    s.values[5] = static_cast<std::uint32_t>(r->places.size());
    s.digest = 0xCBF29CE484222325ull;
    Mix(s.digest, r->name.data(), r->name.size());
    Mix(s.digest, r->text.data(), r->text.size());
    Mix(s.digest, r->places.begin(), r->places.size() * sizeof(float));
    return s;
}

Snapshot Take(const QuestBriefingRecord* r) {
    Snapshot s{};
    s.values[0] = Bits(&r->exp);
    s.values[1] = Bits(&r->money);
    s.values[2] = r->field10;
    s.digest = 0xCBF29CE484222325ull;
    Mix(s.digest, r->name.data(), r->name.size());
    Mix(s.digest, r->text.data(), r->text.size());
    for (const CStringVector& list : r->lists) {
        s.digest = (s.digest ^ static_cast<std::uint64_t>(list.size())) * 0x100000001B3ull;
        for (const mfc::CString* text = list.begin(); text != list.end(); ++text)
            Mix(s.digest, static_cast<const char*>(*text), static_cast<std::size_t>(text->GetLength()));
    }
    return s;
}

template <typename Record>
void SerializeVerify(const char* name, void(__fastcall* original)(Record*, void*, ChunkArchive*), Record* self,
                     ChunkArchive* ar) {
    ChunkArchive::Ref* before = ar->Current();
    self->Serialize(ar);
    if (!ar->loading_) {
        ei::verify::Check(name, true, ar->Current() == before, "save");
        return;
    }
    Snapshot ours = Take(self);
    original(self, nullptr, ar);
    Snapshot theirs = Take(self);
    int diff = -1;
    for (int i = 0; i < 6 && diff < 0; ++i)
        if (ours.values[i] != theirs.values[i]) diff = i;
    bool same = diff < 0 && ours.digest == theirs.digest;
    ei::verify::Check(name, true, same, "%.*s: %s %d", static_cast<int>(self->name.size()), self->name.data(),
                      diff < 0 ? "texts differ" : "value", diff);
}

} // namespace

// 006AD7D0: PUSH ESI; MOV ESI,[ESP+8]; PUSH EDI (6 bytes).
static void(__fastcall* s_questSerialize)(QuestRecord*, void*, ChunkArchive*);
static void __fastcall QuestSerializeVerify(QuestRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("QuestRecord::Serialize", s_questSerialize, self, ar);
}
EI_VERIFY(0x006AD7D0, QuestSerializeVerify, 6, s_questSerialize);

// 006AD900: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x14] (8 bytes).
static void(__fastcall* s_briefingSerialize)(QuestBriefingRecord*, void*, ChunkArchive*);
static void __fastcall BriefingSerializeVerify(QuestBriefingRecord* self, void*, ChunkArchive* ar) {
    SerializeVerify("QuestBriefingRecord::Serialize", s_briefingSerialize, self, ar);
}
EI_VERIFY(0x006AD900, BriefingSerializeVerify, 8, s_briefingSerialize);

// 006B1F20 / 006B2010 (stolen 0): the current chunk is the same afterwards.
static void __fastcall SerializeQuestsVerify(ChunkArchive* self, void*, int id, stlp::vector<QuestRecord>* quests, void* unused) {
    ChunkArchive::Ref* before = self->Current();
    self->SerializeQuests(id, quests, unused);
    ei::verify::Check("ChunkArchive::SerializeQuests", true, self->Current() == before, "%u quests",
                      static_cast<unsigned>(quests->size()));
}
EI_VERIFY(0x006B1F20, SerializeQuestsVerify, 0, s_unused);

static void __fastcall SerializeBriefingsVerify(ChunkArchive* self, void*, int id, stlp::vector<QuestBriefingRecord>* briefings,
                                                void* unused) {
    ChunkArchive::Ref* before = self->Current();
    self->SerializeBriefings(id, briefings, unused);
    ei::verify::Check("ChunkArchive::SerializeBriefings", true, self->Current() == before, "%u briefings",
                      static_cast<unsigned>(briefings->size()));
}
EI_VERIFY(0x006B2010, SerializeBriefingsVerify, 0, s_unused);
