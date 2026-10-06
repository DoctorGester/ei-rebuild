// The particle manager's draw pass (00498940) and its draw order: every source's depth along the
// camera's view axis goes into the array at 007A9C50, which is sorted far-to-near with STLport's
// introsort (as instantiated in game.exe: median of three, unguarded partition, heap sort at the
// depth limit, insertion sort below 16 elements) and drawn in that order. Names are provisional.
#include "effects.h"

#include "engine_object.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }

// Rounds an x87 intermediate to float, as the original's FSTP to a float does.
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

ParticleDepthArray& DrawOrder() { return *reinterpret_cast<ParticleDepthArray*>(0x007A9C50); }

// The camera's orientation (0079B2DC).
const Quaternion& CameraRotation() { return *reinterpret_cast<const Quaternion*>(0x0079B2DC); }

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

// STLport __adjust_heap: sift the hole at `hole` down to a leaf, then push `value` up (not above
// `top`).
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

// 004994E0's insertion steps. __unguarded_linear_insert: no lower bound check.
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

// Calls through the original addresses, so that the verify wrappers of the hooked sort functions
// see the draw pass's calls (the same code in the release build).
__declspec(naked) void __fastcall IntroSortAt(ParticleDepth*, ParticleDepth*, void*, int, int) EI_ORIGINAL(0x00499380)
__declspec(naked) void __fastcall FinalSortAt(ParticleDepth*, ParticleDepth*, int) EI_ORIGINAL(0x004994E0)
__declspec(naked) int __fastcall DrawSourceAt(CParticleSource*, void*, void* /*renderer*/) EI_ORIGINAL(0x004976B0)

// The renderer's blend-factor setter (vtable slot 10, 00501C60): SRCBLEND, DESTBLEND (5 source
// alpha; 2 one: additive, 6 inverse source alpha: the default).
void SetRenderState(void* renderer, int source, int dest) {
    using Fn = void(__fastcall*)(void*, void*, int, int);
    (*static_cast<Fn**>(renderer))[10](renderer, nullptr, source, dest);
}

// A source attached to a unit (class 0x50) is drawn only while the unit has flag 0x400000.
bool AttachedToHiddenUnit(const CParticleSource* s) {
    auto* object = static_cast<CEngineObject*>(s->attachedTo);
    if (!object) return false;
    return object->GetClassId() == 0x50 && !(object->flags_ & 0x400000);
}

} // namespace

// 00499CD0 (STLport __make_heap; the tag arguments are unused).
void __fastcall MakeHeapByDepth(ParticleDepth* first, ParticleDepth* last, void*, int, void*) {
    if (last - first < 2) return;
    const int length = static_cast<int>(last - first);
    for (int parent = (length - 2) / 2;; --parent) {
        AdjustHeap(first, parent, length, first[parent]);
        if (parent == 0) return;
    }
}
EI_REPLACE(0x00499CD0, MakeHeapByDepth);

// 00499C20 (STLport __pop_heap): the top goes to `result`, `value` (the old *result) is re-sifted
// into [first, last).
void __fastcall PopHeapByDepth(ParticleDepth* first, ParticleDepth* last, ParticleDepth* result,
                               ParticleDepth value, int, void*) {
    *result = *first;
    AdjustHeap(first, 0, static_cast<int>(last - first), value);
}
EI_REPLACE(0x00499C20, PopHeapByDepth);

// 00499380 (STLport __introsort_loop): partitions until the range is at most 16 entries; past the
// depth limit the rest is heap sorted (make_heap, then pop_heap to the end).
void __fastcall IntroSortByDepth(ParticleDepth* first, ParticleDepth* last, void*, int depthLimit, int compare) {
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
EI_REPLACE(0x00499380, IntroSortByDepth);

// 004994E0 (STLport __final_insertion_sort).
void __fastcall FinalSortByDepth(ParticleDepth* first, ParticleDepth* last, int) {
    if (last - first > kThreshold) {
        InsertionSort(first, first + kThreshold);
        for (ParticleDepth* i = first + kThreshold; i != last; ++i) LinearInsert(i, *i);
    } else {
        InsertionSort(first, last);
    }
}
EI_REPLACE(0x004994E0, FinalSortByDepth);

// 00499200. MFC CArray::SetAtGrow: grows the array (new entries zeroed) to hold `index`.
EI_REPLACE_METHOD(0x00499200, ParticleDepthArray::SetAtGrow);

// 00484AE0. The attachment point plus the local position, or the local position alone.
Vector3* CParticleSource::WorldPosition(Vector3* out) {
    if (attachedTo) {
        Vector3 point;
        const Vector3* a = AttachmentPoint(&point, attachedTo);
        double z = D(a->z) + D(position.z);
        double y = D(a->y) + D(position.y);
        double x = D(a->x) + D(position.x);
        out->x = Store(x);
        out->y = Store(y);
        out->z = Store(z);
        return out;
    }
    std::memcpy(out, &position, sizeof(Vector3));
    return out;
}
EI_REPLACE_METHOD(0x00484AE0, CParticleSource::WorldPosition);

// 00498940. Rebuilds the draw order (depth = position . camera view axis, iterating the manager's
// sources until the first empty entry), sorts it far to near and draws each source that is not
// hidden (flag 0x40) and not on a hidden unit, switching to additive blending for sources with
// +0x150 == 1. Returns 0 as soon as a source fails to draw. The manager is held (+4) meanwhile.
int ParticlePool::DrawAll(void* renderer) {
    ++refCount;
    ParticleDepthArray& order = DrawOrder();
    if (order.data) {
        gamecrt::operator_delete(order.data);
        order.data = nullptr;
    }
    order.maxSize = 0;
    order.size = 0;
    Vector3 axis;
    CameraRotation().AxisZ(&axis);

    auto* node = sources.head;
    CParticleSource* source = node ? node->data : nullptr;
    while (source) {
        node = node->next;
        Vector3 scratch;
        const Vector3* p = source->WorldPosition(&scratch);
        ParticleDepth entry;
        entry.source = source;
        entry.depth = Store(D(axis.y) * D(p->y) + D(axis.z) * D(p->z) + D(axis.x) * D(p->x));
        order.SetAtGrow(order.size, entry);
        source = node ? node->data : nullptr;
    }

    ParticleDepth* first = order.data;
    ParticleDepth* last = order.data + order.size;
    if (first != last) {
        int log2 = 0;
        for (int n = static_cast<int>(last - first); n != 1; n >>= 1) ++log2;
        int compare = 0;    // the empty comparator (its stack slot carries garbage in the original)
        IntroSortAt(first, last, nullptr, log2 * 2, compare);
        FinalSortAt(first, last, compare);
    }

    bool additive = false;
    for (int i = 0; i < order.size; ++i) {
        CParticleSource* s = order.data[i].source;
        if (s->flags_ & 0x40) continue;
        if (AttachedToHiddenUnit(s)) continue;
        int blend;
        std::memcpy(&blend, &s->unknown_150, sizeof(blend));
        if (blend == 1 && !additive) {
            SetRenderState(renderer, 5, 2);
            additive = true;
        }
        std::memcpy(&blend, &s->unknown_150, sizeof(blend));
        if (blend == 0 && additive) {
            SetRenderState(renderer, 5, 6);
            additive = false;
        }
        if (!DrawSourceAt(s, nullptr, renderer)) {
            --refCount;
            return 0;
        }
    }
    --refCount;
    return 1;
}
EI_REPLACE_METHOD(0x00498940, ParticlePool::DrawAll);
