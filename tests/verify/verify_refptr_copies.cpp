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
