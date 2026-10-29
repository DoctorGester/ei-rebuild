// The 3D object base under CMapObject: loading the figure's parts, rescaling them and their
// bounding box; its lifetime, Create / Destroy, effect sources, attached half and small virtuals. See
// map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "map_object_figures.h"

#include "ai_map.h"   // FootprintPart
#include "core_transform.h"
#include "effects.h"
#include "ei/x87.h"
#include "figure_manager.h"
#include "figure_mesh.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/file_io.h"
#include "lib/gameapi.h"
#include "lib/nan_audit.h"
#include "link_figure.h"   // LinkHeaderMap: the figure's pieces
#include "map_object_shadow.h"   // WoundsMaterial: the client half's
#include "model.h"               // RenderPreset: the wounds material's
#include "world.h"   // CWorld: the figure manager and cache

#include <bit>
#include <concepts>
#include <cstring>
#include <memory>

// The bounds must keep the original's order of operations, its float stores and its NaN behaviour: values it
// keeps on the FPU stack are double locals, values it stores are floats.

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
float Sum3(float a, float b, float c) { return static_cast<float>((static_cast<double>(a) + b) + c); }

// The box being grown, as float bits (the original selects bits, never loading them twice).
struct BoxBits {
    std::uint32_t low[3] = {kFloatMax, kFloatMax, kFloatMax};
    std::uint32_t high[3] = {kFloatLowest, kFloatLowest, kFloatLowest};
};

// (x * x + y * y) + z * z of the part's position, unrounded.
double DistanceSquared(const FigureMesh* part) { return LengthSquaredUnrounded<Sum3::XYZ, double>(part->localPosition); }

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

void CObject3D::ComputeBounds() {
    BoxBits box;
    for (int i = 0; i < std::ssize(parts); ++i) {
        FigureMesh* part = parts[i];
        if (DistanceSquared(part) > kNearSquared) continue;   // NaN distances count as near
        AddPlacedPart(box, part);
    }
    if (parts.empty()) return;

    float lowX = Float(box.low[0]), lowY = Float(box.low[1]), lowZ = Float(box.low[2]);
    float highX = Float(box.high[0]), highY = Float(box.high[1]), highZ = Float(box.high[2]);
    double sumX = static_cast<double>(lowX) + highX;                  // kept on the stack
    float sumY = static_cast<float>(static_cast<double>(lowY) + highY);
    float sumZ = static_cast<float>(static_cast<double>(lowZ) + highZ);
    float centerX = static_cast<float>(sumX * 0.5f);
    float centerY = static_cast<float>(static_cast<double>(sumY) * 0.5f);
    float centerZ = static_cast<float>(static_cast<double>(sumZ) * 0.5f);
    float aboveX = static_cast<float>(static_cast<double>(highX) - centerX);
    float aboveY = static_cast<float>(static_cast<double>(highY) - centerY);
    float aboveZ = static_cast<float>(static_cast<double>(highZ) - centerZ);
    double belowX = static_cast<double>(lowX) - centerX;             // kept on the stack (radius)
    float belowXf = static_cast<float>(belowX);
    float belowY = static_cast<float>(static_cast<double>(lowY) - centerY);
    float belowZ = static_cast<float>(static_cast<double>(lowZ) - centerZ);
    bounds.boxMin.x = belowXf;
    bounds.boxMin.y = belowY;
    bounds.boxMin.z = belowZ;
    double length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(belowX, belowY, belowZ));
    bounds.boxMax.x = aboveX;
    bounds.boxMax.y = aboveY;
    bounds.boxMax.z = aboveZ;
    bounds.flags |= BoundsFlag::Box;
    bounds.flags |= BoundsFlag::Sphere;
    bounds.radius = static_cast<float>(length);
    bounds.center.x = centerX;
    bounds.center.y = centerY;
    bounds.center.z = centerZ;
}

void CObject3D::SetFigureScale(const float* newScale) {
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

void CObject3D::LoadFigures() {
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

void CObject3D::ClearBounds() {
    std::memset(&bounds, 0, sizeof(bounds));
    eyeHeight = 0;
}

CObject3D::CObject3D() {
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
CObject3D::~CObject3D() {
    Destroy3D();
    figure.Reset();
    lib::FreeStorage(parts);
}

// --- Create / Destroy and the effect sources ------------------------------------------------------

namespace {
// Globals the game fills at startup: the identity quaternion and the default figure scale.
constexpr Quaternion kIdentityOrientation = /*007AF1C0*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser
} // namespace

void CObject3D::Create3D() {
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

void CObject3D::Destroy3D() {
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

void CObject3D::AddEffectSource(ParticleSource* source) {
    if (std::ranges::find(effectSources, source, &Ref<ParticleSource>::Get) != effectSources.end()) return;
    effectSources.emplace_back(source);
    if (source) source->Attach(this);
}

void CObject3D::RemoveEffectSource(ParticleSource* source) {
    auto node = std::ranges::find(effectSources, source, &Ref<ParticleSource>::Get);
    if (node == effectSources.end()) return;
    HeldRef<ParticleSource> held = std::move(*node);   // released after the detach
    effectSources.erase(node);
    if (source) source->Attach(nullptr);
}

// --- Virtuals shared by every map object's vtable: the world rotation, copying a figure, the side half, the
// effect sources and the drawing slots.

// The same body as the base's.
void CObject3D::CombineRotation(const Quaternion& parent) { TransformObject::CombineRotation(parent); }

// TransformObject's functions, called directly.
void CObject3D::SaveTransform(lib::File* file) { TransformObject::SaveTransform(file); }

void CObject3D::LoadTransform(lib::File* file) { TransformObject::LoadTransform(file); }

// The parts are copied uncounted, as the original copies the pointers: the old ones are dropped without a release
// and the copies take no reference.
void CObject3D::CopyFigure(const CObject3D* other) {
    for (HeldRef<FigureMesh>& part : parts) part.Take();
    parts = other->parts;
    for (FigureMesh* part : parts)
        if (part) part->ForgetReference();
    TransformObject::CopyTransform(*other);
}

void CObject3D::AttachSpecific(EngineObject* half) {
    if (!half || SideTypeOf(half) != SideObjectType::Object3D) return;
    if (attached) {
        attached->object = nullptr;
        attached->Release();
    }
    attached = static_cast<Object3DSpecific*>(half);
    attached->object = this;
    attached->AddRef();
}

void CObject3D::RemoveAllEffectSources() {
    while (!effectSources.empty()) RemoveEffectSource(effectSources.front());
}

int CObject3D::DrawFigure(SceneRenderer* context) { return attached ? attached->DrawParts(context, nullptr) : 1; }

int CObject3D::DrawCollectDirty(SceneRenderer* context) {
    gameapi::SetRectEmpty(&dirtyRect);
    return attached ? attached->DrawParts(context, &dirtyRect) : 1;
}

int CObject3D::DrawFigureShadow(SceneRenderer* context) { return attached ? attached->DrawPartsShadow(context) : 1; }

// --- Small virtuals ----------------------------------------------------------------------------

bool CObject3D::AcceptsAnimation(const FigureAnimation*) { return true; }
void CObject3D::AttachObject(EngineObject* object) {
    AttachSpecific(object);
}
bool CObject3D::InvisibleSlot() { return false; }
void CObject3D::AdvanceTime(float) {}

// --- The attached half and animation state, and the setters that reach every part --------------------

namespace {
constexpr float kFirstFrame = 0.0f;

FigureMesh* PartAt(const CObject3D* object, int index) { return object->parts[index]; }
} // namespace

void CObject3D::SetSpecific(Object3DSpecific* half) {
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

void CObject3D::SetPartFlags(std::uint32_t mask) {
    for (int i = 0; i < std::ssize(parts); ++i) PartAt(this, i)->flags |= mask;
}

void CObject3D::ClearPartFlags(std::uint32_t mask) {
    for (int i = 0; i < std::ssize(parts); ++i) PartAt(this, i)->flags &= ~mask;
}

void CObject3D::SetPartsFlatColour(std::uint32_t value) {
    for (int i = 0; i < std::ssize(parts); ++i) PartAt(this, i)->flatColour = value;
}

void CObject3D::SetPartsFlatSpecular(std::uint32_t value) {
    for (int i = 0; i < std::ssize(parts); ++i) PartAt(this, i)->flatSpecular = value;
}

void CObject3D::SetAnimationState(FigureAnimationState* state) {
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

void CObject3D::PlayAnimation(FigureAnimation* figureAnimation) {
    if (animation) animation->Play(figureAnimation);
}

int CObject3D::AttachPartMesh(EngineObject* part) {
    if (!animation) return 0;
    return animation->AttachMesh(part);
}

void CObject3D::RestartPose() {
    if (!animation) return;
    animation->SetTime(kFirstFrame);
    animation->PlaceIfChanged();
}
