// The halves of a 3D object: the client half (Object3DClientSpecific, class id 0x37: its lifetime, the part sets, the
// skin textures, the materials' lit state, the shadow state, the pulses, the wounds material), the base half's empty
// slots and the server half (Object3DServer, class id 0x38). Layouts: map_object_shadow.h, map_object_figures.h.
#include "map_object_shadow.h"

#include "core_refcount.h"
#include "figure_mesh.h"    // VertexPacket
#include "game_strings.h"
#include "ground_print.h"   // NamedTexture
#include "link_figure.h"   // LinkMesh::screenRect
#include "map_object_figures.h"
#include "material.h"
#include "model.h"
#include "object.h"
#include "options.h"
#include "texture.h"
#include "world.h"
#include "world_data.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"
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

Texture* Object3DClientSpecific::FindOrAddTexture(int kind) {
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

Texture* Object3DClientSpecific::ReplaceTexture(Texture* replacement) {
    if (!replacement) return nullptr;
    Texture* old = nullptr;
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
    TextureManager* manager = WorldContext()->GetTextureManager();
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
        Texture* skin = set->texture;
        skin->name = name;
        manager->LoadTexture(skin);
    }
    return 1;
}

void Object3DClientSpecific::ApplyMaterials() {
    MaterialManager* manager = WorldContext()->GetMaterialManager();
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

// --- The other halves --------------------------------------------------------------------------------

// The halves of a 3D object besides the client half's part sets: the client half's shadow state, the base
// half's empty slots, the server half (Object3DServer, class id 0x38), the wounds material's destructor and
// the copies of a named texture and a material stage. Layouts: map_object_shadow.h, map_object_figures.h.

namespace {

constexpr const char* kServerClassName = "CObject3DServer";
// The shadow switches by the object's class: units (0x50), class 0x52, any other.
constexpr std::uint32_t kHalfShown = 0x20;
constexpr int kOutsideView = 2;

int Setting(const int& setting) { return static_cast<const volatile int&>(setting); }

// The switch for the class of `object`.
int ShadowSwitchOf(Object3D* object) {
    ClassId classId = object->GetClassId();
    if (classId == ClassId::Unit) return Setting(options::g_options.unitShadow);
    if (classId == ClassId::Tree) return Setting(options::g_options.floraShadow);
    return Setting(options::g_options.buildingShadow);
}

} // namespace

// --- The client half's shadow state -----------------------------------------------------------------

std::uint32_t Object3DClientSpecific::ShadowsShown() {
    int setting = worlddata::ShadowSetting();
    if (setting <= 0) return static_cast<std::uint32_t>(setting) & 0xFFFFFF00u;
    return ShadowSwitchOf(object) != 0 ? 1u : 0u;
}

void Object3DClientSpecific::ClearShadow() {
    shadowMade = false;
    std::memset(&sphere, 0, sizeof(sphere));
}

bool Object3DClientSpecific::ShadowInView() {
    if (worlddata::ShadowSetting() <= 0 || ShadowSwitchOf(object) == 0) return false;
    if (flags & kHalfShown) return true;
    const Vector3& at = object->localPosition;
    sphere.center.x = sphere.center.x + at.x;
    sphere.center.y = at.y + sphere.center.y;
    sphere.center.z = at.z + sphere.center.z;
    int side = g_sceneRenderer.pipeline->ClipTest(&sphere);
    const Vector3& back = object->localPosition;
    sphere.center.x = sphere.center.x - back.x;
    sphere.center.y = sphere.center.y - back.y;
    sphere.center.z = sphere.center.z - back.z;
    return side != kOutsideView;
}

// --- The base half's empty slots (the server half keeps them) ---------------------------------------

// Nothing is drawn; 0.
int Object3DSpecific::DrawParts(SceneRenderer* /*context*/, Rect* /*dirty*/) { return 0; }
int Object3DSpecific::DrawPartsShadow(SceneRenderer*) { return 0; }
int Object3DSpecific::BoundsContainPoint(const std::int32_t*) { return 0; }
int Object3DSpecific::PartsIntersectRect(const Rect*) { return 0; }
int Object3DSpecific::PartsContainPoint(const std::int32_t*) { return 0; }

// --- The server half ---------------------------------------------------------------------------------

// The base half's (the engine object's fields, no object). Deleting form: the base half's.
Object3DServer::Object3DServer() {}
Object3DServer::~Object3DServer() {}

// No object.
void Object3DServer::Create() {}
void Object3DServer::Destroy() { object = nullptr; }

// Class name and id.
const char* Object3DServer::ClassName() { return kServerClassName; }
ClassId Object3DServer::GetClassId() { return ClassId::Object3DServer; }

// The part virtuals: no parts; 0.
int Object3DServer::OnPartAdded(FigureMesh*) { return 0; }
FigureMesh* Object3DServer::RemoveFromPartSets(const char*) { return nullptr; }
int Object3DServer::OnPartRemoved(FigureMesh*) { return 0; }
FigureMesh* Object3DServer::FindPart(std::string_view) { return nullptr; }
FigureMesh* Object3DServer::FindPartOfType(int) { return nullptr; }

// --- The wounds material and the stage copies ---------------------------------------------------------

WoundsMaterial::~WoundsMaterial() { preset.Reset(); }

// --- The client half's extra scale and part materials -----------------------------------------------

Vector3 Object3DClientSpecific::GetScale() { return {figureScale.x, figureScale.y, figureScale.z}; }

void Object3DClientSpecific::RegisterPartMaterials() {
    MaterialManager* manager = WorldContext()->GetMaterialManager();
    if (materials.empty()) return;
    materialCursor = 0;
    for (Material* material = materials[0]; material;) {
        manager->FillMaterial(material, static_cast<std::uint32_t>(material->type));
        ++materialCursor;
        if (std::ssize(materials) <= materialCursor) break;
        material = materials[materialCursor];
    }
}

// --- Screen picking, part lookups, class slots, the base half's destructor -----------------

namespace {
// "Object3DClientSpecific".
const char* ClientClassName() { return "CObject3DClientSpecific"; }

Point AsPoint(const std::int32_t* point) { return Point{point[0], point[1]}; }
const Rect* PartRect(const FigureMesh* part) { return &part->drawer->screen; }

constexpr float kNoScale = 1.0f;

} // namespace

int Object3DClientSpecific::BoundsContainPoint(const std::int32_t* point) {
    if (!(object->flags & MapObjectFlag::Shown)) return 0;
    return rects::Contains(&object->dirtyRect, AsPoint(point));
}

int Object3DClientSpecific::PartsContainPoint(const std::int32_t* point) {
    Object3D* target = object;
    if (!(target->flags & MapObjectFlag::Shown)) return 0;
    for (int i = 0; i < std::ssize(target->parts); ++i) {
        FigureMesh* part = target->parts[i];
        if (!(part->flags & FigureMeshFlag::Drawn)) continue;
        if (rects::Contains(PartRect(part), AsPoint(point))) return 1;
    }
    return 0;
}

int Object3DClientSpecific::PartRectsMeet(const Rect* rect) {
    Object3D* target = object;
    if (!(target->flags & MapObjectFlag::Shown)) return 0;
    for (int i = 0; i < std::ssize(target->parts); ++i) {
        FigureMesh* part = target->parts[i];
        if (!(part->flags & FigureMeshFlag::Drawn)) continue;
        Rect common;
        rects::Intersect(&common, PartRect(part), rect);
        if (!rects::IsEmpty(&common)) return 1;
    }
    return 0;
}

FigureMesh* Object3DClientSpecific::FindPart(std::string_view name) { return object->FindPart(name); }

FigureMesh* Object3DClientSpecific::FindPartOfType(int partType) { return object->FindPartOfType(partType); }

// The class name and id 0x37.
const char* Object3DClientSpecific::ClassName() { return ClientClassName(); }
ClassId Object3DClientSpecific::GetClassId() { return ClassId::Object3DClientSpecific; }

// The base half's destructor (deleting form): the object forgotten.
Object3DSpecific::~Object3DSpecific() { object = nullptr; }

Vector3 Object3DSpecific::GetScale() { return {kNoScale, kNoScale, kNoScale}; }

namespace {
constexpr double kPulsePeriod = 30.0;
constexpr float kPulseFold = 15.0f;
constexpr float kPulseFull = 30.0f;
constexpr float kPulseScale = 0.06666667f;            // 1/15
constexpr float kPulseBase = 0.2f;

// The pulse's value after the clock moved on by `frames`.
float Pulse(float& clock, float frames) {
    float now = frames + clock;   // stored as a float, and to the clock
    clock = now;
    float phase = static_cast<float>(std::fmod(static_cast<double>(now), kPulsePeriod));   // exact
    if (phase > kPulseFold) phase = kPulseFull - phase;
    float value = phase * kPulseScale * 0.5f + kPulseBase;
    return value;
}
} // namespace

void Object3DClientSpecific::InvisiblePulse(float frames) {
    float value = Pulse(pulseClock, frames);
    for (int i = 0; i < std::ssize(materials); ++i) materials[i]->colors.diffuse.w = value;
}

void Object3DClientSpecific::StunnedPulse(float frames) {
    float value = Pulse(pulseClock, frames);
    for (int i = 0; i < std::ssize(materials); ++i) {
        Material* part = materials[i];
        part->colors.specular.x = value;
        part->colors.specular.y = 0.0f;
        part->colors.specular.z = 0.0f;
        part->flags |= MaterialFlag::KeepSpecular;
    }
}

Material* Object3DClientSpecific::FirstMaterial() {
    if (materials.empty()) {
        materialCursor = -1;
        return nullptr;
    }
    materialCursor = 0;
    return materials[0];
}

Material* Object3DClientSpecific::NextMaterial() {
    ++materialCursor;
    return std::ssize(materials) > materialCursor ? materials[materialCursor] : nullptr;
}

namespace {
constexpr int kWoundsTextureKind = static_cast<int>(TextureKind::Effects);
constexpr unsigned kWoundsPreset = 4;
constexpr const char* kNoWoundsTexture = "Can't create wounds texture for object \"%s\" ID = %d\n";
} // namespace

void Object3DClientSpecific::HideWounds() {
    woundsTexture.Reset();
    ClearWoundsMaterial();
}

// The constructor: no references, then its own fields; the stages vector is constructed empty.
WoundsMaterial::WoundsMaterial() {
    blend = 1;
    sourceBlend = render::Blend::SourceAlpha;
    destBlend = render::Blend::InvSourceAlpha;
}

int Object3DClientSpecific::ShowWounds(const char* name) {
    if (!partSets[0]) return 0;
    TextureManager* manager = g_clientWorld.textureManager;
    if (!woundsTexture) {
        woundsTexture.Assign(manager->FindTypedTexture(kWoundsTextureKind, name));
        if (!woundsTexture) {
            woundsTexture.Assign(manager->FindOrCreate(kWoundsTextureKind, name));
            if (!woundsTexture) return game::FatalError(kNoWoundsTexture, object->ClassName(), object->id);
        }
    }
    std::vector<std::uint8_t> extra{};
    manager->LoadMmpExtra(name, &extra);
    const std::vector<std::uint8_t>& loaded = woundsTexture->extra;
    bool same = !extra.empty() && std::ssize(loaded) == std::ssize(extra) &&
                std::memcmp(loaded.data(), extra.data(), static_cast<std::size_t>(std::ssize(extra))) == 0;
    if (!same) {
        woundsTexture->flags &= ~TextureFlag::Loaded;
        if (!manager->LoadTexture(woundsTexture)) {
            woundsTexture.Reset();
            woundsMaterial.Reset();
            lib::FreeStorage(extra);
            return 0;
        }
    }
    if (!woundsMaterial) {
        woundsMaterial.Assign(new WoundsMaterial());
        RenderHelper* helper = g_sceneRenderer.helper;
        woundsMaterial->preset.Assign(helper->Preset(kWoundsPreset));
        woundsMaterial->stages.push_back(MaterialStage(partSets[0]->texture));
        woundsMaterial->stages.push_back(MaterialStage(woundsTexture));
    }
    lib::FreeStorage(extra);
    return 1;
}
