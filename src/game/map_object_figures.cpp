// The 3D object base under MapObject (Object3D): loading the figure's parts, rescaling them and their bounding
// box; its lifetime, Create / Destroy, effect sources, attached half and small virtuals; the random animations
// of its figure and SetUpAnimatedFigure. See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "map_object_figures.h"

#include "ai_map.h"   // FootprintPart
#include "core_maths.h"
#include "core_transform.h"
#include "effects.h"
#include "figure_manager.h"
#include "figure_mesh.h"
#include "link_figure.h"   // LinkHeaderMap: the figure's pieces
#include "map_object.h"
#include "map_object_shadow.h"   // WoundsMaterial: the client half's
#include "model.h"               // RenderPreset: the wounds material's
#include "random.h"
#include "world.h"   // World: the figure manager and cache
#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/rect.h"
#include "lib/text_case.h"
#include "platform/platform.h"

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

constexpr std::uint32_t kFloatMax = 0x7F7FFFFF;      // FLT_MAX / -FLT_MAX: the empty box
constexpr std::uint32_t kFloatLowest = 0xFF7FFFFF;
constexpr float kNearSquared = 1e9f;                 // parts farther away are skipped

std::uint32_t Bits(float v) { return std::bit_cast<std::uint32_t>(v); }

// Animation state: rescale to x, y, z.
void AnimationSetScale(void* state, float x, float y, float z) {
    static_cast<FigureAnimationState*>(state)->Rescale(x, y, z);
}

// The original's branch-free selects: the stored candidate `v` replaces the bound when the comparison sets C0
// (it is beyond the bound, or unordered).
std::uint32_t KeepLower(std::uint32_t bound, float v) { return v >= lib::FloatFromBits(bound) ? bound : Bits(v); }
std::uint32_t KeepHigher(std::uint32_t bound, float v) { return lib::FloatFromBits(bound) >= v ? bound : Bits(v); }

// a + b + c in that order, stored to a float.
float Sum3(float a, float b, float c) { return (a + b) + c; }

// The box being grown, as float bits (the original selects bits, never loading them twice).
struct BoxBits {
    std::uint32_t low[3] = {kFloatMax, kFloatMax, kFloatMax};
    std::uint32_t high[3] = {kFloatLowest, kFloatLowest, kFloatLowest};
};

// (x * x + y * y) + z * z of the part's position.
float DistanceSquared(const FigureMesh* part) { return LengthSquaredUnrounded<Sum3::XYZ>(part->localPosition); }

// A placed part: its piece's box moved by its position and the piece's offset.
void AddPlacedPart(BoxBits& box, const FigureMesh* p) {
    box.low[0] = KeepLower(box.low[0], Sum3(p->box.bounds.boxMin.x, p->localPosition.x, p->box.bounds.center.x));
    box.high[0] = KeepHigher(box.high[0], Sum3(p->box.bounds.boxMax.x, p->localPosition.x, p->box.bounds.center.x));
    box.low[1] = KeepLower(box.low[1], Sum3(p->box.bounds.boxMin.y, p->localPosition.y, p->box.bounds.center.y));
    box.high[1] = KeepHigher(box.high[1], Sum3(p->box.bounds.boxMax.y, p->localPosition.y, p->box.bounds.center.y));
    box.low[2] = KeepLower(box.low[2], Sum3(p->box.bounds.boxMin.z, p->localPosition.z, p->box.bounds.center.z));
    box.high[2] = KeepHigher(box.high[2], Sum3(p->box.bounds.boxMax.z, p->localPosition.z, p->box.bounds.center.z));
}

} // namespace

void Object3D::ComputeBounds() {
    BoxBits box;
    for (int i = 0; i < std::ssize(parts); ++i) {
        FigureMesh* part = parts[i];
        if (DistanceSquared(part) > kNearSquared) continue;   // NaN distances count as near
        AddPlacedPart(box, part);
    }
    if (parts.empty()) return;

    float lowX = lib::FloatFromBits(box.low[0]);
    float lowY = lib::FloatFromBits(box.low[1]);
    float lowZ = lib::FloatFromBits(box.low[2]);
    float highX = lib::FloatFromBits(box.high[0]);
    float highY = lib::FloatFromBits(box.high[1]);
    float highZ = lib::FloatFromBits(box.high[2]);
    float centerX = (lowX + highX) * 0.5f;
    float centerY = (lowY + highY) * 0.5f;
    float centerZ = (lowZ + highZ) * 0.5f;
    float aboveX = highX - centerX;
    float aboveY = highY - centerY;
    float aboveZ = highZ - centerZ;
    float belowX = lowX - centerX;
    float belowY = lowY - centerY;
    float belowZ = lowZ - centerZ;
    bounds.boxMin.x = belowX;
    bounds.boxMin.y = belowY;
    bounds.boxMin.z = belowZ;
    float length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(belowX, belowY, belowZ));
    bounds.boxMax.x = aboveX;
    bounds.boxMax.y = aboveY;
    bounds.boxMax.z = aboveZ;
    bounds.flags |= BoundsFlag::Box;
    bounds.flags |= BoundsFlag::Sphere;
    bounds.radius = length;
    bounds.center.x = centerX;
    bounds.center.y = centerY;
    bounds.center.z = centerZ;
}

void Object3D::SetFigureScale(const float* newScale) {
    EI_NAN_AUDIT(scale.x, scale.y, scale.z, newScale[0], newScale[1], newScale[2]);
    if (scale.x == newScale[0] && scale.y == newScale[1] && scale.z == newScale[2])
        return;
    lib::LoadBytes(scale, newScale);
    if (LinkMesh* current = figure) {
        LinkFigureLoader* loader = WorldContext()->FigureLoader();
        LinkMesh* rebuilt = loader->Remorph(current, scale.x, scale.y, scale.z);
        figure.Assign(rebuilt);
        for (int i = 0; i < std::ssize(parts); ++i) {
            FigureMesh* part = parts[i];
            const char* pieceName = part->GetName();
            std::map<std::string, LinkPartHeader>::const_iterator piece;
            {
                std::string key(pieceName);
                piece = figure->headers.find(key);
            }
            part = parts[i];
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
    {
        std::string key(name);
        FigureManagerOf(this)->FindModel(&key);
    }
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
    attached = nullptr;
    animation = nullptr;
    scale = {};
    defaultScale = kDefaultFigureScale;
    // The original leaves the view position as the heap held it, and the draw sorts read it for an object listed
    // only for its shadow before it was ever in view (a NaN there made the draw order irreflexive). It starts at
    // zero here, and in the original through a harness patch (harness_leftovers.cpp).
    worldPosition = {};
}

// The name is destroyed after the body, then EngineObject's part.
Object3D::~Object3D() {
    Destroy3D();
    figure.Reset();
    lib::FreeStorage(parts);
}

// --- Create / Destroy and the effect sources ------------------------------------------------------

namespace {
// Globals the game fills at startup: the identity quaternion and the default figure scale.
constexpr Quaternion kIdentityOrientation = /*007AF1C0*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser
} // namespace

void Object3D::Create3D() {
    localPosition = {};
    localRotation = kIdentityOrientation;
    attached = nullptr;
    flags = 0;
    animations = nullptr;
    scale = {};
    figure.Reset();
    defaultScale = kDefaultFigureScale;
    ClearBounds();
    std::vector<FootprintPart>().swap(partBoxes);
}

void Object3D::Destroy3D() {
    figure.Reset();
    RemoveAllEffectSources();
    if (animation) {
        animation->DetachMeshes();
        if (animation) {
            animation->owner = nullptr;
            animation->Release();
        }
        animation = nullptr;
    }
    for (HeldRef<FigureMesh>& part : parts)
        if (FigureMesh* forgotten = part.Take()) forgotten->ForgetReference();
    parts.clear();
    if (attached) attached->Release();
    attached = nullptr;
    animations = nullptr;
    flags = 0;
}

void Object3D::AddEffectSource(ParticleSource* source) {
    if (std::ranges::find(effectSources, source, &Ref<ParticleSource>::Get) != effectSources.end()) return;
    effectSources.emplace_back(source);
    if (source) source->Attach(this);
}

void Object3D::RemoveEffectSource(ParticleSource* source) {
    auto node = std::ranges::find(effectSources, source, &Ref<ParticleSource>::Get);
    if (node == effectSources.end()) return;
    HeldRef<ParticleSource> held = std::move(*node);   // released after the detach
    effectSources.erase(node);
    if (source) source->Attach(nullptr);
}

// --- Virtuals shared by every map object's vtable: the world rotation, copying a figure, the side half, the
// effect sources and the drawing slots.

// The same body as the base's.
void Object3D::CombineRotation(const Quaternion& parent) { TransformObject::CombineRotation(parent); }

// TransformObject's functions, called directly.
void Object3D::SaveTransform(platform::File* file) { TransformObject::SaveTransform(file); }

void Object3D::LoadTransform(platform::File* file) { TransformObject::LoadTransform(file); }

// The parts are copied uncounted, as the original copies the pointers: the old ones are dropped without a release
// and the copies take no reference.
void Object3D::CopyFigure(const Object3D* other) {
    for (HeldRef<FigureMesh>& part : parts) part.Take();
    parts = other->parts;
    for (FigureMesh* part : parts)
        if (part) part->ForgetReference();
    TransformObject::CopyTransform(*other);
}

void Object3D::AttachSpecific(EngineObject* half) {
    if (!half || SideTypeOf(half) != SideObjectType::Object3D) return;
    if (attached) {
        attached->object = nullptr;
        attached->Release();
    }
    attached = static_cast<Object3DSpecific*>(half);
    attached->object = this;
    attached->AddRef();
}

void Object3D::RemoveAllEffectSources() {
    while (!effectSources.empty()) RemoveEffectSource(effectSources.front());
}

int Object3D::DrawFigure(SceneRenderer* context) { return attached ? attached->DrawParts(context, nullptr) : 1; }

int Object3D::DrawCollectDirty(SceneRenderer* context) {
    rects::SetEmpty(&dirtyRect);
    return attached ? attached->DrawParts(context, &dirtyRect) : 1;
}

int Object3D::DrawFigureShadow(SceneRenderer* context) { return attached ? attached->DrawPartsShadow(context) : 1; }

// --- Small virtuals ----------------------------------------------------------------------------

bool Object3D::AcceptsAnimation(const FigureAnimation*) { return true; }
void Object3D::AttachObject(EngineObject* object) {
    AttachSpecific(object);
}
bool Object3D::InvisibleSlot() { return false; }
void Object3D::AdvanceTime(float) {}

// --- The attached half and animation state, and the setters that reach every part --------------------

namespace {
constexpr float kFirstFrame = 0.0f;

FigureMesh* PartAt(const Object3D* object, int index) { return object->parts[index]; }
} // namespace

void Object3D::SetSpecific(Object3DSpecific* half) {
    if (attached) {
        attached->object = nullptr;
        attached->Release();
    }
    attached = half;
    if (half) {
        half->object = this;
        attached->AddRef();
    }
}

void Object3D::SetPartFlags(std::uint32_t mask) {
    for (int i = 0; i < std::ssize(parts); ++i) PartAt(this, i)->flags |= mask;
}

void Object3D::ClearPartFlags(std::uint32_t mask) {
    for (int i = 0; i < std::ssize(parts); ++i) PartAt(this, i)->flags &= ~mask;
}

void Object3D::SetPartsFlatColour(std::uint32_t value) {
    for (int i = 0; i < std::ssize(parts); ++i) PartAt(this, i)->flatColour = value;
}

void Object3D::SetPartsFlatSpecular(std::uint32_t value) {
    for (int i = 0; i < std::ssize(parts); ++i) PartAt(this, i)->flatSpecular = value;
}

void Object3D::SetAnimationState(FigureAnimationState* state) {
    if (animation) {
        animation->owner = nullptr;
        animation->Release();
    }
    animation = state;
    if (state) {
        state->AddRef();
        animation->owner = this;
    }
}

void Object3D::PlayAnimation(FigureAnimation* figureAnimation) {
    if (animation) animation->Play(figureAnimation);
}

int Object3D::AttachPartMesh(EngineObject* part) {
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
// FindAnimation picks idle, attack and death animations with them). See map_object_figures.h.

namespace {
constexpr std::uint32_t kKeyKindBits = 0xFFFF8000;     // must match exactly

// A draw below `n` (> 0) from `random`'s generator.
int RandomBelow(AnimationRandom random, int n) {
    return random == AnimationRandom::Client ? ClientRandBelow(n) : libcrt::RandBelow(n);
}

} // namespace

FigureAnimation* Object3D::RandomAnimation(std::uint32_t key, AnimationRandom random) {
    AnimationDatabase* database = Animations();
    if (!database) return nullptr;
    std::vector<FigureAnimation*> candidates{};
    std::uint32_t wanted = key & kAnimationVariantBits;
    for (int i = 0; i < static_cast<int>(animations->count); ++i) {
        FigureAnimation* entry = &animations->animations[i];
        std::uint32_t entryKey = entry->key;
        bool variant =
            wanted == 0 ? (entryKey & kAnimationVariantBits) == 0 : (~entryKey & key & kAnimationVariantBits) == 0;
        if (!variant || ((entryKey ^ key) & kKeyKindBits) != 0) continue;
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
    for (int i = 0; i < static_cast<int>(animations->count); ++i) {
        FigureAnimation* entry = &animations->animations[i];
        if (((entry->key ^ key) & kKeyKindBits) != 0) continue;
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
// object's bounds around the boxes. See map_object_figures.h and docs/notes/map_objects.md ("Figures").

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

        FigureModel* figureModel;
        {
            std::string modelName(model);
            figureModel = manager->FindModel(&modelName);
        }
        int index = 0;
        for (auto node = animation->bones.begin(); node != animation->bones.end(); ++index) {
            FigureBone* bone = *node;
            ++node;
            FootprintPart* box = &partBoxes[index];
            manager->LoadFigureBounds(&box->bounds, figureModel, model, bone->GetName(),
                                            std::bit_cast<std::uint32_t>(scale.x),
                                            std::bit_cast<std::uint32_t>(scale.y),
                                            std::bit_cast<std::uint32_t>(scale.z));
            box = &partBoxes[index];
            box->pivot = {bone->position.x, bone->position.y, bone->position.z};
            box->bounds.flags |= BoundsFlag::Loaded;
            partNames[index] = bone->GetName();

            box = &partBoxes[index];
            high.x = MaxKeepB(high.x, static_cast<float>(box->pivot.x + box->bounds.boxMax.x + box->bounds.center.x));
            high.y = MaxKeepB(high.y, static_cast<float>(box->pivot.y + box->bounds.boxMax.y + box->bounds.center.y));
            high.z = MaxKeepB(high.z, static_cast<float>(box->pivot.z + box->bounds.boxMax.z + box->bounds.center.z));
            low.x = MinKeepA(static_cast<float>(box->pivot.x + box->bounds.boxMin.x + box->bounds.center.x), low.x);
            low.y = MinKeepA(static_cast<float>(box->pivot.y + box->bounds.boxMin.y + box->bounds.center.y), low.y);
            low.z = MinKeepA(static_cast<float>(box->pivot.z + box->bounds.boxMin.z + box->bounds.center.z), low.z);

            const std::string_view partName = bone->GetName();
            if (text::CompareIgnoringCase(partName.substr(0, 4), "BASE") == 0) box->bounds.flags |= BoundsFlag::Base;
            if (text::CompareIgnoringCase(partName.substr(0, 5), "CROWN") == 0) box->bounds.flags |= BoundsFlag::Crown;
            if (text::CompareIgnoringCase(partName.substr(0, 5), "EMPTY") == 0) box->bounds.flags |= BoundsFlag::NoFootprint | BoundsFlag::Crown;
        }
        animation->ReleaseBones();
    }
    if (MapType() == MapObjectType::Hero) {
        high.y = kHeroHalfWidth;
        high.x = kHeroHalfWidth;
        low.y = -kHeroHalfWidth;
        low.x = -kHeroHalfWidth;
    }
    bounds.center.x = static_cast<float>((low.x + high.x) * 0.5f);
    bounds.center.y = static_cast<float>((low.y + high.y) * 0.5f);
    bounds.center.z = static_cast<float>((low.z + high.z) * 0.5f);
    bounds.boxMin.x = low.x - bounds.center.x;
    bounds.boxMin.y = low.y - bounds.center.y;
    bounds.boxMin.z = low.z - bounds.center.z;
    bounds.boxMax.x = high.x - bounds.center.x;
    bounds.boxMax.y = high.y - bounds.center.y;
    bounds.boxMax.z = high.z - bounds.center.z;
    bounds.flags |= BoundsFlag::Box;
    bounds.radius = LengthXYZ(bounds.boxMax);
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
    // The x sum stays on the FPU stack; the y and z sums are stored first.
    float centreX = static_cast<float>((highX + lowX) * 0.5f);
    float centreY = static_cast<float>(static_cast<float>(highY + lowY) * 0.5f);
    float centreZ = static_cast<float>(static_cast<float>(highZ + lowZ) * 0.5f);
    bounds.boxMax.x = highX - centreX;
    bounds.boxMax.y = highY - centreY;
    bounds.boxMax.z = highZ - centreZ;
    bounds.boxMin.x = lowX - centreX;
    bounds.boxMin.y = lowY - centreY;
    bounds.boxMin.z = lowZ - centreZ;
    bounds.flags |= BoundsFlag::Box;
    bounds.flags |= BoundsFlag::Sphere;
    bounds.radius = LengthZYX(bounds.boxMin);
    bounds.center.x = centreX - localPosition.x;
    bounds.center.y = centreY - localPosition.y;
    bounds.center.z = centreZ - localPosition.z;
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
        bool loaded;
        {
            std::string key(partName);
            loaded = current->LoadPart(&key);
        }
        if (!loaded) return nullptr;   // the part is kept, unreleased (as in the game)
        const LinkPartHeader& piece = current->headers.find(std::string(partName))->second;
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
    part->box.pivot.x = part->localPosition.x;
    part->box.pivot.y = part->localPosition.y;
    part->box.bounds.flags |= BoundsFlag::Loaded;
    part->box.pivot.z = part->localPosition.z;
    return part;
}

int Object3D::LoadParts(std::string_view model, const std::vector<std::string>* names) {
    FigureManager* manager = FigureManagerOf(this);
    {
        std::string key(model);
        manager->FindModel(&key);   // loads and caches the model
    }
    name = model;
    if (!manager) return game::FatalError(kNoFigureManager);
    for (const std::string& part : *names) {
        if (!LoadPart(part)) game::DebugLog(kCantCreatePart, name, part);
    }
    return 1;
}

// --- Building the figure ----------------------------------------------------------------------

int Object3D::MakeFigure(std::string_view model, std::vector<std::string>* names, std::uint32_t arg) {
    name = model;
    if (animation) {
        animation->Rescale(scale.x, scale.y, scale.z);
        animation->SetModel(model);
    }
    if (names->empty() && animation) {
        *names = animation->BoneNames();
        std::string key(model);
        LinkMesh* made = WorldContext()->FigureLoader()->LoadLink(&key, scale.x, scale.y, scale.z);
        figure.Assign(made);
    } else {
        // The piece names as the loader's LoadFigure reads them.
        std::vector<std::string> pieces = *names;
        {
            std::string key(model);
            LinkMesh* made = WorldContext()->FigureLoader()->LoadFigure(&key, &pieces, scale.x, scale.y, scale.z);
            figure.Assign(made);
        }
    }
    LoadParts(model, names);
    ClassId classId = GetClassId();
    if ((classId == ClassId::Building || GetClassId() == ClassId::Unit) && animation) {
        animation->Play(nullptr);
        animation->UpdatePose();
    }
    ComputeBounds();
    if (arg == 0 && !figure) flags &= ~MapObjectFlag::FiguresLoaded;
    else flags |= MapObjectFlag::FiguresLoaded;
    return 1;
}
