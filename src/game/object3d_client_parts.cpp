// The client half of a 3D object (CObject3DClientSpecific, class id 0x37):
// its lifetime, the part sets (the figure's parts grouped by texture and material, which Draw
// walks), the skin textures and the materials' lit state. Layouts: map_object_shadow.h.
#include "map_object_shadow.h"
#include "game_strings.h"

#include "map_object_figures.h"
#include "material.h"
#include "texture.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr const char* kNoneText = "none";
// "Adding new primary texture set with one exist\nFor object \"%s\" with ID=%d!\n"
constexpr const char* kSecondPrimarySetText = "Adding new primary texture set with one exist\nFor object \"%s\" with ID=%d!\n";
constexpr int kTypeBuildingLike = 0x35;             // CObject3D::type_: its textures get flag 0x2000
constexpr std::uint32_t kTextureFlag2000 = 0x2000;

// *slot = value with counted references; the old one released (its slot 4).
template <typename T>
void AssignCounted(T** slot, T* value) {
    T* old = *slot;
    *slot = value;
    if (value) ++value->refCount_;
    if (old) old->Release();
}

// Whether a texture of `kind` belongs to the first part set (a table for kinds 1..0x10;
// any other kind goes to the second).
bool IsPrimaryKind(int kind) {
    switch (kind) {
    case 1: case 3: case 4: case 6: case 7: case 8: case 9: case 0xB: case 0xD: case 0xE: case 0xF: case 0x10:
        return true;
    default:
        return false;
    }
}

// `part` taken out of the group that holds it (the tail moved down) and released: true, or false
// when no group of either set holds it (slots 14 / 15 share the loop).
bool RemoveFromGroups(CObject3DClientSpecific* self, CFigureMesh* part) {
    for (int k = 0; k < 2; ++k) {
        TextureParts* set = self->partSets[k];
        if (!set) continue;
        for (int g = 0; g < std::ssize(set->groups); ++g) {
            PartGroup* group = set->groups[g];
            for (int i = 0; i < std::ssize(group->parts); ++i) {
                if (group->parts[i] != part) continue;
                group->parts.erase(group->parts.begin() + i);
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
    for (int i = 0; i < std::ssize(parts); ++i) {
        if (!parts[i]) continue;
        parts[i]->Release();
        parts[i] = nullptr;
    }
    lib::FreeStorage(parts);
    return 1;
}

TextureParts::~TextureParts() {
    for (PartGroup* group : groups) {
        if (!group) continue;
        group->ReleaseParts();
        if (group->material) group->material->Release();
        delete group;
    }
}

CObject3DClientSpecific::CObject3DClientSpecific() {
    texture = nullptr;
    std::memset(&sphere, 0, 0x38);
    woundsTexture = nullptr;
    woundsMaterial = nullptr;
    materialCursor = -1;
    AssignCounted<CTexture>(&texture, nullptr);
    AssignCounted<CTexture>(&woundsTexture, nullptr);
    ClearWoundsMaterial();
    bounds[3] = 0.0f;
    bounds[2] = 0.0f;
    bounds[1] = 0.0f;
    bounds[0] = 0.0f;
    pulseClock = 0.0f;
}

int CObject3DClientSpecific::Create() {
    materialCursor = -1;
    flags_ = 0;
    AssignCounted<CTexture>(&texture, nullptr);
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

void CObject3DClientSpecific::Destroy() {
    AssignCounted<CTexture>(&texture, nullptr);
    AssignCounted<CTexture>(&woundsTexture, nullptr);
    woundsMaterial.Assign(nullptr);
    for (int k = 0; k < 2; ++k) {
        delete partSets[k];
    }
    partSets[0] = nullptr;
    partSets[1] = nullptr;
    for (int i = 0; i < std::ssize(textures); ++i) {
        if (!textures[i]) continue;
        textures[i]->Release();
        textures[i] = nullptr;
    }
    textures.clear();
    for (int i = 0; i < std::ssize(materials); ++i) {
        if (!materials[i]) continue;
        materials[i]->Release();
        materials[i] = nullptr;
    }
    materials.clear();
    object = nullptr;
    touchTime = 0;
    flags_ = 0;
}

CObject3DClientSpecific::~CObject3DClientSpecific() {
    CObject3DClientSpecific::Destroy();
    if (woundsMaterial) woundsMaterial->Release();
    if (woundsTexture) woundsTexture->Release();
    if (texture) texture->Release();
}

// --- Parts, textures and materials ----------------------------------------------------------------

CTexture* CObject3DClientSpecific::FindOrAddTexture(int kind) {
    for (int i = 0; i < std::ssize(textures); ++i)
        if (textures[i]->type_ == kind) return textures[i];
    int k = IsPrimaryKind(kind) ? 0 : 1;
    TextureParts* set;
    if (!partSets[k]) {
        set = new TextureParts();
        set->texture = ClientWorld()->NewTexture(kind);
        partSets[k] = set;
        int count = std::ssize(textures);
        textures.resize(count + 1);
        textures[count] = set->texture;
        ++set->texture->refCount_;
    } else {
        if (k == 0) game::DebugLog(kSecondPrimarySetText, object->name.c_str(), object->id_);
        partSets[k]->texture->type_ = kind;
        set = partSets[k];
    }
    if (object->type_ == kTypeBuildingLike) set->texture->flags_ |= kTextureFlag2000;
    return set->texture;
}

CMaterial* CObject3DClientSpecific::FindMaterial(int textureKind, int materialKind) {
    for (int k = 0; k < 2; ++k) {
        TextureParts* set = partSets[k];
        if (!set || !set->texture || set->texture->type_ != textureKind) continue;
        for (int g = 0; g < std::ssize(set->groups); ++g) {
            CMaterial* material = set->groups[g]->material;
            if (material && material->type_ == materialKind) return partSets[k]->groups[g]->material;
        }
    }
    return nullptr;
}

CMaterial* CObject3DClientSpecific::FindOrAddMaterial(int textureKind, int materialKind) {
    int k = 0;
    while (!partSets[k] || !partSets[k]->texture || partSets[k]->texture->type_ != textureKind) {
        if (++k > 1) return nullptr;
    }
    TextureParts* set = partSets[k];
    for (int g = 0; g < std::ssize(set->groups); ++g) {
        CMaterial* material = set->groups[g]->material;
        if (material && material->type_ == materialKind) return set->groups[g]->material;
    }
    PartGroup* group = new PartGroup();
    CMaterial* material = ClientWorld()->NewMaterial(materialKind);
    group->material = material;
    ++material->refCount_;
    set = partSets[k];
    int count = std::ssize(set->groups);
    set->groups.resize(count + 1);
    set->groups[count] = group;
    count = std::ssize(materials);
    materials.resize(count + 1);
    materials[count] = group->material;
    ++group->material->refCount_;
    return group->material;
}

int CObject3DClientSpecific::OnPartAdded(CFigureMesh* part) {
    int materialKind = static_cast<int>(part->materialValue_);
    int textureKind = static_cast<int>(part->meshType_);
    bool known = false;
    for (int i = 0; i < std::ssize(textures); ++i) {
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
    for (int g = 0; g < std::ssize(set->groups); ++g) {
        PartGroup* group = set->groups[g];
        if (!group->material || group->material->type_ != materialKind) continue;
        for (int i = 0; i < std::ssize(group->parts); ++i)
            if (group->parts[i] == part) return 1;
        int count = std::ssize(group->parts);
        group->parts.resize(count + 1);
        group->parts[count] = part;
        ++part->refCount_;
        return 1;
    }
    return 1;
}

int CObject3DClientSpecific::OnPartRemoved(CFigureMesh* part) { return RemoveFromGroups(this, part) ? 1 : 0; }

CFigureMesh* CObject3DClientSpecific::RemoveFromPartSets(const char* name) {
    CFigureMesh* part = FindPart(name);
    if (!part) return nullptr;
    return RemoveFromGroups(this, part) ? part : nullptr;
}

CTexture* CObject3DClientSpecific::ReplaceTexture(CTexture* replacement) {
    if (!replacement) return nullptr;
    CTexture* old = nullptr;
    for (int i = 0; i < std::ssize(textures); ++i) {
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

// --- Skins and materials --------------------------------------------------------------------------

int CObject3DClientSpecific::SetSkins(const char* first, const char* second) {
    CTextureManager* manager = WorldContext()->TextureManager();
    skinName = first ? first : "";   // (a null name was the empty text)
    secondSkinName = second ? second : "";
    const char* names[2] = {first, second};
    for (int k = 0; k < 2; ++k) {
        const char* name = names[k];
        if (!name) continue;
        bool named;
        {
            std::string copy = name;
            named = text::CompareIgnoringCase(copy, kNoneText) != 0;
        }
        if (!named) continue;
        TextureParts* set = partSets[k];
        if (set && set->texture) {
            auto* found = manager->FindTypedTexture(set->texture->type_, name);
            if (found) {
                ReplaceTexture(found);
                continue;
            }
        }
        set = partSets[k];
        if (!set || !set->texture) continue;
        CTexture* skin = set->texture;
        skin->name_ = name;
        manager->LoadTexture(skin);
    }
    return 1;
}

void CObject3DClientSpecific::ApplyMaterials() {
    CMaterialManager* manager = WorldContext()->MaterialManager();
    if (materials.empty()) {
        materialCursor = -1;
    } else {
        materialCursor = 0;
        for (CMaterial* material = materials[0]; material;) {
            manager->FillMaterial(material, static_cast<std::uint32_t>(material->type_));
            ++materialCursor;
            if (std::ssize(materials) <= materialCursor) break;
            material = materials[materialCursor];
        }
    }
    if (str::Compare(secondSkinName, kNoneText) == 0)
        SetSkins(skinName.c_str(), nullptr);
    else
        SetSkins(skinName.c_str(), secondSkinName.c_str());
}

void CObject3DClientSpecific::SetMaterialsLit(int lit) {
    if (lit) {
        for (int i = 0; i < std::ssize(materials); ++i) {
            CMaterial* material = materials[i];
            material->entries.push_back(MaterialEntry{material->colors.emissive});
            material = materials[i];
            material->colors.emissive.x = 1.0f;
            material->colors.emissive.y = 1.0f;
            material->colors.emissive.z = 1.0f;
        }
        return;
    }
    for (int i = 0; i < std::ssize(materials); ++i) {
        CMaterial* material = materials[i];
        if (material->entries.empty()) continue;
        material->colors.emissive = material->entries.back().emissive;
        material->entries.pop_back();
    }
}

void CObject3DClientSpecific::SetLit(int lit) { SetMaterialsLit(lit); }
