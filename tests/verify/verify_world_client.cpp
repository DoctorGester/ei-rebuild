// Checks for the client's per-frame view code (src/game/world_client_view.cpp): the visible-object
// collection, the draw list sorts and the cursor picking.
#include "verify.h"

#include "../../src/game/world.h"
#include "../../src/hook/log.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// ---- Sorts ---------------------------------------------------------------------------------------

using Obj = CEngineObject*;

// UnitOrderLess is a strict weak ordering only while every element is non-null, has a model and a
// non-NaN view depth (+0x30): its depth test is FCOM + TEST AH,1, so with a NaN depth
// Less(x, x) is true and STLport's unguarded scans run past the array (the rare Rebirth crash at
// 00574318, see docs/notes/world.md). Such ranges are logged and left to the original alone, on
// the real array: a scratch copy would read (and swap) different memory past its ends. A range
// with first > last is what the original's own recursion passes after its partition ran past the
// end; the original returns at once there.
// NaN is tested on the bits: under /fp:fast the compiler folded `x != x` to false (it did, so the
// first version of this check never fired for NaN).
bool IsNanBits(std::uint32_t bits) { return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0; }

constexpr int kReversed = -2;

int Suspicious(Obj* first, Obj* last) {
    if (last < first) return kReversed;
    for (Obj* p = first; p != last; ++p) {
        if (!*p || !At<void*>(*p, 0x94)) return static_cast<int>(p - first);
        if (IsNanBits(At<std::uint32_t>(*p, 0x30))) return static_cast<int>(p - first);
    }
    return -1;
}

void ReportSuspicious(const char* name, Obj* first, Obj* last, int index) {
    static int s_logged = 0;
    if (s_logged >= 20) return;
    ++s_logged;
    if (index == kReversed) {
        ei::Log("%s: reversed range (first %p, last %p, %d elements): a partition ran past the end", name,
                static_cast<void*>(first), static_cast<void*>(last), static_cast<int>(last - first));
        return;
    }
    Obj o = first[index];
    void* model = o ? At<void*>(o, 0x94) : nullptr;
    std::uint32_t depthBits = o ? At<std::uint32_t>(o, 0x30) : 0;
    ei::Log("%s: element %d of %d is not orderable (object %p, model %p, depth bits %08X, position %08X %08X %08X)",
            name, index, static_cast<int>(last - first), static_cast<void*>(o), model, depthBits,
            o ? At<std::uint32_t>(o, 0x1C) : 0, o ? At<std::uint32_t>(o, 0x20) : 0, o ? At<std::uint32_t>(o, 0x24) : 0);
}

// The keys each ordering compares, for mismatch reports.
void DescribeByModel(Obj o, char* out, std::size_t size) {
    if (!o) {
        std::snprintf(out, size, "null");
        return;
    }
    void* model = At<void*>(o, 0x94);
    std::uint32_t* rank = model ? At<std::uint32_t*>(model, 0x20) : nullptr;
    std::snprintf(out, size, "%p rank %u f0 %u depth %08X", static_cast<void*>(o), rank ? *rank : 0u,
                  At<std::uint32_t>(o, 0xF0), At<std::uint32_t>(o, 0x30));
}
void DescribeByModelUse(Obj o, char* out, std::size_t size) {
    void* model = o ? At<void*>(o, 0x94) : nullptr;
    std::snprintf(out, size, "%p use %u", static_cast<void*>(o), model ? At<std::uint32_t>(model, 0xEC) : 0u);
}

template <typename Original, typename Ours>
void CompareSort(const char* name, Obj* first, Obj* last, Original original, Ours ours,
                 void (*describe)(Obj, char*, std::size_t)) {
    if (last < first) {   // the original returns at once; nothing to compare
        original(first, last);
        return;
    }
    std::vector<Obj> input(first, last);
    std::vector<Obj> scratch(first, last);
    Obj* s = scratch.data();
    original(s, s + scratch.size());
    ours(first, last);
    int mismatch = -1;
    for (std::size_t i = 0; i < scratch.size(); ++i)
        if (scratch[i] != first[i]) {
            mismatch = static_cast<int>(i);
            break;
        }
    char expected[96] = "", actual[96] = "";
    if (mismatch >= 0) {
        describe(scratch[mismatch], expected, sizeof(expected));
        describe(first[mismatch], actual, sizeof(actual));
        // The whole range (input, the original's order, ours) for the first few mismatches.
        static int s_dumps = 0;
        if (s_dumps < 3) {
            ++s_dumps;
            ei::Log("%s mismatch: %d elements, first difference at %d", name, static_cast<int>(input.size()), mismatch);
            for (std::size_t i = 0; i < input.size(); ++i) {
                char a[96], b[96], c[96];
                describe(input[i], a, sizeof(a));
                describe(scratch[i], b, sizeof(b));
                describe(first[i], c, sizeof(c));
                ei::Log("  %2u in: %s | original: %s | ours: %s", static_cast<unsigned>(i), a, b, c);
            }
        }
    }
    ei::verify::Check(name, -1, mismatch, "n %d, first difference at %d: original %s, ours %s",
                      static_cast<int>(last - first), mismatch, expected, actual);
}
} // namespace

// 005740D0: SUB ESP,0x18; PUSH EBX; PUSH EBP (5 bytes). Output only (the array is reordered).
static void(__fastcall* s_introByModel)(Obj*, Obj*, void*, int, int);
static void __fastcall IntroByModelVerify(Obj* first, Obj* last, void* valueType, int depth, int comp) {
    int bad = Suspicious(first, last);
    if (bad >= 0) {
        ReportSuspicious("IntroSortByModel", first, last, bad);
        s_introByModel(first, last, valueType, depth, comp);
        return;
    }
    CompareSort(
        "IntroSortByModel", first, last, [&](Obj* f, Obj* l) { s_introByModel(f, l, valueType, depth, comp); },
        [&](Obj* f, Obj* l) { IntroSortByModel(f, l, valueType, depth, comp); }, DescribeByModel);
}
EI_VERIFY(0x005740D0, IntroByModelVerify, 5, s_introByModel);

// 00574390: SUB ESP,0x14; MOV EAX,EDX (5 bytes).
static void(__fastcall* s_finalByModel)(Obj*, Obj*, int);
static void __fastcall FinalByModelVerify(Obj* first, Obj* last, int comp) {
    int bad = Suspicious(first, last);
    if (bad >= 0) {
        ReportSuspicious("FinalSortByModel", first, last, bad);
        s_finalByModel(first, last, comp);
        return;
    }
    CompareSort(
        "FinalSortByModel", first, last, [&](Obj* f, Obj* l) { s_finalByModel(f, l, comp); },
        [&](Obj* f, Obj* l) { FinalSortByModel(f, l, comp); }, DescribeByModel);
}
EI_VERIFY(0x00574390, FinalByModelVerify, 5, s_finalByModel);

// 00573D50: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV EBP,EDX (6 bytes). The model-use ordering
// is a plain unsigned comparison (always a strict weak ordering).
static void(__fastcall* s_introByUse)(Obj*, Obj*, void*, int, int);
static void __fastcall IntroByUseVerify(Obj* first, Obj* last, void* valueType, int depth, int comp) {
    CompareSort(
        "IntroSortByModelUse", first, last, [&](Obj* f, Obj* l) { s_introByUse(f, l, valueType, depth, comp); },
        [&](Obj* f, Obj* l) { IntroSortByModelUse(f, l, valueType, depth, comp); }, DescribeByModelUse);
}
EI_VERIFY(0x00573D50, IntroByUseVerify, 6, s_introByUse);

// 00573F10: SUB ESP,0x8; PUSH EBX; PUSH EBP (5 bytes).
static void(__fastcall* s_finalByUse)(Obj*, Obj*, int);
static void __fastcall FinalByUseVerify(Obj* first, Obj* last, int comp) {
    CompareSort(
        "FinalSortByModelUse", first, last, [&](Obj* f, Obj* l) { s_finalByUse(f, l, comp); },
        [&](Obj* f, Obj* l) { FinalSortByModelUse(f, l, comp); }, DescribeByModelUse);
}
EI_VERIFY(0x00573F10, FinalByUseVerify, 5, s_finalByUse);

// 00573EE0: MOV EAX,[ESP+4]; MOV EDX,[ESP+8] (8 bytes). Pure, 0/1 in EAX.
static BOOL(__fastcall* s_modelUseLess)(ModelUseLess*, void*, Obj, Obj);
static BOOL __fastcall ModelUseLessVerify(ModelUseLess* self, void*, Obj a, Obj b) {
    BOOL expected = s_modelUseLess(self, nullptr, a, b);
    ei::verify::Check("ModelUseLess::Less", expected, self->Less(a, b), "%p %p", static_cast<void*>(a), static_cast<void*>(b));
    return expected;
}
EI_VERIFY(0x00573EE0, ModelUseLessVerify, 8, s_modelUseLess);

// ---- Cursor picking ------------------------------------------------------------------------------

// 0056BF30 / 0056C350: PUSH EBP; MOV EBP,ESP; SUB ESP,0x30 (6 bytes). Output only: the original
// writes a scratch vector, ours the caller's; both leave the picking stack as they found it (the
// slot they push is rewritten with the same matrix).
static float*(__fastcall* s_pickGround)(CWorldClient*, void*, float*, int, int);
static float* __fastcall PickGroundVerify(CWorldClient* self, void*, float* out, int x, int y) {
    float expected[3];
    s_pickGround(self, nullptr, expected, x, y);
    float* result = self->PickGround(out, x, y);
    ei::verify::Check("CWorldClient::PickGround", true, std::memcmp(expected, out, 12) == 0 && result == out,
                      "(%d %d) -> %g %g %g, ours %g %g %g", x, y, expected[0], expected[1], expected[2], out[0], out[1], out[2]);
    return out;
}
EI_VERIFY(0x0056BF30, PickGroundVerify, 6, s_pickGround);

static float*(__fastcall* s_pickSurface)(CWorldClient*, void*, float*, int, int);
static float* __fastcall PickSurfaceVerify(CWorldClient* self, void*, float* out, int x, int y) {
    float expected[3];
    s_pickSurface(self, nullptr, expected, x, y);
    float* result = self->PickSurface(out, x, y);
    ei::verify::Check("CWorldClient::PickSurface", true, std::memcmp(expected, out, 12) == 0 && result == out,
                      "(%d %d) -> %g %g %g, ours %g %g %g", x, y, expected[0], expected[1], expected[2], out[0], out[1], out[2]);
    return out;
}
EI_VERIFY(0x0056C350, PickSurfaceVerify, 6, s_pickSurface);

// ---- Visible-object collection -------------------------------------------------------------------

namespace {
struct DrawVector {
    Obj* begin;
    Obj* end;
    Obj* capacity;
};
DrawVector& Vec(std::uintptr_t address) { return *reinterpret_cast<DrawVector*>(address); }

// The draw lists by class (visible, shadow); the default row has class 0.
struct ClassLists {
    int classId;
    std::uintptr_t visible;
    std::uintptr_t shadow;
};
constexpr ClassLists kLists[] = {
    {0x50, 0x007AF578, 0x007AF528}, {0x52, 0x007AF568, 0x007AF518}, {0x54, 0x007AF5A8, 0x007AF558},
    {0x5D, 0x007AF588, 0x007AF538}, {0, 0x007AF598, 0x007AF548},
};
constexpr std::uintptr_t kTouched = 0x007AF508;

struct ListNode {
    ListNode* next;
    ListNode* prev;
    Obj value;
};
ListNode* DeferredHead() { return *reinterpret_cast<ListNode**>(0x007AF5B4); }

const ClassLists& ListsFor(int classId) {
    for (const ClassLists& l : kLists)
        if (l.classId == classId) return l;
    return kLists[4];
}
} // namespace

// 0056A990: ours only (it animates units through slot 25, takes references, applies pending
// figure changes and moves the frame's lists, none of which may run twice). Checks what it
// appended: the return value counts the touched-model entries; every visible entry has flag 0x20
// and sits in the lists of its class, with the same entry appended to the class's shadow list;
// deferred units are units and only appear on the first pass.
static int __fastcall CollectVisibleVerify(CWorldClient* self, void*, void* list, const float* view, float animTime,
                                           std::uint32_t now, char animateUnits, char withScripted) {
    // 0056AFE0 passes the top of the device's view stack (+0x5C, depth +0x60) without checking
    // the depth: with an empty stack the "matrix" is the 64 bytes before the buffer, and the view
    // depths (+0x30) the sort orders by are garbage, possibly NaN (see IntroSortByModel).
    auto* device = *reinterpret_cast<std::uint8_t**>(0x007AF068);
    if (device && At<int>(device, 0x60) <= 0) {
        static int s_logged = 0;
        if (s_logged < 5) {
            ++s_logged;
            ei::Log("CWorldClient::CollectVisible: view stack depth %d, view matrix %p (stack %p)", At<int>(device, 0x60),
                    static_cast<const void*>(view), At<void*>(device, 0x5C));
        }
    }
    std::size_t before[11];
    std::uintptr_t vectors[11] = {kTouched};
    for (int i = 0; i < 5; ++i) {
        vectors[1 + i] = kLists[i].visible;
        vectors[6 + i] = kLists[i].shadow;
    }
    for (int i = 0; i < 11; ++i) before[i] = Vec(vectors[i]).end - Vec(vectors[i]).begin;
    ListNode* lastDeferred = DeferredHead()->prev;

    int touched = self->CollectVisible(list, view, animTime, now, animateUnits, withScripted);

    bool ok = touched == static_cast<int>((Vec(kTouched).end - Vec(kTouched).begin) - before[0]);
    for (int i = 0; i < 5 && ok; ++i) {
        DrawVector& visible = Vec(kLists[i].visible);
        DrawVector& shadow = Vec(kLists[i].shadow);
        Obj* v = visible.begin + before[1 + i];
        Obj* s = shadow.begin + before[6 + i];
        for (; v != visible.end && ok; ++v) {
            ok = ((*v)->flags_ & 0x20) != 0 && &ListsFor((*v)->GetClassId()) == &kLists[i];
            // The shadow list gets the same entry (possibly after out-of-view shadow casters).
            while (ok && s != shadow.end && *s != *v) ++s;
            ok = ok && s != shadow.end;
            if (ok) ++s;
        }
        for (s = shadow.begin + before[6 + i]; s != shadow.end && ok; ++s) ok = &ListsFor((*s)->GetClassId()) == &kLists[i];
    }
    for (ListNode* n = lastDeferred->next; ok && n != DeferredHead(); n = n->next)
        ok = !animateUnits && n->value && n->value->GetClassId() == world::kClassUnit;
    ei::verify::Check("CWorldClient::CollectVisible", true, ok, "touched %d animate %d", touched, animateUnits);
    return touched;
}
EI_VERIFY(0x0056A990, CollectVisibleVerify, 0, s_unused);

// 005641F0 (CWorldClient::Segment, slot 6) runs the client's tick: ours only. It returns 1 (0 only
// when the connection is gone), and the game time advances by one tick unless +0x6C is set. The
// server's messages, handled before that point (the session update), may change both: the pause
// message (00668CFF -> 00566350) sets +0x6C, and the world message (00669010 -> 00567AB0) loads
// the client world, reading the tick from the stream and zeroing the frame count (+0x6EC) that
// Segment increments first. So the pause flag is read afterwards, and after a load any tick goes.
static void* s_segmentUnused;
static int __fastcall SegmentVerify(CWorldClient* self, void*) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(self);
    int before = self->tick_;
    int framesBefore = *reinterpret_cast<const int*>(bytes + 0x6EC);
    int result = self->Segment();
    bool paused = bytes[0x6C] != 0;
    bool loaded = *reinterpret_cast<const int*>(bytes + 0x6EC) != framesBefore + 1;
    int expected = before + (paused ? 0 : 1);
    bool ok = result == 0 || (result == 1 && (loaded || self->tick_ == expected));
    ei::verify::Check("CWorldClient::Segment", true, ok, "result %d, tick %d -> %d%s%s", result, before, self->tick_,
                      paused ? " (paused)" : "", loaded ? " (loaded)" : "");
    return result;
}
EI_VERIFY(0x005641F0, SegmentVerify, 0, s_segmentUnused);
