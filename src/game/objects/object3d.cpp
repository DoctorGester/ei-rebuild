// The 3D object base under MapObject (Object3D): loading the figure's parts, rescaling them and their bounding
// box; its lifetime, Create / Destroy, effect sources, attached half and small virtuals; the random animations
// of its figure and SetUpAnimatedFigure. See objects/object3d.h and docs/notes/map_objects.md ("Figures").
#include "game/objects/object3d.h"

#include "game/ai/ai_map.h"   // FootprintPart
#include "engine/core/core_maths.h"
#include "engine/core/core_transform.h"
#include "game/effects/effects.h"
#include "engine/models/figure_manager.h"
#include "engine/models/figure_mesh.h"
#include "engine/models/link_figure.h"   // LinkHeaderMap: the figure's pieces
#include "game/objects/map_object.h"
#include "game/objects/map_object_shadow.h"   // WoundsMaterial: the client half's
#include "engine/models/link_loader.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "engine/core/random.h"
#include "game/world/world.h"   // World: the figure manager and cache
#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/rect.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

// The bounds must keep the original's order of operations and its NaN behaviour.

namespace {
constexpr std::uint32_t kBoundsValid = 1 | 2;

// Animation state: rescale to x, y, z.
void AnimationSetScale(FigureAnimationState* state, float x, float y, float z) { state->Rescale(x, y, z); }

// The box being grown; empty is FLT_MAX / -FLT_MAX.
struct GrowingBox {
    Vector3 low = {FLT_MAX, FLT_MAX, FLT_MAX};
    Vector3 high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
};

// The squared distance of the part's position from the figure's origin.
float DistanceSquared(const FigureMesh* part) { return LengthSquared(part->localPosition); }

// A placed part: its piece's box moved by its position and the piece's offset.
void AddPlacedPart(GrowingBox& box, const FigureMesh* p) {
    const Vector3 low = p->box.bounds.boxMin + p->localPosition + p->box.bounds.center;
    const Vector3 high = p->box.bounds.boxMax + p->localPosition + p->box.bounds.center;
    box.low = MinKeepA(low, box.low);
    box.high = MaxKeepB(box.high, high);
}

} // namespace

void Object3D::ComputeBounds() {
    GrowingBox box;
    for (int i = 0; i < std::ssize(parts); ++i) {
        FigureMesh* part = parts[i];
        if (DistanceSquared(part) > kPartsNearSquared) continue;   // NaN distances count as near
        AddPlacedPart(box, part);
    }
    if (parts.empty()) return;

    const Vector3 low = box.low;
    const Vector3 high = box.high;
    Vector3 center = (low + high) * 0.5f;
    bounds.boxMin = low - center;
    bounds.boxMax = high - center;
    bounds.flags |= BoundsFlag::Box;
    bounds.flags |= BoundsFlag::Sphere;
    bounds.radius = std::sqrt(LengthSquared(bounds.boxMin));
    bounds.center = center;
}

void Object3D::SetFigureScale(const Vector3& newScale) {
    if (scale.x == newScale.x && scale.y == newScale.y && scale.z == newScale.z)
        return;
    scale = newScale;
    if (LinkMesh* current = figure) {
        LinkFigureLoader* loader = GetManagerContext()->FigureLoader();
        LinkMesh* rebuilt = loader->Remorph(current, {scale.x, scale.y, scale.z});
        figure.Assign(rebuilt);
        for (int i = 0; i < std::ssize(parts); ++i) {
            FigureMesh* part = parts[i];
            auto piece = figure->headers.find(std::string_view(part->GetName()));
            const ClipBounds& fitted = piece->second.box.bounds;
            part->box.bounds.center = fitted.center;
            part->box.bounds.boxMin = fitted.boxMin;
            part->box.bounds.boxMax = fitted.boxMax;
            part->box.bounds.radius = fitted.radius;
        }
    }
    if (animation) AnimationSetScale(animation, scale.x, scale.y, scale.z);
    ComputeBounds();
}

void Object3D::LoadFigures() {
    if (flags & MapObjectFlag::FiguresLoaded) return;
    FigureManagerOf(this)->FindModel(name);
    if (GetClassId() != ClassId::Unit) {
        for (int i = 0; i < std::ssize(parts); ++i) parts[i]->flags |= FigureMeshFlag::Stationary;
        flags |= MapObjectFlag::Stationary;
    }
    attached->ApplyMaterials();
    flags |= MapObjectFlag::FiguresLoaded;
}

// --- Lifetime ------------------------------------------------------------------------------------

void Object3D::ClearBounds() {
    std::memset(&bounds, 0, sizeof(bounds));
    eyeHeight = 0;
}

Object3D::Object3D() {
    localRotation.SetIdentity();
    worldRotation.SetIdentity();
    ClearBounds();
    scale = {};
    defaultScale = kDefaultFigureScale;
    // The original leaves the view position as the heap held it, and the draw sorts read it for an object listed
    // only for its shadow before it was ever in view (a NaN there made the draw order irreflexive). It starts at
    // zero here.
    worldPosition = {};
}

Object3D::~Object3D() { Object3D::Destroy(); }

// --- Create / Destroy and the effect sources ------------------------------------------------------

namespace {
// Globals the game fills at startup: the identity quaternion and the default figure scale.
constexpr Quaternion kIdentityOrientation = /*007AF1C0*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser
} // namespace

void Object3D::Create() {
    localPosition = {};
    localRotation = kIdentityOrientation;
    SetHalf(nullptr);
    flags = 0;
    animations = nullptr;
    scale = {};
    figure.Reset();
    defaultScale = kDefaultFigureScale;
    ClearBounds();
    std::vector<FootprintPart>().swap(partBoxes);
}

void Object3D::Destroy() {
    figure.Reset();
    RemoveAllEffectSources();
    if (animation) animation->DetachMeshes();
    SetAnimationState(nullptr);
    parts.clear();   // each part's reference released (the client half's part groups hold their own)
    attached.Reset();   // the half keeps its back pointer (its own Destroy clears it)
    animations = nullptr;
    flags = 0;
}

void Object3D::AddEffectSource(ParticleSource* source) {
    if (std::ranges::find(effectSources, source, &Ref<ParticleSource>::Get) != effectSources.end()) return;
    effectSources.emplace_back(source);
}

void Object3D::RemoveEffectSource(ParticleSource* source) {
    auto node = std::ranges::find(effectSources, source, &Ref<ParticleSource>::Get);
    if (node == effectSources.end()) return;
    HeldRef<ParticleSource> held = std::move(*node);   // released after the detach
    effectSources.erase(node);
    if (source) source->Attach(nullptr);
}

// --- Virtuals shared by every map object: the world rotation, copying a figure, the side half, the effect
// sources and the drawing slots.

void Object3D::RemoveAllEffectSources() {
    while (!effectSources.empty()) RemoveEffectSource(effectSources.front());
}

int Object3D::DrawFigure(SceneRenderer* context) { return attached ? attached->DrawParts(context, nullptr) : 1; }

int Object3D::DrawCollectDirty(SceneRenderer* context) {
    dirtyRect = {};
    return attached ? attached->DrawParts(context, &dirtyRect) : 1;
}

int Object3D::DrawFigureShadow(SceneRenderer* context) { return attached ? attached->DrawPartsShadow(context) : 1; }

// --- Small virtuals ----------------------------------------------------------------------------

bool Object3D::AcceptsAnimation(const FigureAnimation*) { return true; }
void Object3D::AttachObject(EngineObject* object) {
    if (object && SideTypeOf(object) == SideObjectType::Object3D) SetHalf(static_cast<Object3DSpecific*>(object));
}
bool Object3D::InvisibleSlot() { return false; }
void Object3D::AdvanceTime(float) {}

// --- The attached half and animation state, and the setters that reach every part --------------------

namespace {
constexpr float kFirstFrame = 0.0f;
} // namespace

void Object3D::SetHalf(Object3DSpecific* half) {
    if (attached) attached->object = nullptr;
    attached.Assign(half);
    if (half) half->object = this;
}

void Object3D::SetPartFlags(std::uint32_t mask) {
    for (FigureMesh* part : parts) part->flags |= mask;
}

void Object3D::ClearPartFlags(std::uint32_t mask) {
    for (FigureMesh* part : parts) part->flags &= ~mask;
}

void Object3D::SetPartsFlatColour(std::uint32_t value) {
    for (FigureMesh* part : parts) part->flatColour = value;
}

void Object3D::SetPartsFlatSpecular(std::uint32_t value) {
    for (FigureMesh* part : parts) part->flatSpecular = value;
}

void Object3D::SetAnimationState(FigureAnimationState* state) {
    if (animation) animation->owner = nullptr;
    animation.Assign(state);
    if (state) state->owner = this;
}

void Object3D::PlayAnimation(FigureAnimation* figureAnimation) {
    if (animation) animation->Play(figureAnimation);
}

int Object3D::AttachPartMesh(FigureMesh* part) {
    if (!animation) return 0;
    return animation->AttachMesh(part);
}

void Object3D::RestartPose() {
    if (!animation) return;
    animation->SetTime(kFirstFrame);
    animation->PlaceIfChanged();
}

// --- Random animations -------------------------------------------------------------------------------

// Random animations of a 3D object's figure (Object3D::RandomAnimation and RandomAnimationOfKind; units'
// FindAnimation picks idle, attack and death animations with them). See objects/object3d.h.

namespace {

// A draw below `n` (> 0) from `random`'s generator.
int RandomBelow(AnimationRandom random, int n) {
    return random == AnimationRandom::Client ? ClientRandBelow(n) : libcrt::RandBelow(n);
}

} // namespace

FigureAnimation* Object3D::RandomAnimation(std::uint32_t key, AnimationRandom random) {
    AnimationDatabase* database = Animations();
    if (!database) return nullptr;
    std::vector<FigureAnimation*> candidates{};
    for (FigureAnimation& candidate : database->animations) {
        FigureAnimation* entry = &candidate;
        if (!AnimationKeyFits(entry->key, key)) continue;
        if (AcceptsAnimation(entry)) candidates.push_back(entry);
    }
    FigureAnimation** list = candidates.data();
    int count = lib::Count(candidates);
    if (count < 2) {
        FigureAnimation* only = count == 1 ? list[0] : nullptr;
        return only;
    }
    std::uint32_t percent = static_cast<std::uint32_t>(RandomBelow(random, 100));
    FigureAnimation* picked = list[RandomBelow(random, count)];
    for (int i = 0; i < count; ++i) {
        if (list[i]->chance < percent) {
            for (int k = i; k < count - 1; ++k) list[k] = list[k + 1];
            --count;
            --i;
        }
    }
    if (count > 0) picked = list[RandomBelow(random, count)];
    return picked;
}

FigureAnimation* Object3D::RandomAnimationOfKind(std::uint32_t key, AnimationRandom random) {
    AnimationDatabase* database = Animations();
    if (!database) return nullptr;
    std::vector<FigureAnimation*> candidates{};
    for (FigureAnimation& candidate : database->animations) {
        FigureAnimation* entry = &candidate;
        if (((entry->key ^ key) & kAnimationKindBits) != 0) continue;
        if (!AcceptsAnimation(entry)) continue;
        int index = lib::Count(candidates);
        candidates.resize(index + 1);
        candidates[index] = entry;
    }
    if (candidates.empty()) {
        return nullptr;
    }
    FigureAnimation* picked = candidates[RandomBelow(random, lib::Count(candidates))];
    return picked;
}

FigureMesh* Object3D::FindPartOfType(int partType) {
    for (int i = 0; i < std::ssize(parts); ++i)
        if (parts[i]->type == partType) return parts[i];
    return nullptr;
}

// --- The figure of a model with an animation state ---------------------------------------------------

// Object3D::SetUpAnimatedFigure: the figure of a model with an animation state (units,
// levers, map objects with animations; called by the zone loader, the world's object setup, Lever::SetState
// and the unit controller): the state takes the model, one box and one name per animated part, and the
// object's bounds around the boxes. See objects/object3d.h and docs/notes/map_objects.md ("Figures").

namespace {
constexpr float kHeroHalfWidth = 0.5f;
constexpr const char* kCantLoadPart = "CObject3D::LoadFigure.\n Can't load figure \"%s\" for object \"%s\".\n";
constexpr const char* kNoFigureManager = "CObject3D::LoadFigures.\n No valid figure manager.\n";
constexpr const char* kCantCreatePart = "CObject3D::LoadFigures.\n Can't create object \"%s\" with bodypart \"%s\".\n";

} // namespace

void Object3D::SetUpAnimatedFigure(std::string_view model) {
    FigureManager* manager = FigureManagerOf(this);
    Vector3 high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    Vector3 low = {FLT_MAX, FLT_MAX, FLT_MAX};
    if (animation) {
        animation->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
        animation->Play(nullptr);
        animation->UpdatePose();
        int count = lib::Count(animation->bones);
        partBoxes.resize(count);   // new boxes are all zero; the old ones are kept
        partNames.resize(animation->bones.size());

        FigureModel* figureModel = manager->FindModel(model);
        int index = 0;
        for (FigureBone* bone : animation->bones) {
            FootprintPart* box = &partBoxes[index];
            manager->LoadFigureBounds(&box->bounds, figureModel, model, bone->GetName(), scale.x, scale.y, scale.z);
            box->pivot = bone->position;
            box->bounds.flags |= BoundsFlag::Loaded;
            partNames[index] = bone->GetName();

            high = MaxKeepB(high, box->pivot + box->bounds.boxMax + box->bounds.center);
            low = MinKeepA(box->pivot + box->bounds.boxMin + box->bounds.center, low);

            const std::string_view partName = bone->GetName();
            if (text::CompareIgnoringCase(partName.substr(0, 4), "BASE") == 0) box->bounds.flags |= BoundsFlag::Base;
            if (text::CompareIgnoringCase(partName.substr(0, 5), "CROWN") == 0) box->bounds.flags |= BoundsFlag::Crown;
            if (text::CompareIgnoringCase(partName.substr(0, 5), "EMPTY") == 0) box->bounds.flags |= BoundsFlag::NoFootprint | BoundsFlag::Crown;
            ++index;
        }
        animation->bones.clear();
    }
    if (MapType() == MapObjectType::Hero) {
        high.y = kHeroHalfWidth;
        high.x = kHeroHalfWidth;
        low.y = -kHeroHalfWidth;
        low.x = -kHeroHalfWidth;
    }
    bounds.center = (low + high) * 0.5f;
    bounds.boxMin = low - bounds.center;
    bounds.boxMax = high - bounds.center;
    bounds.flags |= BoundsFlag::Box;
    bounds.radius = Length(bounds.boxMax);
    bounds.flags |= BoundsFlag::Sphere;
}

// --- Bounds of the placed parts -----------------------------------------------------------------

void Object3D::UpdatePartBounds() {
    if (!(flags & MapObjectFlag::FiguresLoaded)) return;
    float highX = -FLT_MAX, highY = -FLT_MAX, highZ = -FLT_MAX;
    float lowX = FLT_MAX, lowY = FLT_MAX, lowZ = FLT_MAX;
    for (int i = 0; i < std::ssize(parts); ++i) {
        // A part is a figure mesh (figure_mesh.h) on a TransformObject.
        parts[i]->UpdateWorld(localRotation, localPosition);
        parts[i]->ExtendWorldBox(&highX, &highY, &highZ, &lowX, &lowY, &lowZ);
    }
    Vector3 high = {highX, highY, highZ};
    Vector3 low = {lowX, lowY, lowZ};
    Vector3 centre = (high + low) * 0.5f;
    bounds.boxMax = high - centre;
    bounds.boxMin = low - centre;
    bounds.flags |= BoundsFlag::Box;
    bounds.flags |= BoundsFlag::Sphere;
    bounds.radius = Length(bounds.boxMin);
    bounds.center = centre - localPosition;
}

// --- Parts by name prefix -----------------------------------------------------------------------

std::vector<FigureMesh*> Object3D::CollectPartsByPrefix(std::string_view prefix) {
    std::vector<FigureMesh*> found;
    // The game asks each part for its name twice; parts return the same string both times.
    auto startsWith = [prefix](FigureMesh* part) { return std::string_view(part->GetName()).starts_with(prefix); };
    for (int i = 0; i < std::ssize(parts); ++i) {
        FigureMesh* part = parts[i];
        if (!startsWith(part)) continue;
        found.push_back(part);
    }
    return found;
}

// --- Loading single parts ---------------------------------------------------------------------

FigureMesh* Object3D::LoadPart(std::string_view partName) {
    FigureManager* manager = FigureManagerOf(this);
    FigureMesh* part = manager->NewPart(name, partName, flags & MapObjectFlag::Detached);
    if (!part) {
        game::DebugLog(kCantLoadPart, std::string(partName), name);
        return nullptr;
    }
    if (LinkMesh* current = figure) {
        // The figure gets the piece (it loads it unless it has it).
        if (!current->loader->LoadPartInto(current, std::string(partName))) {
            manager->ReleaseObject(part);
            return nullptr;
        }
        const LinkPartHeader& piece = current->headers.find(partName)->second;
        part->materialValue = piece.materialValue;
        part->meshType = piece.meshType;
        // The box and the pivot.
        part->box = piece.box;
    }
    if (text::CompareIgnoringCase(partName.substr(0, 4), "BASE") == 0) part->box.bounds.flags |= BoundsFlag::Base;
    if (text::CompareIgnoringCase(partName.substr(0, 5), "CROWN") == 0) part->box.bounds.flags |= BoundsFlag::Crown;
    if (text::CompareIgnoringCase(partName.substr(0, 5), "EMPTY") == 0) {
        part->box.bounds.flags |= BoundsFlag::NoFootprint;
        part->box.bounds.flags |= BoundsFlag::Crown;
    }
    // Joins the parts unless it is there (appended).
    bool present = false;
    for (int i = 0; i < std::ssize(parts) && !present; ++i) present = parts[i] == part;
    if (!present) {
        parts.push_back(part);
        part->SetObject(this);
        if (animation) animation->AttachMesh(part);
        if (attached) attached->OnPartAdded(part);
    }
    part->box.pivot = part->localPosition;
    part->box.bounds.flags |= BoundsFlag::Loaded;
    return part;
}

int Object3D::LoadParts(std::string_view model, const std::vector<std::string>& names) {
    FigureManager* manager = FigureManagerOf(this);
    name = model;
    if (!manager) game::FatalExit(kNoFigureManager);
    manager->FindModel(model);   // loads and caches the model
    for (const std::string& part : names) {
        if (!LoadPart(part)) game::DebugLog(kCantCreatePart, name, part);
    }
    return 1;
}

// --- Building the figure ----------------------------------------------------------------------

void Object3D::MakeFigure(std::string_view model, std::vector<std::string>* names) {
    name = model;
    if (animation) {
        animation->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
    }
    LinkFigureLoader* loader = GetManagerContext()->FigureLoader();
    if (names->empty() && animation) {
        *names = animation->BoneNames();
        figure.Assign(loader->LoadLink(model, {scale.x, scale.y, scale.z}));
    } else {
        figure.Assign(loader->LoadFigure(model, *names, {scale.x, scale.y, scale.z}));
    }
    LoadParts(model, *names);
    ClassId classId = GetClassId();
    if ((classId == ClassId::Building || GetClassId() == ClassId::Unit) && animation) {
        animation->Play(nullptr);
        animation->UpdatePose();
    }
    ComputeBounds();
    flags |= MapObjectFlag::FiguresLoaded;
}
