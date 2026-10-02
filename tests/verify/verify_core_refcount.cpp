// Checks for the reference-counting helpers. They call the objects' virtual destructor, shutdown and
// clone, so neither side can be run twice on real objects. Instead both the original and ours are
// first dry-run on stand-ins: copies of the 8-byte headers of the objects involved, whose vtable only
// records the calls. The headers, the call records and the resulting pointer must agree. Then ours
// runs for real.
#include "verify.h"

#include "../../src/game/core_refcount.h"

#include <cstring>

namespace {

struct Fake {
    void* const* vtable;
    std::int16_t refs;
    std::uint8_t flags6, flags7;
};
static_assert(sizeof(Fake) == sizeof(CRefObject));

// One dry run: up to two objects (index 0, 1) and the clone the clone slot hands out (index 2).
struct DryRun {
    Fake objects[3];
    int log[8];
    int count;
};
DryRun* g_run;

int IndexOf(const Fake* f) { return static_cast<int>(f - g_run->objects); }
void Log(int event) {
    if (g_run->count < 8) g_run->log[g_run->count] = event;
    ++g_run->count;
}
void* __fastcall FakeDelete(Fake* self, void*, int flags) {
    Log(0x100 | IndexOf(self) << 4 | flags);
    return self;
}
int __fastcall FakeShutdown(Fake* self, void*) {
    Log(0x200 | IndexOf(self) << 4 | (self->refs & 0xF));
    return 0x5A;
}
Fake* __fastcall FakeClone(Fake* self, void*) {
    Log(0x300 | IndexOf(self) << 4);
    return &g_run->objects[2];
}
void* const* FakeVtable() {
    static void* vtable[4] = {reinterpret_cast<void*>(&FakeDelete), nullptr, reinterpret_cast<void*>(&FakeShutdown),
                              reinterpret_cast<void*>(&FakeClone)};
    return vtable;
}

// Sets up a dry run from the real objects a and b (either may be null).
void Prepare(DryRun& run, const CRefObject* a, const CRefObject* b) {
    std::memset(&run, 0, sizeof(run));
    const CRefObject* real[2] = {a, b};
    for (int i = 0; i < 2; ++i) {
        if (real[i]) std::memcpy(&run.objects[i], real[i], sizeof(Fake));
        run.objects[i].vtable = FakeVtable();
    }
    run.objects[2].vtable = FakeVtable();   // a fresh clone: no references, no flags
}
CRefObject* AsReal(DryRun& run, int i) { return reinterpret_cast<CRefObject*>(&run.objects[i]); }
int PointerIndex(DryRun& run, const void* p) {
    for (int i = 0; i < 3; ++i)
        if (p == &run.objects[i]) return i;
    return p ? 9 : -1;
}
bool SameRuns(DryRun& e, DryRun& a) {
    for (int i = 0; i < 3; ++i)
        if (std::memcmp(&e.objects[i].refs, &a.objects[i].refs, 4) != 0) return false;
    return e.count == a.count && std::memcmp(e.log, a.log, sizeof(int) * (e.count < 8 ? e.count : 8)) == 0;
}

// Runs `original` and `ours` on dry runs set up from a and b; `op` gets the run and the function.
template <typename Op>
bool DryCompare(const CRefObject* a, const CRefObject* b, Op op, int* expectedResult = nullptr) {
    static DryRun expected, actual;
    Prepare(expected, a, b);
    Prepare(actual, a, b);
    g_run = &expected;
    int re = op(expected, true);
    g_run = &actual;
    int ra = op(actual, false);
    g_run = nullptr;
    if (expectedResult) *expectedResult = re;
    return re == ra && SameRuns(expected, actual);
}

// 0040A3A0: DEC word [ECX+4] (4), MOV AX,[ECX+4] (4).
void(__fastcall* s_release)(CRefObject*);
void __fastcall ReleaseVerify(CRefObject* self) {
    std::int16_t refs = self->refs_;
    bool same = DryCompare(self, nullptr, [](DryRun& run, bool original) {
        if (original) s_release(AsReal(run, 0));
        else AsReal(run, 0)->Release();
        return 0;
    });
    ei::verify::Check("CRefObject::Release", true, same, "refs %d", refs);
    self->Release();
}

// 00417DF0: PUSH ESI (1), MOV ESI,ECX (2), MOV EAX,[ESI] (2).
int(__fastcall* s_shutdown)(CRefObject*);
int __fastcall ShutdownVerify(CRefObject* self) {
    std::int16_t refs = self->refs_;
    bool same = DryCompare(self, nullptr, [](DryRun& run, bool original) {
        return original ? s_shutdown(AsReal(run, 0)) : AsReal(run, 0)->Shutdown();
    });
    ei::verify::Check("CRefObject::Shutdown", true, same, "refs %d", refs);
    return self->Shutdown();
}

// RefPtr operations: the holder in the dry run points at stand-in 0, the argument is stand-in 1.
// 00419CD0: MOV EDX,[ESP+4] (4), MOV EAX,[ECX] (2).
void(__fastcall* s_assign)(RefPtr*, void*, CRefObject*);
void __fastcall AssignVerify(RefPtr* self, void*, CRefObject* object) {
    CRefObject* old = self->ptr;
    bool same = DryCompare(old, object, [old, object](DryRun& run, bool original) {
        RefPtr p{old ? AsReal(run, 0) : nullptr};
        CRefObject* arg = object ? AsReal(run, 1) : nullptr;
        if (old == object) arg = p.ptr;   // the same object on both sides
        if (original) s_assign(&p, nullptr, arg);
        else p.Assign(arg);
        return PointerIndex(run, p.ptr);
    });
    ei::verify::Check("RefPtr::Assign", true, same, "%p -> %p", old, object);
    self->Assign(object);
}

// 00418BE0: PUSH ESI (1), MOV ESI,ECX (2), MOV ECX,[ESP+8] (4).
void(__fastcall* s_share)(RefPtr*, void*, CRefObject*);
void __fastcall ShareVerify(RefPtr* self, void*, CRefObject* object) {
    bool same = DryCompare(nullptr, object, [object](DryRun& run, bool original) {
        RefPtr p{nullptr};
        CRefObject* arg = object ? AsReal(run, 1) : nullptr;
        if (original) s_share(&p, nullptr, arg);
        else p.Share(arg);
        return PointerIndex(run, p.ptr);
    });
    ei::verify::Check("RefPtr::Share", true, same, "%p", object);
    self->Share(object);
}

// 00413270: PUSH ESI (1), PUSH EDI (1), MOV EDI,ECX (2), MOV ESI,[EDI] (2).
void(__fastcall* s_destroy)(RefPtr*);
void __fastcall DestroyVerify(RefPtr* self) {
    CRefObject* object = self->ptr;
    bool same = DryCompare(object, nullptr, [object](DryRun& run, bool original) {
        RefPtr p{object ? AsReal(run, 0) : nullptr};
        if (original) s_destroy(&p);
        else p.Destroy();
        return PointerIndex(run, p.ptr);
    });
    ei::verify::Check("RefPtr::Destroy", true, same, "%p", object);
    self->Destroy();
}

} // namespace

EI_VERIFY(0x0040A3A0, ReleaseVerify, 8, s_release);
EI_VERIFY(0x00417DF0, ShutdownVerify, 5, s_shutdown);
EI_VERIFY(0x00419CD0, AssignVerify, 6, s_assign);
EI_VERIFY(0x00418BE0, ShareVerify, 7, s_share);
EI_VERIFY(0x00413270, DestroyVerify, 6, s_destroy);
