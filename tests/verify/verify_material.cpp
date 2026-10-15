// Checks for material.cpp (A-H24). Every interface window makes two materials (NewMaterial, then
// FillMaterial, 005FE700), the client world makes the manager and the landscape / sky their
// materials, so every scenario reaches them. The constructor and FillMaterial run against the
// original (on a scratch copy); the rest allocate or free, so they run ours only and check the
// state left. The getters are MOV EAX,imm32 (P).
#include "verify.h"

#include "../../src/game/material.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {
void* s_unused;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t v;
    std::memcpy(&v, object, 4);
    return v;
}

bool IsNone(const mfc::CString& s) { return std::strcmp(s, "none") == 0; }

// The deque's shape: empty, map size, where its node sits in the map.
bool SameEmptyDeque(const stlp::deque<MaterialEntry>& a, const stlp::deque<MaterialEntry>& b) {
    return a.empty() && b.empty() && a.MapSize() == b.MapSize() &&
           a.Start().node - a.Map() == b.Start().node - b.Map() && a.Finish().node - a.Map() == b.Finish().node - b.Map() &&
           a.Start().cur == a.Start().first && b.Start().cur == b.Start().first;
}

bool ResetDone(const CMaterial* m) {
    return IsNone(m->name) && IsNone(m->name2) && IsNone(m->name3) && m->spare90 == 0 && m->spare98 == 0 &&
           m->colors.diffuse.w == 1.0f && m->entries.empty();
}

// 0068DBC0: PUSH -1 (2), PUSH imm32 (5). The original builds a copy of the object's old bytes (the
// colours stay unwritten); compared field by field, then the copy is destroyed by ours.
CMaterial*(__fastcall* s_construct)(CMaterial*);
CMaterial* __fastcall ConstructVerify(CMaterial* self) {
    alignas(CMaterial) std::uint8_t scratch[sizeof(CMaterial)];
    std::memcpy(scratch, self, sizeof(CMaterial));
    auto* expected = reinterpret_cast<CMaterial*>(scratch);
    CMaterial* resultExpected = s_construct(expected);
    CMaterial* result = self->Construct();
    const bool ok = resultExpected == expected && result == self &&
                    std::memcmp(expected, self, sizeof(CEngineObject)) == 0 &&
                    std::strcmp(expected->name, self->name) == 0 && std::strcmp(expected->name2, self->name2) == 0 &&
                    std::strcmp(expected->name3, self->name3) == 0 &&
                    std::memcmp(&expected->colors, &self->colors, sizeof(MaterialColors)) == 0 &&
                    expected->spare90 == self->spare90 && expected->spare98 == self->spare98 &&
                    SameEmptyDeque(expected->entries, self->entries) && VtableOf(self) == CMaterial::kVtableAddress;
    ei::verify::Check("CMaterial::Construct", true, ok, "material %p", self);
    expected->Destruct();
    return result;
}

int __fastcall CreateVerify(CMaterial* self) {
    int result = self->CMaterial::Create();
    ei::verify::Check("CMaterial::Create", true, result == 1 && ResetDone(self), "material %p", self);
    return result;
}

void __fastcall DestroyVerify(CMaterial* self) {
    self->CMaterial::Destroy();
    ei::verify::Check("CMaterial::Destroy", true, self->entries.empty(), "material %p", self);
}

void __fastcall DestructVerify(CMaterial* self) {
    self->Destruct();
    ei::verify::Check("CMaterial::Destruct", true,
                      VtableOf(self) == CEngineObject::kVtableAddress && self->owner_ == nullptr, "material %p", self);
}

CMaterial* __fastcall DeleteVerify(CMaterial* self, void*, unsigned flags) {
    CMaterial* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CMaterial::DeletingDestruct", true,
                      result == self && VtableOf(self) == CEngineObject::kVtableAddress && self->owner_ == nullptr,
                      "material %p", self);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

const char*(__fastcall* s_materialName)(CMaterial*);
const char* __fastcall MaterialNameVerify(CMaterial* self) {
    const char* expected = s_materialName(self);
    ei::verify::Check("CMaterial::GetClassName", expected, MaterialClassName(self), "%p", self);
    return expected;
}
int(__fastcall* s_materialId)(CMaterial*);
int __fastcall MaterialIdVerify(CMaterial* self) {
    int expected = s_materialId(self);
    ei::verify::Check("CMaterial::GetClassId", expected, MaterialClassId(self), "%p", self);
    return expected;
}

// --- The manager -----------------------------------------------------------------------------------

CMaterialManager* __fastcall ManagerConstructVerify(CMaterialManager* self) {
    CMaterialManager* result = self->Construct();
    const bool ok = result == self && VtableOf(self) == CMaterialManager::kVtableAddress &&
                    self->colors_.BucketCount() == stlp::NextPrime(100) && self->colors_.Size() == 0 &&
                    self->colors_.First() == nullptr;
    ei::verify::Check("CMaterialManager::Construct", true, ok, "manager %p", self);
    return result;
}

void __fastcall ManagerDestructVerify(CMaterialManager* self) {
    self->Destruct();
    ei::verify::Check("CMaterialManager::Destruct", true, VtableOf(self) == CEngineObject::kVtableAddress,
                      "manager %p", self);
}

CMaterialManager* __fastcall ManagerDeleteVerify(CMaterialManager* self, void*, unsigned flags) {
    CMaterialManager* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CMaterialManager::DeletingDestruct", true,
                      result == self && VtableOf(self) == CEngineObject::kVtableAddress, "manager %p", self);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

// 0068E640 (A-H26; Rebirth-patched, so hooked in the vanilla launch only). Ours only (it opens the
// archive and fills the table); afterwards every kind of the game's table must be entered with the
// colours its file holds, read again here (the defaults without the file), magic's emissive 1.
// Reached at every start of the game: the client world creates its managers.
int __fastcall ManagerCreateVerify(CMaterialManager* self) {
    const int result = self->CMaterialManager::Create();
    if (result != 1) return result;
    int bad = 0;
    const MaterialKindFile* const kinds = MaterialKindFileTable();
    // Backwards: a later kind of the same number would have replaced an earlier one (none repeats).
    for (int i = kMaterialKindFiles - 1; i >= 0; --i) {
        MaterialColors expected = {};
        void* data = self->GetFile(kinds[i].file, nullptr);
        if (data) {
            std::memcpy(&expected, MaterialFileColors(data), sizeof(expected));
        } else {
            expected.diffuse = {0.3f, 0.3f, 0.3f, 1.0f};
            expected.ambient = {0.7f, 0.7f, 0.7f, 0.0f};
            expected.specular = {0.2f, 0.2f, 0.2f, 0.0f};
            expected.power = 5.0f;
        }
        self->CloseFile(data);
        if (kinds[i].kind == 0x17) expected.emissive.x = expected.emissive.y = expected.emissive.z = 1.0f;
        auto* entry = self->colors_.Find(static_cast<unsigned>(kinds[i].kind));
        if (!entry || std::memcmp(&entry->value, &expected, sizeof(expected)) != 0) ++bad;
    }
    ei::verify::Check("CMaterialManager::Create", 0, bad, "manager %p: %u kinds entered", self,
                      static_cast<unsigned>(self->colors_.Size()));
    return result;
}

CEngineObject* __fastcall NewObjectVerify(CMaterialManager* self, void*, int type) {
    CEngineObject* object = self->CMaterialManager::NewObjectImpl(type);
    const auto* material = static_cast<const CMaterial*>(object);
    const bool ok = object && VtableOf(object) == CMaterial::kVtableAddress && object->type_ == type &&
                    object->id_ == -1 && object->refCount_ == 0 && ResetDone(material);
    ei::verify::Check("CMaterialManager::NewObject", true, ok, "manager %p type 0x%X -> %p", self, type, object);
    return object;
}

CMaterial* __fastcall NewMaterialVerify(CMaterialManager* self, void*, int type) {
    CMaterial* material = self->NewMaterial(type);
    bool ok = material != nullptr;
    if (ok) {
        auto* entry = self->colors_.Find(static_cast<unsigned>(material->type_));
        ok = (material->flags_ & 8) != 0 && material->owner_ == self && entry &&
             std::memcmp(&entry->value, &material->colors, sizeof(MaterialColors)) == 0;
    }
    ei::verify::Check("CMaterialManager::NewMaterial", true, ok, "manager %p type 0x%X -> %p", self, type, material);
    return material;
}

// 0068EAF0: SUB ESP,0x48 (3), PUSH EBP (1), MOV EBP,[ESP+0x50] (4). The original fills a byte copy of
// the material (it writes only the colours; an absent key is entered by it, ours then finds it).
int(__fastcall* s_fill)(CMaterialManager*, void*, CMaterial*, unsigned);
int __fastcall FillMaterialVerify(CMaterialManager* self, void*, CMaterial* material, unsigned key) {
    alignas(CMaterial) std::uint8_t scratch[sizeof(CMaterial)];
    CMaterial* copy = nullptr;
    if (material) {
        std::memcpy(scratch, material, sizeof(CMaterial));
        copy = reinterpret_cast<CMaterial*>(scratch);
    }
    const int expected = s_fill(self, nullptr, copy, key);
    const int actual = self->FillMaterial(material, key);
    const bool ok = expected == actual &&
                    (!material || std::memcmp(&copy->colors, &material->colors, sizeof(MaterialColors)) == 0);
    ei::verify::Check("CMaterialManager::FillMaterial", true, ok, "manager %p material %p key 0x%X: %d / %d", self,
                      material, key, expected, actual);
    return expected;
}

const char*(__fastcall* s_folder)(CMaterialManager*);
const char* __fastcall FolderVerify(CMaterialManager* self) {
    const char* expected = s_folder(self);
    ei::verify::Check("CMaterialManager::Folder", expected, MaterialManagerFolder(self), "%p", self);
    return expected;
}
const char*(__fastcall* s_managerName)(CMaterialManager*);
const char* __fastcall ManagerNameVerify(CMaterialManager* self) {
    const char* expected = s_managerName(self);
    ei::verify::Check("CMaterialManager::GetClassName", expected, MaterialManagerClassName(self), "%p", self);
    return expected;
}
int(__fastcall* s_managerId)(CMaterialManager*);
int __fastcall ManagerIdVerify(CMaterialManager* self) {
    int expected = s_managerId(self);
    ei::verify::Check("CMaterialManager::GetClassId", expected, MaterialManagerClassId(self), "%p", self);
    return expected;
}

} // namespace

EI_VERIFY(0x0068DBC0, ConstructVerify, 7, s_construct);
EI_VERIFY(0x0068DF90, CreateVerify, 0, s_unused);
EI_VERIFY(0x0068E040, DestroyVerify, 0, s_unused);
EI_VERIFY(0x0068DE30, DestructVerify, 0, s_unused);
EI_VERIFY(0x0068DD20, DeleteVerify, 0, s_unused);
EI_VERIFY(0x0068DD00, MaterialNameVerify, 5, s_materialName);
EI_VERIFY(0x0068DD10, MaterialIdVerify, 5, s_materialId);
EI_VERIFY(0x0068E3F0, ManagerConstructVerify, 0, s_unused);
EI_VERIFY(0x0068E540, ManagerDestructVerify, 0, s_unused);
EI_VERIFY(0x0068E520, ManagerDeleteVerify, 0, s_unused);
EI_VERIFY(0x0068E640, ManagerCreateVerify, 0, s_unused);
EI_VERIFY(0x0068E900, NewObjectVerify, 0, s_unused);
EI_VERIFY(0x0068E970, NewMaterialVerify, 0, s_unused);
EI_VERIFY(0x0068EAF0, FillMaterialVerify, 8, s_fill);
EI_VERIFY(0x0068E4F0, FolderVerify, 5, s_folder);
EI_VERIFY(0x0068E500, ManagerNameVerify, 5, s_managerName);
EI_VERIFY(0x0068E510, ManagerIdVerify, 5, s_managerId);
