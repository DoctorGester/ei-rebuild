// The 3D object base under CMapObject: loading the figure's parts, rescaling them and their
// bounding box. See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "map_object_figures.h"

#include "lib/crt.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_units.h"
#include "originals_library.h"

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

// Manager slots: the figure manager (ManagerContext slot 15) and the figure cache (slot 23),
// whose slot 6 builds a figure at a scale.
constexpr int kContextFigureManager = 0x3C / 4;
constexpr int kContextFigureCache = 0x5C / 4;
constexpr int kCacheScaledFigure = 0x18 / 4;
// The attached object (+0x94): a part was added (13), the figure is complete (21).
constexpr int kAttachedPartAdded = 0x34 / 4;
constexpr int kAttachedFiguresLoaded = 0x54 / 4;
constexpr int kSlotGetName = 7;

std::uint32_t Bits(float v) { return std::bit_cast<std::uint32_t>(v); }
float Float(std::uint32_t bits) { return std::bit_cast<float>(bits); }

// Not rewritten, forwarders:
// 005AA5F0. Figure manager: the model named `name`.
__declspec(naked) void* __fastcall FindModel(void*, void*, const stlp::string*) EI_ORIGINAL(0x005AA5F0)
// 005AC2F0. Figure manager: builds `part` from `model` at the scale x, y, z given as float bits.
__declspec(naked) void __fastcall BuildPart(void*, void*, FigurePart*, void*, std::uint32_t, std::uint32_t,
                                            std::uint32_t) EI_ORIGINAL(0x005AC2F0)
// 0059F3D0. Part: remembers its owner.
void __fastcall SetPartOwner(FigurePart* a0, void*, CObject3D* a2) { FigurePartSetOwnerOriginal((void*)a0, nullptr, (void*)a2); }
// 0050D0E0. Animation state: a part was added.
void AnimationAddPart(void* state, void*, FigurePart* part) { AnimationStateAddPartOriginal(state, nullptr, part); }
// 0050D7C0. Animation state: rescale to x, y, z given as float bits.
void AnimationSetScale(void* state, void*, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    AnimationStateSetScaleOriginal(state, nullptr, x, y, z);
}
// 005197B0. map<string, piece>::find on the figure's piece map at figure + 0x14: writes the node,
// or the map's header when absent, to *out.
void __fastcall FindPiece(void* a0, void*, const FigurePieceRecord** a2, const stlp::string* a3) { StringMapFindOriginal(a0, nullptr, (void**)a2, (const void*)a3); }
// Rewritten here, called through their game addresses so their verify wrappers stay in the path.
__declspec(naked) std::uint32_t __fastcall ComputeBoundsAt(CObject3D*) EI_ORIGINAL(0x00518310)
__declspec(naked) void __fastcall SetAtGrowAt(AlignedPtrArray*, void*, int, void*) EI_ORIGINAL(0x00519E80)
__declspec(naked) void __fastcall SetSizeAt(AlignedPtrArray*, void*, int, int) EI_ORIGINAL(0x00519D70)

// 00519580: *slot = value with counted references; an old value that drops to 0 is deleted
// (its slot 0 with 1).
void AssignCounted(CEngineObject** slot, CEngineObject* value) {
    CEngineObject* old = *slot;
    *slot = value;
    if (value) ++value->refCount_;
    if (old && --old->refCount_ == 0) ei::VCall<void>(old, 0, 1);
}

// The original's branch-free selects: the stored candidate `v` replaces the bound when the
// comparison sets C0 (it is beyond the bound, or unordered).
std::uint32_t KeepLower(std::uint32_t bound, float v) { return !(v >= Float(bound)) ? Bits(v) : bound; }
std::uint32_t KeepHigher(std::uint32_t bound, float v) { return !(Float(bound) >= v) ? Bits(v) : bound; }

// a + b + c in that order, stored to a float.
float Sum3(float a, float b, float c) { return static_cast<float>((static_cast<double>(a) + b) + c); }

// Ordered and different (FCOMP; TEST AH,0x40 clear).
bool Differs(float a, float b) { return a < b || a > b; }

// 32-byte aligned storage for `count` pointers; the block itself is kept just before it.
void** AllocateAligned(int count) {
    auto* block = static_cast<char*>(gamecrt::operator_new(static_cast<std::size_t>(count) * 4 + 0x24));
    auto* data = reinterpret_cast<void**>((reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F});
    data[-1] = block;
    return data;
}
void FreeAligned(void** data) { gamecrt::operator_delete(data[-1]); }

// The grow-by when none is set: an eighth of the size, 4..1024.
int DefaultGrowBy(int size) {
    int grow = size / 8;
    if (grow < 4) return 4;
    if (grow > 0x400) return 0x400;
    return grow;
}
} // namespace

// --- AlignedPtrArray --------------------------------------------------------------------------

void AlignedPtrArray::SetAtGrow(int index, void* value) {
    if (size <= index) {
        int newSize = index + 1;
        if (newSize == 0) {
            if (data) {
                FreeAligned(data);
                data = nullptr;
            }
            capacity = 0;
            size = 0;
            // The original then stores through the null array (data[-1]).
            *(*static_cast<void** volatile*>(&data) + index) = value;
            return;
        }
        if (!data) {
            data = AllocateAligned(newSize);
            capacity = newSize;
            size = newSize;
            data[index] = value;
            return;
        }
        if (newSize <= capacity) {
            size = newSize;
            data[index] = value;
            return;
        }
        int grow = growBy != 0 ? growBy : DefaultGrowBy(size);
        int newCapacity = grow + capacity;
        if (newCapacity <= newSize) newCapacity = newSize;
        void** fresh = AllocateAligned(newCapacity);
        std::memcpy(fresh, data, static_cast<std::size_t>(size & 0x3FFFFFFF) * 4);
        FreeAligned(data);
        data = fresh;
        size = index + 1;
        capacity = newCapacity;
    }
    data[index] = value;
}
EI_REPLACE_METHOD(0x00519E80, AlignedPtrArray::SetAtGrow);

void AlignedPtrArray::SetSize(int newSize, int newGrowBy) {
    if (newGrowBy != -1) growBy = newGrowBy;
    if (newSize == 0) {
        if (data) {
            FreeAligned(data);
            data = nullptr;
        }
        capacity = 0;
        size = 0;
        return;
    }
    if (!data) {
        data = AllocateAligned(newSize);
        capacity = newSize;
        size = newSize;
        return;
    }
    if (newSize <= capacity) {
        size = newSize;
        return;
    }
    int grow = growBy != 0 ? growBy : DefaultGrowBy(size);
    int newCapacity = grow + capacity;
    if (newCapacity <= newSize) newCapacity = newSize;
    void** fresh = AllocateAligned(newCapacity);
    std::memcpy(fresh, data, static_cast<std::size_t>(size & 0x3FFFFFFF) * 4);
    FreeAligned(data);
    data = fresh;
    size = newSize;
    capacity = newCapacity;
}
EI_REPLACE_METHOD(0x00519D70, AlignedPtrArray::SetSize);

// --- Bounds -----------------------------------------------------------------------------------

namespace {
// The box being grown, as float bits (the original selects bits, never loading them twice).
struct BoxBits {
    std::uint32_t low[3] = {kFloatMax, kFloatMax, kFloatMax};
    std::uint32_t high[3] = {kFloatLowest, kFloatLowest, kFloatLowest};
};

// (x * x + y * y) + z * z of the part's position, unrounded.
double DistanceSquared(const FigurePart* part) {
    double x = part->position[0];
    double y = part->position[1];
    double z = part->position[2];
    return (x * x + y * y) + z * z;
}

// A placed part: its piece's box moved by its position and the piece's offset.
void AddPlacedPart(BoxBits& box, const FigurePart* p) {
    box.low[0] = KeepLower(box.low[0], Sum3(p->boxMin[0], p->position[0], p->offset[0]));
    box.high[0] = KeepHigher(box.high[0], Sum3(p->boxMax[0], p->position[0], p->offset[0]));
    box.low[1] = KeepLower(box.low[1], Sum3(p->boxMin[1], p->position[1], p->offset[1]));
    box.high[1] = KeepHigher(box.high[1], Sum3(p->boxMax[1], p->position[1], p->offset[1]));
    box.low[2] = KeepLower(box.low[2], Sum3(p->boxMin[2], p->position[2], p->offset[2]));
    box.high[2] = KeepHigher(box.high[2], Sum3(p->boxMax[2], p->position[2], p->offset[2]));
}

// A pending part (position just reset to 0; the sums keep the original's operand order).
void AddPendingPart(BoxBits& box, const FigurePart* p) {
    box.low[0] = KeepLower(box.low[0], static_cast<float>(static_cast<double>(p->boxMin[0]) + p->offset[0]));
    box.high[0] = KeepHigher(box.high[0], Sum3(p->boxMax[0], p->offset[0], p->position[0]));
    box.low[1] = KeepLower(box.low[1], Sum3(p->boxMin[1], p->offset[1], p->position[1]));
    box.high[1] = KeepHigher(box.high[1], Sum3(p->boxMax[1], p->offset[1], p->position[1]));
    box.low[2] = KeepLower(box.low[2], Sum3(p->boxMin[2], p->position[2], p->offset[2]));
    box.high[2] = KeepHigher(box.high[2], Sum3(p->boxMax[2], p->position[2], p->offset[2]));
}
} // namespace

std::uint32_t CObject3D::ComputeBounds() {
    BoxBits box;
    for (int i = 0; i < parts.size; ++i) {
        auto* part = static_cast<FigurePart*>(parts.data[i]);
        if (DistanceSquared(part) > kNearSquared) continue;   // NaN distances count as near
        AddPlacedPart(box, part);
    }
    for (int i = 0; i < pendingParts.size; ++i) {
        auto* part = static_cast<FigurePart*>(pendingParts.data[i]);
        std::memset(part->position, 0, sizeof(part->position));
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
    below[0] = belowXf;
    below[1] = belowY;
    below[2] = belowZ;
    double yz = static_cast<double>(belowZ) * belowZ + static_cast<double>(belowY) * belowY;
    double length = x87::fsqrt(yz + belowX * belowX);
    above[0] = aboveX;
    above[1] = aboveY;
    above[2] = aboveZ;
    boundsFlags |= 2;
    boundsFlags |= 1;
    radius = static_cast<float>(length);
    center[0] = centerX;
    center[1] = centerY;
    center[2] = centerZ;
    return Bits(centerZ);   // EAX: the last value copied
}
EI_REPLACE_METHOD(0x00518310, CObject3D::ComputeBounds);

// --- Scale ------------------------------------------------------------------------------------

std::uintptr_t CObject3D::SetFigureScale(const float* newScale) {
    if (!Differs(scale[0], newScale[0]) && !Differs(scale[1], newScale[1]) && !Differs(scale[2], newScale[2]))
        return 0;   // the original leaves the FPU status word in AX here
    std::memcpy(scale, newScale, sizeof(scale));
    if (CEngineObject* current = figure) {
        void* cache = ei::VCall<void*>(ManagerContext(), kContextFigureCache);
        auto* rebuilt = ei::VCall<CEngineObject*>(cache, kCacheScaledFigure, current, Bits(scale[0]), Bits(scale[1]),
                                                  Bits(scale[2]));
        AssignCounted(&figure, rebuilt);
        for (int i = 0; i < parts.size; ++i) {
            auto* part = static_cast<FigurePart*>(parts.data[i]);
            const char* pieceName = ei::VCall<const char*>(part, kSlotGetName);
            const FigurePieceRecord* piece;
            {
                stlp::string key(pieceName);
                FindPiece(reinterpret_cast<char*>(figure) + 0x14, nullptr, &piece, &key);
            }
            part = static_cast<FigurePart*>(parts.data[i]);
            std::memcpy(part->offset, piece->offset, sizeof(part->offset));
            std::memcpy(part->boxMin, piece->boxMin, sizeof(part->boxMin));
            std::memcpy(part->boxMax, piece->boxMax, sizeof(part->boxMax));
            std::memcpy(&part->unknown_198, &piece->unknown_34, sizeof(float));
        }
    }
    if (animation) AnimationSetScale(animation, nullptr, Bits(scale[0]), Bits(scale[1]), Bits(scale[2]));
    return ComputeBoundsAt(this);
}
EI_REPLACE_METHOD(0x00517700, CObject3D::SetFigureScale);

// --- Parts ------------------------------------------------------------------------------------

int CObject3D::AddPart(FigurePart* part) {
    for (int k = 0; k < parts.size; ++k)
        if (parts.data[k] == part) return 1;
    parts.SetAtGrow(parts.size, part);   // inlined in the original (allocating through 0051A170)
    ++part->refCount_;
    SetPartOwner(part, nullptr, this);
    if (animation) AnimationAddPart(animation, nullptr, part);
    if (!attached) return 1;
    return ei::VCall<int>(attached, kAttachedPartAdded, part);
}
EI_REPLACE_METHOD(0x005164B0, CObject3D::AddPart);

// --- Loading ----------------------------------------------------------------------------------

int CObject3D::LoadFigures() {
    if (flags_ & kFlagFiguresLoaded) return 1;
    void* manager = ei::VCall<void*>(ManagerContext(), kContextFigureManager);
    void* model;
    {
        stlp::string key(name);
        model = FindModel(manager, nullptr, &key);
    }
    for (int i = 0; i < pendingParts.size; ++i) {
        BuildPart(manager, nullptr, static_cast<FigurePart*>(pendingParts.data[i]), model, Bits(scale[0]),
                  Bits(scale[1]), Bits(scale[2]));
        --static_cast<FigurePart*>(pendingParts.data[i])->refCount_;   // no release at 0
        auto* part = static_cast<FigurePart*>(pendingParts.data[i]);
        bool known = false;
        for (int k = 0; k < parts.size && !known; ++k) known = parts.data[k] == part;
        if (known) continue;
        SetAtGrowAt(&parts, nullptr, parts.size, part);
        ++part->refCount_;
        SetPartOwner(part, nullptr, this);
        if (animation) AnimationAddPart(animation, nullptr, part);
        if (attached) ei::VCall<void>(attached, kAttachedPartAdded, part);
    }
    SetSizeAt(&pendingParts, nullptr, 0, -1);
    if (GetClassId() != kClassUnit) {
        for (int i = 0; i < parts.size; ++i) static_cast<FigurePart*>(parts.data[i])->flags_ |= kPartFlagStatic;
        flags_ |= kFlagPartsMarked;
    }
    ei::VCall<void>(attached, kAttachedFiguresLoaded);
    flags_ |= kFlagFiguresLoaded;
    return 1;
}
EI_REPLACE_METHOD(0x00517500, CObject3D::LoadFigures);
