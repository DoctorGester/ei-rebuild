// The units database records' serialisers: unit prototypes (0052D740), unit models (0052DC70) and NPCs
// (0052DA60); the skill (perk) database's records, vectors and lookups (A-U10).
// See unit_db.h and docs/notes/units.md ("Database records").
#include "chunk_archive.h"
#include "unit_db.h"

#include "model.h"   // ChunkArchive

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_engine.h"
#include "record_lifetime.h"

namespace {

// The archive's field primitives (chunk_archive.cpp), through their game addresses: a field of
// `size` bytes (missing ones read as zero), a CString field, entering / leaving chunk `id`.
// Not rewritten: 0052DF40 (ArchiveCStringsOriginal, originals_library.h); 0052D6A0, four dwords
// as fields 0..3; 0052D6F0, a model's body slot as fields 0..3.
// The pieces of 0052DF40 that the NPC serialiser inlines: counting the fields `id` of the current
// chunk [00409A60, rewritten in chunk_archive.cpp], emptying a vector<CString> [0052E010] and
// resizing it [0051F5B0].
void __fastcall StringsClear(CStringVector* a0) { CStringVectorClearOriginal((void*)a0); }

void Dword(ChunkArchive* ar, int id, void* data) { ar->Field(id, data, 4); }

} // namespace

void InlineStrings(ChunkArchive* ar, int id, CStringVector* strings) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        StringsClear(strings);
        count = ar->CountChunks(1);
        CStringVectorResizeOriginal(strings, nullptr, static_cast<std::uint32_t>(count));
    } else {
        count = static_cast<int>(strings->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ar->CStringField(1, &strings->begin()[i]);
    }
    ar->LeaveChunk();
}

void UnitPrototypeRecord::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, &name);
    ar->CStringField(1, &modelName);
    Dword(ar, 2, &modelIndex);
    Dword(ar, 3, &skin);
    Dword(ar, 4, &hair);
    Dword(ar, 5, &scale.x);
    Dword(ar, 6, &scale.y);
    Dword(ar, 7, &scale.z);
    ar->CStringField(8, &soundFolder);
    Dword(ar, 9, &hitPoints);
    Dword(ar, 10, &mana);
    Dword(ar, 11, &protectionScale);
    Dword(ar, 12, &field12);
    Dword(ar, 13, &speed);
    Dword(ar, 14, &combat[0]);
    Dword(ar, 15, &combat[1]);
    Dword(ar, 16, &combat[3]);
    Dword(ar, 17, &combat[4]);
    Dword(ar, 0x12, &naturalWeapon);
    Dword(ar, 0x13, &damage);
    Dword(ar, 0x14, &damageSpread);
    Dword(ar, 0x15, &skillLevel);
    Dword(ar, 0x16, &attributes01);
    Dword(ar, 0x17, &attribute2);
    Dword(ar, 0x18, &peripheralRange);
    ar->Field(0x19, valuesB0, sizeof(valuesB0));
    ar->Field(0x1A, valuesE0, sizeof(valuesE0));
    if (ar->EnterChunk(0x1B)) {
        Dword(ar, 0, &gold.chance);
        Dword(ar, 1, &gold.field1);
        Dword(ar, 2, &gold.minimum);
        Dword(ar, 3, &gold.maximum);
        ar->LeaveChunk();
    }
    if (ar->EnterChunk(0x1C)) {
        SerializeFourDwords(reinterpret_cast<unsigned int*>(&secondGold), nullptr, ar);
        ar->LeaveChunk();
    }
    ar->SerializeCStrings(0x1D, &lootItems, ar);
    ar->Field(0x1E, skills3to5, sizeof(skills3to5));
    ar->SerializeCStrings(0x1F, &spellNames, ar);
    ar->SerializeCStrings(0x20, &armourNames, ar);
    ar->CStringField(0x21, &weaponName);
    Dword(ar, 0x22, &panelViewSize);
    Dword(ar, 0x23, &height);
    Dword(ar, 0x24, &combat[2]);
    Dword(ar, 0x25, &viewDistance);
    Dword(ar, 0x26, &viewHeight);
    Dword(ar, 0x27, &hitSoundKind);
    Dword(ar, 0x28, &shake);
    Dword(ar, 0x29, &level);
    ar->CStringField(0x2A, &secondWeaponName);
    Dword(ar, 0x2B, &experienceValue);
}
EI_REPLACE_METHOD(0x0052D740, UnitPrototypeRecord::Serialize);

void UnitModelRecord::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, &name);
    Dword(ar, 1, &objectClass);
    Dword(ar, 2, &hitPointRegen);
    Dword(ar, 3, &manaRegen);
    Dword(ar, 4, &field4);
    Dword(ar, 5, &movementKind);
    Dword(ar, 6, &angle);
    ar->Field(7, modeSpeeds, sizeof(modeSpeeds));
    Dword(ar, 8, &field8);
    Dword(ar, 9, &sizeClass);
    Dword(ar, 10, &sizeClassSmall);
    ar->Field(11, damageWeights, sizeof(damageWeights));
    for (int i = 0; i < 6; ++i) {
        if (ar->EnterChunk(0x0C + i)) {
            slots[i].Serialize(ar);
            ar->LeaveChunk();
        }
    }
    ar->CStringField(0x1E, &figureName);
    ar->SerializeCStrings(0x1F, &field1F, ar);
    ar->SerializeCStrings(0x20, &field20, ar);
    Dword(ar, 0x21, &height);
    ar->CStringField(0x22, &soundFolder);
    ar->SerializeCStrings(0x23, &soundNames, ar);
    ar->Field(0x24, actionSpeeds, sizeof(actionSpeeds));
    Dword(ar, 0x25, &idleSoundChance);
    Dword(ar, 0x26, &attackSoundChance);
    ar->Field(0x27, protection, sizeof(protection));
    Dword(ar, 0x28, &bloodKind);
    Dword(ar, 0x29, &castEffect);
    Dword(ar, 0x2A, &footprintKind);
    Dword(ar, 0x2B, &footPart);
    ar->CStringField(0x2C, &hitMaterial);
    ar->Field(0x2D, &swapFeet, 1);
    Dword(ar, 0x2E, &sizeScale);
    Dword(ar, 0x2F, &eyeLeft);
    Dword(ar, 0x30, &eyeBottom);
    Dword(ar, 0x31, &eyeWidth);
    Dword(ar, 0x32, &eyeHeight);
}
EI_REPLACE_METHOD(0x0052DC70, UnitModelRecord::Serialize);

void UnitNpcRecord::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, &name);
    Dword(ar, 1, &field1);
    Dword(ar, 2, &experience);
    Dword(ar, 3, &stats[0]);
    Dword(ar, 4, &stats[1]);
    Dword(ar, 5, &stats[2]);
    ar->Field(6, attributes, sizeof(attributes));
    ar->Field(7, skills, sizeof(skills));
    ar->SerializeCStrings(8, &skillNames, ar);
    InlineStrings(ar, 9, &startWeapons);
    InlineStrings(ar, 10, &startQuickItems);
    InlineStrings(ar, 11, &startSpells);
    Dword(ar, 12, &experienceFloor);
    Dword(ar, 13, &startMoney);
    ar->Field(14, &voiced, 1);
}
EI_REPLACE_METHOD(0x0052DA60, UnitNpcRecord::Serialize);

// --- A-U11: the unit database's pieces and vectors -----------------------------------------------

void __fastcall SerializeFourDwords(std::uint32_t* values, void*, ChunkArchive* ar) {
    for (int i = 0; i < 4; ++i) Dword(ar, i, &values[i]);
}
EI_REPLACE(0x0052D6A0, SerializeFourDwords);

void UnitModelSlot::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, &levelName);
    Dword(ar, 1, &levelIndex);
    Dword(ar, 2, &share);
    Dword(ar, 3, &factor);
}
EI_REPLACE_METHOD(0x0052D6F0, UnitModelSlot::Serialize);

void UnitLevelRecord::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, &name);
    ar->Field(1, valuesA, sizeof(valuesA));
    ar->Field(2, valuesB, sizeof(valuesB));
    Dword(ar, 3, &vitalMask);
    Dword(ar, 4, &slotType);
}
EI_REPLACE_METHOD(0x0052DEE0, UnitLevelRecord::Serialize);

void ChunkArchive::SerializeCStrings(int id, stlp::vector<mfc::CString>* strings, void*) { InlineStrings(this, id, strings); }
EI_REPLACE_METHOD(0x0052DF40, ChunkArchive::SerializeCStrings);

namespace {

// The records' Serialize and destructors through their game addresses (their verify wrappers stay in
// the path); the vectors' resizes are STLport code.
__declspec(naked) void __fastcall PrototypeSerializeAt(UnitPrototypeRecord*, void*, ChunkArchive*) EI_ORIGINAL(0x0052D740)
__declspec(naked) void __fastcall NpcSerializeAt(UnitNpcRecord*, void*, ChunkArchive*) EI_ORIGINAL(0x0052DA60)
__declspec(naked) void __fastcall ModelSerializeAt(UnitModelRecord*, void*, ChunkArchive*) EI_ORIGINAL(0x0052DC70)
__declspec(naked) void __fastcall LevelSerializeAt(UnitLevelRecord*, void*, ChunkArchive*) EI_ORIGINAL(0x0052DEE0)

// A vector of records as chunk `id`: one chunk 1 per record. Loading destroys the records (the
// original erases them through stlp::copy of an empty range first), empties the vector and resizes
// it to the count of chunks 1. Each record's chunk is the next occurrence (set before entering it);
// the vector's start is read before entering.
template <typename Record, typename Destroy, typename Resize, typename Serialize>
void SerializeRecords(ChunkArchive* ar, int id, stlp::vector<Record>* records, Destroy destroy, Resize resize,
                      Serialize serialize) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        for (Record* record = records->begin(); record != records->end(); ++record) destroy(record);
        records->Finish() = records->Start();
        count = ar->CountChunks(1);
        resize(records, nullptr, static_cast<std::uint32_t>(count));
    } else {
        count = static_cast<int>(records->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        Record* first = records->begin();
        if (ar->EnterChunk(1)) {
            serialize(first + i, nullptr, ar);
            ar->LeaveChunk();
        }
    }
    ar->LeaveChunk();
}

void DestroyLevelName(UnitLevelRecord* record) { record->name.~CString(); }

} // namespace

void ChunkArchive::SerializePrototypes(int id, stlp::vector<UnitPrototypeRecord>* records, void*) {
    SerializeRecords(this, id, records, [](UnitPrototypeRecord* r) { unitdb::DestroyPrototypeRecord(r); },
                     UnitPrototypesResizeOriginal, PrototypeSerializeAt);
}
EI_REPLACE_METHOD(0x0053DC10, ChunkArchive::SerializePrototypes);

void ChunkArchive::SerializeNpcs(int id, stlp::vector<UnitNpcRecord>* records, void*) {
    SerializeRecords(this, id, records, [](UnitNpcRecord* r) { unitdb::DestroyNpcRecord(r); }, UnitNpcsResizeOriginal,
                     NpcSerializeAt);
}
EI_REPLACE_METHOD(0x0053DD00, ChunkArchive::SerializeNpcs);

void ChunkArchive::SerializeModels(int id, stlp::vector<UnitModelRecord>* records, void*) {
    SerializeRecords(this, id, records, [](UnitModelRecord* r) { unitdb::DestroyModelRecord(r); },
                     UnitModelsResizeOriginal, ModelSerializeAt);
}
EI_REPLACE_METHOD(0x0053DF50, ChunkArchive::SerializeModels);

void ChunkArchive::SerializeUnitLevels(int id, stlp::vector<UnitLevelRecord>* records, void*) {
    SerializeRecords(this, id, records, DestroyLevelName, UnitLevelsResizeOriginal, LevelSerializeAt);
}
EI_REPLACE_METHOD(0x0053E1B0, ChunkArchive::SerializeUnitLevels);

// --- The skill database --------------------------------------------------------------------------

namespace {
constexpr int kSkillLevelValues = 10;
constexpr int kFirstValueField = 12;

bool SameName(const mfc::CString& name, const char* other) {
    return libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(name)),
                           reinterpret_cast<const unsigned char*>(other)) == 0;
}
} // namespace

void SkillRecord::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, &name);
    ar->CStringField(1, &field1);
    Dword(ar, 2, &field2);
    InlineStrings(ar, 10, &statNames);
}
EI_REPLACE_METHOD(0x0051F3A0, SkillRecord::Serialize);

void SkillLevelRecord::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, &field0);
    ar->CStringField(1, &name);
    Dword(ar, 2, &field2);
    ar->CStringField(10, &requiredLevelName);
    ar->CStringField(11, &requiredSkillName);
    for (int i = 0; i < kSkillLevelValues; ++i) Dword(ar, kFirstValueField + i, &values[i]);
    ar->Field(0x16, &learnable, 1);
    Dword(ar, 0x17, &exclusionGroup);
}
EI_REPLACE_METHOD(0x0051F480, SkillLevelRecord::Serialize);

void SkillRecord::Destruct() {
    records::DestroyVector(statNames);
    field1.~CString();
    name.~CString();
}
EI_REPLACE_METHOD(0x00520D20, SkillRecord::Destruct);

void SkillLevelRecord::Destruct() {
    requiredSkillName.~CString();
    requiredLevelName.~CString();
    name.~CString();
    field0.~CString();
}
EI_REPLACE_METHOD(0x00520DE0, SkillLevelRecord::Destruct);

void SkillDatabase::Reset() {
    for (SkillLevelRecord* p = levels.begin(); p != levels.end(); ++p) p->Destruct();
    levels.FreeStorage();
    for (SkillRecord* p = skills.begin(); p != skills.end(); ++p) p->Destruct();
    skills.FreeStorage();
    skills.Start() = skills.Finish() = skills.EndOfStorage() = nullptr;
    levels.Start() = levels.Finish() = levels.EndOfStorage() = nullptr;
}
EI_REPLACE_METHOD(0x0057CD60, SkillDatabase::Reset);

void ChunkArchive::SerializeSkills(int id, stlp::vector<SkillRecord>* skills, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        for (SkillRecord* record = skills->begin(); record != skills->end(); ++record) static_cast<SkillRecord*>(record)->Destruct();
        skills->Finish() = skills->Start();
        count = CountChunks(1);
        SkillVectorResizeOriginal(skills, nullptr, static_cast<std::uint32_t>(count));
    } else {
        count = static_cast<int>(skills->size());
    }
    for (int i = 0; i < count; ++i) {
        Current()->occurrence = i + 1;
        SkillRecord* first = skills->begin();
        if (EnterChunk(1)) {
            (first + i)->Serialize(this);
            LeaveChunk();
        }
    }
    LeaveChunk();
}
EI_REPLACE_METHOD(0x005207D0, ChunkArchive::SerializeSkills);

void ChunkArchive::SerializeSkillLevels(int id, stlp::vector<SkillLevelRecord>* levels, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        for (SkillLevelRecord* record = levels->begin(); record != levels->end(); ++record) static_cast<SkillLevelRecord*>(record)->Destruct();
        levels->Finish() = levels->Start();
        count = CountChunks(1);
        SkillLevelVectorResizeOriginal(levels, nullptr, static_cast<std::uint32_t>(count));
    } else {
        count = static_cast<int>(levels->size());
    }
    for (int i = 0; i < count; ++i) {
        Current()->occurrence = i + 1;
        SkillLevelRecord* first = levels->begin();
        if (EnterChunk(1)) {
            (first + i)->Serialize(this);
            LeaveChunk();
        }
    }
    LeaveChunk();
}
EI_REPLACE_METHOD(0x005208C0, ChunkArchive::SerializeSkillLevels);

void ChunkArchive::SkillLevelChunk(int id, SkillLevelRecord* record) {
    if (!EnterChunk(id)) return;
    record->Serialize(this);
    LeaveChunk();
}
EI_REPLACE_METHOD(0x00521480, ChunkArchive::SkillLevelChunk);

SkillLevelRecord* SkillDatabase::FindLevel(const char* levelName) {
    for (std::uint32_t i = 0; i < levels.size(); ++i)
        if (SameName(levels[i].name, levelName)) return &levels[i];
    return nullptr;
}
EI_REPLACE_METHOD(0x0051FE00, SkillDatabase::FindLevel);

int SkillDatabase::IndexOf(const char* levelName) const {
    for (std::uint32_t i = 0; i < levels.size(); ++i)
        if (SameName(levels[i].name, levelName)) return static_cast<int>(i);
    return -1;
}
EI_REPLACE_METHOD(0x0051FE80, SkillDatabase::IndexOf);

// 0051FF00 SkillDatabase::IndexOfLevel is in unit_skill_price.cpp (ei_vectors links it there).
