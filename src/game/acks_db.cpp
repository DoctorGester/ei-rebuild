// Unit acknowledgements from acks.db (006D8430-006D9F80): which speech a unit says for an ack kind.
// The database's loading (006D7620, Rebirth-patched) and the records' STLport plumbing stay original
// (model_db.cpp has the vector serialisers).
#include "acks_db.h"

#include "map_object_figures.h"
#include "originals_engine.h"
#include "sound.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace acks {

namespace {

CWorldClient& ClientWorld() { return *reinterpret_cast<CWorldClient*>(0x007AF5B8); }

// The roll a speech's chance is compared with: rand() scaled to 0..99.
int Roll() { return gamecrt::rand() * 100 / 0x7FFF % 100; }

// The ack kinds a party (text-key) unit says differently while it walks slowly (move mode 0 or 1):
// 0..5 and 7..10 become 0x27, 0x0B..0x13 and 0x1D become 0x28 (the switch's table at 006D8758).
std::uint32_t PartyKind(std::uint32_t kind) {
    static const std::uint8_t kGroup[0x1E] = {0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 1, 1, 1,
                                              1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1};
    if (kind > 0x1D) return kind;
    return kGroup[kind] == 0 ? 0x27 : kGroup[kind] == 1 ? 0x28 : kind;
}

// The lookups and picks are called through their addresses, so that their verify wrappers run
// (SayAck and SayNamedAck are their only callers).
__declspec(naked) UnitAcks* __fastcall FindUnitAcks(AckTable<UnitAcks>*, void*, const char*) EI_ORIGINAL(0x006D9A30)
__declspec(naked) ModelAcks* __fastcall FindModelAcks(AckTable<ModelAcks>*, void*, const char*) EI_ORIGINAL(0x006D9CE0)
__declspec(naked) ExtraAcks* __fastcall FindExtraAcks(AckTable<ExtraAcks>*, void*, const char*) EI_ORIGINAL(0x006D9F80)
__declspec(naked) const SpeechRecord* __fastcall PickSpeechAt(const SpeechList*, void*, int) EI_ORIGINAL(0x006D8780)
__declspec(naked) const SpeechRecord* __fastcall PickEnvironmentSpeechAt(const stlp::vector<EnvironmentSpeech>*, void*,
                                                                         std::uint32_t, int) EI_ORIGINAL(0x006D88E0)

// The speech list a record keeps for `kind`, or null.
SpeechList* ListFor(const AckRecordHead& record, std::uint32_t kind) {
    auto* node = record.speeches.Find(static_cast<int>(kind));
    return node ? node->value : nullptr;
}

} // namespace

template <typename Record>
Record* AckTable<Record>::Find(const char* name) {
    return byName[stlp::string(name)];
}
EI_REPLACE_METHOD(0x006D9A30, AckTable<UnitAcks>::Find);
EI_REPLACE_METHOD(0x006D9CE0, AckTable<ModelAcks>::Find);
EI_REPLACE_METHOD(0x006D9F80, AckTable<ExtraAcks>::Find);

// The original gathers the eligible entries into a temporary vector first; counting them and
// taking the chosen one by index gives the same speech and the same rand() draws.
const SpeechRecord* __fastcall PickSpeech(const SpeechList* list, void*, int roll) {
    if (!list) return nullptr;
    int count = 0;
    for (const SpeechRecord* s = list->begin(); s != list->end(); ++s)
        if (s->chance >= roll) ++count;
    if (count == 0) return nullptr;
    int chosen = gamecrt::rand() * count / 0x7FFF % count;
    for (const SpeechRecord* s = list->begin();; ++s)
        if (s->chance >= roll && chosen-- == 0) return s;
}
EI_REPLACE(0x006D8780, PickSpeech);

const SpeechRecord* __fastcall PickEnvironmentSpeech(const stlp::vector<EnvironmentSpeech>* list, void*,
                                                     std::uint32_t environment, int roll) {
    if (!list) return nullptr;
    auto eligible = [&](const EnvironmentSpeech& s) {
        return (s.environment & environment) == environment && s.speech.chance >= roll;
    };
    int count = 0;
    for (const EnvironmentSpeech* s = list->begin(); s != list->end(); ++s)
        if (eligible(*s)) ++count;
    if (count == 0) return nullptr;
    int chosen = gamecrt::rand() * count / 0x7FFF % count;
    for (const EnvironmentSpeech* s = list->begin();; ++s)
        if (eligible(*s) && chosen-- == 0) return &s->speech;
}
EI_REPLACE(0x006D88E0, PickEnvironmentSpeech);

// 0xFF stops the unit's speech. A party unit walking slowly says its own kinds (PartyKind). 0x25 /
// 0x26 only play weapons\timecrash.wav at the unit; 0x23 / 0x24 play weapons\crash.wav and go on.
// Kinds up to 0x29 come from the unit's prototype record in table 0 (0x29: its environment
// speeches, only while the unit's animation is of kind 0x100000 / 0x140000 / 0x180000), 0x2A..0x2D
// from table 2, 0x2E..0x32 from table 1 by the prototype's name, else by its model's name; others
// say nothing. The speech is picked at random among those whose chance reaches a roll of 0..99.
const SpeechRecord* __fastcall SayAck(CUnit* unit, int kindArg) {
    if (!unit) return nullptr;
    auto kind = static_cast<std::uint32_t>(kindArg);
    if (kind == 0xFF) {
        unit->Controller()->StopSpeech();
        return nullptr;
    }
    if ((IsTextKeyOriginal(unit->id_) & 0xFF) && unit->moveMode < 2) kind = PartyKind(kind);
    auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype23C());
    if (!prototype) return nullptr;

    if (kind == 0x25 || kind == 0x26) {
        sound::Manager()->Play3D("weapons\\timecrash.wav", 0, &unit->position.x, 8.0f, 20.0f, 0, 0);
        return nullptr;
    }
    AckDatabase& db = Database();
    const SpeechList* list = nullptr;
    const stlp::vector<EnvironmentSpeech>* environmentList = nullptr;
    if (kind <= 0x29) {
        if (kind == 0x23 || kind == 0x24)
            sound::Manager()->Play3D("weapons\\crash.wav", 0, &unit->position.x, 8.0f, 20.0f, 0, 0);
        UnitAcks* record = FindUnitAcks(&db.units, nullptr, prototype->name);
        if (!record) return nullptr;
        if (kind == 0x29) {
            environmentList = &record->environmentSpeeches;
        } else {
            list = ListFor(record->head, kind);
            if (!list) return nullptr;
        }
    } else if (kind <= 0x2D) {
        ExtraAcks* record = FindExtraAcks(&db.extras, nullptr, prototype->name);
        if (!record) return nullptr;
        list = ListFor(record->head, kind);
        if (!list) return nullptr;
    } else if (kind <= 0x32) {
        ModelAcks* record = FindModelAcks(&db.models, nullptr, prototype->name);
        if (!record) {
            auto* model = reinterpret_cast<const UnitModelRecord*>(
                unitdb::Models().At(static_cast<std::uint32_t>(prototype->modelIndex), unitdb::kModelSize));
            record = FindModelAcks(&db.models, nullptr, model->name);
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
        std::uint32_t animationKind = animation->key & 0x3C0000;
        if (animationKind != 0x100000 && animationKind != 0x140000 && animationKind != 0x180000) return nullptr;
        std::uint32_t environment = ClientWorld().EnvironmentFlags(unit);
        speech = PickEnvironmentSpeechAt(environmentList, nullptr, environment, Roll());
    } else {
        speech = PickSpeechAt(list, nullptr, Roll());
    }
    if (speech) unit->Controller()->Say(speech, static_cast<int>(kind));
    return speech;
}
EI_REPLACE(0x006D8430, SayAck);

const SpeechRecord* __fastcall SayNamedAck(CUnit* unit, const char* name, bool alternate) {
    if (!unit) return nullptr;
    auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype23C());
    if (!prototype) return nullptr;
    UnitAcks* record = FindUnitAcks(&Database().units, nullptr, prototype->name);
    if (!record) return nullptr;
    std::size_t length = std::strlen(name);
    for (const SpeechRecord* s = record->namedSpeeches.begin(); s != record->namedSpeeches.end(); ++s) {
        if (s->name.size() == length && std::memcmp(s->name.data(), name, length) == 0) {
            unit->Controller()->Say(s, alternate ? 0x2B : 0x2A);
            return s;
        }
    }
    return nullptr;
}
EI_REPLACE(0x006D8A40, SayNamedAck);

} // namespace acks
