// Checks for a unit's motion (src/game/ai_motion.cpp): the AIPath order header and the trajectory.
//
// The trajectory functions only change the motion object (and its three arrays): the original runs
// on a deep copy with its own game-heap arrays, ours on the real object, and everything is compared.
#include "verify.h"

#include "../../src/game/ai_motion.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

template <typename T>
void CloneArray(mfc::CArray<T>& copy, const mfc::CArray<T>& source) {
    copy = source;
    if (source.data) {
        int capacity = source.maxSize > source.size ? source.maxSize : source.size;
        copy.data = static_cast<T*>(gamecrt::operator_new((capacity > 0 ? capacity : 1) * sizeof(T)));
        std::memcpy(copy.data, source.data, source.size * sizeof(T));
    }
}

template <typename T>
void FreeArray(mfc::CArray<T>& array) {
    if (array.data) gamecrt::operator_delete(array.data);
    array.data = nullptr;
}

template <typename T>
bool SameArray(const mfc::CArray<T>& a, const mfc::CArray<T>& b) {
    if (a.size != b.size || a.maxSize != b.maxSize || a.growBy != b.growBy || a.vtable != b.vtable) return false;
    if ((a.data == nullptr) != (b.data == nullptr)) return false;
    return a.size <= 0 || !a.data || std::memcmp(a.data, b.data, a.size * sizeof(T)) == 0;
}

// A motion object with its own copies of the arrays.
struct MotionClone {
    UnitMotion m;
    explicit MotionClone(const UnitMotion& source) {
        std::memcpy(&m, &source, sizeof(m));
        CloneArray(m.points, source.points);
        CloneArray(m.waypoints, source.waypoints);
        CloneArray(m.stepSpeeds, source.stepSpeeds);
    }
    ~MotionClone() {
        FreeArray(m.points);
        FreeArray(m.waypoints);
        FreeArray(m.stepSpeeds);
    }
    MotionClone(const MotionClone&) = delete;
    MotionClone& operator=(const MotionClone&) = delete;
};

bool SameMotion(const UnitMotion& a, const UnitMotion& b) {
    constexpr std::size_t scalars = offsetof(UnitMotion, speedScale);
    return SameArray(a.points, b.points) && SameArray(a.waypoints, b.waypoints) &&
           SameArray(a.stepSpeeds, b.stepSpeeds) &&
           std::memcmp(reinterpret_cast<const char*>(&a) + scalars, reinterpret_cast<const char*>(&b) + scalars,
                       sizeof(UnitMotion) - scalars) == 0;
}

// The fields Sample / SamplePath write besides the motion object.
struct SampleOut {
    bool turning;
    float position[2];
    float direction[3];
    float speed;
    int cellX, cellY;

    void Load(const bool* t, const float* p, const float* d, const float* s, const int* x, const int* y) {
        turning = *t;
        std::memcpy(position, p, sizeof(position));
        std::memcpy(direction, d, sizeof(direction));
        std::memcpy(&speed, s, sizeof(speed));
        cellX = *x;
        cellY = *y;
    }
    bool Same(const SampleOut& o) const {
        return turning == o.turning && std::memcmp(position, o.position, sizeof(position)) == 0 &&
               std::memcmp(direction, o.direction, sizeof(direction)) == 0 &&
               std::memcmp(&speed, &o.speed, sizeof(speed)) == 0 && cellX == o.cellX && cellY == o.cellY;
    }
};

// The AIPath bytes the order functions may touch (header, steps, cells, flags and the buffer at +0x74).
constexpr std::size_t kPathBytes = 0x88;

// A copy of a path whose lists and buffer are empty, so original code can "free" them harmlessly.
struct PathScratch {
    alignas(4) unsigned char bytes[kPathBytes];
    explicit PathScratch(const AIPath* source) {
        std::memcpy(bytes, source, kPathBytes);
        AIPath* p = Path();
        for (PathCellList* list : {&p->steps, &p->cells}) {
            list->head = list->tail = list->free = nullptr;
            list->count = 0;
            list->blocks = nullptr;
        }
        *reinterpret_cast<void**>(bytes + 0x74) = nullptr;
    }
    AIPath* Path() { return reinterpret_cast<AIPath*>(bytes); }
};

bool ListIsEmpty(const PathCellList& list) {
    return !list.head && !list.tail && !list.free && !list.blocks && list.count == 0;
}
} // namespace

// --- Trajectory -----------------------------------------------------------------------------------

// 005C7430: PUSH EBP; MOV EBP,ESP; SUB ESP,0x28 (6 bytes).
static bool(__fastcall* s_sample)(UnitMotion*, void*, void*, MotionOrder*, float, bool*, float*, float*, float*,
                                  int*, int*);
static bool __fastcall SampleVerify(UnitMotion* self, void*, void* world, MotionOrder* order, float time, bool* turning,
                                    float* position, float* direction, float* speed, int* cellX, int* cellY) {
    UnitMotion copy;
    std::memcpy(&copy, self, sizeof(copy));     // the arrays are only read: shared
    SampleOut expected;
    expected.Load(turning, position, direction, speed, cellX, cellY);
    bool expectedResult = s_sample(&copy, nullptr, world, order, time, &expected.turning, expected.position,
                                   expected.direction, &expected.speed, &expected.cellX, &expected.cellY);
    bool result = self->Sample(world, order, time, turning, position, direction, speed, cellX, cellY);
    SampleOut actual;
    actual.Load(turning, position, direction, speed, cellX, cellY);
    bool same = expectedResult == result && expected.Same(actual) && std::memcmp(&copy, self, sizeof(copy)) == 0;
    ei::verify::Check("UnitMotion::Sample", true, same, "kind %d t %g: %d (%g,%g) vs %d (%g,%g)", order->kind, time,
                      expectedResult, expected.position[0], expected.position[1], result, position[0], position[1]);
    return result;
}
EI_VERIFY(0x005C7430, SampleVerify, 6, s_sample);

// 005C6FC0: PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH EBX; PUSH ESI (6 bytes).
static bool(__fastcall* s_samplePath)(UnitMotion*, void*, float, bool*, float*, float*, float*, int*, int*);
static bool __fastcall SamplePathVerify(UnitMotion* self, void*, float time, bool* turning, float* position,
                                        float* direction, float* speed, int* cellX, int* cellY) {
    UnitMotion copy;
    std::memcpy(&copy, self, sizeof(copy));
    SampleOut expected;
    expected.Load(turning, position, direction, speed, cellX, cellY);
    bool expectedResult = s_samplePath(&copy, nullptr, time, &expected.turning, expected.position, expected.direction,
                                       &expected.speed, &expected.cellX, &expected.cellY);
    bool result = self->SamplePath(time, turning, position, direction, speed, cellX, cellY);
    SampleOut actual;
    actual.Load(turning, position, direction, speed, cellX, cellY);
    bool same = expectedResult == result && expected.Same(actual) && std::memcmp(&copy, self, sizeof(copy)) == 0;
    ei::verify::Check("UnitMotion::SamplePath", true, same, "t %g segment %d: %d vs %d", time, copy.segment,
                      expectedResult, result);
    return result;
}
EI_VERIFY(0x005C6FC0, SamplePathVerify, 6, s_samplePath);

// 005C67D0: SUB ESP,0x70; PUSH EBX; PUSH EBP (5 bytes).
static void(__fastcall* s_build)(UnitMotion*, void*, AIPath*);
static void __fastcall BuildVerify(UnitMotion* self, void*, AIPath* path) {
    MotionClone clone(*self);
    s_build(&clone.m, nullptr, path);
    self->Build(path);
    ei::verify::Check("UnitMotion::Build", true, SameMotion(clone.m, *self), "kind %d: %d vs %d points",
                      OrderOf(path)->kind, clone.m.points.size, self->points.size);
}
EI_VERIFY(0x005C67D0, BuildVerify, 5, s_build);

// 005C6750: PUSH ECX; MOV EAX,[ESP+8] (5 bytes).
static void(__fastcall* s_assignCells)(UnitMotion*, void*, AIPath*);
static void __fastcall AssignCellsVerify(UnitMotion* self, void*, AIPath* path) {
    MotionClone clone(*self);
    s_assignCells(&clone.m, nullptr, path);
    self->AssignCells(path);
    ei::verify::Check("UnitMotion::AssignCells", true, SameMotion(clone.m, *self), "%d points, %d steps",
                      self->points.size, path->steps.count);
}
EI_VERIFY(0x005C6750, AssignCellsVerify, 5, s_assignCells);

// 005C66C0: SUB ESP,0xC; PUSH ESI; PUSH EDI (5 bytes).
static void(__fastcall* s_subdivide)(UnitMotion*, void*);
static void __fastcall SubdivideVerify(UnitMotion* self, void*) {
    MotionClone clone(*self);
    s_subdivide(&clone.m, nullptr);
    self->Subdivide();
    ei::verify::Check("UnitMotion::Subdivide", true, SameMotion(clone.m, *self), "%d waypoints: %d vs %d points",
                      self->waypoints.size, clone.m.points.size, self->points.size);
}
EI_VERIFY(0x005C66C0, SubdivideVerify, 5, s_subdivide);

// 005C6370: SUB ESP,0x2C; PUSH EBX; MOV EBX,ECX (6 bytes).
static void(__fastcall* s_subdivideSegment)(UnitMotion*, void*, const MotionWaypoint*, const MotionWaypoint*, int,
                                            float, float, int*);
static void __fastcall SubdivideSegmentVerify(UnitMotion* self, void*, const MotionWaypoint* a, const MotionWaypoint* b,
                                              int cells, float startOffset, float endOffset, int* stepIndex) {
    // a and b point into the real waypoint array, which neither run changes.
    MotionClone clone(*self);
    int expectedIndex = *stepIndex;
    s_subdivideSegment(&clone.m, nullptr, a, b, cells, startOffset, endOffset, &expectedIndex);
    self->SubdivideSegment(a, b, cells, startOffset, endOffset, stepIndex);
    ei::verify::Check("UnitMotion::SubdivideSegment", true, SameMotion(clone.m, *self) && expectedIndex == *stepIndex,
                      "%d cells (%g,%g): %d vs %d points", cells, startOffset, endOffset, clone.m.points.size,
                      self->points.size);
}
EI_VERIFY(0x005C6370, SubdivideSegmentVerify, 6, s_subdivideSegment);

// 005C6290: PUSH EBP; MOV EBP,ESP; PUSH ECX; MOV EAX,[ECX+8] (7 bytes).
static void(__fastcall* s_limitSpeeds)(UnitMotion*, void*, float);
static void __fastcall LimitSpeedsVerify(UnitMotion* self, void*, float rate) {
    MotionClone clone(*self);
    s_limitSpeeds(&clone.m, nullptr, rate);
    self->LimitSpeeds(rate);
    ei::verify::Check("UnitMotion::LimitSpeeds", true, SameMotion(clone.m, *self), "%d points rate %g",
                      self->points.size, rate);
}
EI_VERIFY(0x005C6290, LimitSpeedsVerify, 7, s_limitSpeeds);

// 005C6270: XOR EAX,EAX; MOV [ECX+0xA0],-1 (12 bytes).
static int(__fastcall* s_clearMotion)(UnitMotion*, void*);
static int __fastcall ClearMotionVerify(UnitMotion* self, void*) {
    UnitMotion copy;
    std::memcpy(&copy, self, sizeof(copy));
    int expected = s_clearMotion(&copy, nullptr);
    int result = self->Clear();
    ei::verify::Check("UnitMotion::Clear", true, expected == result && std::memcmp(&copy, self, sizeof(copy)) == 0,
                      "%d vs %d", expected, result);
    return result;
}
EI_VERIFY(0x005C6270, ClearMotionVerify, 12, s_clearMotion);

// 005C73B0: FLD [0x0073B84C] (6 bytes). Writes only the four coefficients.
static void(__fastcall* s_fit)(MotionCubic*, void*, float, float, float, float, float);
static void __fastcall FitVerify(MotionCubic* self, void*, float p0, float d0, float p1, float d1, float length) {
    MotionCubic expected = *self;
    s_fit(&expected, nullptr, p0, d0, p1, d1, length);
    self->Fit(p0, d0, p1, d1, length);
    ei::verify::Check("MotionCubic::Fit", true, std::memcmp(&expected, self, sizeof(expected)) == 0,
                      "(%g,%g)-(%g,%g) L %g: c2 %g/%g c3 %g/%g", p0, d0, p1, d1, length, expected.c2, self->c2,
                      expected.c3, self->c3);
}
EI_VERIFY(0x005C73B0, FitVerify, 6, s_fit);

// 005C7410: FLD [ESP+4]; FMUL [ECX+0xC] (7 bytes). Pure; the result is compared as a double.
static double(__fastcall* s_slope)(MotionCubic*, void*, float);
static double __fastcall SlopeVerify(MotionCubic* self, void*, float u) {
    double expected = s_slope(self, nullptr, u);
    double actual = self->Slope(u);
    ei::verify::Check("MotionCubic::Slope", expected, actual, "u %g: %g vs %g", u, expected, actual);
    return actual;
}
EI_VERIFY(0x005C7410, SlopeVerify, 7, s_slope);

// --- Order (AIPath header) --------------------------------------------------------------------------

// 005C7B00: MOV EAX,[ESP+8]; PUSH EBX (5 bytes). Original on a copy with empty lists.
static bool(__fastcall* s_start)(AIPath*, void*, const Vector3*, const Vector3*, int);
static bool __fastcall StartVerify(AIPath* self, void*, const Vector3* position, const Vector3* direction, int time) {
    PathScratch scratch(self);
    bool expected = s_start(scratch.Path(), nullptr, position, direction, time);
    bool result = self->Start(position, direction, time);
    bool same = expected == result && std::memcmp(scratch.Path(), self, sizeof(MotionOrder)) == 0 &&
                ListIsEmpty(self->steps);
    ei::verify::Check("AIPath::Start", true, same, "(%g,%g) t %d: %d vs %d", position->x, position->y, time, expected,
                      result);
    return result;
}
EI_VERIFY(0x005C7B00, StartVerify, 5, s_start);

// 005C7D50: SUB ESP,8; PUSH ESI; PUSH EDI (5 bytes).
static int(__fastcall* s_reset)(AIPath*, void*);
static int __fastcall ResetVerify(AIPath* self, void*) {
    PathScratch scratch(self);
    int expected = s_reset(scratch.Path(), nullptr);
    int result = self->Reset();
    bool same = expected == result && std::memcmp(scratch.Path(), self, sizeof(MotionOrder)) == 0 &&
                ListIsEmpty(self->steps);
    ei::verify::Check("AIPath::Reset", true, same, "%d vs %d", expected, result);
    return result;
}
EI_VERIFY(0x005C7D50, ResetVerify, 5, s_reset);

// 005B23A0: SUB ESP,8; PUSH EBX; PUSH ESI (5 bytes). Both runs leave empty lists and no buffer, so
// all the bytes must agree.
static void(__fastcall* s_clearPath)(AIPath*, void*);
static void __fastcall ClearPathVerify(AIPath* self, void*) {
    PathScratch scratch(self);
    s_clearPath(scratch.Path(), nullptr);
    self->Clear();
    ei::verify::Check("AIPath::Clear", true, std::memcmp(scratch.Path(), self, kPathBytes) == 0, "");
}
EI_VERIFY(0x005B23A0, ClearPathVerify, 5, s_clearPath);

// The setters write only the header: original into a copy of it.
template <typename Fn, typename Ours, typename... Args>
void CheckHeader(const char* name, Fn original, Ours ours, AIPath* self, Args... args) {
    PathScratch scratch(self);
    original(scratch.Path(), nullptr, args...);
    (self->*ours)(args...);
    ei::verify::Check(name, true, std::memcmp(scratch.Path(), self, sizeof(MotionOrder)) == 0, "kind %d",
                      OrderOf(self)->kind);
}

// 005C7C00: MOV EAX,[ESP+4]; MOV [ECX],1 (10 bytes).
static void(__fastcall* s_setTurn)(AIPath*, void*, const Vector3*, float);
static void __fastcall SetTurnVerify(AIPath* self, void*, const Vector3* direction, float rate) {
    CheckHeader("AIPath::SetTurn", s_setTurn, &AIPath::SetTurn, self, direction, rate);
}
EI_VERIFY(0x005C7C00, SetTurnVerify, 10, s_setTurn);

// 005C7C20: MOV EAX,[ESP+4]; MOV EDX,[ESP+8] (8 bytes).
static void(__fastcall* s_setTrack)(AIPath*, void*, int, int, int);
static void __fastcall SetTrackVerify(AIPath* self, void*, int id, int from, int to) {
    CheckHeader("AIPath::SetTrack", s_setTrack, &AIPath::SetTrack, self, id, from, to);
}
EI_VERIFY(0x005C7C20, SetTrackVerify, 8, s_setTrack);

// 005C7C40: MOV EAX,[ESP+4]; MOV EDX,[ESP+0xC] (8 bytes).
static void(__fastcall* s_setTurnUntil)(AIPath*, void*, const Vector3*, int, int);
static void __fastcall SetTurnUntilVerify(AIPath* self, void*, const Vector3* direction, int arg6, int arg7) {
    CheckHeader("AIPath::SetTurnUntil", s_setTurnUntil, &AIPath::SetTurnUntil, self, direction, arg6, arg7);
}
EI_VERIFY(0x005C7C40, SetTurnUntilVerify, 8, s_setTurnUntil);

// 005C7C70: MOV EAX,[ESP+4]; MOV EDX,[ESP+8] (8 bytes).
static void(__fastcall* s_setTarget)(AIPath*, void*, float, float);
static void __fastcall SetTargetVerify(AIPath* self, void*, float x, float y) {
    CheckHeader("AIPath::SetTarget", s_setTarget, &AIPath::SetTarget, self, x, y);
}
EI_VERIFY(0x005C7C70, SetTargetVerify, 8, s_setTarget);

// 005C7CB0: MOV EAX,[ESP+4]; MOV EDX,[ECX+0x10] (7 bytes).
static void(__fastcall* s_shiftTime)(AIPath*, void*, int);
static void __fastcall ShiftTimeVerify(AIPath* self, void*, int delta) {
    CheckHeader("AIPath::ShiftTime", s_shiftTime, &AIPath::ShiftTime, self, delta);
}
EI_VERIFY(0x005C7CB0, ShiftTimeVerify, 7, s_shiftTime);

// 005C7C90 starts with a 3-byte CMP and a JNZ: no trampoline. Ours only.
static void __fastcall ForgetObjectVerify(AIPath* self, void*, int id) {
    MotionOrder before = *OrderOf(self);
    self->ForgetObject(id);
    const MotionOrder& after = *OrderOf(self);
    bool forgets = before.kind == 3 && before.arg5 == static_cast<std::uint32_t>(id);
    bool ok = after.arg5 == (forgets ? 0xFFFFFFFFu : before.arg5) && after.kind == before.kind;
    ei::verify::Check("AIPath::ForgetObject", true, ok, "kind %d id %d", before.kind, id);
}
EI_VERIFY(0x005C7C90, ForgetObjectVerify, 0, s_unused);

// 005C7CE0 writes the shared step nodes: ours only, every step must carry the speed.
static int __fastcall SetStepSpeedsVerify(AIPath* self, void*, std::uint16_t speed) {
    int result = self->SetStepSpeeds(speed);
    bool ok = result == 0;
    for (PathCellList::Node* node = self->steps.head; node; node = node->next) ok = ok && node->data.flags == speed;
    ei::verify::Check("AIPath::SetStepSpeeds", true, ok, "speed %u", speed);
    return result;
}
EI_VERIFY(0x005C7CE0, SetStepSpeedsVerify, 0, s_unused);

// 005C7D00: FLD [0x0073FE4C] (6 bytes). Writes only `out`.
static Vector3*(__fastcall* s_destination)(AIPath*, void*, Vector3*);
static Vector3* __fastcall DestinationVerify(AIPath* self, void*, Vector3* out) {
    Vector3 expected = *out;
    Vector3* expectedResult = s_destination(self, nullptr, &expected);
    Vector3* result = self->Destination(out);
    bool same = expectedResult == &expected && result == out && std::memcmp(&expected, out, sizeof(expected)) == 0;
    ei::verify::Check("AIPath::Destination", true, same, "(%g,%g) vs (%g,%g)", expected.x, expected.y, out->x, out->y);
    return result;
}
EI_VERIFY(0x005C7D00, DestinationVerify, 6, s_destination);

// ---- The unit's save (A-W11) ----------------------------------------------------------------------

namespace {
// A stand-in stream whose slot 2 (Write) records the bytes.
std::vector<std::uint8_t>* s_bytes;
void __fastcall RecordWrite(void*, void*, const void* buffer, int size) {
    auto* p = static_cast<const std::uint8_t*>(buffer);
    s_bytes->insert(s_bytes->end(), p, p + size);
}
void* s_streamVtable[8];
struct StandIn {
    void** vtable;
};

using SaveStateFn = void(__fastcall*)(UnitMotion*, void*, CStream*);
SaveStateFn s_saveState;
} // namespace

// 005C79B0: PUSH ECX; PUSH ESI; MOV ESI,[ESP+0xC] (6 bytes). Differential: the original, then ours,
// into the stand-in stream; then ours into the real one. Every save with units (world_load).
static void __fastcall SaveStateVerify(UnitMotion* self, void*, CStream* stream) {
    std::vector<std::uint8_t> expected, actual;
    s_streamVtable[2] = reinterpret_cast<void*>(&RecordWrite);
    StandIn standIn{s_streamVtable};
    s_bytes = &expected;
    s_saveState(self, nullptr, reinterpret_cast<CStream*>(&standIn));
    s_bytes = &actual;
    self->SaveState(reinterpret_cast<CStream*>(&standIn));
    ei::verify::Check("UnitMotion::SaveState", true, actual == expected, "%d bytes (original %d)",
                      static_cast<int>(actual.size()), static_cast<int>(expected.size()));
    self->SaveState(stream);
}
EI_VERIFY(0x005C79B0, SaveStateVerify, 6, s_saveState);

// 005C7860: reads the real stream, so ours only: no segment, every array within its storage. Every
// load with units (world_load).
static void __fastcall LoadStateVerify(UnitMotion* self, void*, CStream* stream) {
    self->LoadState(stream);
    bool ok = self->segment == -1 && self->points.size <= self->points.maxSize &&
              self->waypoints.size <= self->waypoints.maxSize && self->stepSpeeds.size <= self->stepSpeeds.maxSize;
    ei::verify::Check("UnitMotion::LoadState", true, ok, "%d points, %d waypoints, %d steps", self->points.size,
                      self->waypoints.size, self->stepSpeeds.size);
}
EI_VERIFY(0x005C7860, LoadStateVerify, 0, s_unused);

// 005C6E30 MotionWaypointConstruct (A-W14; MOV EDX,[ESP+8]; MOV EAX,ECX: 6 bytes): the original
// into a scratch waypoint, ours into the real one; the 12 bytes and the returned pointers compared.
// No caller is left: UnitMotion::Build (rewritten) builds its corners in place.
namespace {
MotionWaypoint*(__fastcall* s_waypointConstruct)(MotionWaypoint*, void*, std::uint32_t, std::uint32_t);
MotionWaypoint* __fastcall WaypointConstructVerify(MotionWaypoint* self, void*, std::uint32_t xBits, std::uint32_t yBits) {
    MotionWaypoint scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    MotionWaypoint* expected = s_waypointConstruct(&scratch, nullptr, xBits, yBits);
    MotionWaypoint* result = MotionWaypointConstruct(self, nullptr, xBits, yBits);
    bool ok = expected == &scratch && result == self && std::memcmp(&scratch, self, sizeof(scratch)) == 0;
    ei::verify::Check("MotionWaypointConstruct", true, ok, "%08X %08X", xBits, yBits);
    return result;
}
} // namespace
EI_VERIFY(0x005C6E30, WaypointConstructVerify, 6, s_waypointConstruct);
