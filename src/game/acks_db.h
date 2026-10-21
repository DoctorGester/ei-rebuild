#pragma once

// acks.db (database.res): what units say when they are selected, ordered or hurt ("acks"), loaded
// once at startup by 006D7620 (Rebirth-patched) into the database at 007C8A60. Three tables of
// records, each a vector of records and an index by name; every record starts with its name and a
// map from the ack kind to one of its speech lists. The speeches are SpeechRecord entries
// (unit_client_models.h) that the unit's controller plays through its slot 51 (Say). Names are
// provisional.

#include <cstddef>
#include <cstdint>

#include "lib/stlp.h"
#include "unit_client_models.h"
#include "game_data.h"

class ChunkArchive;
class CUnit;

namespace acks {

using SpeechList = stlp::vector<SpeechRecord>;

// A speech that also depends on the surroundings (0x2C bytes): eligible when the world's
// environment flags for the unit (CWorldClient::EnvironmentFlags) include all of `environment`.
struct EnvironmentSpeech {
    SpeechRecord speech;            // +0x00
    std::uint32_t environment;      // +0x28
};
static_assert(sizeof(EnvironmentSpeech) == 0x2C);

// The part every record type starts with.
struct AckRecordHead {
    stlp::string name;              // +0x00
    stlp::hash_map<int, SpeechList*, stlp::IntHash> speeches;   // +0x0C by ack kind

    // The compiler's destructor (006DCC50 and inline in the records' ones): the map, then the name.
    void Destruct();
};
static_assert(sizeof(AckRecordHead) == 0x20);

// Table 0 (0x1C4 bytes): per unit prototype name.
struct UnitAcks {
    // 006E3780 (RET 4; acks_db.cpp). The name (field 1), then each list as chunk 10.. (see the
    // order there): chunk {1: speech} per element, the speech's fields 1 sound, 2 chance, 3 text,
    // 4 name (and 10 the environment flags). Loading empties each list first and sizes it to the
    // number of speech chunks.
    void Serialize(ChunkArchive* ar);
    // 006DC390 (S13; the compiler's destructor): the members in reverse, each vector's records
    // destroyed in order before its storage is freed.
    void Destruct();

    AckRecordHead head;                                     // +0x000
    SpeechList lists[33];                                   // +0x020 the speech lists the map points into
    stlp::vector<EnvironmentSpeech> environmentSpeeches;    // +0x1AC kind 0x29
    SpeechList namedSpeeches;                               // +0x1B8 by SpeechRecord::name (006D8A40)
};
static_assert(sizeof(UnitAcks) == 0x1C4);

// Table 1 (0x5C bytes): per unit model name (the fallback of kinds 0x2E..0x32).
struct ModelAcks {
    // 006E3FD0 (RET 4). The name, then the lists as chunks 10..14.
    void Serialize(ChunkArchive* ar);
    // 006DC920 (S13): the lists from the last, then the head.
    void Destruct();

    AckRecordHead head;
    SpeechList lists[5];            // +0x20
};
static_assert(sizeof(ModelAcks) == 0x5C);

// Table 2 (0x38 bytes): per unit prototype name, kinds 0x2A..0x2D.
struct ExtraAcks {
    // 006E3DC0 (RET 4). The name, then the lists as chunks 10 and 11.
    void Serialize(ChunkArchive* ar);
    // 006DCAF0 (S13): the lists from the last, then the head (all inline in the original).
    void Destruct();

    AckRecordHead head;
    SpeechList lists[2];            // +0x20
};
static_assert(sizeof(ExtraAcks) == 0x38);

// A table: the records and the index by name (0x20 bytes).
template <typename Record>
struct AckTable {
    // 006D9A30 / 006D9CE0 / 006D9F80 (RET 4): the record named `name`. The index's operator[], so
    // a missing name is added with a null record (and null is returned).
    Record* Find(const char* name);
    // 006D8E30 (RET 4; the extras' table only, from the loader 006D7620): record `index`, null
    // past the end.
    Record* RecordAt(std::uint32_t index);

    stlp::vector<Record> records;                                   // +0x00
    stlp::hash_map<stlp::string, Record*, stlp::StringHash> byName; // +0x0C
};
static_assert(sizeof(AckTable<UnitAcks>) == 0x20);

struct AckDatabase {
    // 006D7620 (RET 4; model_db_open.cpp, A-H26; Rebirth-patched, hooked in the vanilla launch
    // only). "acks.db" of `folder` + "database.res": the three tables, each read through an
    // archive of its own and filed under its records' names; then every record's map from the
    // ack kind to its speech list is filled (units: kinds 0..0x15, 0x1C..0x24, 0x27, 0x28 and the
    // named speeches as 0x2A, with 0x16..0x1B entered as null; models: 0x2E..0x32; extras: 0x2C,
    // 0x2D). 1; FatalError's result when the archive (reported as "databases.res") or the file
    // cannot be opened.
    int Load(const char* folder);
    // 006D7220 (database_construct.cpp; the static initialiser 006D7080). The three tables empty,
    // each index with 100 buckets (193). Returns this.
    AckDatabase* Construct();
    // 006D70B0 (database_destruct.cpp, S3c; the exit function 006D70A0). The tables from the
    // last: the index's nodes and buckets, then the records and their storage.
    void Destruct();

    AckTable<UnitAcks> units;       // +0x00
    AckTable<ModelAcks> models;     // +0x20
    AckTable<ExtraAcks> extras;     // +0x40
};
inline AckDatabase& Database() { return ei::GameData<AckDatabase>(0x007C8A60); }

// The archive's helpers for the records (acks_db.cpp; thiscall on the archive): 006E4F40 a speech
// as chunk `id` (RET 8), 006E42F0 / 006E4410 a speech list / environment speech list as chunk `id`
// (RET 0xC; the last argument, the archive again, is unused).
void __fastcall SpeechField(ChunkArchive* ar, void*, int id, SpeechRecord* speech);
void __fastcall SpeechListField(ChunkArchive* ar, void*, int id, SpeechList* list, ChunkArchive*);
void __fastcall EnvironmentSpeechListField(ChunkArchive* ar, void*, int id, stlp::vector<EnvironmentSpeech>* list,
                                           ChunkArchive*);

// 006D8780 (thiscall on the list, RET 4): a random speech among those whose chance is at least
// `roll` (the game's rand()); null, without drawing, when there is none.
const SpeechRecord* __fastcall PickSpeech(const SpeechList* list, void*, int roll);
// 006D88E0 (RET 8): the same among the speeches whose environment flags `environment` includes.
const SpeechRecord* __fastcall PickEnvironmentSpeech(const stlp::vector<EnvironmentSpeech>* list, void*,
                                                     std::uint32_t environment, int roll);

// 006D8430 (__fastcall, from the interface 006366F0). The unit says ack `kind`: see acks_db.cpp.
// Returns the speech given to the controller's Say, or null.
const SpeechRecord* __fastcall SayAck(CUnit* unit, int kind);
// 006D8A40 (__fastcall, RET 4, from 006366F0). The unit says its named speech `name` as kind 0x2A
// (0x2B with `alternate`). Returns it, or null.
const SpeechRecord* __fastcall SayNamedAck(CUnit* unit, const char* name, bool alternate);

} // namespace acks
