// The halves of a 3D object: the client half (Object3DClientSpecific, class id 0x37: its lifetime, the part sets, the
// skin textures, the materials' lit state, the shadow state, the pulses, the wounds material), the base half's empty
// slots and the server half (Object3DServer, class id 0x38). Layouts: map_object_shadow.h, objects/object3d.h.
#include "game/app/singletons.h"
#include "game/objects/map_object_shadow.h"

#include "engine/core/core_refcount.h"
#include "engine/models/figure_mesh.h"   // VertexPacket
#include "engine/models/link_figure.h"   // FigureMeshDrawer
#include "game/terrain/ground_print.h"   // NamedTexture
#include "game/objects/object3d.h"
#include "engine/models/material.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "engine/scene/render_helper.h"
#include "engine/models/texture.h"
#include "game/world/hoover.h"
#include "game/app/options.h"
#include "game/world/world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cfloat>
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

} // namespace

Object3DClientSpecific::Object3DClientSpecific() {
    std::memset(&sphere, 0, sizeof(sphere));
    ClearWoundsMaterial();
}

void Object3DClientSpecific::Create() {
    flags = 0;
    shadowMade = false;
    woundsTexture.Reset();
    ClearWoundsMaterial();
    std::memset(&sphere, 0, sizeof(sphere));
    partSets[0].reset();
    partSets[1].reset();
    figureScale = {1.0f, 1.0f, 1.0f};
}

void Object3DClientSpecific::Destroy() {
    shadowMade = false;
    woundsTexture.Reset();
    woundsMaterial.Reset();
    partSets[0].reset();
    partSets[1].reset();
    textures.clear();
    materials.clear();
    object = nullptr;
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
        partSets[k] = std::make_unique<TextureParts>();
        set = partSets[k].get();
        set->texture = ClientWorld()->NewTexture(kind);
        textures.emplace_back(set->texture);
    } else {
        if (k == 0) game::DebugLog(kSecondPrimarySetText, object->name, object->id);
        partSets[k]->texture->type = kind;
        set = partSets[k].get();
    }
    if (object->MapType() == MapObjectType::Tree) set->texture->flags |= TextureFlag::Tree;
    return set->texture;
}

Material* Object3DClientSpecific::FindMaterial(int textureKind, int materialKind) {
    for (int k = 0; k < 2; ++k) {
        TextureParts* set = partSets[k].get();
        if (!set || !set->texture || set->texture->type != textureKind) continue;
        for (const std::unique_ptr<PartGroup>& group : set->groups)
            if (group->material && group->material->type == materialKind) return group->material;
    }
    return nullptr;
}

Material* Object3DClientSpecific::FindOrAddMaterial(int textureKind, int materialKind) {
    int k = 0;
    while (!partSets[k] || !partSets[k]->texture || partSets[k]->texture->type != textureKind) {
        if (++k > 1) return nullptr;
    }
    TextureParts* set = partSets[k].get();
    for (const std::unique_ptr<PartGroup>& group : set->groups)
        if (group->material && group->material->type == materialKind) return group->material;
    Material* material = ClientWorld()->NewMaterial(materialKind);
    set->groups.emplace_back(std::make_unique<PartGroup>())->material.Assign(material);
    materials.emplace_back(material);
    return material;
}

int Object3DClientSpecific::OnPartAdded(FigureMesh* part) {
    int materialKind = static_cast<int>(part->materialValue);
    int textureKind = static_cast<int>(part->meshType);
    bool known = false;
    for (int i = 0; i < std::ssize(textures); ++i) {
        if (!textures[i] || textures[i]->type != textureKind) continue;
        known = true;
        break;
    }
    if (!known) FindOrAddTexture(textureKind);
    if (!FindMaterial(textureKind, materialKind)) FindOrAddMaterial(textureKind, materialKind);
    int k = 0;
    while (!partSets[k] || partSets[k]->texture->type != textureKind) {
        if (++k >= 2) return 0;
    }
    for (const std::unique_ptr<PartGroup>& group : partSets[k]->groups) {
        if (!group->material || group->material->type != materialKind) continue;
        if (std::ranges::find(group->parts, part, &Ref<FigureMesh>::Get) == group->parts.end())
            group->parts.emplace_back(part);
        return 1;
    }
    return 1;
}

int Object3DClientSpecific::OnPartRemoved(FigureMesh* part) {
    for (const std::unique_ptr<TextureParts>& set : partSets) {
        if (!set) continue;
        for (const std::unique_ptr<PartGroup>& group : set->groups) {
            auto found = std::ranges::find(group->parts, part, &Ref<FigureMesh>::Get);
            if (found == group->parts.end()) continue;
            group->parts.erase(found);
            return 1;
        }
    }
    return 0;
}

Texture* Object3DClientSpecific::ReplaceTexture(Texture* replacement) {
    if (!replacement) return nullptr;
    Texture* old = nullptr;
    for (int i = 0; i < std::ssize(textures); ++i) {
        if (textures[i]->type != replacement->type) continue;
        old = textures[i];
        textures[i] = replacement;   // the replacement referenced, then the old one released
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
    TextureManager* manager = OwningWorld()->GetTextureManager();
    skinName = first.value_or("");   // (an absent name was the empty text)
    secondSkinName = second.value_or("");
    const std::optional<std::string_view> names[2] = {first, second};
    for (int k = 0; k < 2; ++k) {
        if (!names[k]) continue;
        const std::string_view name = *names[k];
        if (text::CompareIgnoringCase(name, kNoneText) == 0) continue;
        TextureParts* set = partSets[k].get();
        if (set && set->texture) {
            auto* found = manager->FindTypedTexture(set->texture->type, name);
            if (found) {
                ReplaceTexture(found);
                continue;
            }
        }
        if (!set || !set->texture) continue;
        Texture* skin = set->texture;
        skin->name = name;
        manager->LoadTexture(skin);
    }
    return 1;
}

void Object3DClientSpecific::ApplyMaterials() {
    RegisterPartMaterials();
    if (str::Compare(secondSkinName, kNoneText) == 0)
        SetSkins(skinName, std::nullopt);
    else
        SetSkins(skinName, secondSkinName);
}

void Object3DClientSpecific::SetLit(bool lit) {
    if (lit) {
        for (int i = 0; i < std::ssize(materials); ++i) {
            Material* material = materials[i];
            material->entries.push_back(MaterialEntry{material->colors.emissive});
            material->colors.emissive = {1.0f, 1.0f, 1.0f, material->colors.emissive.w};
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

// --- The other halves --------------------------------------------------------------------------------

namespace {

constexpr const char* kServerClassName = "CObject3DServer";

} // namespace

// --- The client half's shadow state -----------------------------------------------------------------

void Object3DClientSpecific::ClearShadow() {
    shadowMade = false;
    std::memset(&sphere, 0, sizeof(sphere));
}

bool Object3DClientSpecific::ShadowInView() {
    if (!options::g_options.shadows) return false;
    if (lib::Has(flags, ClientHalfFlag::Shown)) return true;
    auto placed = sphere;
    placed.center += object->localPosition;
    return g_sceneRenderer.pipeline->ClipTest(&placed) != TransformPipeline::ClipResult::Outside;
}

// --- The base half's empty slots (the server half keeps them) ---------------------------------------

// Nothing is drawn; 0.
int Object3DSpecific::DrawParts(SceneRenderer* /*context*/, Rect* /*dirty*/) { return 0; }
int Object3DSpecific::DrawPartsShadow(SceneRenderer*) { return 0; }
int Object3DSpecific::BoundsContainPoint(const std::int32_t*) { return 0; }
int Object3DSpecific::PartsIntersectRect(const Rect*) { return 0; }
int Object3DSpecific::PartsContainPoint(const std::int32_t*) { return 0; }
float Object3DSpecific::PickDepth(const Point&) { return FLT_MAX; }

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
int Object3DServer::OnPartRemoved(FigureMesh*) { return 0; }
FigureMesh* Object3DServer::FindPart(std::string_view) { return nullptr; }
FigureMesh* Object3DServer::FindPartOfType(int) { return nullptr; }

// --- The wounds material and the stage copies ---------------------------------------------------------

WoundsMaterial::~WoundsMaterial() { preset.Reset(); }

// --- The client half's extra scale and part materials -----------------------------------------------

Vector3 Object3DClientSpecific::GetScale() { return {figureScale.x, figureScale.y, figureScale.z}; }

void Object3DClientSpecific::RegisterPartMaterials() {
    MaterialManager* manager = OwningWorld()->GetMaterialManager();
    for (Material* material : materials) manager->FillMaterial(material, static_cast<std::uint32_t>(material->type));
}

// --- Screen picking, part lookups, class slots, the base half's destructor -----------------

namespace {

Point AsPoint(const std::int32_t* point) { return Point{point[0], point[1]}; }
const Rect* PartRect(const FigureMesh* part) { return &part->drawer->screen; }

} // namespace

int Object3DClientSpecific::BoundsContainPoint(const std::int32_t* point) {
    if (!(object->flags & MapObjectFlag::Shown)) return 0;
    return object->dirtyRect.Contains(AsPoint(point));
}

int Object3DClientSpecific::PartsContainPoint(const std::int32_t* point) {
    Object3D* target = object;
    if (!(target->flags & MapObjectFlag::Shown)) return 0;
    for (int i = 0; i < std::ssize(target->parts); ++i) {
        FigureMesh* part = target->parts[i];
        if (!(part->flags & FigureMeshFlag::Drawn)) continue;
        if (PartRect(part)->Contains(AsPoint(point))) return 1;
    }
    return 0;
}

// The drawn parts whose rectangle holds the point give their surface's depth there (FigureMeshDrawer::SurfaceDepth);
// without any surface under the point (the rectangle's empty corners, a hit by the bounds) the origin's depth counts,
// the original's key for every hit, so a unit's generous click area still works as it did.
float Object3DClientSpecific::PickDepth(const Point& point) {
    float surface = FLT_MAX;
    for (FigureMesh* part : object->parts) {
        if (!(part->flags & FigureMeshFlag::Drawn)) continue;
        if (PartRect(part)->Contains(point)) surface = std::min(surface, part->drawer->SurfaceDepth(point));
    }
    return surface != FLT_MAX ? surface : originDepth;
}

int Object3DClientSpecific::PartsIntersectRect(const Rect* rect) {
    Object3D* target = object;
    if (!(target->flags & MapObjectFlag::Shown)) return 0;
    for (int i = 0; i < std::ssize(target->parts); ++i) {
        FigureMesh* part = target->parts[i];
        if (!(part->flags & FigureMeshFlag::Drawn)) continue;
        if (!PartRect(part)->Intersection(*rect).IsEmpty()) return 1;
    }
    return 0;
}

FigureMesh* Object3DClientSpecific::FindPart(std::string_view name) { return object->FindPart(name); }

FigureMesh* Object3DClientSpecific::FindPartOfType(int partType) { return object->FindPartOfType(partType); }

// The class name and id 0x37.
const char* Object3DClientSpecific::ClassName() { return "CObject3DClientSpecific"; }
ClassId Object3DClientSpecific::GetClassId() { return ClassId::Object3DClientSpecific; }

// The base half's destructor (deleting form): the object forgotten.
Object3DSpecific::~Object3DSpecific() { object = nullptr; }

Vector3 Object3DSpecific::GetScale() { return {1.0f, 1.0f, 1.0f}; }

namespace {
constexpr float kPulsePeriod = 30.0f;
constexpr float kPulseFold = 15.0f;
constexpr float kPulseFull = 30.0f;
constexpr float kPulseScale = 0.06666667f;            // 1/15
constexpr float kPulseBase = 0.2f;

// The pulse's value after the clock moved on by `frames`.
float Pulse(float& clock, float frames) {
    float now = frames + clock;   // stored as a float, and to the clock
    clock = now;
    float phase = std::fmod(now, kPulsePeriod);
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
        part->colors.specular = {value, 0.0f, 0.0f, part->colors.specular.w};
        part->flags |= MaterialFlag::KeepSpecular;
    }
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
    TextureManager* manager = g_clientWorld.textureManager.get();
    if (!woundsTexture) {
        woundsTexture.Assign(manager->FindTypedTexture(kWoundsTextureKind, name));
        if (!woundsTexture) {
            woundsTexture.Assign(manager->FindOrCreate(kWoundsTextureKind, name));
            if (!woundsTexture) game::FatalExit(kNoWoundsTexture, object->ClassName(), object->id);
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
    return 1;
}
