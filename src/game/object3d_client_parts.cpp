// The client half of a 3D object (Object3DClientSpecific, class id 0x37):
// its lifetime, the part sets (the figure's parts grouped by texture and material, which Draw
// walks), the skin textures and the materials' lit state. Layouts: map_object_shadow.h.
#include "map_object_shadow.h"
#include "game_strings.h"

#include "map_object_figures.h"
#include "material.h"
#include "texture.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr const char* kNoneText = "none";
// "Adding new primary texture set with one exist\nFor object \"%s\" with ID=%d!\n"
constexpr const char* kSecondPrimarySetText = "Adding new primary texture set with one exist\nFor object \"%s\" with ID=%d!\n";

// The texture kinds (a part's meshType, TextureKind) whose parts make the first part set; any other kind makes the
// second. The data does not say what they have in common.
constexpr int kPrimaryKinds[] = {1, 3, 4, 6, 7, 8, 9, 0xB, 0xD, 0xE, 0xF, 0x10};

// Whether a texture of `kind` belongs to the first part set.
bool IsPrimaryKind(int kind) { return std::ranges::find(kPrimaryKinds, kind) != std::end(kPrimaryKinds); }

// `part` taken out of the group that holds it (the tail moved down) and released: true, or false
// when no group of either set holds it.
bool RemoveFromGroups(Object3DClientSpecific* self, FigureMesh* part) {
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

Object3DClientSpecific::Object3DClientSpecific() {
    shadowMade = false;
    std::memset(&sphere, 0, sizeof(sphere));
    materialCursor = -1;
    ClearWoundsMaterial();
    pulseClock = 0.0f;
}

void Object3DClientSpecific::Create() {
    materialCursor = -1;
    flags = 0;
    shadowMade = false;
    woundsTexture.Reset();
    ClearWoundsMaterial();
    std::memset(&sphere, 0, sizeof(sphere));
    partSets[0] = nullptr;
    partSets[1] = nullptr;
    touchTime = 0;
    figureScale.z = 1.0f;
    figureScale.y = 1.0f;
    figureScale.x = 1.0f;
}

void Object3DClientSpecific::Destroy() {
    shadowMade = false;
    woundsTexture.Reset();
    woundsMaterial.Reset();
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
    flags = 0;
}

Object3DClientSpecific::~Object3DClientSpecific() { Object3DClientSpecific::Destroy(); }

// --- Parts, textures and materials ----------------------------------------------------------------

CTexture* Object3DClientSpecific::FindOrAddTexture(int kind) {
    for (int i = 0; i < std::ssize(textures); ++i)
        if (textures[i]->type == kind) return textures[i];
    int k = IsPrimaryKind(kind) ? 0 : 1;
    TextureParts* set;
    if (!partSets[k]) {
        set = new TextureParts();
        set->texture = ClientWorld()->NewTexture(kind);
        partSets[k] = set;
        int count = lib::Count(textures);
        textures.resize(count + 1);
        textures[count] = set->texture;
        set->texture->AddRef();
    } else {
        if (k == 0) game::DebugLog(kSecondPrimarySetText, object->name, object->id);
        partSets[k]->texture->type = kind;
        set = partSets[k];
    }
    if (object->MapType() == MapObjectType::Tree) set->texture->flags |= TextureFlag::Tree;
    return set->texture;
}

Material* Object3DClientSpecific::FindMaterial(int textureKind, int materialKind) {
    for (int k = 0; k < 2; ++k) {
        TextureParts* set = partSets[k];
        if (!set || !set->texture || set->texture->type != textureKind) continue;
        for (int g = 0; g < std::ssize(set->groups); ++g) {
            Material* material = set->groups[g]->material;
            if (material && material->type == materialKind) return partSets[k]->groups[g]->material;
        }
    }
    return nullptr;
}

Material* Object3DClientSpecific::FindOrAddMaterial(int textureKind, int materialKind) {
    int k = 0;
    while (!partSets[k] || !partSets[k]->texture || partSets[k]->texture->type != textureKind) {
        if (++k > 1) return nullptr;
    }
    TextureParts* set = partSets[k];
    for (int g = 0; g < std::ssize(set->groups); ++g) {
        Material* material = set->groups[g]->material;
        if (material && material->type == materialKind) return set->groups[g]->material;
    }
    PartGroup* group = new PartGroup();
    Material* material = ClientWorld()->NewMaterial(materialKind);
    group->material = material;
    material->AddRef();
    set = partSets[k];
    int count = lib::Count(set->groups);
    set->groups.resize(count + 1);
    set->groups[count] = group;
    count = lib::Count(materials);
    materials.resize(count + 1);
    materials[count] = group->material;
    group->material->AddRef();
    return group->material;
}

int Object3DClientSpecific::OnPartAdded(FigureMesh* part) {
    int materialKind = static_cast<int>(part->materialValue);
    int textureKind = static_cast<int>(part->meshType);
    bool known = false;
    for (int i = 0; i < std::ssize(textures); ++i) {
        if (textures[i]->type != textureKind) continue;
        known = textures[i] != nullptr;
        break;
    }
    if (!known) FindOrAddTexture(textureKind);
    if (!FindMaterial(textureKind, materialKind)) FindOrAddMaterial(textureKind, materialKind);
    int k = 0;
    while (!partSets[k] || partSets[k]->texture->type != textureKind) {
        if (++k >= 2) return 0;
    }
    TextureParts* set = partSets[k];
    for (int g = 0; g < std::ssize(set->groups); ++g) {
        PartGroup* group = set->groups[g];
        if (!group->material || group->material->type != materialKind) continue;
        for (int i = 0; i < std::ssize(group->parts); ++i)
            if (group->parts[i] == part) return 1;
        int count = lib::Count(group->parts);
        group->parts.resize(count + 1);
        group->parts[count] = part;
        part->AddRef();
        return 1;
    }
    return 1;
}

int Object3DClientSpecific::OnPartRemoved(FigureMesh* part) { return RemoveFromGroups(this, part) ? 1 : 0; }

FigureMesh* Object3DClientSpecific::RemoveFromPartSets(const char* name) {
    FigureMesh* part = FindPart(name);
    if (!part) return nullptr;
    return RemoveFromGroups(this, part) ? part : nullptr;
}

CTexture* Object3DClientSpecific::ReplaceTexture(CTexture* replacement) {
    if (!replacement) return nullptr;
    CTexture* old = nullptr;
    for (int i = 0; i < std::ssize(textures); ++i) {
        if (textures[i]->type != replacement->type) continue;
        old = textures[i];
        textures[i] = replacement;
        textures[i]->AddRef();
        old->Release();
        break;
    }
    for (int k = 0; k < 2; ++k) {
        if (!partSets[k] || partSets[k]->texture->type != replacement->type) continue;
        partSets[k]->texture = replacement;
        break;
    }
    return old;
}

// --- Skins and materials --------------------------------------------------------------------------

int Object3DClientSpecific::SetSkins(std::optional<std::string_view> first, std::optional<std::string_view> second) {
    CTextureManager* manager = WorldContext()->GetTextureManager();
    skinName = first.value_or("");   // (an absent name was the empty text)
    secondSkinName = second.value_or("");
    const std::optional<std::string_view> names[2] = {first, second};
    for (int k = 0; k < 2; ++k) {
        if (!names[k]) continue;
        const std::string_view name = *names[k];
        if (text::CompareIgnoringCase(name, kNoneText) == 0) continue;
        TextureParts* set = partSets[k];
        if (set && set->texture) {
            auto* found = manager->FindTypedTexture(set->texture->type, name);
            if (found) {
                ReplaceTexture(found);
                continue;
            }
        }
        set = partSets[k];
        if (!set || !set->texture) continue;
        CTexture* skin = set->texture;
        skin->name = name;
        manager->LoadTexture(skin);
    }
    return 1;
}

void Object3DClientSpecific::ApplyMaterials() {
    CMaterialManager* manager = WorldContext()->GetMaterialManager();
    if (materials.empty()) {
        materialCursor = -1;
    } else {
        materialCursor = 0;
        for (Material* material = materials[0]; material;) {
            manager->FillMaterial(material, static_cast<std::uint32_t>(material->type));
            ++materialCursor;
            if (std::ssize(materials) <= materialCursor) break;
            material = materials[materialCursor];
        }
    }
    if (str::Compare(secondSkinName, kNoneText) == 0)
        SetSkins(skinName, std::nullopt);
    else
        SetSkins(skinName, secondSkinName);
}

void Object3DClientSpecific::SetMaterialsLit(bool lit) {
    if (lit) {
        for (int i = 0; i < std::ssize(materials); ++i) {
            Material* material = materials[i];
            material->entries.push_back(MaterialEntry{material->colors.emissive});
            material = materials[i];
            material->colors.emissive.x = 1.0f;
            material->colors.emissive.y = 1.0f;
            material->colors.emissive.z = 1.0f;
        }
        return;
    }
    for (int i = 0; i < std::ssize(materials); ++i) {
        Material* material = materials[i];
        if (material->entries.empty()) continue;
        material->colors.emissive = material->entries.back().emissive;
        material->entries.pop_back();
    }
}

void Object3DClientSpecific::SetLit(bool lit) { SetMaterialsLit(lit); }
