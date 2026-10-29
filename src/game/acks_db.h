#pragma once

// acks.db (database.res): what units say when they are selected, ordered or hurt ("acks"), loaded once at
// startup into g_ackDatabase. Three tables of records, each a vector of records and an index by name; every
// record starts with its name and a map from the ack kind to one of its speech lists. The speeches are
// SpeechRecord entries that the unit's controller plays through Say.

#include <cstddef>
#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <unordered_map>

#include "ack_kind.h"
#include "unit_client_models.h"

namespace acks {

struct Speeches : std::vector<SpeechRecord> {
    // A random speech among those whose chance is at least `roll` (the game's rand()); null, without drawing,
    // when there is none. A null list answers null too.
    const SpeechRecord* Pick(int roll) const;
};

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

// The part every record type starts with. By default an empty name and an empty map. A copy's map still points
// into the source record's lists, as in the game.
struct AckRecordHead {
    std::string name;
    // By ack kind. The kinds are below 0x33, so STLport's 193 buckets walked them ascending, as this does (the
    // acks dump, GenerateAcks, lists them so).
    std::map<int, Speeches*> speeches;
};

// Table 0: per unit prototype name.
struct UnitAcks {
    // The name (field 1), then each list as chunk 10.. (see the order in acks_db.cpp): chunk {1: speech} per
    // element, the speech's fields 1 sound, 2 chance, 3 text, 4 name (and 10 the environment flags). Loading
    // empties each list first and sizes it to the number of speech chunks.
    void Serialize(ChunkArchive* ar);

    AckRecordHead head;
    Speeches lists[33];                                   // the speech lists the map points into
    EnvironmentSpeeches environmentSpeeches;                // kind 0x29
    Speeches namedSpeeches;                               // by SpeechRecord::name
};

// Table 1: per unit model name (the fallback of kinds 0x2E..0x32).
struct ModelAcks {
    // The name, then the lists as chunks 10..14.
    void Serialize(ChunkArchive* ar);

    AckRecordHead head;
    Speeches lists[5];
};

// Table 2: per unit prototype name, kinds 0x2A..0x2D.
struct ExtraAcks {
    // The name, then the lists as chunks 10 and 11.
    void Serialize(ChunkArchive* ar);

    AckRecordHead head;
    Speeches lists[2];
};

// A table: the records and the index by name.
template <typename Record>
struct AckTable {
    // The index's operator[], so a missing name is added with a null record (and null is returned).
    Record* Find(std::string_view name);
    // Null past the end (the extras' table only, used by the loader).
    Record* RecordAt(std::uint32_t index);

    std::vector<Record> records;
    std::unordered_map<std::string, Record*> byName;
};

struct AckDatabase {
    // "acks.db" of `folder` + "database.res": the three tables, each read through an archive of its own and
    // filed under its records' names; then every record's map from the ack kind to its speech list is filled
    // (units: kinds 0..0x15, 0x1C..0x24, 0x27, 0x28 and the named speeches as 0x2A, with 0x16..0x1B entered as
    // null; models: 0x2E..0x32; extras: 0x2C, 0x2D). 1; FatalError's result when the archive (reported as
    // "databases.res") or the file cannot be opened.
    int Load(const std::string& folder);

    AckTable<UnitAcks> units;
    AckTable<ModelAcks> models;
    AckTable<ExtraAcks> extras;
};
} // namespace acks
extern acks::AckDatabase g_ackDatabase;
namespace acks {

// The unit says ack `kind` (see acks_db.cpp). Returns the speech given to the controller's Say, or null.
const SpeechRecord* SayAck(CUnit* unit, AckKind kind);
// The unit says its named speech `name` as kind 0x2A (0x2B with `alternate`). Returns it, or null.
const SpeechRecord* SayNamedAck(CUnit* unit, const char* name, bool alternate);
} // namespace acks
