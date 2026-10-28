// The 3D object base under CMapObject: loading the figure's parts, rescaling them and their
// bounding box. See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "figure_manager.h"
#include "figure_mesh.h"
#include "map_object_figures.h"
#include "link_figure.h"   // LinkHeaderMap: the figure's pieces
#include "world.h"   // CWorld: the figure manager and cache

#include "lib/crt.h"
#include "lib/stlp.h"

#include "ei/x87.h"
#include "library_trees.h"
#include "lib/nan_audit.h"

#include <concepts>
#include <bit>
#include <cstring>

// The bounds must keep the original's order of operations, its float stores and its NaN behaviour: values it
// keeps on the FPU stack are double locals, values it stores are floats.

namespace {
constexpr std::uint32_t kFlagFiguresLoaded = 0x80;   // flags_
constexpr std::uint32_t kFlagPartsMarked = 0x200;    // flags_ of a non-unit object
constexpr std::uint32_t kPartFlagStatic = 8;         // flags_ of its parts
constexpr std::uint32_t kBoundsValid = 1 | 2;

constexpr std::uint32_t kFloatMax = 0x7F7FFFFF;      // FLT_MAX / -FLT_MAX: the empty box
constexpr std::uint32_t kFloatLowest = 0xFF7FFFFF;
constexpr float kNearSquared = 1e9f;                 // parts farther away are skipped
constexpr float kHalf = 0.5f;

std::uint32_t Bits(float v) { return std::bit_cast<std::uint32_t>(v); }
float Float(std::uint32_t bits) { return std::bit_cast<float>(bits); }
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float Float(T) = delete;

// Figure manager: the model named `name`.
// Part: remembers its owner.
// Animation state: a part was added.
void AnimationAddPart(void* state, void*, CFigureMesh* part) { static_cast<CFigureAnimationState*>(state)->AttachMesh(part); }
// Animation state: rescale to x, y, z.
void AnimationSetScale(void* state, void*, float x, float y, float z) {
    static_cast<CFigureAnimationState*>(state)->Rescale(x, y, z);
}

// The original's branch-free selects: the stored candidate `v` replaces the bound when the comparison sets C0
// (it is beyond the bound, or unordered).
std::uint32_t KeepLower(std::uint32_t bound, float v) { return v >= Float(bound) ? bound : Bits(v); }
std::uint32_t KeepHigher(std::uint32_t bound, float v) { return Float(bound) >= v ? bound : Bits(v); }

// a + b + c in that order, stored to a float.
float Sum3(float a, float b, float c) { return static_cast<float>((static_cast<double>(a) + b) + c); }

} // namespace

namespace {
// The box being grown, as float bits (the original selects bits, never loading them twice).
struct BoxBits {
    std::uint32_t low[3] = {kFloatMax, kFloatMax, kFloatMax};
    std::uint32_t high[3] = {kFloatLowest, kFloatLowest, kFloatLowest};
};

// (x * x + y * y) + z * z of the part's position, unrounded.
double DistanceSquared(const CFigureMesh* part) { return LengthSquaredUnrounded<Sum3::XYZ, double>(part->localPosition_); }

// A placed part: its piece's box moved by its position and the piece's offset.
void AddPlacedPart(BoxBits& box, const CFigureMesh* p) {
    box.low[0] = KeepLower(box.low[0], Sum3(p->box_.bounds.boxMin.x, p->localPosition_.x, p->box_.bounds.center.x));
    box.high[0] = KeepHigher(box.high[0], Sum3(p->box_.bounds.boxMax.x, p->localPosition_.x, p->box_.bounds.center.x));
    box.low[1] = KeepLower(box.low[1], Sum3(p->box_.bounds.boxMin.y, p->localPosition_.y, p->box_.bounds.center.y));
    box.high[1] = KeepHigher(box.high[1], Sum3(p->box_.bounds.boxMax.y, p->localPosition_.y, p->box_.bounds.center.y));
    box.low[2] = KeepLower(box.low[2], Sum3(p->box_.bounds.boxMin.z, p->localPosition_.z, p->box_.bounds.center.z));
    box.high[2] = KeepHigher(box.high[2], Sum3(p->box_.bounds.boxMax.z, p->localPosition_.z, p->box_.bounds.center.z));
}

// A pending part (position just reset to 0; the sums keep the original's operand order).
void AddPendingPart(BoxBits& box, const CFigureMesh* p) {
    box.low[0] = KeepLower(box.low[0], static_cast<float>(static_cast<double>(p->box_.bounds.boxMin.x) + p->box_.bounds.center.x));
    box.high[0] = KeepHigher(box.high[0], Sum3(p->box_.bounds.boxMax.x, p->box_.bounds.center.x, p->localPosition_.x));
    box.low[1] = KeepLower(box.low[1], Sum3(p->box_.bounds.boxMin.y, p->box_.bounds.center.y, p->localPosition_.y));
    box.high[1] = KeepHigher(box.high[1], Sum3(p->box_.bounds.boxMax.y, p->box_.bounds.center.y, p->localPosition_.y));
    box.low[2] = KeepLower(box.low[2], Sum3(p->box_.bounds.boxMin.z, p->localPosition_.z, p->box_.bounds.center.z));
    box.high[2] = KeepHigher(box.high[2], Sum3(p->box_.bounds.boxMax.z, p->localPosition_.z, p->box_.bounds.center.z));
}
} // namespace

void CObject3D::ComputeBounds() {
    BoxBits box;
    for (int i = 0; i < parts.size; ++i) {
        auto* part = parts.data[i];
        if (DistanceSquared(part) > kNearSquared) continue;   // NaN distances count as near
        AddPlacedPart(box, part);
    }
    for (int i = 0; i < pendingParts.size; ++i) {
        auto* part = pendingParts.data[i];
        std::memset(&part->localPosition_, 0, sizeof(part->localPosition_));
        AddPendingPart(box, part);
    }
    if (parts.size == 0 && pendingParts.size == 0) return;

    float lowX = Float(box.low[0]), lowY = Float(box.low[1]), lowZ = Float(box.low[2]);
    float highX = Float(box.high[0]), highY = Float(box.high[1]), highZ = Float(box.high[2]);
    double sumX = static_cast<double>(lowX) + highX;                  // kept on the stack
    float sumY = static_cast<float>(static_cast<double>(lowY) + highY);
    float sumZ = static_cast<float>(static_cast<double>(lowZ) + highZ);
    float centerX = static_cast<float>(sumX * kHalf);
    float centerY = static_cast<float>(static_cast<double>(sumY) * kHalf);
    float centerZ = static_cast<float>(static_cast<double>(sumZ) * kHalf);
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
    bounds.flags |= 2;
    bounds.flags |= 1;
    bounds.radius = static_cast<float>(length);
    bounds.center.x = centerX;
    bounds.center.y = centerY;
    bounds.center.z = centerZ;
}

void CObject3D::SetFigureScale(const float* newScale) {
    EI_NAN_AUDIT(scale.x, scale.y, scale.z, newScale[0], newScale[1], newScale[2]);
    if (scale.x == newScale[0] && scale.y == newScale[1] && scale.z == newScale[2])
        return;
    std::memcpy(&scale, newScale, sizeof(scale));
    if (CLinkMesh* current = figure) {
        CLinkFigureLoader* loader = WorldContext()->FigureLoader();
        CLinkMesh* rebuilt = loader->Remorph(current, scale.x, scale.y, scale.z);
        figure.Assign(rebuilt);
        for (int i = 0; i < parts.size; ++i) {
            auto* part = parts.data[i];
            const char* pieceName = part->GetName();
            const LinkHeaderMap::Node* piece;
            {
                std::string key(pieceName);
                piece = lib_trees::FindLinkHeader(&figure->headers_, &key);
            }
            part = parts.data[i];
            const ClipBounds& fitted = piece->second.box.bounds;
            std::memcpy(&part->box_.bounds.center, &fitted.center, sizeof(part->box_.bounds.center));
            std::memcpy(&part->box_.bounds.boxMin, &fitted.boxMin, sizeof(part->box_.bounds.boxMin));
            std::memcpy(&part->box_.bounds.boxMax, &fitted.boxMax, sizeof(part->box_.bounds.boxMax));
            std::memcpy(&part->box_.bounds.radius, &fitted.radius, sizeof(float));
        }
    }
    if (animation) AnimationSetScale(animation, nullptr, scale.x, scale.y, scale.z);
    ComputeBounds();
}

int CObject3D::AddPart(CFigureMesh* part) {
    for (int k = 0; k < parts.size; ++k)
        if (parts.data[k] == part) return 1;
    parts.SetAtGrow(parts.size, part);
    ++part->refCount_;
    part->SetObject(this);
    if (animation) AnimationAddPart(animation, nullptr, part);
    if (!attached) return 1;
    return Specific()->OnPartAdded(part);
}

void CObject3D::LoadFigures() {
    if (flags_ & kFlagFiguresLoaded) return;
    CFigureManager* manager = FigureManagerOf(this);
    FigureModel* model;
    {
        std::string key(name.c_str());
        model = manager->FindModel(&key);
    }
    for (int i = 0; i < pendingParts.size; ++i) {
        manager->LoadPart(pendingParts.data[i], model, scale.x, scale.y, scale.z);
        --pendingParts.data[i]->refCount_;   // no release at 0
        auto* part = pendingParts.data[i];
        bool known = false;
        for (int k = 0; k < parts.size && !known; ++k) known = parts.data[k] == part;
        if (known) continue;
        parts.SetAtGrow(parts.size, part);
        ++part->refCount_;
        part->SetObject(this);
        if (animation) AnimationAddPart(animation, nullptr, part);
        if (attached) Specific()->OnPartAdded(part);
    }
    pendingParts.SetSize(0, -1);
    if (GetClassId() != kClassUnit) {
        for (int i = 0; i < parts.size; ++i) parts.data[i]->flags_ |= kPartFlagStatic;
        flags_ |= kFlagPartsMarked;
    }
    Specific()->ApplyMaterials();
    flags_ |= kFlagFiguresLoaded;
}
