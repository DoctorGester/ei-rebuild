// The units database records' serialisers: unit prototypes (0052D740) and unit models (0052DC70).
// See unit_db.h and docs/notes/units.md ("Database records").
#include "unit_db.h"

#include "model.h"   // ChunkArchive

#include "ei/hook.h"

namespace {

// The archive's field primitives (chunk_archive.cpp), through their game addresses: a field of
// `size` bytes (missing ones read as zero), a CString field, entering / leaving chunk `id`.
__declspec(naked) void __fastcall ArchiveField(ChunkArchive*, void*, int /*id*/, void* /*data*/, unsigned /*size*/) EI_ORIGINAL(0x004092B0)
__declspec(naked) void __fastcall ArchiveString(ChunkArchive*, void*, int /*id*/, mfc::CString*) EI_ORIGINAL(0x004093A0)
__declspec(naked) bool __fastcall ArchiveEnter(ChunkArchive*, void*, int /*id*/) EI_ORIGINAL(0x004098D0)
__declspec(naked) void __fastcall ArchiveLeave(ChunkArchive*) EI_ORIGINAL(0x00409980)
// Not rewritten: 0052DF40, a vector<CString> as chunk `id` (loading: the vector is emptied and
// resized to the chunk's count of fields 1; the third argument, the archive again, is unused but
// popped); 0052D6A0, four dwords as fields 0..3; 0052D6F0, a model's body slot as fields 0..3.
__declspec(naked) void __fastcall ArchiveStrings(ChunkArchive*, void*, int /*id*/, CStringVector*, ChunkArchive*) EI_ORIGINAL(0x0052DF40)
__declspec(naked) void __fastcall FourDwords(std::uint32_t*, void*, ChunkArchive*) EI_ORIGINAL(0x0052D6A0)
__declspec(naked) void __fastcall ModelSlot(UnitModelSlot*, void*, ChunkArchive*) EI_ORIGINAL(0x0052D6F0)

void Dword(ChunkArchive* ar, int id, void* data) { ArchiveField(ar, nullptr, id, data, 4); }

} // namespace

void UnitPrototypeRecord::Serialize(ChunkArchive* ar) {
    ArchiveString(ar, nullptr, 0, &name);
    ArchiveString(ar, nullptr, 1, &modelName);
    Dword(ar, 2, &modelIndex);
    Dword(ar, 3, &skin);
    Dword(ar, 4, &hair);
    Dword(ar, 5, &scale[0]);
    Dword(ar, 6, &scale[1]);
    Dword(ar, 7, &scale[2]);
    ArchiveString(ar, nullptr, 8, &unknown_34);
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
    ArchiveStrings(ar, nullptr, 0x1F, &unknown_100, ar);
    ArchiveStrings(ar, nullptr, 0x20, &unknown_10C, ar);
    ArchiveString(ar, nullptr, 0x21, &unknown_118);
    Dword(ar, 0x22, &unknown_20);
    Dword(ar, 0x23, &unknown_24);
    Dword(ar, 0x24, &combat[2]);
    Dword(ar, 0x25, &unknown_28);
    Dword(ar, 0x26, &unknown_2C);
    Dword(ar, 0x27, &unknown_7C);
    Dword(ar, 0x28, &unknown_30);
    Dword(ar, 0x29, &unknown_50);
    ArchiveString(ar, nullptr, 0x2A, &unknown_11C);
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
    ArchiveString(ar, nullptr, 0x22, &unknown_124);
    ArchiveStrings(ar, nullptr, 0x23, &soundNames, ar);
    ArchiveField(ar, nullptr, 0x24, unknown_2C, sizeof(unknown_2C));
    Dword(ar, 0x25, &unknown_150);
    Dword(ar, 0x26, &unknown_154);
    ArchiveField(ar, nullptr, 0x27, protection, sizeof(protection));
    Dword(ar, 0x28, &unknown_108);
    Dword(ar, 0x29, &unknown_114);
    Dword(ar, 0x2A, &unknown_10C);
    Dword(ar, 0x2B, &unknown_110);
    ArchiveString(ar, nullptr, 0x2C, &unknown_118);
    ArchiveField(ar, nullptr, 0x2D, &unknown_11C, 1);
    Dword(ar, 0x2E, &unknown_120);
    Dword(ar, 0x2F, &unknown_158);
    Dword(ar, 0x30, &unknown_15C);
    Dword(ar, 0x31, &unknown_160);
    Dword(ar, 0x32, &unknown_164);
}
EI_REPLACE_METHOD(0x0052DC70, UnitModelRecord::Serialize);
