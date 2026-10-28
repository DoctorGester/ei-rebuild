// The units database records' serialisers: unit prototypes, unit models and NPCs the skill (perk)
// database's records, vectors and lookups. See unit_db.h and docs/notes/units.md ("Database records").
#include "chunk_archive.h"
#include "unit_db.h"

#include "model.h"   // ChunkArchive

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/text_case.h"

#include <memory>
#include <string>
#include <vector>

namespace {

void Dword(ChunkArchive* ar, int id, void* data) { ar->Field(id, data, 4); }

constexpr int kSkillLevelValues = 10;
constexpr int kFirstValueField = 12;

bool SameName(const std::string& name, const char* other) {
    return text::CompareIgnoringCase(name, other) == 0;
}

} // namespace

template <typename Text>
void InlineStrings(ChunkArchive* ar, int id, std::vector<Text>* strings) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        strings->clear();
        count = ar->CountChunks(1);
        strings->resize(static_cast<std::uint32_t>(count));
    } else {
        count = static_cast<int>(strings->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ar->StringField(1, &(*strings)[i]);
    }
    ar->LeaveChunk();
}
template void InlineStrings(ChunkArchive*, int, std::vector<std::string>*);

void UnitPrototypeRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &modelName);
    Dword(ar, 2, &modelIndex);
    Dword(ar, 3, &skin);
    Dword(ar, 4, &hair);
    Dword(ar, 5, &scale.x);
    Dword(ar, 6, &scale.y);
    Dword(ar, 7, &scale.z);
    ar->StringField(8, &soundFolder);
    Dword(ar, 9, &hitPoints);
    Dword(ar, 10, &mana);
    Dword(ar, 11, &protectionScale);
    Dword(ar, 12, &attackCategory);
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
        Dword(ar, 1, &gold.unreadField);
        Dword(ar, 2, &gold.minimum);
        Dword(ar, 3, &gold.maximum);
        ar->LeaveChunk();
    }
    if (ar->EnterChunk(0x1C)) {
        secondGold.Serialize(ar);
        ar->LeaveChunk();
    }
    ar->SerializeStrings(0x1D, &lootItems, ar);
    ar->Field(0x1E, skills3to5, sizeof(skills3to5));
    ar->SerializeStrings(0x1F, &spellNames, ar);
    ar->SerializeStrings(0x20, &armourNames, ar);
    ar->StringField(0x21, &weaponName);
    Dword(ar, 0x22, &panelViewSize);
    Dword(ar, 0x23, &height);
    Dword(ar, 0x24, &combat[2]);
    Dword(ar, 0x25, &viewDistance);
    Dword(ar, 0x26, &viewHeight);
    Dword(ar, 0x27, &hitSoundKind);
    Dword(ar, 0x28, &shake);
    Dword(ar, 0x29, &level);
    ar->StringField(0x2A, &secondWeaponName);
    Dword(ar, 0x2B, &experienceValue);
}

void UnitModelRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    Dword(ar, 1, &objectClass);
    Dword(ar, 2, &hitPointRegen);
    Dword(ar, 3, &manaRegen);
    Dword(ar, 4, &unreadField);
    Dword(ar, 5, &movementKind);
    Dword(ar, 6, &angle);
    ar->Field(7, modeSpeeds, sizeof(modeSpeeds));
    Dword(ar, 8, &reachHeightScale);
    Dword(ar, 9, &sizeClass);
    Dword(ar, 10, &sizeClassSmall);
    ar->Field(11, damageWeights, sizeof(damageWeights));
    for (int i = 0; i < 6; ++i) {
        if (ar->EnterChunk(0x0C + i)) {
            slots[i].Serialize(ar);
            ar->LeaveChunk();
        }
    }
    ar->StringField(0x1E, &figureName);
    ar->SerializeStrings(0x1F, &unreadLists[0], ar);
    ar->SerializeStrings(0x20, &unreadLists[1], ar);
    Dword(ar, 0x21, &height);
    ar->StringField(0x22, &soundFolder);
    ar->SerializeStrings(0x23, &soundNames, ar);
    ar->Field(0x24, actionSpeeds, sizeof(actionSpeeds));
    Dword(ar, 0x25, &idleSoundChance);
    Dword(ar, 0x26, &attackSoundChance);
    ar->Field(0x27, protection, sizeof(protection));
    Dword(ar, 0x28, &bloodKind);
    Dword(ar, 0x29, &castEffect);
    Dword(ar, 0x2A, &footprintKind);
    Dword(ar, 0x2B, &footPart);
    ar->StringField(0x2C, &hitMaterial);
    ar->Field(0x2D, &swapFeet, 1);
    Dword(ar, 0x2E, &sizeScale);
    Dword(ar, 0x2F, &eyeLeft);
    Dword(ar, 0x30, &eyeBottom);
    Dword(ar, 0x31, &eyeWidth);
    Dword(ar, 0x32, &eyeHeight);
}

void UnitNpcRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    Dword(ar, 1, &unreadField);
    Dword(ar, 2, &experience);
    Dword(ar, 3, &stats[0]);
    Dword(ar, 4, &stats[1]);
    Dword(ar, 5, &stats[2]);
    ar->Field(6, attributes, sizeof(attributes));
    ar->Field(7, skills, sizeof(skills));
    ar->SerializeStrings(8, &skillNames, ar);
    InlineStrings(ar, 9, &startWeapons);
    InlineStrings(ar, 10, &startQuickItems);
    InlineStrings(ar, 11, &startSpells);
    Dword(ar, 12, &experienceFloor);
    Dword(ar, 13, &startMoney);
    ar->Field(14, &voiced, 1);
}

// --- The unit database's pieces and vectors -----------------------------------------------

void UnitGoldRoll::Serialize(ChunkArchive* ar) {
    Dword(ar, 0, &chance);
    Dword(ar, 1, &unreadField);
    Dword(ar, 2, &minimum);
    Dword(ar, 3, &maximum);
}

void UnitModelSlot::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &levelName);
    Dword(ar, 1, &levelIndex);
    Dword(ar, 2, &share);
    Dword(ar, 3, &factor);
}

void UnitLevelRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, valuesA, sizeof(valuesA));
    ar->Field(2, valuesB, sizeof(valuesB));
    Dword(ar, 3, &vitalMask);
    Dword(ar, 4, &slotType);
}

void ChunkArchive::SerializeStrings(int id, std::vector<std::string>* strings, void*) { InlineStrings(this, id, strings); }

void ChunkArchive::SerializePrototypes(int id, std::vector<UnitPrototypeRecord>* records, void*) {
    RecordChunks(this, id, records, &UnitPrototypeRecord::Serialize);
}

void ChunkArchive::SerializeNpcs(int id, std::vector<UnitNpcRecord>* records, void*) {
    RecordChunks(this, id, records, &UnitNpcRecord::Serialize);
}

void ChunkArchive::SerializeModels(int id, std::vector<UnitModelRecord>* records, void*) {
    RecordChunks(this, id, records, &UnitModelRecord::Serialize);
}

void ChunkArchive::SerializeUnitLevels(int id, std::vector<UnitLevelRecord>* records, void*) {
    RecordChunks(this, id, records, &UnitLevelRecord::Serialize);
}

// --- The skill database --------------------------------------------------------------------------

void SkillRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &unreadText);
    Dword(ar, 2, &unreadField);
    InlineStrings(ar, 10, &statNames);
}

void SkillLevelRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &displayName);
    ar->StringField(1, &name);
    Dword(ar, 2, &unreadField);
    ar->StringField(10, &requiredLevelName);
    ar->StringField(11, &requiredSkillName);
    for (int i = 0; i < kSkillLevelValues; ++i) Dword(ar, kFirstValueField + i, &values[i]);
    ar->Field(0x16, &learnable, 1);
    Dword(ar, 0x17, &exclusionGroup);
}

// Both vectors emptied and their storage freed.
void SkillDatabase::Reset() {
    lib::FreeStorage(levels);
    lib::FreeStorage(skills);
}

void ChunkArchive::SerializeSkills(int id, std::vector<SkillRecord>* skills, void*) {
    RecordChunks(this, id, skills, &SkillRecord::Serialize);
}

void ChunkArchive::SkillLevelChunk(int id, SkillLevelRecord* record) {
    if (!EnterChunk(id)) return;
    record->Serialize(this);
    LeaveChunk();
}

SkillLevelRecord* SkillDatabase::FindLevel(const char* levelName) {
    for (std::uint32_t i = 0; i < levels.size(); ++i)
        if (SameName(levels[i].name, levelName)) return &levels[i];
    return nullptr;
}

int SkillDatabase::IndexOf(const char* levelName) const {
    for (std::uint32_t i = 0; i < levels.size(); ++i)
        if (SameName(levels[i].name, levelName)) return static_cast<int>(i);
    return -1;
}

// SkillDatabase::IndexOfLevel is in unit_skill_price.cpp (ei_vectors links it there).
