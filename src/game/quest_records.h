#pragma once

// The quest database's records (quest_database.cpp fills them from the quests' .reg files): the
// quests and the briefings (0x6C). The database's archive load (Rebirth-
// patched) writes and reads its two vectors of them through ChunkArchive::SerializeQuests /
// SerializeBriefings (chunk_archive.h), one chunk 1 per record. quest_records.cpp.

#include "unit_db.h"   // InlineStrings

#include "lib/stlp.h"
#include "lib/static_object.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class ChunkArchive;

struct QuestRecord {
    // Reads (archive loading) or writes the record: string field 0 the name, 1 exp, 2 +0x10, 3 the zone,
    // string field 4 the text, 5 money, 6 the order, chunk 7 the places (one field 1 per float; loading
    // empties the vector and fills it with that many zeros first).
    void Serialize(ChunkArchive* ar);
    // The compiler's destructor. The text, the places' storage, the name.
    void Destruct();
    // The copy constructor: each member copied in order (the places
    // copy-constructed). operator= (inlined in the vector's code): each member assigned. The vector's
    // resize(n) temporary: empty strings with room for 8 bytes, no places.
    void CopyConstruct(const QuestRecord* source);
    void AssignQuest(const QuestRecord* source);
    void ConstructDefault();

    std::string name;
    float exp;
    std::uint32_t field2;           // archive field 2 (0 when loaded from the quests); nothing else reads it
    float money;
    int zone;                       // the zone index
    int order;
    std::vector<float> places;     // x and y in turn
    std::string text;
};

struct QuestBriefingRecord {
    // Reads or writes the record: string field 0 the name, 1 exp, 2 money, chunk 3 the given items, string
    // field 4 the text, chunks 5-9 the other lists (InlineStrings), 10 +0x68.
    void Serialize(ChunkArchive* ar);
    // The compiler's destructor. The text, the lists from the last (each string, then the storage), the name.
    void Destruct();
    // The copy constructor, operator= (inlined in the vector's code) and the vector's
    // resize(n) temporary (empty strings with room for 8 bytes, empty lists), as the quest's.
    void CopyConstruct(const QuestBriefingRecord* source);
    void AssignBriefing(const QuestBriefingRecord* source);
    void ConstructDefault();

    // The lists (lists[k]): item names given and taken, quest variables ("a.b.<quest>") given and
    // completed, zone variables raised to 1 and set to 2 (CWorldServer::CompleteBriefing).
    enum ListKind { kGiveItems, kRemoveItems, kGiveQuests, kCompleteQuests, kAvailableZones, kCompleteZones };

    std::string name;
    float exp;
    float money;
    std::vector<std::string> lists[6];   // by ListKind
    std::string text;
    // +0x68 archive field 10 (0 when loaded from the quests): the briefing's specials, text
    // "brief_get_specials_<n>" when above 0 (the dialogue's rewards).
    std::int32_t specials;
};

// The quest database's briefings (its +0x20): the records and their index by name.
struct QuestBriefingTable {
    using Index = stlp::hash_map<std::string, QuestBriefingRecord*, stlp::StringHash>;

    // QuestBriefingTable::Add (quest_database.cpp; the "reject" and "complete" briefings of a
    // quest file). Appends a copy of `record` unless its name is indexed with a record; the index
    // is then emptied and filled again over the whole vector (the push_back may have moved the
    // records). False for null or a name already there.
    bool Add(const QuestBriefingRecord* record);

    stlp::vector<QuestBriefingRecord> records;
    Index index;
};
static_assert(offsetof(QuestBriefingTable, index) == 0x0C && sizeof(QuestBriefingTable) == 0x20);

// The quest database: the quests and the briefings, each with its index by name.
struct QuestDatabase {
    using QuestIndex = stlp::hash_map<std::string, QuestRecord*, stlp::StringHash>;

    // Rebirth-patched; called by WinMain at startup. "quests.qdb" of `folder` + "database.res": the quests
    // (chunk 1) and the briefings (chunk 2), each read through an archive of its own and filed under its
    // records' names. 1; FatalError's result when the archive (reported as "databases.res") or the file cannot
    // be opened.
    int Load(const char* folder);
    // The constructor (database_construct.cpp; run by the static initialiser). Both vectors empty, both
    // indexes with 100 buckets (193). Returns this.
    QuestDatabase* Construct();
    // The quests of a registry file in memory (`image`) filed into the database; false when it is not a
    // registry.
    bool LoadRegistry(const void* image);
    // The destructor (database_destruct.cpp; run by the exit function). The members from the
    // last: each index's nodes and buckets, then its records and their storage.
    void Destruct();
    // script_quest.cpp (and a copy of it; the script commands' quest lookup, QuestComplete awards the
    // record's +0x0C). The record of quest `name`, null if none. The index's operator[] (the game's
    // find_or_insert on a {name, null} pair) leaves a null entry for a missing name.
    QuestRecord* FindQuest(const char* name);

    stlp::vector<QuestRecord> quests;
    QuestIndex questsByName;
    QuestBriefingTable briefings;
};
static_assert(offsetof(QuestDatabase, briefings) == 0x20 && sizeof(QuestDatabase) == 0x40);
extern lib::StaticObject<QuestDatabase> g_questDatabase;   // database_globals.cpp
inline QuestDatabase& TheQuestDatabase() { return *g_questDatabase; }


class CFileRegistry;
// (__fastcall registry, section; key and list on the stack; quest_database.cpp).
// The registry's string list `section` / `key` into `list`: the list emptied, reserved for
// the strings' count, then every non-empty string appended (the briefings' lists).
void ReadStringList(CFileRegistry* registry, const char* section, const char* key, std::vector<std::string>* list);
