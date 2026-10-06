// CObject3D::SetUpAnimatedFigure (005179E0): the figure of a model with an animation state (units,
// levers, map objects with animations; called by the zone loader, the world's object setup,
// Lever::SetState and the unit controller): the state takes the model, one box and one name per
// animated part, and the object's bounds around the boxes. See map_object_figures.h and
// docs/notes/map_objects.md ("Figures").
#include "map_object_figures.h"

#include "ai_map.h"
#include "core_maths.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_units.h"

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace {
constexpr int kTypeHero = 0x32;                  // type_ of the player's units: a fixed x / y box
constexpr float kHeroHalfWidth = 0.5f;
constexpr float kHalf = 0.5f;                    // 0073B86C
constexpr std::uint32_t kBoundsSphere = 1;       // boundsFlags: radius valid
constexpr std::uint32_t kBoundsBox = 2;          // boundsFlags: box valid

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
