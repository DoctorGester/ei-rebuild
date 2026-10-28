#pragma once

// acks.db (database.res): what units say when they are selected, ordered or hurt ("acks"), loaded once at
// startup into g_ackDatabase. Three tables of records, each a vector of records and an index by name; every
// record starts with its name and a map from the ack kind to one of its speech lists. The speeches are
// SpeechRecord entries that the unit's controller plays through Say.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "lib/stlp.h"
#include "lib/static_object.h"
#include "unit_client_models.h"
#include "game_data.h"

namespace acks {

struct Speeches : std::vector<SpeechRecord> {
    // A random speech among those whose chance is at least `roll` (the game's rand()); null, without drawing,
    // when there is none. A null list answers null too.
    const SpeechRecord* Pick(int roll) const;
};
static_assert(sizeof(Speeches) == 0xC);
using SpeechList = Speeches;

// A speech that also depends on the surroundings: eligible when the world's environment flags for the unit
// (CWorldClient::EnvironmentFlags) include all of `environment`.
struct EnvironmentSpeech {
    SpeechRecord speech;
    std::uint32_t environment;
};

struct EnvironmentSpeeches : std::vector<EnvironmentSpeech> {
    // The same among the speeches whose environment flags include `environment`.
    const SpeechRecord* Pick(std::uint32_t environment, int roll) const;
};
static_assert(sizeof(EnvironmentSpeeches) == 0xC);

// The part every record type starts with. By default an empty name and an empty map of 193 buckets (its functor
// bytes are an uninitialised stack byte in the game, 0 here). A copy's map still points into the source record's
// lists (STLport's copy constructor and operator=, over _M_copy_from), as in the game.
struct AckRecordHead {
    AckRecordHead() { speeches.Construct(100, 0, 0, 0); }
    AckRecordHead(const AckRecordHead& from) : name(from.name) { speeches.CopyConstruct(from.speeches); }
    AckRecordHead& operator=(const AckRecordHead& from) {
        name = from.name;
        speeches.Assign(from.speeches);
        return *this;
    }
    ~AckRecordHead() { speeches.Destroy(); }

    std::string name;
    stlp::hash_map<int, SpeechList*, stlp::IntHash> speeches;   // by ack kind
};

// Table 0: per unit prototype name.
struct UnitAcks {
    // The name (field 1), then each list as chunk 10.. (see the order in acks_db.cpp): chunk {1: speech} per
    // element, the speech's fields 1 sound, 2 chance, 3 text, 4 name (and 10 the environment flags). Loading
    // empties each list first and sizes it to the number of speech chunks.
    void Serialize(ChunkArchive* ar);

    AckRecordHead head;
    SpeechList lists[33];                                   // the speech lists the map points into
    EnvironmentSpeeches environmentSpeeches;                // kind 0x29
    SpeechList namedSpeeches;                               // by SpeechRecord::name
};

// Table 1: per unit model name (the fallback of kinds 0x2E..0x32).
struct ModelAcks {
    // The name, then the lists as chunks 10..14.
    void Serialize(ChunkArchive* ar);

    AckRecordHead head;
    SpeechList lists[5];
};

// Table 2: per unit prototype name, kinds 0x2A..0x2D.
struct ExtraAcks {
    // The name, then the lists as chunks 10 and 11.
    void Serialize(ChunkArchive* ar);

    AckRecordHead head;
    SpeechList lists[2];
};

// A table: the records and the index by name.
template <typename Record>
struct AckTable {
    // The index's operator[], so a missing name is added with a null record (and null is returned).
    Record* Find(const char* name);
    // Null past the end (the extras' table only, used by the loader).
    Record* RecordAt(std::uint32_t index);

    std::vector<Record> records;
    stlp::hash_map<std::string, Record*, stlp::StringHash> byName;
};
static_assert(sizeof(AckTable<UnitAcks>) == 0x20);

struct AckDatabase {
    // "acks.db" of `folder` + "database.res": the three tables, each read through an archive of its own and
    // filed under its records' names; then every record's map from the ack kind to its speech list is filled
    // (units: kinds 0..0x15, 0x1C..0x24, 0x27, 0x28 and the named speeches as 0x2A, with 0x16..0x1B entered as
    // null; models: 0x2E..0x32; extras: 0x2C, 0x2D). 1; FatalError's result when the archive (reported as
    // "databases.res") or the file cannot be opened.
    int Load(const char* folder);
    // Each index with 193 buckets. Returns this.
    AckDatabase* Construct();
    void Destruct();

    AckTable<UnitAcks> units;
    AckTable<ModelAcks> models;
    AckTable<ExtraAcks> extras;
};
static_assert(sizeof(AckDatabase) == 0x60);
} // namespace acks
extern lib::StaticObject<acks::AckDatabase> g_ackDatabase;
namespace acks {
inline AckDatabase& Database() { return *g_ackDatabase; }

// The unit says ack `kind` (see acks_db.cpp). Returns the speech given to the controller's Say, or null.
const SpeechRecord* SayAck(CUnit* unit, int kind);
// The unit says its named speech `name` as kind 0x2A (0x2B with `alternate`). Returns it, or null.
const SpeechRecord* SayNamedAck(CUnit* unit, const char* name, bool alternate);

} // namespace acks
