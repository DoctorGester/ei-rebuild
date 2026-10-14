// The 3D object base under CMapObject: loading the figure's parts, rescaling them and their
// bounding box. See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "map_object_figures.h"
#include "link_figure.h"   // LinkHeaderMap: the figure's pieces
#include "world.h"   // CWorld: the figure manager and cache

#include "lib/crt.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_units.h"
#include "originals_library.h"

#include <concepts>
#include <bit>
#include <cstring>

// The bounds must keep the original's order of operations, its float stores and its NaN
// behaviour: values it keeps on the FPU stack are double locals, values it stores are floats.
#pragma float_control(precise, on)

namespace {
constexpr std::uint32_t kFlagFiguresLoaded = 0x80;   // flags_
constexpr std::uint32_t kFlagPartsMarked = 0x200;    // flags_ of a non-unit object
constexpr std::uint32_t kPartFlagStatic = 8;         // flags_ of its parts
constexpr int kClassUnit = 0x50;
constexpr std::uint32_t kBoundsValid = 1 | 2;

constexpr std::uint32_t kFloatMax = 0x7F7FFFFF;      // FLT_MAX / -FLT_MAX: the empty box
constexpr std::uint32_t kFloatLowest = 0xFF7FFFFF;
constexpr float kNearSquared = 1e9f;                 // 0073DE58: parts farther away are skipped
constexpr float kHalf = 0.5f;                        // 0073B86C

std::uint32_t Bits(float v) { return std::bit_cast<std::uint32_t>(v); }
float Float(std::uint32_t bits) { return std::bit_cast<float>(bits); }
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float Float(T) = delete;

// Not rewritten, forwarders:
// 005AA5F0. Figure manager: the model named `name`.
// 005AC2F0. Figure manager: builds `part` from `model` at the scale x, y, z given as float bits.
__declspec(naked) void __fastcall BuildPart(void*, void*, CFigureMesh*, void*, std::uint32_t, std::uint32_t,
                                            std::uint32_t) EI_ORIGINAL(0x005AC2F0)
// 0059F3D0. Part: remembers its owner.
void __fastcall SetPartOwner(CFigureMesh* a0, void*, CObject3D* a2) { FigurePartSetOwnerOriginal((void*)a0, nullptr, (void*)a2); }
// 0050D0E0. Animation state: a part was added.
void AnimationAddPart(void* state, void*, CFigureMesh* part) { AnimationStateAddPartOriginal(state, nullptr, part); }
// 0050D7C0. Animation state: rescale to x, y, z given as float bits.
void AnimationSetScale(void* state, void*, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    AnimationStateSetScaleOriginal(state, nullptr, x, y, z);
}
// 005197B0. map<string, piece>::find on the figure's piece map at figure + 0x14: writes the node,
// or the map's header when absent, to *out.
void __fastcall FindPiece(void* a0, void*, const LinkHeaderMap::Node** a2, const stlp::string* a3) { StringMapFindOriginal(a0, nullptr, (void**)a2, (const void*)a3); }
// Rewritten here, called through their game addresses so their verify wrappers stay in the path.
__declspec(naked) std::uint32_t __fastcall ComputeBoundsAt(CObject3D*) EI_ORIGINAL(0x00518310)
__declspec(naked) void __fastcall SetAtGrowAt(AlignedPtrArray*, void*, int, void*) EI_ORIGINAL(0x00519E80)
__declspec(naked) void __fastcall SetSizeAt(AlignedPtrArray*, void*, int, int) EI_ORIGINAL(0x00519D70)

// The original's branch-free selects: the stored candidate `v` replaces the bound when the
// comparison sets C0 (it is beyond the bound, or unordered).
std::uint32_t KeepLower(std::uint32_t bound, float v) { return !(v >= Float(bound)) ? Bits(v) : bound; }
std::uint32_t KeepHigher(std::uint32_t bound, float v) { return !(Float(bound) >= v) ? Bits(v) : bound; }

// a + b + c in that order, stored to a float.
float Sum3(float a, float b, float c) { return static_cast<float>((static_cast<double>(a) + b) + c); }

// Ordered and different (FCOMP; TEST AH,0x40 clear).
bool Differs(float a, float b) { return a < b || a > b; }

} // namespace

// --- AlignedPtrArray --------------------------------------------------------------------------

// 00519E80 / 00519D70: SetAtGrow / SetSize (RET 8). The original's SetAtGrow(-1) stores through
// the null array it has just freed; ours does too (data[-1] of null), both fault.
EI_REPLACE_METHOD(0x00519E80, AlignedPtrArray::SetAtGrow);

EI_REPLACE_METHOD(0x00519D70, AlignedPtrArray::SetSize);

// --- Bounds -----------------------------------------------------------------------------------

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
    box.low[0] = KeepLower(box.low[0], Sum3(p->bounds_.boxMin.x, p->localPosition_.x, p->bounds_.center.x));
    box.high[0] = KeepHigher(box.high[0], Sum3(p->bounds_.boxMax.x, p->localPosition_.x, p->bounds_.center.x));
    box.low[1] = KeepLower(box.low[1], Sum3(p->bounds_.boxMin.y, p->localPosition_.y, p->bounds_.center.y));
    box.high[1] = KeepHigher(box.high[1], Sum3(p->bounds_.boxMax.y, p->localPosition_.y, p->bounds_.center.y));
    box.low[2] = KeepLower(box.low[2], Sum3(p->bounds_.boxMin.z, p->localPosition_.z, p->bounds_.center.z));
    box.high[2] = KeepHigher(box.high[2], Sum3(p->bounds_.boxMax.z, p->localPosition_.z, p->bounds_.center.z));
}

// A pending part (position just reset to 0; the sums keep the original's operand order).
void AddPendingPart(BoxBits& box, const CFigureMesh* p) {
    box.low[0] = KeepLower(box.low[0], static_cast<float>(static_cast<double>(p->bounds_.boxMin.x) + p->bounds_.center.x));
    box.high[0] = KeepHigher(box.high[0], Sum3(p->bounds_.boxMax.x, p->bounds_.center.x, p->localPosition_.x));
    box.low[1] = KeepLower(box.low[1], Sum3(p->bounds_.boxMin.y, p->bounds_.center.y, p->localPosition_.y));
    box.high[1] = KeepHigher(box.high[1], Sum3(p->bounds_.boxMax.y, p->bounds_.center.y, p->localPosition_.y));
    box.low[2] = KeepLower(box.low[2], Sum3(p->bounds_.boxMin.z, p->localPosition_.z, p->bounds_.center.z));
    box.high[2] = KeepHigher(box.high[2], Sum3(p->bounds_.boxMax.z, p->localPosition_.z, p->bounds_.center.z));
}
} // namespace

std::uint32_t CObject3D::ComputeBounds() {
    BoxBits box;
    for (int i = 0; i < parts.size; ++i) {
        auto* part = static_cast<CFigureMesh*>(parts.data[i]);
        if (DistanceSquared(part) > kNearSquared) continue;   // NaN distances count as near
        AddPlacedPart(box, part);
    }
    for (int i = 0; i < pendingParts.size; ++i) {
        auto* part = static_cast<CFigureMesh*>(pendingParts.data[i]);
        std::memset(&part->localPosition_, 0, sizeof(part->localPosition_));
        AddPendingPart(box, part);
    }
    if (parts.size == 0 && pendingParts.size == 0) return 0;   // EAX: the pending count

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
    below.x = belowXf;
    below.y = belowY;
    below.z = belowZ;
    double length = x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(belowX, belowY, belowZ));
    above.x = aboveX;
    above.y = aboveY;
    above.z = aboveZ;
    boundsFlags |= 2;
    boundsFlags |= 1;
    radius = static_cast<float>(length);
    center.x = centerX;
    center.y = centerY;
    center.z = centerZ;
    return Bits(centerZ);   // EAX: the last value copied
}
EI_REPLACE_METHOD(0x00518310, CObject3D::ComputeBounds);

// --- Scale ------------------------------------------------------------------------------------

std::uintptr_t CObject3D::SetFigureScale(const float* newScale) {
    if (!Differs(scale.x, newScale[0]) && !Differs(scale.y, newScale[1]) && !Differs(scale.z, newScale[2]))
        return 0;   // the original leaves the FPU status word in AX here
    std::memcpy(&scale, newScale, sizeof(scale));
    if (CLinkMesh* current = figure) {
        CFigureCache* cache = static_cast<CWorld*>(ManagerContext())->FigureCache();
        CLinkMesh* rebuilt = cache->ScaledFigure(current, Bits(scale.x), Bits(scale.y), Bits(scale.z));
        FigureRef().Assign(rebuilt);   // 00519580
        for (int i = 0; i < parts.size; ++i) {
            auto* part = static_cast<CFigureMesh*>(parts.data[i]);
            const char* pieceName = part->GetName();
            const LinkHeaderMap::Node* piece;
            {
                stlp::string key(pieceName);
                FindPiece(&figure->headers_, nullptr, &piece, &key);
            }
            part = static_cast<CFigureMesh*>(parts.data[i]);
            const ClipBounds& bounds = piece->second.bounds;
            std::memcpy(&part->bounds_.center, &bounds.center, sizeof(part->bounds_.center));
            std::memcpy(&part->bounds_.boxMin, &bounds.boxMin, sizeof(part->bounds_.boxMin));
            std::memcpy(&part->bounds_.boxMax, &bounds.boxMax, sizeof(part->bounds_.boxMax));
            std::memcpy(&part->bounds_.radius, &bounds.radius, sizeof(float));
        }
    }
    if (animation) AnimationSetScale(animation, nullptr, Bits(scale.x), Bits(scale.y), Bits(scale.z));
    return ComputeBoundsAt(this);
}
EI_REPLACE_METHOD(0x00517700, CObject3D::SetFigureScale);

// --- Parts ------------------------------------------------------------------------------------

int CObject3D::AddPart(CFigureMesh* part) {
    for (int k = 0; k < parts.size; ++k)
        if (parts.data[k] == part) return 1;
    parts.SetAtGrow(parts.size, part);   // inlined in the original (allocating through 0051A170)
    ++part->refCount_;
    SetPartOwner(part, nullptr, this);
    if (animation) AnimationAddPart(animation, nullptr, part);
    if (!attached) return 1;
    return Specific()->OnPartAdded(part);
}
EI_REPLACE_METHOD(0x005164B0, CObject3D::AddPart);

// --- Loading ----------------------------------------------------------------------------------

int CObject3D::LoadFigures() {
    if (flags_ & kFlagFiguresLoaded) return 1;
    void* manager = static_cast<CWorld*>(ManagerContext())->FigureManager();
    void* model;
    {
        stlp::string key(name);
        model = FigureManagerFindModelOriginal(manager, nullptr, &key);
    }
    for (int i = 0; i < pendingParts.size; ++i) {
        BuildPart(manager, nullptr, static_cast<CFigureMesh*>(pendingParts.data[i]), model, Bits(scale.x),
                  Bits(scale.y), Bits(scale.z));
        --static_cast<CFigureMesh*>(pendingParts.data[i])->refCount_;   // no release at 0
        auto* part = static_cast<CFigureMesh*>(pendingParts.data[i]);
        bool known = false;
        for (int k = 0; k < parts.size && !known; ++k) known = parts.data[k] == part;
        if (known) continue;
        SetAtGrowAt(&parts, nullptr, parts.size, part);
        ++part->refCount_;
        SetPartOwner(part, nullptr, this);
        if (animation) AnimationAddPart(animation, nullptr, part);
        if (attached) Specific()->OnPartAdded(part);
    }
    SetSizeAt(&pendingParts, nullptr, 0, -1);
    if (GetClassId() != kClassUnit) {
        for (int i = 0; i < parts.size; ++i) static_cast<CFigureMesh*>(parts.data[i])->flags_ |= kPartFlagStatic;
        flags_ |= kFlagPartsMarked;
    }
    Specific()->ApplyMaterials();
    flags_ |= kFlagFiguresLoaded;
    return 1;
}
EI_REPLACE_METHOD(0x00517500, CObject3D::LoadFigures);
