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
};
static_assert(sizeof(AckRecordHead) == 0x20);

// Table 0 (0x1C4 bytes): per unit prototype name.
struct UnitAcks {
    AckRecordHead head;                                     // +0x000
    std::uint8_t unknown_020[0x1AC - 0x20];                 // the speech lists the map points into
    stlp::vector<EnvironmentSpeech> environmentSpeeches;    // +0x1AC kind 0x29
    SpeechList namedSpeeches;                               // +0x1B8 by SpeechRecord::name (006D8A40)
};
static_assert(sizeof(UnitAcks) == 0x1C4);

// Table 1 (0x5C bytes): per unit model name (the fallback of kinds 0x2E..0x32).
struct ModelAcks {
    AckRecordHead head;
    std::uint8_t unknown_020[0x5C - 0x20];
};
static_assert(sizeof(ModelAcks) == 0x5C);

// Table 2 (0x38 bytes): per unit prototype name, kinds 0x2A..0x2D.
struct ExtraAcks {
    AckRecordHead head;
    std::uint8_t unknown_020[0x38 - 0x20];
};
static_assert(sizeof(ExtraAcks) == 0x38);

// A table: the records and the index by name (0x20 bytes).
template <typename Record>
struct AckTable {
    // 006D9A30 / 006D9CE0 / 006D9F80 (RET 4): the record named `name`. The index's operator[], so
    // a missing name is added with a null record (and null is returned).
    Record* Find(const char* name);

    stlp::vector<Record> records;                                   // +0x00
    stlp::hash_map<stlp::string, Record*, stlp::StringHash> byName; // +0x0C
};
static_assert(sizeof(AckTable<UnitAcks>) == 0x20);

struct AckDatabase {
    AckTable<UnitAcks> units;       // +0x00
    AckTable<ModelAcks> models;     // +0x20
    AckTable<ExtraAcks> extras;     // +0x40
};
inline AckDatabase& Database() { return *reinterpret_cast<AckDatabase*>(0x007C8A60); }

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
