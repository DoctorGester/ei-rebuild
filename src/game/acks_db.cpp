// Unit acknowledgements from acks.db (006D8430-006D9F80): which speech a unit says for an ack kind.
// The database's loading (006D7620, Rebirth-patched) and the records' STLport plumbing stay original
// (model_db.cpp has the vector serialisers); the records' Serialize functions are at the end.
#include "acks_db.h"

#include "chunk_archive.h"
#include "map_object_figures.h"
#include "object_tables.h"
#include "originals_engine.h"
#include "sound.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "record_lifetime.h"

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

template <typename Record>
Record* AckTable<Record>::RecordAt(std::uint32_t index) {
    return index < records.size() ? &records[index] : nullptr;
}
EI_REPLACE_METHOD(0x006D8E30, AckTable<ExtraAcks>::RecordAt);

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
    if ((IsTextKey(unit->id_) & 0xFF) && unit->moveMode < 2) kind = PartyKind(kind);
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
        UnitAcks* record = db.units.Find(prototype->name);
        if (!record) return nullptr;
        if (kind == 0x29) {
            environmentList = &record->environmentSpeeches;
        } else {
            list = ListFor(record->head, kind);
            if (!list) return nullptr;
        }
    } else if (kind <= 0x2D) {
        ExtraAcks* record = db.extras.Find(prototype->name);
        if (!record) return nullptr;
        list = ListFor(record->head, kind);
        if (!list) return nullptr;
    } else if (kind <= 0x32) {
        ModelAcks* record = db.models.Find(prototype->name);
        if (!record) {
            auto* model = reinterpret_cast<const UnitModelRecord*>(
                unitdb::Models().RecordAt(static_cast<std::uint32_t>(prototype->modelIndex), unitdb::kModelSize));
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
        std::uint32_t animationKind = animation->key & 0x3C0000;
        if (animationKind != 0x100000 && animationKind != 0x140000 && animationKind != 0x180000) return nullptr;
        std::uint32_t environment = ClientWorld().EnvironmentFlags(unit);
        speech = PickEnvironmentSpeech(environmentList, nullptr, environment, Roll());
    } else {
        speech = PickSpeech(list, nullptr, Roll());
    }
    if (speech) unit->Controller()->Say(speech, static_cast<int>(kind));
    return speech;
}
EI_REPLACE(0x006D8430, SayAck);

const SpeechRecord* __fastcall SayNamedAck(CUnit* unit, const char* name, bool alternate) {
    if (!unit) return nullptr;
    auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype23C());
    if (!prototype) return nullptr;
    UnitAcks* record = Database().units.Find(prototype->name);
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

// ---- Serialising the records (006E3780-006E4F95; loaded once at startup through 006D7620) ----------

namespace {

// vector::resize of the two speech list instantiations (new elements default-constructed).
__declspec(naked) void __fastcall ResizeSpeeches(SpeechList*, void*, unsigned) EI_ORIGINAL(0x006E4560)
__declspec(naked) void __fastcall ResizeEnvironmentSpeeches(stlp::vector<EnvironmentSpeech>*, void*, unsigned)
    EI_ORIGINAL(0x006E47F0)

void SpeechFields(ChunkArchive* ar, SpeechRecord* speech) {
    ar->StringField(1, &speech->sound);
    ar->Field(2, &speech->chance, 4);
    ar->StringField(3, &speech->text);
    ar->StringField(4, &speech->name);
}

// Chunk `id` of a list, if present: loading empties the list (its elements destroyed in order) and
// sizes it to the number of element chunks (id 1); then element i is chunk 1's (i + 1)-th occurrence.
template <typename T, typename Fields>
void ListField(ChunkArchive* ar, int id, stlp::vector<T>* list,
               void(__fastcall* resize)(stlp::vector<T>*, void*, unsigned), Fields fields) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        list->clear();
        count = ar->CountChunks(1);
        resize(list, nullptr, static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(list->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        T* element = &(*list)[static_cast<std::size_t>(i)];
        if (ar->EnterChunk(1)) {
            fields(element);
            ar->LeaveChunk();
        }
    }
    ar->LeaveChunk();
}

} // namespace

// The archive's helpers for the records (thiscall on the archive): 006E4F40 a speech as chunk `id`
// (RET 8), 006E42F0 / 006E4410 a speech list / environment speech list as chunk `id` (RET 0xC; the
// third argument is the archive again, unused). The records inline the same code for some lists.
void __fastcall SpeechField(ChunkArchive* ar, void*, int id, SpeechRecord* speech) {
    if (!ar->EnterChunk(id)) return;
    SpeechFields(ar, speech);
    ar->LeaveChunk();
}
void __fastcall SpeechListField(ChunkArchive* ar, void*, int id, SpeechList* list, ChunkArchive*) {
    ListField(ar, id, list, ResizeSpeeches, [ar](SpeechRecord* speech) { SpeechFields(ar, speech); });
}
void __fastcall EnvironmentSpeechListField(ChunkArchive* ar, void*, int id, stlp::vector<EnvironmentSpeech>* list,
                                           ChunkArchive*) {
    ListField(ar, id, list, ResizeEnvironmentSpeeches, [ar](EnvironmentSpeech* speech) {
        SpeechFields(ar, &speech->speech);
        ar->Field(10, &speech->environment, 4);
    });
}
EI_REPLACE(0x006E4F40, SpeechField);
EI_REPLACE(0x006E42F0, SpeechListField);
EI_REPLACE(0x006E4410, EnvironmentSpeechListField);

namespace {

// UnitAcks' chunks in the order the original writes them: the list index, or one of these two.
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
void Lists(ChunkArchive* ar, SpeechList (&lists)[N]) {
    for (std::size_t i = 0; i < N; ++i) SpeechListField(ar, nullptr, static_cast<int>(10 + i), &lists[i], ar);
}

} // namespace

void UnitAcks::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &head.name);
    for (const ListChunk& chunk : kUnitChunks) {
        if (chunk.list == kEnvironment) EnvironmentSpeechListField(ar, nullptr, chunk.id, &environmentSpeeches, ar);
        else if (chunk.list == kNamed) SpeechListField(ar, nullptr, chunk.id, &namedSpeeches, ar);
        else SpeechListField(ar, nullptr, chunk.id, &lists[chunk.list], ar);
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

EI_REPLACE_METHOD(0x006E3780, UnitAcks::Serialize);
EI_REPLACE_METHOD(0x006E3FD0, ModelAcks::Serialize);
EI_REPLACE_METHOD(0x006E3DC0, ExtraAcks::Serialize);

// ---- The records' destructors (S13) -------------------------------------------------------------

void AckRecordHead::Destruct() {
    speeches.Destroy();
    name.~string();
}

void UnitAcks::Destruct() {
    records::DestroyVector(namedSpeeches);
    records::DestroyVector(environmentSpeeches);
    for (int i = 32; i >= 0; --i) records::DestroyVector(lists[i]);
    head.Destruct();
}
EI_REPLACE_METHOD(0x006DC390, UnitAcks::Destruct);

void ModelAcks::Destruct() {
    for (int i = 4; i >= 0; --i) records::DestroyVector(lists[i]);
    head.Destruct();
}
EI_REPLACE_METHOD(0x006DC920, ModelAcks::Destruct);

void ExtraAcks::Destruct() {
    records::DestroyVector(lists[1]);
    records::DestroyVector(lists[0]);
    head.Destruct();
}
EI_REPLACE_METHOD(0x006DCAF0, ExtraAcks::Destruct);

} // namespace acks
