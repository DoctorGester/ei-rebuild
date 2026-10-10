// Checks for refptr_copies.cpp, as for the core RefPtr helpers (verify_core_refcount.cpp): the
// original and ours are dry-run on a stand-in (a copy of the object's 8-byte header whose vtable
// only records the destructor and shutdown calls); header, call records must agree. Then ours runs
// on the real object.
//
// The RefPtr::Destroy copies run the original through their trampolines. The release copies start
// with a short conditional jump (no room for a trampoline): their 28-byte body (identical in all 21,
// with only internal jumps) is copied to executable memory when the DLL loads, and that copy is the
// original.
#include "verify.h"

#include "../../src/game/core_refcount.h"

#include <cstring>

#include <windows.h>

void __fastcall RefPtrRelease(RefPtr* self);   // refptr_copies.cpp
RefPtr* __fastcall RefPtrShareReturn(RefPtr* self, void*, CRefObject* object);   // refptr_copies.cpp
void __fastcall PairReleaseSecond(void* self);                                    // refptr_copies.cpp
void __fastcall PairDestroySecond(void* self);                                    // refptr_copies.cpp
void __fastcall ReleaseThird(void* self);                                         // refptr_copies.cpp
RefPtr* __fastcall RefPtrAssignHolder(RefPtr* self, void*, const RefPtr* other);  // refptr_copies.cpp
RefPtr* __fastcall RefPtrAssignObject(RefPtr* self, void*, CRefObject* object);   // refptr_copies.cpp
bool __fastcall RefIsUnique(const CRefObject* object);                            // refptr_copies.cpp
int __fastcall RefDetachShutdown(CRefObject* object);                             // refptr_copies.cpp
void __fastcall ObjectHolderRelease(void** self);                                 // refptr_copies.cpp
void __fastcall ObjectHolderAssign(void** self, void*, void* object);             // refptr_copies.cpp
void __fastcall RefPtrClear(RefPtr* self);                                        // refptr_copies.cpp
RefPtr* __fastcall RefPtrNull(RefPtr* self);                                      // refptr_copies.cpp
RefPtr* __fastcall RefPtrFromRaw(RefPtr* self, CRefObject* object);               // refptr_copies.cpp
RefPtr* __fastcall RefPtrFromRawCounted(RefPtr* self, void*, CRefObject* object); // refptr_copies.cpp

namespace {

void* s_unused;

struct Fake {
    void* const* vtable;
    std::int16_t refs;
    std::uint8_t flags6, flags7;
};
static_assert(sizeof(Fake) == sizeof(CRefObject));

struct DryRun {
    Fake object;
    int log[4];
    int count;
    Fake clone;   // what slot 3 (clone) returns in a dry run
};
DryRun* g_run;

void Log(int event) {
    if (g_run->count < 4) g_run->log[g_run->count] = event;
    ++g_run->count;
}
void* __fastcall FakeDelete(Fake* self, void*, int flags) {
    Log(0x100 | flags);
    return self;
}
int __fastcall FakeShutdown(Fake* self, void*) {
    Log(0x200 | (self->refs & 0xF));
    return 0x5A;
}
void* const* FakeVtable();
Fake* __fastcall FakeClone(Fake* self, void*) {
    Log(0x300 | (self->refs & 0xF));
    g_run->clone.vtable = FakeVtable();
    return &g_run->clone;
}
void* const* FakeVtable() {
    static void* vtable[4] = {reinterpret_cast<void*>(&FakeDelete), nullptr, reinterpret_cast<void*>(&FakeShutdown),
                              reinterpret_cast<void*>(&FakeClone)};
    return vtable;
}

// Runs `fn` (original or ours) on a stand-in copy of `object` and returns the run.
template <typename Fn>
void DryRunOn(DryRun& run, const CRefObject* object, Fn fn) {
    std::memset(&run, 0, sizeof(run));
    if (object) std::memcpy(&run.object, object, sizeof(Fake));
    run.object.vtable = FakeVtable();
    RefPtr holder{object ? reinterpret_cast<CRefObject*>(&run.object) : nullptr};
    g_run = &run;
    fn(&holder);
    g_run = nullptr;
}

bool Same(const DryRun& a, const DryRun& b) {
    return std::memcmp(&a.object.refs, &b.object.refs, 4) == 0 && a.count == b.count &&
           std::memcmp(a.log, b.log, sizeof(int) * (a.count < 4 ? a.count : 4)) == 0;
}

using HolderFn = void(__fastcall*)(RefPtr*);

void CheckDestroy(const char* name, HolderFn original, RefPtr* self) {
    static DryRun expected, actual;
    CRefObject* object = self->ptr;
    DryRunOn(expected, object, [&](RefPtr* p) { original(p); });
    DryRunOn(actual, object, [](RefPtr* p) { p->Destroy(); });
    ei::verify::Check(name, true, Same(expected, actual), "%p", object);
    self->Destroy();
}

// The Destroy copies: PUSH ESI (1), PUSH EDI (1), MOV EDI,ECX (2), MOV ESI,[EDI] (2).
#define EI_DESTROY_COPY(addr)                                                                        \
    HolderFn s_destroy_##addr;                                                                       \
    void __fastcall DestroyVerify_##addr(RefPtr* self) { CheckDestroy("RefPtr::Destroy@" #addr, s_destroy_##addr, self); }

EI_DESTROY_COPY(00417A30)
EI_DESTROY_COPY(005587A0)
EI_DESTROY_COPY(0055EDA0)
EI_DESTROY_COPY(0055EDE0)
EI_DESTROY_COPY(00561A20)
EI_DESTROY_COPY(005BE240)
EI_DESTROY_COPY(00417A70)
EI_DESTROY_COPY(00419D00)
EI_DESTROY_COPY(00419F50)
EI_DESTROY_COPY(0041C910)
EI_DESTROY_COPY(0041C9D0)
EI_DESTROY_COPY(00421600)
EI_DESTROY_COPY(00421640)
EI_DESTROY_COPY(00421680)
EI_DESTROY_COPY(004216C0)
EI_DESTROY_COPY(00421700)
EI_DESTROY_COPY(00421740)
EI_DESTROY_COPY(00421780)
EI_DESTROY_COPY(004291A0)
EI_DESTROY_COPY(004291E0)
EI_DESTROY_COPY(00429220)
EI_DESTROY_COPY(00429260)
EI_DESTROY_COPY(0042A610)
EI_DESTROY_COPY(0047A3F0)
EI_DESTROY_COPY(0047C1E0)
EI_DESTROY_COPY(0047D560)
EI_DESTROY_COPY(00436430)
EI_DESTROY_COPY(00438AA0)
EI_DESTROY_COPY(00438B30)

// The release copies: one copy of the body serves as the original of all of them.
HolderFn CopyReleaseBody() {
    constexpr std::size_t kSize = 0x1C;
    void* copy = ::VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (copy) std::memcpy(copy, reinterpret_cast<const void*>(0x004141D0), kSize);
    return reinterpret_cast<HolderFn>(copy);
}
HolderFn s_releaseBody = CopyReleaseBody();

void __fastcall ReleaseVerify(RefPtr* self) {
    if (s_releaseBody) {
        static DryRun expected, actual;
        CRefObject* object = self->ptr;
        DryRunOn(expected, object, [](RefPtr* p) { s_releaseBody(p); });
        DryRunOn(actual, object, [](RefPtr* p) { RefPtrRelease(p); });
        ei::verify::Check("RefPtrRelease", true, Same(expected, actual), "%p", object);
    }
    RefPtrRelease(self);
}

// The share copies (RefPtrShareReturn): a dry run with the argument on the stand-in (a clone, when
// the object is copy-on-share, is a second stand-in); both stand-ins' headers, the calls, the
// returned holder and which stand-in the holder points to must agree.
// PUSH ESI (1), MOV ESI,ECX (2), MOV ECX,[ESP+8] (4).
using ShareFn = RefPtr*(__fastcall*)(RefPtr*, void*, CRefObject*);

int SharedIndex(const DryRun& run, const RefPtr& p) {
    if (!p.ptr) return 0;
    if (p.ptr == reinterpret_cast<const CRefObject*>(&run.object)) return 1;
    if (p.ptr == reinterpret_cast<const CRefObject*>(&run.clone)) return 2;
    return 3;
}

void CheckShare(const char* name, ShareFn original, RefPtr* self, CRefObject* object) {
    static DryRun expected, actual;
    int index[2];
    bool returned[2];
    DryRun* runs[2] = {&expected, &actual};
    for (int k = 0; k < 2; ++k) {
        DryRun& run = *runs[k];
        std::memset(&run, 0, sizeof(run));
        if (object) std::memcpy(&run.object, object, sizeof(Fake));
        run.object.vtable = FakeVtable();
        RefPtr holder{nullptr};
        CRefObject* arg = object ? reinterpret_cast<CRefObject*>(&run.object) : nullptr;
        g_run = &run;
        RefPtr* r = k == 0 ? original(&holder, nullptr, arg) : RefPtrShareReturn(&holder, nullptr, arg);
        g_run = nullptr;
        returned[k] = r == &holder;
        index[k] = SharedIndex(run, holder);
    }
    bool same = Same(expected, actual) && std::memcmp(&expected.clone.refs, &actual.clone.refs, 4) == 0 &&
                index[0] == index[1] && returned[0] && returned[1];
    ei::verify::Check(name, true, same, "%p", object);
    RefPtrShareReturn(self, nullptr, object);
}

#define EI_SHARE_COPY(addr)                                                                      \
    ShareFn s_share_##addr;                                                                      \
    RefPtr* __fastcall ShareVerify_##addr(RefPtr* self, void*, CRefObject* object) {             \
        CheckShare("RefPtrShareReturn@" #addr, s_share_##addr, self, object);                      \
        return self;                                                                             \
    }

EI_SHARE_COPY(004292F0)
EI_SHARE_COPY(00429350)
EI_SHARE_COPY(00429430)
EI_SHARE_COPY(00429710)
EI_SHARE_COPY(00429930)
EI_SHARE_COPY(00429990)
EI_SHARE_COPY(004299F0)
EI_SHARE_COPY(00429A50)
EI_SHARE_COPY(00429AB0)

} // namespace

EI_VERIFY(0x004292F0, ShareVerify_004292F0, 7, s_share_004292F0);
EI_VERIFY(0x00429350, ShareVerify_00429350, 7, s_share_00429350);
EI_VERIFY(0x00429430, ShareVerify_00429430, 7, s_share_00429430);
EI_VERIFY(0x00429710, ShareVerify_00429710, 7, s_share_00429710);
EI_VERIFY(0x00429930, ShareVerify_00429930, 7, s_share_00429930);
EI_VERIFY(0x00429990, ShareVerify_00429990, 7, s_share_00429990);
EI_VERIFY(0x004299F0, ShareVerify_004299F0, 7, s_share_004299F0);
EI_VERIFY(0x00429A50, ShareVerify_00429A50, 7, s_share_00429A50);
EI_VERIFY(0x00429AB0, ShareVerify_00429AB0, 7, s_share_00429AB0);

EI_VERIFY(0x00417A30, DestroyVerify_00417A30, 6, s_destroy_00417A30);
EI_VERIFY(0x005587A0, DestroyVerify_005587A0, 6, s_destroy_005587A0);   // A-U11 copies
EI_VERIFY(0x0055EDA0, DestroyVerify_0055EDA0, 6, s_destroy_0055EDA0);
EI_VERIFY(0x0055EDE0, DestroyVerify_0055EDE0, 6, s_destroy_0055EDE0);
EI_VERIFY(0x00417A70, DestroyVerify_00417A70, 6, s_destroy_00417A70);
EI_VERIFY(0x00419D00, DestroyVerify_00419D00, 6, s_destroy_00419D00);
EI_VERIFY(0x00419F50, DestroyVerify_00419F50, 6, s_destroy_00419F50);
EI_VERIFY(0x0041C910, DestroyVerify_0041C910, 6, s_destroy_0041C910);
EI_VERIFY(0x0041C9D0, DestroyVerify_0041C9D0, 6, s_destroy_0041C9D0);
EI_VERIFY(0x00421600, DestroyVerify_00421600, 6, s_destroy_00421600);
EI_VERIFY(0x00421640, DestroyVerify_00421640, 6, s_destroy_00421640);
EI_VERIFY(0x00421680, DestroyVerify_00421680, 6, s_destroy_00421680);
EI_VERIFY(0x004216C0, DestroyVerify_004216C0, 6, s_destroy_004216C0);
EI_VERIFY(0x00421700, DestroyVerify_00421700, 6, s_destroy_00421700);
EI_VERIFY(0x00421740, DestroyVerify_00421740, 6, s_destroy_00421740);
EI_VERIFY(0x00421780, DestroyVerify_00421780, 6, s_destroy_00421780);
EI_VERIFY(0x004291A0, DestroyVerify_004291A0, 6, s_destroy_004291A0);
EI_VERIFY(0x004291E0, DestroyVerify_004291E0, 6, s_destroy_004291E0);
EI_VERIFY(0x00429220, DestroyVerify_00429220, 6, s_destroy_00429220);
EI_VERIFY(0x00429260, DestroyVerify_00429260, 6, s_destroy_00429260);
EI_VERIFY(0x0042A610, DestroyVerify_0042A610, 6, s_destroy_0042A610);
EI_VERIFY(0x0047A3F0, DestroyVerify_0047A3F0, 6, s_destroy_0047A3F0);
EI_VERIFY(0x0047C1E0, DestroyVerify_0047C1E0, 6, s_destroy_0047C1E0);
EI_VERIFY(0x0047D560, DestroyVerify_0047D560, 6, s_destroy_0047D560);
EI_VERIFY(0x00436430, DestroyVerify_00436430, 6, s_destroy_00436430);
EI_VERIFY(0x00438AA0, DestroyVerify_00438AA0, 6, s_destroy_00438AA0);
EI_VERIFY(0x00438B30, DestroyVerify_00438B30, 6, s_destroy_00438B30);
EI_VERIFY(0x00561A20, DestroyVerify_00561A20, 6, s_destroy_00561A20);   // A-W12 copies
EI_VERIFY(0x005BE240, DestroyVerify_005BE240, 6, s_destroy_005BE240);

EI_VERIFY(0x004141D0, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x00415550, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x004169F0, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x00417080, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0041BC20, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0041C890, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0041C8D0, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0041C8F0, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0041DDA0, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0041E670, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0041E830, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x00412260, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x004122B0, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x00427240, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x00427460, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x00427540, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x004275E0, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0042A9E0, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x004781D0, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0047A430, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0047CA80, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0047D170, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0047D7B0, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x00438A10, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x00439910, ReleaseVerify, 0, s_unused);
EI_VERIFY(0x0059B4B0, ReleaseVerify, 0, s_unused);   // A-W12 copy

// The pair releases: the 29-byte body of 004215A0 (identical in all four; only internal jumps)
// copied to executable memory is the original; dry runs on a stand-in held at +4.
namespace {
using PairFn = void(__fastcall*)(void*);
PairFn CopyPairReleaseBody() {
    constexpr std::size_t kSize = 0x1D;
    void* copy = ::VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (copy) std::memcpy(copy, reinterpret_cast<const void*>(0x004215A0), kSize);
    return reinterpret_cast<PairFn>(copy);
}
PairFn s_pairBody = CopyPairReleaseBody();

void __fastcall PairReleaseVerify(void* self) {
    if (s_pairBody) {
        static DryRun expected, actual;
        CRefObject* object = reinterpret_cast<RefPtr*>(static_cast<std::uint8_t*>(self) + 4)->ptr;
        DryRunOn(expected, object, [](RefPtr* p) { s_pairBody(reinterpret_cast<std::uint8_t*>(p) - 4); });
        DryRunOn(actual, object, [](RefPtr* p) { PairReleaseSecond(reinterpret_cast<std::uint8_t*>(p) - 4); });
        ei::verify::Check("PairReleaseSecond", true, Same(expected, actual), "%p", object);
    }
    PairReleaseSecond(self);
}

// The holder clears and null constructors: side by side on a copy of the holder.
// MOV [ECX],0 (6) / MOV EAX,ECX (2), MOV [EAX],0 (6).
#define EI_CLEAR_COPY(addr)                                                                      \
    void(__fastcall* s_clear_##addr)(RefPtr*);                                                   \
    void __fastcall ClearVerify_##addr(RefPtr* self) {                                           \
        RefPtr expected = *self;                                                                 \
        s_clear_##addr(&expected);                                                               \
        RefPtrClear(self);                                                                       \
        ei::verify::Check("RefPtrClear@" #addr, expected.ptr, self->ptr, "");                    \
    }
#define EI_NULL_COPY(addr)                                                                       \
    RefPtr*(__fastcall* s_null_##addr)(RefPtr*);                                                 \
    RefPtr* __fastcall NullVerify_##addr(RefPtr* self) {                                         \
        RefPtr expected = *self;                                                                 \
        RefPtr* returned = s_null_##addr(&expected);                                             \
        RefPtr* result = RefPtrNull(self);                                                       \
        ei::verify::Check("RefPtrNull@" #addr, true,                                             \
                          returned == &expected && result == self && expected.ptr == self->ptr, ""); \
        return result;                                                                           \
    }
// PUSH ECX (1), MOV EAX,ECX (2), MOV [ESP],0 (8).
#define EI_RAW_COPY(addr)                                                                        \
    RefPtr*(__fastcall* s_raw_##addr)(RefPtr*, CRefObject*);                                    \
    RefPtr* __fastcall RawVerify_##addr(RefPtr* self, CRefObject* object) {                      \
        RefPtr expected = *self;                                                                 \
        RefPtr* returned = s_raw_##addr(&expected, object);                                      \
        RefPtr* result = RefPtrFromRaw(self, object);                                            \
        ei::verify::Check("RefPtrFromRaw@" #addr, true,                                          \
                          returned == &expected && result == self && expected.ptr == self->ptr, ""); \
        return result;                                                                           \
    }

EI_CLEAR_COPY(00421360) EI_CLEAR_COPY(00421370) EI_CLEAR_COPY(00421380) EI_CLEAR_COPY(00421390)
EI_CLEAR_COPY(004213A0) EI_CLEAR_COPY(004213B0) EI_CLEAR_COPY(004213C0) EI_CLEAR_COPY(004213D0)
EI_CLEAR_COPY(004213E0) EI_CLEAR_COPY(004213F0) EI_CLEAR_COPY(004214B0) EI_CLEAR_COPY(004214C0)
EI_CLEAR_COPY(004214D0) EI_CLEAR_COPY(004214E0) EI_CLEAR_COPY(004214F0) EI_CLEAR_COPY(00421550)
EI_CLEAR_COPY(00438A00)
EI_NULL_COPY(0041FF50) EI_NULL_COPY(0041FF60) EI_NULL_COPY(0041FF70) EI_NULL_COPY(0041FF80)
EI_NULL_COPY(0041FFC0) EI_NULL_COPY(0041FFD0) EI_NULL_COPY(0041FFE0) EI_NULL_COPY(00438A90)
EI_NULL_COPY(00438F50) EI_NULL_COPY(00478FB0) EI_NULL_COPY(005FC1C0)
EI_NULL_COPY(005683A0) EI_NULL_COPY(0058DFE0)
EI_RAW_COPY(00427D50) EI_RAW_COPY(00427FD0) EI_RAW_COPY(00427FE0) EI_RAW_COPY(00428030)
EI_RAW_COPY(00428040) EI_RAW_COPY(00428050) EI_RAW_COPY(00428060) EI_RAW_COPY(00428070)
EI_RAW_COPY(00428080) EI_RAW_COPY(00428A10) EI_RAW_COPY(00428C00) EI_RAW_COPY(00428C10)
EI_RAW_COPY(00428C20) EI_RAW_COPY(00428C30) EI_RAW_COPY(00428C40) EI_RAW_COPY(00428DE0)

// The counted raw holders add a reference: ours only (the copy points at the object).
RefPtr* __fastcall RawCountedVerify(RefPtr* self, void*, CRefObject* object) {
    RefPtr* result = RefPtrFromRawCounted(self, nullptr, object);
    ei::verify::Check("RefPtrFromRawCounted", true, result == self && self->ptr == object, "");
    return result;
}
} // namespace

EI_VERIFY(0x004215A0, PairReleaseVerify, 0, s_unused);
EI_VERIFY(0x00438A30, PairReleaseVerify, 0, s_unused);
EI_VERIFY(0x00438A50, PairReleaseVerify, 0, s_unused);
EI_VERIFY(0x00438A70, PairReleaseVerify, 0, s_unused);
EI_VERIFY(0x0040A770, RawCountedVerify, 0, s_unused);
EI_VERIFY(0x0041DD50, RawCountedVerify, 0, s_unused);
EI_VERIFY(0x00421360, ClearVerify_00421360, 6, s_clear_00421360);
EI_VERIFY(0x00421370, ClearVerify_00421370, 6, s_clear_00421370);
EI_VERIFY(0x00421380, ClearVerify_00421380, 6, s_clear_00421380);
EI_VERIFY(0x00421390, ClearVerify_00421390, 6, s_clear_00421390);
EI_VERIFY(0x004213A0, ClearVerify_004213A0, 6, s_clear_004213A0);
EI_VERIFY(0x004213B0, ClearVerify_004213B0, 6, s_clear_004213B0);
EI_VERIFY(0x004213C0, ClearVerify_004213C0, 6, s_clear_004213C0);
EI_VERIFY(0x004213D0, ClearVerify_004213D0, 6, s_clear_004213D0);
EI_VERIFY(0x004213E0, ClearVerify_004213E0, 6, s_clear_004213E0);
EI_VERIFY(0x004213F0, ClearVerify_004213F0, 6, s_clear_004213F0);
EI_VERIFY(0x004214B0, ClearVerify_004214B0, 6, s_clear_004214B0);
EI_VERIFY(0x004214C0, ClearVerify_004214C0, 6, s_clear_004214C0);
EI_VERIFY(0x004214D0, ClearVerify_004214D0, 6, s_clear_004214D0);
EI_VERIFY(0x004214E0, ClearVerify_004214E0, 6, s_clear_004214E0);
EI_VERIFY(0x004214F0, ClearVerify_004214F0, 6, s_clear_004214F0);
EI_VERIFY(0x00421550, ClearVerify_00421550, 6, s_clear_00421550);
EI_VERIFY(0x00438A00, ClearVerify_00438A00, 6, s_clear_00438A00);
EI_VERIFY(0x0041FF50, NullVerify_0041FF50, 8, s_null_0041FF50);
EI_VERIFY(0x005FC1C0, NullVerify_005FC1C0, 8, s_null_005FC1C0);
EI_VERIFY(0x0041FF60, NullVerify_0041FF60, 8, s_null_0041FF60);
EI_VERIFY(0x0041FF70, NullVerify_0041FF70, 8, s_null_0041FF70);
EI_VERIFY(0x0041FF80, NullVerify_0041FF80, 8, s_null_0041FF80);
EI_VERIFY(0x0041FFC0, NullVerify_0041FFC0, 8, s_null_0041FFC0);
EI_VERIFY(0x0041FFD0, NullVerify_0041FFD0, 8, s_null_0041FFD0);
EI_VERIFY(0x0041FFE0, NullVerify_0041FFE0, 8, s_null_0041FFE0);
EI_VERIFY(0x005683A0, NullVerify_005683A0, 8, s_null_005683A0);   // A-W12 copies
EI_VERIFY(0x0058DFE0, NullVerify_0058DFE0, 8, s_null_0058DFE0);
EI_VERIFY(0x00438A90, NullVerify_00438A90, 8, s_null_00438A90);
EI_VERIFY(0x00438F50, NullVerify_00438F50, 8, s_null_00438F50);
EI_VERIFY(0x00478FB0, NullVerify_00478FB0, 8, s_null_00478FB0);
EI_VERIFY(0x00427D50, RawVerify_00427D50, 11, s_raw_00427D50);
EI_VERIFY(0x00427FD0, RawVerify_00427FD0, 11, s_raw_00427FD0);
EI_VERIFY(0x00427FE0, RawVerify_00427FE0, 11, s_raw_00427FE0);
EI_VERIFY(0x00428030, RawVerify_00428030, 11, s_raw_00428030);
EI_VERIFY(0x00428040, RawVerify_00428040, 11, s_raw_00428040);
EI_VERIFY(0x00428050, RawVerify_00428050, 11, s_raw_00428050);
EI_VERIFY(0x00428060, RawVerify_00428060, 11, s_raw_00428060);
EI_VERIFY(0x00428070, RawVerify_00428070, 11, s_raw_00428070);
EI_VERIFY(0x00428080, RawVerify_00428080, 11, s_raw_00428080);
EI_VERIFY(0x00428A10, RawVerify_00428A10, 11, s_raw_00428A10);
EI_VERIFY(0x00428C00, RawVerify_00428C00, 11, s_raw_00428C00);
EI_VERIFY(0x00428C10, RawVerify_00428C10, 11, s_raw_00428C10);
EI_VERIFY(0x00428C20, RawVerify_00428C20, 11, s_raw_00428C20);
EI_VERIFY(0x00428C30, RawVerify_00428C30, 11, s_raw_00428C30);
EI_VERIFY(0x00428C40, RawVerify_00428C40, 11, s_raw_00428C40);
EI_VERIFY(0x00428DE0, RawVerify_00428DE0, 11, s_raw_00428DE0);

// The owner's release at +4: dry runs on a stand-in held at +4; the 64-byte body of 00421560
// (internal jumps only) copied to executable memory is the original.
namespace {
using PairDestroyFn = void(__fastcall*)(void*);
PairDestroyFn CopyPairDestroyBody() {
    constexpr std::size_t kSize = 0x40;
    void* copy = ::VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (copy) std::memcpy(copy, reinterpret_cast<const void*>(0x00421560), kSize);
    return reinterpret_cast<PairDestroyFn>(copy);
}
PairDestroyFn s_pairDestroyBody = CopyPairDestroyBody();

void __fastcall PairDestroyVerify(void* self) {
    if (s_pairDestroyBody) {
        static DryRun expected, actual;
        CRefObject* object = reinterpret_cast<RefPtr*>(static_cast<std::uint8_t*>(self) + 4)->ptr;
        DryRunOn(expected, object, [](RefPtr* p) { s_pairDestroyBody(reinterpret_cast<std::uint8_t*>(p) - 4); });
        DryRunOn(actual, object, [](RefPtr* p) { PairDestroySecond(reinterpret_cast<std::uint8_t*>(p) - 4); });
        ei::verify::Check("PairDestroySecond", true, Same(expected, actual), "%p", object);
    }
    PairDestroySecond(self);
}

// The release at +8: as the releases at +4 (the 29-byte body of 0043A120).
PairFn CopyThirdReleaseBody() {
    constexpr std::size_t kSize = 0x1D;
    void* copy = ::VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (copy) std::memcpy(copy, reinterpret_cast<const void*>(0x0043A120), kSize);
    return reinterpret_cast<PairFn>(copy);
}
PairFn s_thirdBody = CopyThirdReleaseBody();
void __fastcall ThirdReleaseVerify(void* self) {
    if (s_thirdBody) {
        static DryRun expected, actual;
        CRefObject* object = reinterpret_cast<RefPtr*>(static_cast<std::uint8_t*>(self) + 8)->ptr;
        DryRunOn(expected, object, [](RefPtr* p) { s_thirdBody(reinterpret_cast<std::uint8_t*>(p) - 8); });
        DryRunOn(actual, object, [](RefPtr* p) { ReleaseThird(reinterpret_cast<std::uint8_t*>(p) - 8); });
        ei::verify::Check("ReleaseThird", true, Same(expected, actual), "%p", object);
    }
    ReleaseThird(self);
}

// The assignments, the holder of engine objects: ours only (they release and share).
RefPtr* __fastcall AssignHolderVerify(RefPtr* self, void*, const RefPtr* other) {
    RefPtr* result = RefPtrAssignHolder(self, nullptr, other);
    ei::verify::Check("RefPtrAssignHolder", true, result == self, "");
    return result;
}
RefPtr* __fastcall AssignObjectVerify(RefPtr* self, void*, CRefObject* object) {
    RefPtr* result = RefPtrAssignObject(self, nullptr, object);
    ei::verify::Check("RefPtrAssignObject", true, result == self, "");
    return result;
}
// 00429D80: XOR EAX,EAX (2), CMP word [ECX+4],1 (5). Side by side (it only reads).
bool(__fastcall* s_isUnique)(const CRefObject*);
bool __fastcall IsUniqueVerify(const CRefObject* object) {
    bool expected = s_isUnique(object);
    bool actual = RefIsUnique(object);
    ei::verify::Check("RefIsUnique", expected, actual, "");
    return actual;
}
int __fastcall DetachShutdownVerify(CRefObject* object) {
    int result = RefDetachShutdown(object);
    ei::verify::Check("RefDetachShutdown", true, (object->flags7_ & CRefObject::kDetached) != 0, "");
    return result;
}
void __fastcall HolderReleaseVerify(void** self) {
    ObjectHolderRelease(self);
    ei::verify::Check("ObjectHolderRelease", true, true, "");
}
void __fastcall HolderAssignVerify(void** self, void*, void* object) {
    ObjectHolderAssign(self, nullptr, object);
    ei::verify::Check("ObjectHolderAssign", true, *self == object, "");
}
} // namespace

EI_VERIFY(0x00421560, PairDestroyVerify, 0, s_unused);
EI_VERIFY(0x004215C0, PairDestroyVerify, 0, s_unused);
EI_VERIFY(0x00438AE0, PairDestroyVerify, 0, s_unused);
EI_VERIFY(0x0043A120, ThirdReleaseVerify, 0, s_unused);
EI_VERIFY(0x0042A770, AssignHolderVerify, 0, s_unused);
EI_VERIFY(0x00438110, AssignObjectVerify, 0, s_unused);
EI_VERIFY(0x00429D80, IsUniqueVerify, 7, s_isUnique);
EI_VERIFY(0x00429D90, DetachShutdownVerify, 0, s_unused);
EI_VERIFY(0x00421350, HolderReleaseVerify, 0, s_unused);
EI_VERIFY(0x00511B80, HolderReleaseVerify, 0, s_unused);
EI_VERIFY(0x0051E3D0, HolderReleaseVerify, 0, s_unused);
EI_VERIFY(0x0052D670, HolderReleaseVerify, 0, s_unused);
EI_VERIFY(0x00552B80, HolderReleaseVerify, 0, s_unused);
EI_VERIFY(0x00556AA0, HolderReleaseVerify, 0, s_unused);
EI_VERIFY(0x00598960, HolderReleaseVerify, 0, s_unused);   // A-W12 copies
EI_VERIFY(0x00598970, HolderReleaseVerify, 0, s_unused);
EI_VERIFY(0x00598980, HolderReleaseVerify, 0, s_unused);
EI_VERIFY(0x00429B50, HolderAssignVerify, 0, s_unused);
EI_VERIFY(0x00511B90, HolderAssignVerify, 0, s_unused);
EI_VERIFY(0x0052C690, HolderAssignVerify, 0, s_unused);
EI_VERIFY(0x00556C40, HolderAssignVerify, 0, s_unused);
EI_VERIFY(0x00556C60, HolderAssignVerify, 0, s_unused);
