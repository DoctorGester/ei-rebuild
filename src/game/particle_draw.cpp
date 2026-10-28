// The particle manager's draw pass and its draw order: every source's depth along the camera's view axis goes
// into an array, which is sorted far-to-near with STLport's introsort (as instantiated in game.exe: median of
// three, unguarded partition, heap sort at the depth limit, insertion sort below 16 elements) and drawn in
// that order. Names are provisional.
#include "effects.h"
#include "map_object_figures.h"

#include "engine_object.h"

#include "lib/crt.h"
#include "lib/vecmath.h"
#include "game_data.h"
#include "camera.h"
#include "game_arrays.h"
#include "lib/static_object.h"

#include <cstdlib>
#include <cstring>

namespace {

inline double D(float f) { return f; }

// The particles' draw order: built empty by a static initialiser, destroyed at exit.
constinit lib::StaticObject<ParticleDepthArray> g_drawOrder = /*007A9C50*/ {};
ParticleDepthArray& DrawOrder() { return *g_drawOrder; }
void DestroyDrawOrder() {
    g_drawOrder.Destroy();
}
// The camera's orientation.
const Quaternion& CameraRotation() { return TheCamera()->localRotation_; }

// The sort's ordering: far (greater depth) first. x87 FCOMP / TEST AH,0x41: false for NaN.
bool Before(const ParticleDepth& a, const ParticleDepth& b) { return a.depth > b.depth; }
constexpr int kThreshold = 16;

// STLport __median: the median of a, b, c under Before.
const ParticleDepth& Median(const ParticleDepth& a, const ParticleDepth& b, const ParticleDepth& c) {
    if (Before(a, b)) {
        if (Before(b, c)) return b;
        if (Before(a, c)) return c;
        return a;
    }
    if (Before(a, c)) return a;
    if (Before(b, c)) return c;
    return b;
}

// STLport __unguarded_partition: the pivot is a copy; the scans have no bounds checks (the median
// guarantees a stop on both sides).
ParticleDepth* Partition(ParticleDepth* first, ParticleDepth* last, ParticleDepth pivot) {
    for (;;) {
        while (Before(*first, pivot)) ++first;
        --last;
        while (Before(pivot, *last)) --last;
        if (!(first < last)) return first;
        ParticleDepth t = *first;
        *first = *last;
        *last = t;
        ++first;
    }
}

// STLport __adjust_heap: sift the hole at `hole` down to a leaf, then push `value` up (not above `top`).
void AdjustHeap(ParticleDepth* first, int hole, int length, ParticleDepth value) {
    const int top = hole;
    int child = 2 * hole + 2;
    while (child < length) {
        if (Before(first[child], first[child - 1])) --child;
        first[hole] = first[child];
        hole = child;
        child = 2 * child + 2;
    }
    if (child == length) {
        first[hole] = first[child - 1];
        hole = child - 1;
    }
    int parent = (hole - 1) / 2;
    while (hole > top && Before(first[parent], value)) {
        first[hole] = first[parent];
        hole = parent;
        parent = (hole - 1) / 2;
    }
    first[hole] = value;
}

// The insertion steps. __unguarded_linear_insert: no lower bound check.
void LinearInsert(ParticleDepth* last, ParticleDepth value) {
    ParticleDepth* next = last - 1;
    while (Before(value, *next)) {
        *last = *next;
        last = next;
        --next;
    }
    *last = value;
}

void InsertionSort(ParticleDepth* first, ParticleDepth* last) {
    if (first == last) return;
    for (ParticleDepth* i = first + 1; i != last; ++i) {
        ParticleDepth value = *i;
        if (Before(value, *first)) {
            std::memmove(first + 1, first, static_cast<std::size_t>(i - first) * sizeof(ParticleDepth));
            *first = value;
        } else {
            LinearInsert(i, value);
        }
    }
}

// The blend factors set: SRCBLEND, DESTBLEND (5 source alpha; 2 one: additive, 6 inverse source alpha: the
// default).
void SetRenderState(CSceneRenderer* renderer, int source, int dest) {
    renderer->SetBlend(static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(dest));
}

// A source attached to a unit (class 0x50) is drawn only while the unit has flag 0x400000.
bool AttachedToHiddenUnit(const CParticleSource* s) {
    CEngineObject* object = s->attachedTo;
    if (!object) return false;
    return object->GetClassId() == 0x50 && !(object->flags_ & 0x400000);
}

} // namespace

void ConstructDrawOrder() {
    g_drawOrder.Construct();
    std::atexit(DestroyDrawOrder);
}

// STLport __make_heap (the tag arguments are unused).
void MakeHeapByDepth(ParticleDepth* first, ParticleDepth* last, void*, int, void*) {
    if (last - first < 2) return;
    const int length = static_cast<int>(last - first);
    for (int parent = (length - 2) / 2;; --parent) {
        AdjustHeap(first, parent, length, first[parent]);
        if (parent == 0) return;
    }
}

// STLport __pop_heap: the top goes to `result`, `value` (the old *result) is re-sifted into [first, last).
void PopHeapByDepth(ParticleDepth* first, ParticleDepth* last, ParticleDepth* result,
                               ParticleDepth value, int, void*) {
    *result = *first;
    AdjustHeap(first, 0, static_cast<int>(last - first), value);
}

// STLport __introsort_loop: partitions until the range is at most 16 entries; past the depth limit the rest
// is heap sorted (make_heap, then pop_heap to the end).
void IntroSortByDepth(ParticleDepth* first, ParticleDepth* last, void*, int depthLimit, int compare) {
    while (last - first > kThreshold) {
        if (depthLimit == 0) {
            MakeHeapByDepth(first, last, nullptr, compare, nullptr);
            while (last - first > 1) {
                --last;
                PopHeapByDepth(first, last, last, *last, compare, nullptr);
            }
            return;
        }
        --depthLimit;
        ParticleDepth* middle = first + (last - first) / 2;
        ParticleDepth* cut = Partition(first, last, Median(*first, *middle, *(last - 1)));
        IntroSortByDepth(cut, last, nullptr, depthLimit, compare);
        last = cut;
    }
}

// STLport __final_insertion_sort.
void FinalSortByDepth(ParticleDepth* first, ParticleDepth* last, int) {
    if (last - first > kThreshold) {
        InsertionSort(first, first + kThreshold);
        for (ParticleDepth* i = first + kThreshold; i != last; ++i) LinearInsert(i, *i);
    } else {
        InsertionSort(first, last);
    }
}

// The attachment point plus the local position, or the local position alone.
Vector3* CParticleSource::WorldPosition(Vector3* out) {
    if (attachedTo) {
        Vector3 point;
        const Vector3* a = AttachmentPoint(&point, attachedTo);
        double z = D(a->z) + D(localPosition_.z);
        double y = D(a->y) + D(localPosition_.y);
        double x = D(a->x) + D(localPosition_.x);
        out->x = static_cast<float>(x);
        out->y = static_cast<float>(y);
        out->z = static_cast<float>(z);
        return out;
    }
    std::memcpy(out, &localPosition_, sizeof(Vector3));
    return out;
}

// Rebuilds the draw order (depth = position . camera view axis, iterating the manager's
// sources until the first empty entry), sorts it far to near and draws each source that is not
// hidden (flag 0x40) and not on a hidden unit, switching to additive blending for sources with
// +0x150 == 1. Returns 0 as soon as a source fails to draw. The manager is held (+4) meanwhile.
int CParticleManager::DrawAll(CSceneRenderer* renderer) {
    ++refCount_;
    ParticleDepthArray& order = DrawOrder();
    lib::FreeStorage(order);
    Vector3 axis;
    CameraRotation().AxisZ(&axis);

    // The manager's objects are its sources.
    auto sourceOf = [](CEngineObject* object) { return static_cast<CParticleSource*>(object); };
    auto node = objectList_.begin();
    CParticleSource* source = node != objectList_.end() ? sourceOf(*node) : nullptr;
    while (source) {
        ++node;
        Vector3 scratch;
        const Vector3* p = source->WorldPosition(&scratch);
        ParticleDepth entry;
        entry.source = source;
        entry.depth = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(axis, *p));
        lib::SetAtGrow(order, std::ssize(order), entry);
        source = node != objectList_.end() ? sourceOf(*node) : nullptr;
    }

    ParticleDepth* first = order.data();
    ParticleDepth* last = order.data() + std::ssize(order);
    if (first != last) {
        int log2 = 0;
        for (int n = static_cast<int>(last - first); n != 1; n >>= 1) ++log2;
        int compare = 0;    // the empty comparator (its stack slot carries garbage in the original)
        IntroSortByDepth(first, last, nullptr, log2 * 2, compare);
        FinalSortByDepth(first, last, compare);
    }

    bool additive = false;
    for (int i = 0; i < std::ssize(order); ++i) {
        CParticleSource* s = order[i].source;
        if (s->flags_ & 0x40) continue;
        if (AttachedToHiddenUnit(s)) continue;
        int blend;
        std::memcpy(&blend, &s->additiveBlend, sizeof(blend));
        if (blend == 1 && !additive) {
            SetRenderState(renderer, 5, 2);
            additive = true;
        }
        std::memcpy(&blend, &s->additiveBlend, sizeof(blend));
        if (blend == 0 && additive) {
            SetRenderState(renderer, 5, 6);
            additive = false;
        }
        if (!s->Draw(renderer)) {
            --refCount_;
            return 0;
        }
    }
    --refCount_;
    return 1;
}
