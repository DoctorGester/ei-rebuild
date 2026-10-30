// The 3D object base under MapObject: loading the figure's parts, rescaling them and their
// bounding box; its lifetime, Create / Destroy, effect sources, attached half and small virtuals. See
// map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "map_object_figures.h"

#include "ai_map.h"   // FootprintPart
#include "core_transform.h"
#include "effects.h"
#include "figure_manager.h"
#include "figure_mesh.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "platform/file.h"
#include "lib/rect.h"
#include "lib/nan_audit.h"
#include "link_figure.h"   // LinkHeaderMap: the figure's pieces
#include "map_object_shadow.h"   // WoundsMaterial: the client half's
#include "model.h"               // RenderPreset: the wounds material's
#include "world.h"   // World: the figure manager and cache

#include <bit>
#include <concepts>
#include <cstring>
#include <memory>

// The bounds must keep the original's order of operations and its NaN behaviour.

namespace {
constexpr std::uint32_t kBoundsValid = 1 | 2;

constexpr std::uint32_t kFloatMax = 0x7F7FFFFF;      // FLT_MAX / -FLT_MAX: the empty box
constexpr std::uint32_t kFloatLowest = 0xFF7FFFFF;
constexpr float kNearSquared = 1e9f;                 // parts farther away are skipped

std::uint32_t Bits(float v) { return std::bit_cast<std::uint32_t>(v); }
float Float(std::uint32_t bits) { return std::bit_cast<float>(bits); }
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float Float(T) = delete;

// Animation state: rescale to x, y, z.
void AnimationSetScale(void* state, float x, float y, float z) {
    static_cast<FigureAnimationState*>(state)->Rescale(x, y, z);
}

// The original's branch-free selects: the stored candidate `v` replaces the bound when the comparison sets C0
// (it is beyond the bound, or unordered).
std::uint32_t KeepLower(std::uint32_t bound, float v) { return v >= Float(bound) ? bound : Bits(v); }
std::uint32_t KeepHigher(std::uint32_t bound, float v) { return Float(bound) >= v ? bound : Bits(v); }

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

    float lowX = Float(box.low[0]), lowY = Float(box.low[1]), lowZ = Float(box.low[2]);
    float highX = Float(box.high[0]), highY = Float(box.high[1]), highZ = Float(box.high[2]);
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
