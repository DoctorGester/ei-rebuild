// The client half of a 3D object (CObject3DClientSpecific, class id 0x37, vtable 0073DE90; A-U12):
// its lifetime, the part sets (the figure's parts grouped by texture and material, which Draw
// walks), the skin textures and the materials' lit state. Layouts: map_object_shadow.h.
#include "map_object_shadow.h"
#include "game_strings.h"

#include "map_object_figures.h"
#include "material.h"
#include "texture.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <cstring>
#include <new>

namespace {

constexpr std::uintptr_t kClientVtable = std::uintptr_t(0x0073DE90);
constexpr std::uintptr_t kSpecificBaseVtable = std::uintptr_t(0x0073DEFC);
constexpr std::uintptr_t kNoneText = std::uintptr_t(0x007846F0);            // "none"
// "Adding new primary texture set with one exist\nFor object \"%s\" with ID=%d!\n"
constexpr std::uintptr_t kSecondPrimarySetText = std::uintptr_t(0x0078F494);
constexpr int kTypeBuildingLike = 0x35;             // CObject3D::type_: its textures get flag 0x2000
constexpr std::uint32_t kTextureFlag2000 = 0x2000;

const unsigned char* Bytes(const char* text) { return reinterpret_cast<const unsigned char*>(text); }

// 004EBE60: *slot = value with counted references; the old one released (its slot 4).
template <typename T>
void AssignCounted(T** slot, T* value) {
    T* old = *slot;
    *slot = value;
    if (value) ++value->refCount_;
    if (old) old->Release();
}

// Whether a texture of `kind` belongs to the first part set (the switch of 0051B2F0: the table at
// 0051B460 for kinds 1..0x10; any other kind goes to the second).
bool IsPrimaryKind(int kind) {
    switch (kind) {
    case 1: case 3: case 4: case 6: case 7: case 8: case 9: case 0xB: case 0xD: case 0xE: case 0xF: case 0x10:
        return true;
    default:
        return false;
    }
}

// A zeroed 0x14-byte part set or group; null when the allocation fails (the callers then write
// through null, as the original does).
template <typename T>
T* NewZeroed() {
    auto* p = static_cast<T*>(gamecrt::operator_new(sizeof(T)));
    if (p) std::memset(p, 0, sizeof(T));
    return p;
}

// `part` taken out of the group that holds it (the tail moved down) and released: true, or false
// when no group of either set holds it (slots 14 / 15 share the loop).
bool RemoveFromGroups(CObject3DClientSpecific* self, CFigureMesh* part) {
    for (int k = 0; k < 2; ++k) {
        TextureParts* set = self->partSets[k];
        if (!set) continue;
        for (int g = 0; g < set->groups.size; ++g) {
            PartGroup* group = set->groups[g];
            for (int i = 0; i < group->parts.size; ++i) {
                if (group->parts[i] != part) continue;
                int tail = group->parts.size - i - 1;
                if (tail != 0) std::memmove(&group->parts[i], &group->parts[i + 1], static_cast<std::size_t>(tail) * 4);
                --group->parts.size;
                part->Release();
                return true;
            }
        }
    }
    return false;
}

} // namespace

// --- Part sets ------------------------------------------------------------------------------------

int PartGroup::ReleaseParts() {
    for (int i = 0; i < parts.size; ++i) {
        if (!parts[i]) continue;
        parts[i]->Release();
        parts[i] = nullptr;
    }
    if (parts.data) {
        lib::FreeAligned32(parts.data);
        parts.data = nullptr;
    }
    parts.capacity = 0;
    parts.size = 0;
    return 1;
}
EI_REPLACE_METHOD(0x0051AAA0, PartGroup::ReleaseParts);

void TextureParts::Destruct() {
    for (int i = 0; i < groups.size; ++i) {
        PartGroup* group = groups[i];
        if (!group) continue;
        group->ReleaseParts();
        if (group->material) {
            group->material->Release();
            group->material = nullptr;
        }
        if (group->parts.data) lib::FreeAligned32(group->parts.data);   // freed by ReleaseParts already
        gamecrt::operator_delete(group);
        groups[i] = nullptr;
    }
    groups.SetSize(0, -1);
    texture = nullptr;
    if (groups.data) lib::FreeAligned32(groups.data);
}
EI_REPLACE_METHOD(0x0051A370, TextureParts::Destruct);

// --- Lifetime -------------------------------------------------------------------------------------

CObject3DClientSpecific* CObject3DClientSpecific::Construct() {
    ConstructEngineObject(this);
    ei::SetVtable(this, kSpecificBaseVtable);
    object = nullptr;
    textures = {};
    materials = {};
    vertices = {};
    texCoords = {};
    indices = {};
    texture = nullptr;
    std::memset(&sphere, 0, 0x38);
    woundsTexture = nullptr;
    woundsMaterial = nullptr;
    new (&skinName) mfc::CString();
    new (&secondSkinName) mfc::CString();
    ei::SetVtable(this, kClientVtable);
    materialCursor = -1;
    AssignCounted<CEngineObject>(&texture, nullptr);
    AssignCounted<CTexture>(&woundsTexture, nullptr);
    ClearWoundsMaterial();
    bounds[3] = 0.0f;
    bounds[2] = 0.0f;
    bounds[1] = 0.0f;
    bounds[0] = 0.0f;
    pulseClock = 0.0f;
    return this;
}
EI_REPLACE_METHOD(0x0051AB00, CObject3DClientSpecific::Construct);

int CObject3DClientSpecific::CreateClient() {
    materialCursor = -1;
    flags_ = 0;
    AssignCounted<CEngineObject>(&texture, nullptr);
    AssignCounted<CTexture>(&woundsTexture, nullptr);
    ClearWoundsMaterial();
    bounds[3] = 0.0f;
    bounds[2] = 0.0f;
    bounds[1] = 0.0f;
    bounds[0] = 0.0f;
    std::memset(&sphere, 0, 0x38);
    partSets[0] = nullptr;
    partSets[1] = nullptr;
    touchTime = 0;
    figureScale.z = 1.0f;
    figureScale.y = 1.0f;
    figureScale.x = 1.0f;
    return 1;
}
EI_REPLACE_METHOD(0x0051AEC0, CObject3DClientSpecific::CreateClient);

void CObject3DClientSpecific::DestroyClient() {
    AssignCounted<CEngineObject>(&texture, nullptr);
    AssignCounted<CTexture>(&woundsTexture, nullptr);
    AsCountedRef(woundsMaterial).Assign(nullptr);   // the call to 0051E570
    for (int k = 0; k < 2; ++k) {
        TextureParts* set = partSets[k];
        if (!set) continue;
        set->Destruct();
        gamecrt::operator_delete(set);
    }
    partSets[0] = nullptr;
    partSets[1] = nullptr;
    for (int i = 0; i < textures.size; ++i) {
        if (!textures[i]) continue;
        textures[i]->Release();
        textures[i] = nullptr;
    }
    textures.SetSize(0, -1);
    for (int i = 0; i < materials.size; ++i) {
        if (!materials[i]) continue;
        materials[i]->Release();
        materials[i] = nullptr;
    }
    materials.SetSize(0, -1);
    object = nullptr;
    touchTime = 0;
    flags_ = 0;
}
EI_REPLACE_METHOD(0x0051AF70, CObject3DClientSpecific::DestroyClient);

void CObject3DClientSpecific::DestructBody() {
    ei::SetVtable(this, kClientVtable);
    DestroyClient();
    secondSkinName.~CString();
    skinName.~CString();
    if (woundsMaterial) woundsMaterial->Release();
    if (woundsTexture) woundsTexture->Release();
    if (texture) texture->Release();
    if (indices.data) lib::FreeAligned32(indices.data);
    if (texCoords.data) lib::FreeAligned32(texCoords.data);
    if (vertices.data) lib::FreeAligned32(vertices.data);
    if (materials.data) lib::FreeAligned32(materials.data);
    if (textures.data) lib::FreeAligned32(textures.data);
    ei::SetVtable(this, kSpecificBaseVtable);
    object = nullptr;
    DestructBase();
}
EI_REPLACE_METHOD(0x0051AD90, CObject3DClientSpecific::DestructBody);

CObject3DClientSpecific* CObject3DClientSpecific::DeleteClient(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0051AD70, CObject3DClientSpecific::DeleteClient);

// --- Parts, textures and materials ----------------------------------------------------------------

CTexture* CObject3DClientSpecific::FindOrAddTexture(int kind) {
    for (int i = 0; i < textures.size; ++i)
        if (textures[i]->type_ == kind) return textures[i];
    int k = IsPrimaryKind(kind) ? 0 : 1;
    TextureParts* set;
    if (!partSets[k]) {
        set = NewZeroed<TextureParts>();
        set->texture = static_cast<CWorldClient*>(ManagerContext())->NewTexture(kind);
        partSets[k] = set;
        int count = textures.size;
        textures.SetSize(count + 1, -1);
        textures[count] = set->texture;
        ++set->texture->refCount_;
    } else {
        if (k == 0) game::DebugLog(game::Text(kSecondPrimarySetText), object->name, object->id_);
        partSets[k]->texture->type_ = kind;
        set = partSets[k];
    }
    if (object->type_ == kTypeBuildingLike) set->texture->flags_ |= kTextureFlag2000;
    return set->texture;
}
EI_REPLACE_METHOD(0x0051B2F0, CObject3DClientSpecific::FindOrAddTexture);

CMaterial* CObject3DClientSpecific::FindMaterial(int textureKind, int materialKind) {
    for (int k = 0; k < 2; ++k) {
        TextureParts* set = partSets[k];
        if (!set || !set->texture || set->texture->type_ != textureKind) continue;
        for (int g = 0; g < set->groups.size; ++g) {
            CMaterial* material = set->groups[g]->material;
            if (material && material->type_ == materialKind) return partSets[k]->groups[g]->material;
        }
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x0051B5B0, CObject3DClientSpecific::FindMaterial);

CMaterial* CObject3DClientSpecific::FindOrAddMaterial(int textureKind, int materialKind) {
    int k = 0;
    while (!partSets[k] || !partSets[k]->texture || partSets[k]->texture->type_ != textureKind) {
        if (++k > 1) return nullptr;
    }
    TextureParts* set = partSets[k];
    for (int g = 0; g < set->groups.size; ++g) {
        CMaterial* material = set->groups[g]->material;
        if (material && material->type_ == materialKind) return set->groups[g]->material;
    }
    PartGroup* group = NewZeroed<PartGroup>();
    CMaterial* material = static_cast<CWorldClient*>(ManagerContext())->NewMaterial(materialKind);
    group->material = material;
    ++material->refCount_;
    set = partSets[k];
    int count = set->groups.size;
    set->groups.SetSize(count + 1, -1);
    set->groups[count] = group;
    count = materials.size;
    materials.SetSize(count + 1, -1);
    materials[count] = group->material;
    ++group->material->refCount_;
    return group->material;
}
EI_REPLACE_METHOD(0x0051B470, CObject3DClientSpecific::FindOrAddMaterial);

int CObject3DClientSpecific::PartAdded(CFigureMesh* part) {
    int materialKind = static_cast<int>(part->materialValue_);
    int textureKind = static_cast<int>(part->meshType_);
    bool known = false;
    for (int i = 0; i < textures.size; ++i) {
        if (textures[i]->type_ != textureKind) continue;
        known = textures[i] != nullptr;
        break;
    }
    if (!known) FindOrAddTexture(textureKind);
    if (!FindMaterial(textureKind, materialKind)) FindOrAddMaterial(textureKind, materialKind);
    int k = 0;
    while (!partSets[k] || partSets[k]->texture->type_ != textureKind) {
        if (++k >= 2) return 0;
    }
    TextureParts* set = partSets[k];
    for (int g = 0; g < set->groups.size; ++g) {
        PartGroup* group = set->groups[g];
        if (!group->material || group->material->type_ != materialKind) continue;
        for (int i = 0; i < group->parts.size; ++i)
            if (group->parts[i] == part) return 1;
        int count = group->parts.size;
        group->parts.SetSize(count + 1, -1);
        group->parts[count] = part;
        ++part->refCount_;
        return 1;
    }
    return 1;
}
EI_REPLACE_METHOD(0x0051B050, CObject3DClientSpecific::PartAdded);

int CObject3DClientSpecific::PartRemoved(CFigureMesh* part) { return RemoveFromGroups(this, part) ? 1 : 0; }
EI_REPLACE_METHOD(0x0051B160, CObject3DClientSpecific::PartRemoved);

CFigureMesh* CObject3DClientSpecific::RemovePartNamed(const char* name) {
    CFigureMesh* part = AsSpecific()->FindPart(name);
    if (!part) return nullptr;
    return RemoveFromGroups(this, part) ? part : nullptr;
}
EI_REPLACE_METHOD(0x0051B210, CObject3DClientSpecific::RemovePartNamed);

CTexture* CObject3DClientSpecific::ReplaceTexture(CTexture* replacement) {
    if (!replacement) return nullptr;
    CTexture* old = nullptr;
    for (int i = 0; i < textures.size; ++i) {
        if (textures[i]->type_ != replacement->type_) continue;
        old = textures[i];
        textures[i] = replacement;
        ++textures[i]->refCount_;
        old->Release();
        break;
    }
    for (int k = 0; k < 2; ++k) {
        if (!partSets[k] || partSets[k]->texture->type_ != replacement->type_) continue;
        partSets[k]->texture = replacement;
        break;
    }
    return old;
}
EI_REPLACE_METHOD(0x0051B630, CObject3DClientSpecific::ReplaceTexture);

// --- Skins and materials --------------------------------------------------------------------------

int CObject3DClientSpecific::SetSkins(const char* first, const char* second) {
    CTextureManager* manager = static_cast<CWorld*>(ManagerContext())->TextureManager();
    skinName = first;
    secondSkinName = second;
    const char* names[2] = {first, second};
    for (int k = 0; k < 2; ++k) {
        const char* name = names[k];
        if (!name) continue;
        bool named;
        {
            mfc::CString copy(name);
            named = libcrt::Mbsicmp(Bytes(copy), Bytes(game::Text(kNoneText))) != 0;
        }
        if (!named) continue;
        TextureParts* set = partSets[k];
        if (set && set->texture) {
            auto* found = static_cast<CTexture*>(static_cast<CTextureManager*>(manager)->FindTypedTexture(set->texture->type_, name));
            if (found) {
                ReplaceTexture(found);
                continue;
            }
        }
        set = partSets[k];
        if (!set || !set->texture) continue;
        CTexture* skin = set->texture;
        *skin->Name() = name;
        manager->LoadTexture(skin);
    }
    return 1;
}
EI_REPLACE_METHOD(0x0051C250, CObject3DClientSpecific::SetSkins);

void CObject3DClientSpecific::ApplyMaterials() {
    void* manager = static_cast<CWorld*>(ManagerContext())->MaterialManager();
    if (materials.size <= 0) {
        materialCursor = -1;
    } else {
        materialCursor = 0;
        for (CMaterial* material = materials[0]; material;) {
            static_cast<CMaterialManager*>(manager)->FillMaterial(material, static_cast<std::uint32_t>(material->type_));
            ++materialCursor;
            if (materials.size <= materialCursor) break;
            material = materials[materialCursor];
        }
    }
    if (gamecrt::mbscmp(Bytes(secondSkinName), Bytes(game::Text(kNoneText))) == 0)
        SetSkins(skinName, nullptr);
    else
        SetSkins(skinName, secondSkinName);
}
EI_REPLACE_METHOD(0x0051CB30, CObject3DClientSpecific::ApplyMaterials);

void CObject3DClientSpecific::SetLit(int lit) {
    if (lit) {
        for (int i = 0; i < materials.size; ++i) {
            CMaterial* material = materials[i];
            material->entries.push_back(MaterialEntry{material->colors.emissive});
            material = materials[i];
            material->colors.emissive.x = 1.0f;
            material->colors.emissive.y = 1.0f;
            material->colors.emissive.z = 1.0f;
        }
        return;
    }
    for (int i = 0; i < materials.size; ++i) {
        CMaterial* material = materials[i];
        if (material->entries.empty()) continue;
        material->colors.emissive = material->entries.back().emissive;
        material->entries.pop_back();
    }
}
EI_REPLACE_METHOD(0x0051C9F0, CObject3DClientSpecific::SetLit);

void CObject3DClientSpecific::SetLitSlot(int lit) { SetLit(lit); }
EI_REPLACE_METHOD(0x0051C9E0, CObject3DClientSpecific::SetLitSlot);

void CObject3DClientSpecific::EmptySlot25(int) {}
EI_REPLACE_METHOD(0x0051C9D0, CObject3DClientSpecific::EmptySlot25);
