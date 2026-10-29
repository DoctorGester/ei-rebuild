// Unit acknowledgements from acks.db: which speech a unit says for an ack kind.
// The database's loading is in model_db_open.cpp and the vector serialisers in model_db.cpp;
// the records' Serialize functions are at the end.
#include "acks_db.h"

#include "chunk_archive.h"
#include "game_singletons.h"
#include "map_object_figures.h"
#include "object_tables.h"
#include "sound.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "random.h"

#include <cstring>

namespace acks {

namespace {



// The roll a speech's chance is compared with: ClientRand() scaled to 0..99.
int Roll() { return ClientRandBelow(100); }

// The ack kinds a party (text-key) unit says differently while it walks slowly (move mode 0 or 1):
// 0..5 and 7..10 become ShopYes, 0x0B..0x13 and OutOfStamina become ShopNo.
AckKind PartyKind(AckKind kind) {
    static const std::uint8_t kGroup[0x1E] = {0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 1, 1, 1,
                                              1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1};
    if (kind > AckKind::OutOfStamina) return kind;
    std::uint8_t group = kGroup[static_cast<std::size_t>(kind)];
    return group == 0 ? AckKind::ShopYes : group == 1 ? AckKind::ShopNo : kind;
}

// The speech list a record keeps for `kind`, or null.
Speeches* ListFor(const AckRecordHead& record, AckKind kind) {
    auto node = record.speeches.find(static_cast<int>(kind));
    return node != record.speeches.end() ? node->second : nullptr;
}

void SpeechFields(SpeechRecord& speech, ChunkArchive* ar) {
    ar->StringField(1, &speech.sound);
    ar->Field(2, &speech.chance);
    ar->StringField(3, &speech.text);
    ar->StringField(4, &speech.name);
}

// UnitAcks' chunks in the order they are written: the list index, or one of these two.
constexpr int kEnvironment = -1;   // environmentSpeeches
constexpr int kNamed = -2;         // namedSpeeches
struct ListChunk {
    std::uint8_t id;
    std::int8_t list;
};
constexpr ListChunk kUnitChunks[] = {
    {10, 0},  {11, 1},  {12, 2},  {13, 3},  {14, 4},  {15, 5},  {16, 6},  {17, 7},  {18, 8},
    {19, 9},  {20, 11}, {21, 13}, {22, 14}, {23, 15}, {24, 16}, {25, 17}, {26, 18}, {27, 19},
    {28, 22}, {29, 23}, {30, 24}, {31, 25}, {32, kEnvironment}, {33, 12}, {34, 20}, {35, 21},
    {36, 10}, {37, 26}, {38, 27}, {39, 28}, {40, 29}, {41, kNamed}, {42, 30}, {43, 31}, {44, 32},
};

// The lists of a record as chunks 10, 11, ... in order.
template <std::size_t N>
void Lists(ChunkArchive* ar, Speeches (&lists)[N]) {
    for (std::size_t i = 0; i < N; ++i) ar->SerializeSpeechList(static_cast<int>(10 + i), &lists[i]);
}

} // namespace

template <typename Record>
Record* AckTable<Record>::Find(std::string_view name) {
    return byName[std::string(name)];
}

template <typename Record>
Record* AckTable<Record>::RecordAt(std::uint32_t index) {
    return index < records.size() ? &records[index] : nullptr;
}
// model_db_open.cpp's AckDatabase::Load reads the extra acks by index.
template ExtraAcks* AckTable<ExtraAcks>::RecordAt(std::uint32_t);

// Counts the eligible entries and takes the chosen one by index: the same speech and the same
// ClientRand() draws as gathering them into a temporary vector first.
const SpeechRecord* Speeches::Pick(int roll) const {
    int count = 0;
    for (const SpeechRecord& s : records)
        if (s.chance >= roll) ++count;
    if (count == 0) return nullptr;
    int chosen = ClientRandBelow(count);
    for (const SpeechRecord* s = records.data();; ++s)
        if (s->chance >= roll && chosen-- == 0) return s;
}

const SpeechRecord* EnvironmentSpeeches::Pick(std::uint32_t environment, int roll) const {
    auto eligible = [&](const EnvironmentSpeech& s) {
        return (s.environment & environment) == environment && s.speech.chance >= roll;
    };
    int count = 0;
    for (const EnvironmentSpeech& s : entries)
        if (eligible(s)) ++count;
    if (count == 0) return nullptr;
    int chosen = ClientRandBelow(count);
    for (const EnvironmentSpeech* s = entries.data();; ++s)
        if (eligible(*s) && chosen-- == 0) return &s->speech;
}

// StopSpeech stops the unit's speech. A party unit walking slowly says its own kinds (PartyKind).
// ArmourWorn / WeaponWorn only play weapons\timecrash.wav at the unit; ArmourBroken / WeaponBroken play
// weapons\crash.wav and go on. Kinds up to Bored come from the unit's prototype record in table 0 (Bored:
// its environment speeches, only while the unit's animation is a run, walk or idle),
// Scenario..Extra2 from table 2, Aggression..FightingRest from table 1 by the prototype's name, else by its
// model's name; others say nothing. The speech is picked at random among those whose chance reaches a roll of 0..99.
const SpeechRecord* SayAck(CUnit* unit, AckKind kind) {
    if (!unit) return nullptr;
    if (kind == AckKind::StopSpeech) {
        unit->Controller()->StopSpeech();
        return nullptr;
    }
    if (IsTextKey(unit->id) && unit->moveMode < 2) kind = PartyKind(kind);
    auto* prototype = unit->GetSecondPrototype();
    if (!prototype) return nullptr;

    if (kind == AckKind::ArmourWorn || kind == AckKind::WeaponWorn) {
        g_clientWorld.sounds->Play3D("weapons\\timecrash.wav", 0, &unit->localPosition.x, 8.0f, 20.0f, 0);
        return nullptr;
    }
    AckDatabase& db = g_ackDatabase;
    const Speeches* list = nullptr;
    const EnvironmentSpeeches* environmentList = nullptr;
    if (kind <= AckKind::Bored) {
        if (kind == AckKind::ArmourBroken || kind == AckKind::WeaponBroken)
            g_clientWorld.sounds->Play3D("weapons\\crash.wav", 0, &unit->localPosition.x, 8.0f, 20.0f, 0);
        UnitAcks* record = db.units.Find(prototype->name);
        if (!record) return nullptr;
        if (kind == AckKind::Bored) {
            environmentList = &record->environmentSpeeches;
        } else {
            list = ListFor(record->head, kind);
            if (!list) return nullptr;
        }
    } else if (kind <= AckKind::Extra2) {
        ExtraAcks* record = db.extras.Find(prototype->name);
        if (!record) return nullptr;
        list = ListFor(record->head, kind);
        if (!list) return nullptr;
    } else if (kind <= AckKind::FightingRest) {
        ModelAcks* record = db.models.Find(prototype->name);
        if (!record) {
            auto* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
            record = db.models.Find(model->name);
            if (!record) return nullptr;
        }
        list = ListFor(record->head, kind);
        if (!list) return nullptr;
    } else {
        return nullptr;
    }

    const SpeechRecord* speech;
    if (environmentList) {
        const FigureAnimation* animation = unit->Object3D()->animation->current;
        if (!animation) return nullptr;
        AnimationKind animationKind = animation->Kind();
        if (animationKind != AnimationKind::Run && animationKind != AnimationKind::Walk &&
            animationKind != AnimationKind::Idle)
            return nullptr;
        std::uint32_t environment = g_clientWorld.EnvironmentFlags(unit);
        speech = environmentList->Pick(environment, Roll());
    } else {
        speech = list->Pick(Roll());
    }
    if (speech) unit->Controller()->Say(speech, kind);
    return speech;
}

const SpeechRecord* SayNamedAck(CUnit* unit, const char* name, bool alternate) {
    if (!unit) return nullptr;
    auto* prototype = unit->GetSecondPrototype();
    if (!prototype) return nullptr;
    UnitAcks* record = g_ackDatabase.units.Find(prototype->name);
    if (!record) return nullptr;
    std::size_t length = std::strlen(name);
    for (const SpeechRecord& s : record->namedSpeeches.records) {
        if (s.name.size() == length && std::memcmp(s.name.data(), name, length) == 0) {
            unit->Controller()->Say(&s, alternate ? AckKind::ScenarioTalking : AckKind::Scenario);
            return &s;
        }
    }
    return nullptr;
}

// ---- Serialising the records (loaded once at startup) ---------------------------------------------

} // namespace acks

using namespace acks;

void ChunkArchive::SerializeSpeechList(int id, Speeches* list) {
    ChunkArchive* ar = this;
    RecordChunks(ar, id, &list->records, SpeechFields);
}
void ChunkArchive::SerializeEnvironmentSpeeches(int id, EnvironmentSpeeches* list) {
    ChunkArchive* ar = this;
    RecordChunks(ar, id, &list->entries, [](EnvironmentSpeech& speech, ChunkArchive* ar) {
        SpeechFields(speech.speech, ar);
        ar->Field(10, &speech.environment);
    });
}

namespace acks {

void UnitAcks::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &head.name);
    for (const ListChunk& chunk : kUnitChunks) {
        if (chunk.list == kEnvironment) ar->SerializeEnvironmentSpeeches(chunk.id, &environmentSpeeches);
        else if (chunk.list == kNamed) ar->SerializeSpeechList(chunk.id, &namedSpeeches);
        else ar->SerializeSpeechList(chunk.id, &lists[chunk.list]);
    }
}

void ModelAcks::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &head.name);
    Lists(ar, lists);
}

void ExtraAcks::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &head.name);
    Lists(ar, lists);
}

} // namespace acks
