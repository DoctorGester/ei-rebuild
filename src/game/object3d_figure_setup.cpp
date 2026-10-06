// CObject3D::SetUpAnimatedFigure (005179E0): the figure of a model with an animation state (units,
// levers, map objects with animations; called by the zone loader, the world's object setup,
// Lever::SetState and the unit controller): the state takes the model, one box and one name per
// animated part, and the object's bounds around the boxes. See map_object_figures.h and
// docs/notes/map_objects.md ("Figures").
#include "map_object_figures.h"

#include "ai_map.h"
#include "core_maths.h"
#include "core_transform.h"
#include "figure_mesh.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_units.h"

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>

namespace {
constexpr int kTypeHero = 0x32;                  // type_ of the player's units: a fixed x / y box
constexpr float kHeroHalfWidth = 0.5f;
constexpr float kHalf = 0.5f;                    // 0073B86C
constexpr std::uint32_t kBoundsSphere = 1;       // boundsFlags: radius valid
constexpr std::uint32_t kBoundsBox = 2;          // boundsFlags: box valid
constexpr std::uint32_t kFlagFigureLoadedBits = 0x80;   // flags_: the figure is loaded

// FootprintPart::flags set here.
constexpr std::uint32_t kBoxLoaded = 4;
constexpr std::uint32_t kBoxBase = 0x400;        // a part named "BASE..."
constexpr std::uint32_t kBoxCrown = 0x90;        // "CROWN..."
constexpr std::uint32_t kBoxEmpty = 0x890;       // "EMPTY..."

// The part box array's grow step when none is set: an eighth of the size, 4..1024 (MFC CArray).
int DefaultGrowBy(int size) {
    int grow = size / 8;
    if (grow < 4) return 4;
    if (grow > 0x400) return 0x400;
    return grow;
}

// 00519FC0 (inlined here: it only zeroes): new part boxes are all zero.
void ClearBoxes(FootprintPart* boxes, int count) {
    std::memset(boxes, 0, static_cast<std::size_t>(count) * sizeof(FootprintPart));
}

// The original's branch-free selects: the stored candidate replaces the bound when the comparison
// sets C0 (it lies beyond the bound, or one of them is NaN).
float Higher(float bound, float v) { return !(bound >= v) ? v : bound; }
float Lower(float bound, float v) { return !(v >= bound) ? v : bound; }
} // namespace

// MFC CArray<FootprintPart>::SetSize(count, -1), inlined by the original.
static void SetBoxCount(CObject3D* object, int count) {
    if (count == 0) {
        if (object->partBoxes) {
            gamecrt::operator_delete(object->partBoxes);
            object->partBoxes = nullptr;
        }
        object->partBoxCapacity = 0;
        object->partBoxCount = 0;
    } else if (!object->partBoxes) {
        object->partBoxes = static_cast<FootprintPart*>(gamecrt::operator_new(count * sizeof(FootprintPart)));
        ClearBoxes(object->partBoxes, count);
        object->partBoxCapacity = count;
        object->partBoxCount = count;
    } else if (count <= object->partBoxCapacity) {
        if (count > object->partBoxCount)
            ClearBoxes(object->partBoxes + object->partBoxCount, count - object->partBoxCount);
        object->partBoxCount = count;
    } else {
        int grow = object->partBoxGrowBy ? object->partBoxGrowBy : DefaultGrowBy(object->partBoxCount);
        int capacity = object->partBoxCapacity + grow;
        if (count >= capacity) capacity = count;
        auto* boxes = static_cast<FootprintPart*>(gamecrt::operator_new(capacity * sizeof(FootprintPart)));
        std::memcpy(boxes, object->partBoxes, object->partBoxCount * sizeof(FootprintPart));
        ClearBoxes(boxes + object->partBoxCount, count - object->partBoxCount);
        gamecrt::operator_delete(object->partBoxes);
        object->partBoxCount = count;
        object->partBoxes = boxes;
        object->partBoxCapacity = capacity;
    }
}

void CObject3D::SetUpAnimatedFigure(const char* model) {
    CEngineObject* manager = static_cast<CWorld*>(ManagerContext())->FigureManager();
    Vector3 high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    Vector3 low = {FLT_MAX, FLT_MAX, FLT_MAX};
    if (animation) {
        AnimationStateSetScaleOriginal(animation, nullptr, std::bit_cast<std::uint32_t>(scale[0]),
                                       std::bit_cast<std::uint32_t>(scale[1]), std::bit_cast<std::uint32_t>(scale[2]));
        AnimationStateSetModelOriginal(animation, nullptr, model);
        animation->Play(nullptr);
        FigureApplyPendingOriginal(animation);
        int count = animation->parts.count;
        SetBoxCount(this, count);
        StringArraySetSize(&partNameArrayVtable, nullptr, animation->parts.count, -1);

        void* figureModel;
        {
            stlp::string modelName(model, model + std::strlen(model));
            figureModel = FigureManagerFindModelOriginal(manager, nullptr, &modelName);
        }
        int index = 0;
        for (auto* node = animation->parts.head; node; ++index) {
            FigurePart* part = node->data;
            node = node->next;
            FootprintPart* box = &partBoxes[index];
            FigureManagerPartBoundsOriginal(manager, nullptr, box, figureModel, model, part->GetName(),
                                            std::bit_cast<std::uint32_t>(scale[0]),
                                            std::bit_cast<std::uint32_t>(scale[1]),
                                            std::bit_cast<std::uint32_t>(scale[2]));
            box = &partBoxes[index];
            box->pivot = {part->pivot[0], part->pivot[1], part->pivot[2]};
            box->flags |= kBoxLoaded;
            reinterpret_cast<mfc::CString*>(partNames)[index] = part->GetName();

            box = &partBoxes[index];
            high.x = Higher(high.x, StoreFloat(box->pivot.x + box->boxMax.x + box->offset.x));
            high.y = Higher(high.y, StoreFloat(box->pivot.y + box->boxMax.y + box->offset.y));
            high.z = Higher(high.z, StoreFloat(box->pivot.z + box->boxMax.z + box->offset.z));
            low.x = Lower(low.x, StoreFloat(box->pivot.x + box->boxMin.x + box->offset.x));
            low.y = Lower(low.y, StoreFloat(box->pivot.y + box->boxMin.y + box->offset.y));
            low.z = Lower(low.z, StoreFloat(box->pivot.z + box->boxMin.z + box->offset.z));

            const char* partName = part->GetName();
            if (gamecrt::strnicmp(partName, "BASE", 4) == 0) box->flags |= kBoxBase;
            if (gamecrt::strnicmp(partName, "CROWN", 5) == 0) box->flags |= kBoxCrown;
            if (gamecrt::strnicmp(partName, "EMPTY", 5) == 0) box->flags |= kBoxEmpty;
        }
        AnimationStateReleasePartsOriginal(animation);
    }
    if (type_ == kTypeHero) {
        high.y = kHeroHalfWidth;
        high.x = kHeroHalfWidth;
        low.y = -kHeroHalfWidth;
        low.x = -kHeroHalfWidth;
    }
    center[0] = StoreFloat((low.x + high.x) * kHalf);
    center[1] = StoreFloat((low.y + high.y) * kHalf);
    center[2] = StoreFloat((low.z + high.z) * kHalf);
    below[0] = low.x - center[0];
    below[1] = low.y - center[1];
    below[2] = low.z - center[2];
    above[0] = high.x - center[0];
    above[1] = high.y - center[1];
    above[2] = high.z - center[2];
    boundsFlags |= kBoundsBox;
    radius = std::sqrt(above[0] * above[0] + above[1] * above[1] + above[2] * above[2]);
    boundsFlags |= kBoundsSphere;
}

EI_REPLACE_METHOD(0x005179E0, CObject3D::SetUpAnimatedFigure);

// --- Bounds of the placed parts -----------------------------------------------------------------

void CObject3D::UpdatePartBounds() {
    if (!(flags_ & kFlagFigureLoadedBits)) return;
    float highX = -FLT_MAX, highY = -FLT_MAX, highZ = -FLT_MAX;
    float lowX = FLT_MAX, lowY = FLT_MAX, lowZ = FLT_MAX;
    for (int i = 0; i < parts.size; ++i) {
        // A part is a figure mesh (figure_mesh.h) on a CTransformObject.
        static_cast<CTransformObject*>(parts.data[i])
            ->UpdateWorld(orientation, *reinterpret_cast<const Vector3*>(position));
        static_cast<const CFigureMesh*>(parts.data[i])->ExtendWorldBox(&highX, &highY, &highZ, &lowX, &lowY, &lowZ);
    }
    // The x sum stays on the FPU stack; the y and z sums are stored first.
    float centreX = StoreFloat((highX + lowX) * kHalf);
    float centreY = StoreFloat(StoreFloat(highY + lowY) * kHalf);
    float centreZ = StoreFloat(StoreFloat(highZ + lowZ) * kHalf);
    above[0] = highX - centreX;
    above[1] = highY - centreY;
    above[2] = highZ - centreZ;
    below[0] = lowX - centreX;
    below[1] = lowY - centreY;
    below[2] = lowZ - centreZ;
    boundsFlags |= kBoundsBox;
    boundsFlags |= kBoundsSphere;
    radius = std::sqrt((below[2] * below[2] + below[1] * below[1]) + below[0] * below[0]);
    center[0] = centreX - position[0];
    center[1] = centreY - position[1];
    center[2] = centreZ - position[2];
}
EI_REPLACE_METHOD(0x00518100, CObject3D::UpdatePartBounds);

// --- Parts by name prefix -----------------------------------------------------------------------

void CObject3D::CollectPartsByPrefix(const char* prefix, AlignedPtrArray* out) {
    // The original asks each part for its name twice; parts return the same string both times.
    auto startsWith = [prefix](FigurePart* part) { return std::strstr(part->GetName(), prefix) == part->GetName(); };
    for (int i = 0; i < parts.size; ++i) {
        auto* part = static_cast<FigurePart*>(parts.data[i]);
        if (!startsWith(part)) continue;
        int index = out->size;
        out->SetSize(index + 1, -1);   // 00519D70
        out->data[index] = part;
    }
    for (int i = 0; i < pendingParts.size; ++i) {
        auto* part = static_cast<FigurePart*>(pendingParts.data[i]);
        if (startsWith(part)) out->SetAtGrow(out->size, part);   // inlined by the original
    }
}
EI_REPLACE_METHOD(0x00516B50, CObject3D::CollectPartsByPrefix);

// --- Loading single parts ---------------------------------------------------------------------

namespace {
constexpr std::uint32_t kFlagDetached = 0x1000;   // flags_: passed to the figure manager
constexpr std::uint32_t kBoxEmptyMark = 0x800;    // 005174E0(1) on the box
constexpr const char* kCantLoadPart = "CObject3D::LoadFigure.\n Can't load figure \"%s\" for object \"%s\".\n";
constexpr const char* kNoFigureManager = "CObject3D::LoadFigures.\n No valid figure manager.\n";
constexpr const char* kCantCreatePart = "CObject3D::LoadFigures.\n Can't create object \"%s\" with bodypart \"%s\".\n";

// An MFC CStringArray as 00517110 reads it.
using PartNameArray = mfc::CStringArray;

// 00519A10 (AlignedPtrArray::Add, below), through its address so its wrapper runs.
__declspec(naked) int __fastcall AddAt(AlignedPtrArray*, void*, void*) EI_ORIGINAL(0x00519A10)

CEngineObject* FigureManagerOf(const CObject3D* object) {
    return static_cast<CWorld*>(object->ManagerContext())->FigureManager();
}
} // namespace

int AlignedPtrArray::Add(void* value) {
    int index = size;
    SetAtGrow(index, value);
    return index;
}
EI_REPLACE_METHOD(0x00519A10, AlignedPtrArray::Add);

FigurePart* CObject3D::LoadPart(const char* partName, void* model, std::uint32_t arg) {
    CEngineObject* manager = FigureManagerOf(this);
    auto* part = static_cast<FigurePart*>(FigureManagerLoadPartOriginal(
        manager, nullptr, name, partName, model, std::bit_cast<std::uint32_t>(scale[0]),
        std::bit_cast<std::uint32_t>(scale[1]), std::bit_cast<std::uint32_t>(scale[2]), arg, flags_ & kFlagDetached));
    if (!part) {
        DebugMessageOriginal(kCantLoadPart, partName, name);
        return nullptr;
    }
    if (figure) {
        auto* instance = static_cast<CFigureInstance*>(static_cast<void*>(figure));
        bool known;
        {
            stlp::string key(partName, partName + std::strlen(partName));
            known = instance->HasPiece(&key);
        }
        if (!known) return nullptr;   // the part is kept, unreleased (as the original)
        void* node;
        {
            stlp::string key(partName, partName + std::strlen(partName));
            StringMapFindOriginal(instance->pieces, nullptr, &node, &key);
        }
        const auto* record = static_cast<const FigurePieceRecord*>(node);
        part->piece158 = record->unknown_20;
        part->piece15C = record->unknown_1C;
        std::memcpy(&part->boxFlags, &record->boxFlags, sizeof(FootprintPart));
    }
    if (gamecrt::strnicmp(partName, "BASE", 4) == 0) part->boxFlags |= kBoxBase;
    if (gamecrt::strnicmp(partName, "CROWN", 5) == 0) part->boxFlags |= kBoxCrown;
    if (gamecrt::strnicmp(partName, "EMPTY", 5) == 0) {
        part->boxFlags |= kBoxEmptyMark;
        part->boxFlags |= kBoxCrown;
    }
    // AddPart, inlined by the original (appending with 00519A10).
    bool present = false;
    for (int i = 0; i < parts.size && !present; ++i) present = parts.data[i] == part;
    if (!present) {
        AddAt(&parts, nullptr, part);
        ++part->refCount_;
        FigurePartSetOwnerOriginal(part, nullptr, this);
        if (animation) AnimationStateAddPartOriginal(animation, nullptr, part);
        if (attached) Specific()->OnPartAdded(part);
    }
    part->boxPivot[0] = part->position[0];
    part->boxPivot[1] = part->position[1];
    part->boxFlags |= kBoxLoaded;
    part->boxPivot[2] = part->position[2];
    return part;
}
EI_REPLACE_METHOD(0x00517240, CObject3D::LoadPart);

int CObject3D::LoadParts(const char* model, const void* partList, std::uint32_t arg) {
    CEngineObject* manager = FigureManagerOf(this);
    void* cached;
    {
        stlp::string key(model, model + std::strlen(model));
        cached = FigureManagerFindModelOriginal(manager, nullptr, &key);
    }
    *reinterpret_cast<mfc::CString*>(&name) = model;
    if (!manager) return FatalErrorOriginal(kNoFigureManager);
    const auto* names = static_cast<const PartNameArray*>(partList);
    for (int i = 0; i < names->size; ++i) {
        if (!Object3DLoadPartOriginal(this, nullptr, static_cast<const char*>((*names)[i]), cached, arg)) DebugMessageOriginal(kCantCreatePart, model, static_cast<const char*>((*names)[i]));
    }
    return 1;
}
EI_REPLACE_METHOD(0x00517110, CObject3D::LoadParts);

// --- Building the figure ----------------------------------------------------------------------

namespace {
constexpr int kClassUnit50 = 0x50;
constexpr int kClassUnit54 = 0x54;
constexpr std::uint32_t kFlagFigureLoaded = 0x80;   // flags_

// An STLport vector<string> of the piece names, as the cache's slot 3 reads it (the original
// builds it with 005195B0 / 004DDBA0 and assigns each name).
struct PieceNames {
    stlp::string* begin;
    stlp::string* end;
    stlp::string* endOfStorage;
};

PieceNames MakePieceNames(const PartNameArray* names) {
    PieceNames out{};
    int count = names->size;
    if (count > 0) {
        out.begin = static_cast<stlp::string*>(stlp::allocate(static_cast<std::size_t>(count) * sizeof(stlp::string)));
        for (int i = 0; i < count; ++i) {
            const char* text = static_cast<const char*>((*names)[i]);
            new (&out.begin[i]) stlp::string(text, text + std::strlen(text));
        }
        out.end = out.begin + count;
        out.endOfStorage = out.end;
    }
    return out;
}

void FreePieceNames(PieceNames& names) {
    for (stlp::string* s = names.begin; s != names.end; ++s) s->~string();
    if (names.begin)
        stlp::deallocate(names.begin, static_cast<std::size_t>(names.endOfStorage - names.begin) * sizeof(stlp::string));
}

// 00517110 (LoadParts, above), through its address so its wrapper runs.
__declspec(naked) int __fastcall LoadPartsAt(CObject3D*, void*, const char*, const void*, std::uint32_t)
    EI_ORIGINAL(0x00517110)

CFigureCache* FigureCacheOf(const CObject3D* object) {
    return static_cast<CWorld*>(object->ManagerContext())->FigureCache();
}
} // namespace

int CObject3D::MakeFigure(const char* model, void* partList, std::uint32_t arg) {
    *reinterpret_cast<mfc::CString*>(&name) = model;
    if (animation) {
        AnimationStateSetScaleOriginal(animation, nullptr, std::bit_cast<std::uint32_t>(scale[0]),
                                       std::bit_cast<std::uint32_t>(scale[1]), std::bit_cast<std::uint32_t>(scale[2]));
        AnimationStateSetModelOriginal(animation, nullptr, model);
    }
    auto* names = static_cast<const PartNameArray*>(partList);
    if (names->size == 0 && animation) {
        AnimationStatePartNamesOriginal(animation, nullptr, partList);
        stlp::string key(model, model + std::strlen(model));
        CFigureInstance* made = FigureCacheOf(this)->BuildWholeFigure(
            &key, std::bit_cast<std::uint32_t>(scale[0]), std::bit_cast<std::uint32_t>(scale[1]),
            std::bit_cast<std::uint32_t>(scale[2]), 0);
        reinterpret_cast<CountedRef*>(&figure)->Assign(static_cast<CEngineObject*>(static_cast<void*>(made)));
    } else {
        PieceNames pieces = MakePieceNames(names);
        {
            stlp::string key(model, model + std::strlen(model));
            CFigureInstance* made = FigureCacheOf(this)->BuildFigure(
                &key, &pieces, std::bit_cast<std::uint32_t>(scale[0]), std::bit_cast<std::uint32_t>(scale[1]),
                std::bit_cast<std::uint32_t>(scale[2]), 0);
            auto* old = static_cast<CFigureInstance*>(static_cast<void*>(figure));
            figure = static_cast<CEngineObject*>(static_cast<void*>(made));
            if (made) ++made->refCount;
            // 00457720: the old figure loses a reference and is deleted at the last one.
            if (old && --old->refCount == 0) old->DeletingDestructor(1);
        }
        FreePieceNames(pieces);
    }
    LoadPartsAt(this, nullptr, model, partList, arg);
    int classId = GetClassId();
    if ((classId == kClassUnit54 || GetClassId() == kClassUnit50) && animation) {
        animation->Play(nullptr);
        FigureApplyPendingOriginal(animation);
    }
    ComputeBounds();
    if (arg == 0 && !figure) flags_ &= ~kFlagFigureLoaded;
    else flags_ |= kFlagFigureLoaded;
    return 1;
}
EI_REPLACE_METHOD(0x00516DA0, CObject3D::MakeFigure);
