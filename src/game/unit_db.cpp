// The units database records' serialisers: unit prototypes (0052D740), unit models (0052DC70) and NPCs
// (0052DA60).
// See unit_db.h and docs/notes/units.md ("Database records").
#include "unit_db.h"

#include "model.h"   // ChunkArchive

#include "ei/hook.h"
#include "originals_library.h"

namespace {

// The archive's field primitives (chunk_archive.cpp), through their game addresses: a field of
// `size` bytes (missing ones read as zero), a CString field, entering / leaving chunk `id`.
__declspec(naked) void __fastcall ArchiveField(ChunkArchive*, void*, int /*id*/, void* /*data*/, unsigned /*size*/) EI_ORIGINAL(0x004092B0)
__declspec(naked) void __fastcall ArchiveString(ChunkArchive*, void*, int /*id*/, mfc::CString*) EI_ORIGINAL(0x004093A0)
__declspec(naked) bool __fastcall ArchiveEnter(ChunkArchive*, void*, int /*id*/) EI_ORIGINAL(0x004098D0)
__declspec(naked) void __fastcall ArchiveLeave(ChunkArchive*) EI_ORIGINAL(0x00409980)
// Not rewritten: 0052DF40 (ArchiveCStringsOriginal, originals_library.h); 0052D6A0, four dwords
// as fields 0..3; 0052D6F0, a model's body slot as fields 0..3.
void __fastcall ArchiveStrings(ChunkArchive* ar, void*, int id, CStringVector* strings, ChunkArchive* unused) {
    ArchiveCStringsOriginal(ar, nullptr, id, strings, unused);
}
__declspec(naked) void __fastcall FourDwords(std::uint32_t*, void*, ChunkArchive*) EI_ORIGINAL(0x0052D6A0)
__declspec(naked) void __fastcall ModelSlot(UnitModelSlot*, void*, ChunkArchive*) EI_ORIGINAL(0x0052D6F0)
// The pieces of 0052DF40 that the NPC serialiser inlines: counting the fields `id` of the current
// chunk [00409A60, rewritten in chunk_archive.cpp], emptying a vector<CString> [0052E010] and
// resizing it [0051F5B0].
__declspec(naked) int __fastcall ArchiveCount(ChunkArchive*, void*, int /*id*/) EI_ORIGINAL(0x00409A60)
void __fastcall StringsClear(CStringVector* a0) { CStringVectorClearOriginal((void*)a0); }
void __fastcall StringsResize(CStringVector* a0, void*, std::uint32_t a2) { CStringVectorResizeOriginal((void*)a0, nullptr, a2); }

void Dword(ChunkArchive* ar, int id, void* data) { ArchiveField(ar, nullptr, id, data, 4); }

} // namespace

void InlineStrings(ChunkArchive* ar, int id, CStringVector* strings) {
    if (!ArchiveEnter(ar, nullptr, id)) return;
    int count;
    if (ar->loading_) {
        StringsClear(strings);
        count = ArchiveCount(ar, nullptr, 1);
        StringsResize(strings, nullptr, static_cast<std::uint32_t>(count));
    } else {
        count = static_cast<int>(strings->end - strings->begin);
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ArchiveString(ar, nullptr, 1, &strings->begin[i]);
    }
    ArchiveLeave(ar);
}

void UnitPrototypeRecord::Serialize(ChunkArchive* ar) {
    ArchiveString(ar, nullptr, 0, &name);
    ArchiveString(ar, nullptr, 1, &modelName);
    Dword(ar, 2, &modelIndex);
    Dword(ar, 3, &skin);
    Dword(ar, 4, &hair);
    Dword(ar, 5, &scale.x);
    Dword(ar, 6, &scale.y);
    Dword(ar, 7, &scale.z);
    ArchiveString(ar, nullptr, 8, &soundFolder);
    Dword(ar, 9, &hitPoints);
    Dword(ar, 10, &mana);
    Dword(ar, 11, &protectionScale);
    Dword(ar, 12, &unknown_5C);
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
    Dword(ar, 0x18, &unknown_94);
    ArchiveField(ar, nullptr, 0x19, valuesB0, sizeof(valuesB0));
    ArchiveField(ar, nullptr, 0x1A, valuesE0, sizeof(valuesE0));
    if (ArchiveEnter(ar, nullptr, 0x1B)) {
        for (int i = 0; i < 4; ++i) Dword(ar, i, &unknown_C8[i]);
        ArchiveLeave(ar);
    }
    if (ArchiveEnter(ar, nullptr, 0x1C)) {
        FourDwords(unknown_D8, nullptr, ar);
        ArchiveLeave(ar);
    }
    ArchiveStrings(ar, nullptr, 0x1D, &unknown_E8, ar);
    ArchiveField(ar, nullptr, 0x1E, skills3to5, sizeof(skills3to5));
    ArchiveStrings(ar, nullptr, 0x1F, &spellNames, ar);
    ArchiveStrings(ar, nullptr, 0x20, &armourNames, ar);
    ArchiveString(ar, nullptr, 0x21, &weaponName);
    Dword(ar, 0x22, &unknown_20);
    Dword(ar, 0x23, &unknown_24);
    Dword(ar, 0x24, &combat[2]);
    Dword(ar, 0x25, &viewDistance);
    Dword(ar, 0x26, &viewHeight);
    Dword(ar, 0x27, &hitSoundKind);
    Dword(ar, 0x28, &shake);
    Dword(ar, 0x29, &unknown_50);
    ArchiveString(ar, nullptr, 0x2A, &secondWeaponName);
    Dword(ar, 0x2B, &unknown_54);
}
EI_REPLACE_METHOD(0x0052D740, UnitPrototypeRecord::Serialize);

void UnitModelRecord::Serialize(ChunkArchive* ar) {
    ArchiveString(ar, nullptr, 0, &name);
    Dword(ar, 1, &objectClass);
    Dword(ar, 2, &hitPointRegen);
    Dword(ar, 3, &manaRegen);
    Dword(ar, 4, &unknown_10);
    Dword(ar, 5, &unknown_14);
    Dword(ar, 6, &angle);
    ArchiveField(ar, nullptr, 7, modeSpeeds, sizeof(modeSpeeds));
    Dword(ar, 8, &unknown_44);
    Dword(ar, 9, &sizeClass);
    Dword(ar, 10, &sizeClassSmall);
    ArchiveField(ar, nullptr, 11, damageWeights, sizeof(damageWeights));
    for (int i = 0; i < 6; ++i) {
        if (ArchiveEnter(ar, nullptr, 0x0C + i)) {
            ModelSlot(&slots[i], nullptr, ar);
            ArchiveLeave(ar);
        }
    }
    ArchiveString(ar, nullptr, 0x1E, &unknown_E8);
    ArchiveStrings(ar, nullptr, 0x1F, &unknown_EC, ar);
    ArchiveStrings(ar, nullptr, 0x20, &unknown_F8, ar);
    Dword(ar, 0x21, &height);
    ArchiveString(ar, nullptr, 0x22, &soundFolder);
    ArchiveStrings(ar, nullptr, 0x23, &soundNames, ar);
    ArchiveField(ar, nullptr, 0x24, unknown_2C, sizeof(unknown_2C));
    Dword(ar, 0x25, &idleSoundChance);
    Dword(ar, 0x26, &attackSoundChance);
    ArchiveField(ar, nullptr, 0x27, protection, sizeof(protection));
    Dword(ar, 0x28, &bloodKind);
    Dword(ar, 0x29, &unknown_114);
    Dword(ar, 0x2A, &footprintKind);
    Dword(ar, 0x2B, &footPart);
    ArchiveString(ar, nullptr, 0x2C, &hitMaterial);
    ArchiveField(ar, nullptr, 0x2D, &swapFeet, 1);
    Dword(ar, 0x2E, &unknown_120);
    Dword(ar, 0x2F, &eyeLeft);
    Dword(ar, 0x30, &eyeBottom);
    Dword(ar, 0x31, &eyeWidth);
    Dword(ar, 0x32, &eyeHeight);
}
EI_REPLACE_METHOD(0x0052DC70, UnitModelRecord::Serialize);

void UnitNpcRecord::Serialize(ChunkArchive* ar) {
    ArchiveString(ar, nullptr, 0, &name);
    Dword(ar, 1, &unknown_04);
    Dword(ar, 2, &experience);
    Dword(ar, 3, &stats[0]);
    Dword(ar, 4, &stats[1]);
    Dword(ar, 5, &stats[2]);
    ArchiveField(ar, nullptr, 6, attributes, sizeof(attributes));
    ArchiveField(ar, nullptr, 7, skills, sizeof(skills));
    ArchiveStrings(ar, nullptr, 8, &skillNames, ar);
    InlineStrings(ar, 9, &startWeapons);
    InlineStrings(ar, 10, &startQuickItems);
    InlineStrings(ar, 11, &startSpells);
    Dword(ar, 12, &experienceFloor);
    Dword(ar, 13, &startMoney);
    ArchiveField(ar, nullptr, 14, &unknown_5C, 1);
}
EI_REPLACE_METHOD(0x0052DA60, UnitNpcRecord::Serialize);
