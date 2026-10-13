#pragma once

// The quest database's records (quest_database.cpp fills them from the quests' .reg files): the
// quests (0x38 bytes) and the briefings (0x6C). The database's archive (006AE3A0, Rebirth-
// patched) writes and reads its two vectors of them through ChunkArchive::SerializeQuests /
// SerializeBriefings (chunk_archive.h), one chunk 1 per record. quest_records.cpp.

#include "unit_db.h"   // CStringVector

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class ChunkArchive;

struct QuestRecord {
    // 006AD7D0 (RET 4). Reads (archive loading) or writes the record: string field 0 the name,
    // 1 exp, 2 +0x10, 3 the zone, string field 4 the text, 5 money, 6 the order, chunk 7 the
    // places (one field 1 per float; loading empties the vector and fills it with that many
    // zeros first).
    void Serialize(ChunkArchive* ar);

    stlp::string name;              // +0x00
    float exp;                      // +0x0C
    std::uint32_t unknown_10;       // +0x10 (0 when loaded from the quests)
    float money;                    // +0x14
    int zone;                       // +0x18 the zone index
    int order;                      // +0x1C
    stlp::vector<float> places;     // +0x20 x and y in turn
    stlp::string text;              // +0x2C
};
static_assert(offsetof(QuestRecord, places) == 0x20);
static_assert(offsetof(QuestRecord, text) == 0x2C);
static_assert(sizeof(QuestRecord) == 0x38);

struct QuestBriefingRecord {
    // 006AD900 (RET 4). Reads or writes the record: string field 0 the name, 1 exp, 2 money,
    // chunk 3 the given items (0052DF40), string field 4 the text, chunks 5-9 the other lists
    // (InlineStrings), 10 +0x68.
    void Serialize(ChunkArchive* ar);

    stlp::string name;              // +0x00
    float exp;                      // +0x0C
    float money;                    // +0x10
    CStringVector lists[6];         // +0x14 give items, remove items, give quests, complete quests, available zones, complete zones
    stlp::string text;              // +0x5C
    std::uint32_t unknown_68;       // +0x68 (0 when loaded from the quests)
};
static_assert(offsetof(QuestBriefingRecord, lists) == 0x14);
static_assert(offsetof(QuestBriefingRecord, text) == 0x5C);
static_assert(sizeof(QuestBriefingRecord) == 0x6C);

// The quest database's briefings (its +0x20): the records and their index by name.
struct QuestBriefingTable {
    using Index = stlp::hash_map<stlp::string, QuestBriefingRecord*, stlp::StringHash>;

    // 006B0600 (RET 4; quest_database.cpp, A-H25; the "reject" and "complete" briefings of a
    // quest file). Appends a copy of `record` unless its name is indexed with a record; the index
    // is then emptied and filled again over the whole vector (the push_back may have moved the
    // records). False for null or a name already there.
    bool Add(const QuestBriefingRecord* record);

    stlp::vector<QuestBriefingRecord> records;   // +0x00
    Index index;                                 // +0x0C
};
static_assert(offsetof(QuestBriefingTable, index) == 0x0C && sizeof(QuestBriefingTable) == 0x20);

class CFileRegistry;   // registry.h
// 006AE740 (__fastcall registry, section; key and list on the stack, RET 8; quest_database.cpp,
// A-H19). The registry's string list `section` / `key` into `list`: the list emptied, reserved for
// the strings' count, then every non-empty string appended (the briefings' lists).
void __fastcall ReadStringList(CFileRegistry* registry, const char* section, const char* key, CStringVector* list);
