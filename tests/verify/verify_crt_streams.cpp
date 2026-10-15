// Checks for MSVC 6's streams (src/lib/crt_iostream.cpp, S4d): streambuf's constructor, destructors and
// default virtuals, ios's constructor, destructors and init, ostream's constructor and destructors.
// - The streambuf bodies run on two scratch streambufs (Recorder: its own virtuals record what the base
//   class's bodies ask of them, underflow serves a script, overflow / seekoff can fail): the original
//   (through its trampoline) on one, ours on the other, from the same state; the fields (pointers as
//   offsets into each one's own memory), that memory, the recorded calls and the result must match. Each
//   game call is shadowed that way from the live streambuf's state (when its pointers lie in its reserve
//   area; the console's xsputn on every console line), then ours runs on it; a start-up battery covers
//   8 states x 5 behaviours x the operations.
// - ios / ostream: scratch objects (the bytes before the call the same on both sides), compared byte for
//   byte but for the critical sections (each must be initialised or deleted as in the original) and with
//   the same change of the live-ios count 007CAEC8; an owned streambuf must be deleted once. The game's
//   calls (the eight console ostreams' destructors at exit, in the hook build) are shadowed on copies.
// - The two deleting destructors without room for a trampoline (006FCAB6, 006FCE0F) are compared with the
//   original destructor plus operator delete, as they are built; doallocate's reserve and the heap
//   objects are compared by size and ownership, not address.
#include "verify.h"

#include "../../src/hook/log.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/crt_iostream.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace {

using ei::verify::Check;
using libcrt::Ios;
using libcrt::Ostream;
using libcrt::Streambuf;

constexpr int kEof = -1;
constexpr int kArea = 0x800;      // a scratch streambuf's memory (a live reserve area is copied to its start)
constexpr int kLiveArea = 0x600;  // the largest live reserve area shadowed
constexpr int kGetArea = 0x700;   // where underflow puts what it reads (three bytes at a time)

int s_cases;

// The scratch objects are shared: the game's threads take turns. An original that calls a function hooked
// here (~streambuf's sync, ostream()'s ios() and init, the ostream deleting destructor's two) runs ours
// there, unshadowed.
std::mutex& Mutex() {
    static std::mutex* mutex = new std::mutex;   // never destroyed: the streams are destroyed at exit
    return *mutex;
}
thread_local int t_inside;
bool Outermost() { return t_inside == 0; }
struct Shadowing {
    std::lock_guard<std::mutex> hold{Mutex()};
    Shadowing() { ++t_inside; }
    ~Shadowing() { --t_inside; }
};

// A streambuf whose own virtuals record what the base class's bodies ask of them.
class Recorder : public Streambuf {
public:
    std::string calls;
    bool failOverflow = false;
    bool failSeek = false;
    std::string input;   // what underflow serves
    std::size_t next = 0;
    char area[kArea] = {};
    static int deleted;

    ~Recorder() override { ++deleted; }
    int Sync() override {
        calls += 'S';
        return 0;
    }
    Streambuf* Setbuf(char*, int length) override {
        calls += "B" + std::to_string(length);
        return this;
    }
    long Seekoff(long offset, int direction, int mode) override {
        calls += "K" + std::to_string(offset) + "," + std::to_string(direction) + "," + std::to_string(mode);
        return failSeek ? kEof : 0;
    }
    long Seekpos(long, int) override {
        calls += 'P';
        return kEof;
    }
    int Xsputn(const char*, int count) override {
        calls += "W" + std::to_string(count);
        return 0;
    }
    int Xsgetn(char*, int count) override {
        calls += "G" + std::to_string(count);
        return 0;
    }
    int Overflow(int c) override {
        calls += "O" + std::to_string(c);
        return failOverflow ? kEof : c & 0xFF;
    }
    // Unbuffered: the next byte. Buffered: what the get area still holds, else up to three bytes into it.
    int Underflow() override {
        calls += 'U';
        if (!unbuffered && gptr && gptr < egptr) return static_cast<unsigned char>(*gptr);
        if (next >= input.size()) return kEof;
        if (unbuffered) return static_cast<unsigned char>(input[next++]);
        const std::size_t n = (std::min)(input.size() - next, std::size_t{3});
        std::memcpy(area + kGetArea, input.data() + next, n);
        next += n;
        eback = gptr = area + kGetArea;
        egptr = gptr + n;
        return static_cast<unsigned char>(*gptr);
    }
    int Pbackfail(int c) override {
        calls += "F" + std::to_string(c);
        return kEof;
    }
    int Doallocate() override {
        calls += 'A';
        return 0;
    }
};
int Recorder::deleted;

CRITICAL_SECTION* Section(std::uint8_t* lock) { return reinterpret_cast<CRITICAL_SECTION*>(lock); }
const void*& VtableOf(void* object) { return *reinterpret_cast<const void**>(object); }

// A streambuf's state: pointers as offsets into the scratch memory (-1: null).
struct State {
    int unbuffered = 0;
    int lastChar = kEof;
    int base = -1, ebuf = -1, pbase = -1, pptr = -1, epptr = -1, eback = -1, gptr = -1, egptr = -1;
    std::string bytes;   // the memory's first bytes
};
struct Behaviour {
    bool failOverflow = false;
    bool failSeek = false;
    const char* input = "";
};

char* At(Recorder& r, int offset) { return offset < 0 ? nullptr : r.area + offset; }

void Apply(Recorder& r, const State& s, const Behaviour& b) {
    std::memset(r.area, 0, sizeof r.area);
    std::memcpy(r.area, s.bytes.data(), s.bytes.size());
    r.allocated = 0;
    r.unbuffered = s.unbuffered;
    r.lastChar = s.lastChar;
    r.base = At(r, s.base);
    r.ebuf = At(r, s.ebuf);
    r.pbase = At(r, s.pbase);
    r.pptr = At(r, s.pptr);
    r.epptr = At(r, s.epptr);
    r.eback = At(r, s.eback);
    r.gptr = At(r, s.gptr);
    r.egptr = At(r, s.egptr);
    r.lockFlag = 0;
    r.calls.clear();
    r.failOverflow = b.failOverflow;
    r.failSeek = b.failSeek;
    r.input = b.input;
    r.next = 0;
}

std::string Where(const Recorder& r, const char* p) {
    if (!p) return "0";
    if (p >= r.area && p <= r.area + kArea) return "@" + std::to_string(p - r.area);
    char text[16];
    std::snprintf(text, sizeof text, "x%p", static_cast<const void*>(p));
    return text;
}

std::string Snapshot(const Recorder& r) {
    return "alloc " + std::to_string(r.allocated) + " unbuf " + std::to_string(r.unbuffered) + " last " +
           std::to_string(r.lastChar) + " reserve " + Where(r, r.base) + "-" + Where(r, r.ebuf) + " put " +
           Where(r, r.pbase) + "/" + Where(r, r.pptr) + "/" + Where(r, r.epptr) + " get " + Where(r, r.eback) +
           "/" + Where(r, r.gptr) + "/" + Where(r, r.egptr) + " lock " + std::to_string(r.lockFlag) + " calls " +
           r.calls + " read " + std::to_string(r.next);
}

struct Scratch {
    Recorder original;
    Recorder ours;
};
Scratch& Recorders() {
    static Scratch* scratch = new Scratch;   // never destroyed: the game's streams outlive the statics
    return *scratch;
}

// One operation compared: `op(recorder, original)` (its result as text) from `state` on both recorders.
template <typename Op>
void Compare(const char* name, const std::string& what, const State& state, const Behaviour& behaviour, Op op) {
    Recorder& a = Recorders().original;
    Recorder& b = Recorders().ours;
    Apply(a, state, behaviour);
    Apply(b, state, behaviour);
    const std::string expected = op(a, true) + " | " + Snapshot(a);
    const std::string actual = op(b, false) + " | " + Snapshot(b);
    ++s_cases;
    const bool same = expected == actual && std::memcmp(a.area, b.area, sizeof a.area) == 0;
    Check(name, true, same, "%s: %s, ours %s", what.c_str(), expected.c_str(), actual.c_str());
}

// The live streambuf's state, when its pointers lie in its reserve area (or it has none and no pointers).
bool StateOf(const Streambuf& live, State& s) {
    s = State{};
    s.unbuffered = live.unbuffered;
    s.lastChar = live.lastChar;
    char* const pointers[] = {live.base, live.ebuf, live.pbase, live.pptr, live.epptr, live.eback, live.gptr, live.egptr};
    int* const offsets[] = {&s.base, &s.ebuf, &s.pbase, &s.pptr, &s.epptr, &s.eback, &s.gptr, &s.egptr};
    const std::ptrdiff_t size = live.base ? live.ebuf - live.base : 0;
    if (size < 0 || size > kLiveArea) return false;
    for (int i = 0; i < 8; ++i) {
        if (!pointers[i]) continue;
        if (!live.base || pointers[i] < live.base || pointers[i] > live.ebuf) return false;
        *offsets[i] = static_cast<int>(pointers[i] - live.base);
    }
    if (live.base) s.bytes.assign(live.base, static_cast<std::size_t>(size));
    return true;
}

template <typename Op>
void ShadowLive(const char* name, const Streambuf& live, const std::string& what, Op op) {
    if (!Outermost()) return;
    Shadowing shadowing;
    State state;
    if (StateOf(live, state)) Compare(name, what, state, {}, op);
}

std::string Number(long value) { return std::to_string(value); }

// --- The streambuf originals and their wrappers. ----------------------------------------------------------

Streambuf*(__fastcall* s_construct)(Streambuf*, void*, char*, int);
void(__fastcall* s_destruct)(Streambuf*);
int(__fastcall* s_sync)(Streambuf*);
Streambuf*(__fastcall* s_setbuf)(Streambuf*, void*, char*, int);
long(__fastcall* s_seekoff)(Streambuf*, void*, long, int, int);
long(__fastcall* s_seekpos)(Streambuf*, void*, long, int);
int(__fastcall* s_xsputn)(Streambuf*, void*, const char*, int);
int(__fastcall* s_xsgetn)(Streambuf*, void*, char*, int);
int(__fastcall* s_pbackfail)(Streambuf*, void*, int);
int(__fastcall* s_doallocate)(Streambuf*);

std::string SetbufResult(const Recorder& r, const Streambuf* result) {
    return result == &r ? "this" : result ? "other" : "null";
}

// Constructed on a recorder: the fields, the vtable written and the critical section initialised (then
// deleted again, and the recorder's own vtable put back).
std::string ConstructOn(Recorder& r, bool original, char* buffer, int length) {
    const void* own = VtableOf(&r);
    Streambuf* result = original ? s_construct(&r, nullptr, buffer, length) : r.Construct(buffer, length);
    const void* written = VtableOf(&r);
    VtableOf(&r) = own;
    const LONG lockCount = Section(r.lock)->LockCount;
    DeleteCriticalSection(Section(r.lock));
    char text[64];
    std::snprintf(text, sizeof text, "%s vtable %p cs %ld", result == &r ? "this" : "other", written, lockCount);
    return text;
}

// Destroyed on a recorder (its critical section initialised first; its own vtable put back afterwards).
std::string DestructOn(Recorder& r, bool original) {
    const void* own = VtableOf(&r);
    InitializeCriticalSection(Section(r.lock));
    if (original) s_destruct(&r);
    else r.Destruct();
    const void* written = VtableOf(&r);
    VtableOf(&r) = own;
    char text[32];
    std::snprintf(text, sizeof text, "vtable %p", written);
    return text;
}

Streambuf* __fastcall ConstructVerify(Streambuf* self, void*, char* buffer, int length) {
    if (Outermost()) {
        Shadowing shadowing;
        Compare("streambuf(char*, int)", Number(length), State{}, {},
                [buffer, length](Recorder& r, bool original) { return ConstructOn(r, original, buffer, length); });
    }
    return self->Construct(buffer, length);
}
EI_VERIFY(0x006FCAD2, ConstructVerify, 5, s_construct);

void __fastcall DestructVerify(Streambuf* self) {
    ShadowLive("~streambuf", *self, "", [](Recorder& r, bool original) { return DestructOn(r, original); });
    self->Destruct();
}
EI_VERIFY(0x006FCB35, DestructVerify, 6, s_destruct);

int __fastcall SyncVerify(Streambuf* self) {
    ShadowLive("streambuf::sync", *self, "", [](Recorder& r, bool original) {
        return Number(original ? s_sync(&r) : r.DefaultSync());
    });
    return self->DefaultSync();
}
EI_VERIFY(0x006FCC86, SyncVerify, 6, s_sync);

Streambuf* __fastcall SetbufVerify(Streambuf* self, void*, char* buffer, int length) {
    ShadowLive("streambuf::setbuf", *self, Number(length), [buffer, length](Recorder& r, bool original) {
        return SetbufResult(r, original ? s_setbuf(&r, nullptr, buffer, length) : r.DefaultSetbuf(buffer, length));
    });
    return self->DefaultSetbuf(buffer, length);
}
EI_VERIFY(0x006FCB65, SetbufVerify, 6, s_setbuf);

long __fastcall SeekoffVerify(Streambuf* self, void*, long offset, int direction, int mode) {
    ShadowLive("streambuf::seekoff", *self, Number(offset), [=](Recorder& r, bool original) {
        return Number(original ? s_seekoff(&r, nullptr, offset, direction, mode) : r.DefaultSeekoff(offset, direction, mode));
    });
    return self->DefaultSeekoff(offset, direction, mode);
}
EI_VERIFY(0x006FCD18, SeekoffVerify, 6, s_seekoff);

long __fastcall SeekposVerify(Streambuf* self, void*, long position, int mode) {
    ShadowLive("streambuf::seekpos", *self, Number(position), [=](Recorder& r, bool original) {
        return Number(original ? s_seekpos(&r, nullptr, position, mode) : r.DefaultSeekpos(position, mode));
    });
    return self->DefaultSeekpos(position, mode);
}
EI_VERIFY(0x006FCD1E, SeekposVerify, 6, s_seekpos);

int __fastcall XsputnVerify(Streambuf* self, void*, const char* text, int count) {
    ShadowLive("streambuf::xsputn", *self, std::string(text, static_cast<std::size_t>((std::max)(count, 0))),
               [=](Recorder& r, bool original) {
                   return Number(original ? s_xsputn(&r, nullptr, text, count) : r.DefaultXsputn(text, count));
               });
    return self->DefaultXsputn(text, count);
}
EI_VERIFY(0x006FCB99, XsputnVerify, 8, s_xsputn);

// xsgetn into a buffer of each side's own (the bytes got are part of the result).
std::string XsgetnOn(Recorder& r, bool original, int count) {
    char text[64] = {};
    const int got = original ? s_xsgetn(&r, nullptr, text, count) : r.DefaultXsgetn(text, count);
    return Number(got) + " '" + std::string(text, sizeof text) + "'";
}

int __fastcall XsgetnVerify(Streambuf* self, void*, char* text, int count) {
    if (count >= 0 && count <= 64)
        ShadowLive("streambuf::xsgetn", *self, Number(count),
                   [count](Recorder& r, bool original) { return XsgetnOn(r, original, count); });
    return self->DefaultXsgetn(text, count);
}
EI_VERIFY(0x006FCBEF, XsgetnVerify, 8, s_xsgetn);

int __fastcall PbackfailVerify(Streambuf* self, void*, int c) {
    ShadowLive("streambuf::pbackfail", *self, Number(c), [c](Recorder& r, bool original) {
        return Number(original ? s_pbackfail(&r, nullptr, c) : r.DefaultPbackfail(c));
    });
    return self->DefaultPbackfail(c);
}
EI_VERIFY(0x006FCD30, PbackfailVerify, 6, s_pbackfail);

// doallocate: the result, then the reserve by size and ownership (each side's own heap block, freed).
std::string DoallocateOn(Recorder& r, bool original) {
    const int result = original ? s_doallocate(&r) : r.DefaultDoallocate();
    const std::string text = Number(result) + " alloc " + std::to_string(r.allocated) + " size " +
                             Number(static_cast<long>(r.ebuf - r.base)) + " heap " +
                             (r.base && (r.base < r.area || r.base >= r.area + kArea) ? "1" : "0");
    if (r.allocated && r.base) gamecrt::operator_delete(r.base);
    r.allocated = 0;
    r.base = r.ebuf = nullptr;
    return text;
}

// Ours only on the game's streambuf (the battery compares with the original): a 0x200-byte owned reserve.
int __fastcall DoallocateVerify(Streambuf* self) {
    const int result = self->DefaultDoallocate();
    Check("streambuf::doallocate", true, result == kEof || (result == 1 && self->allocated == 1 && self->ebuf - self->base == 0x200),
          "result %d allocated %d size %d", result, self->allocated, static_cast<int>(self->ebuf - self->base));
    return result;
}
EI_VERIFY(0x006FCCBB, DoallocateVerify, 8, s_doallocate);

// 006FCAB6 (no room for a trampoline: 3 bytes before its CALL): ours; the battery compares it with the
// original destructor plus operator delete.
Streambuf*(__fastcall* s_unusedDeleting)(Streambuf*, void*, unsigned);
Streambuf* __fastcall DeletingDestructVerify(Streambuf* self, void*, unsigned flags) {
    Streambuf* result = self->DeletingDestruct(flags);
    Check("streambuf deleting destructor", static_cast<void*>(self), static_cast<void*>(result), "%p", result);
    return result;
}
EI_VERIFY(0x006FCAB6, DeletingDestructVerify, 0, s_unusedDeleting);

// --- ios and ostream. -------------------------------------------------------------------------------------

Ios*(__fastcall* s_iosConstruct)(Ios*);
void(__fastcall* s_iosDestruct)(Ios*);
void(__fastcall* s_iosInit)(Ios*, void*, Streambuf*);
void(__fastcall* s_ostreamDestruct)(Ios*);
Ostream*(__fastcall* s_ostreamDelete)(Ios*, void*, unsigned);
Ostream*(__fastcall* s_ostreamConstruct)(Ostream*, void*, Streambuf*, int);

int& LiveIosCount() { return *reinterpret_cast<int*>(0x007CAEC8); }

// An ostream with its ios right after it, as the game's are laid out.
struct RawStream {
    Ostream stream;
    Ios ios;
};
static_assert(offsetof(RawStream, ios) == 8 && sizeof(RawStream) == 0x58);

// The bytes of a stream up to its critical section, and the critical section's lock count.
std::string Bytes(const RawStream& s) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&s);
    std::string out;
    for (std::size_t i = 0; i < offsetof(RawStream, ios) + offsetof(Ios, lock); ++i) {
        char hex[3];
        std::snprintf(hex, sizeof hex, "%02X", bytes[i]);
        out += hex;
    }
    return out + " cs " + std::to_string(reinterpret_cast<const CRITICAL_SECTION*>(s.ios.lock)->LockCount);
}

struct StreamScratch {
    RawStream original;
    RawStream ours;
};
StreamScratch& Streams() {
    static StreamScratch* scratch = new StreamScratch;
    return *scratch;
}

// One ios / ostream operation compared: both scratch streams filled with the same bytes and `prepare`d,
// then `op(stream, original)`; the result, the bytes and the change of the live-ios count must match.
// The count is raised by two meanwhile (no side takes it to zero, which would delete the shared lock) and
// put back afterwards; a scratch ios still alive (`aliveAfter`) has its critical section deleted.
template <typename Prepare, typename Op>
void CompareStreams(const char* name, const std::string& what, bool aliveAfter, Prepare prepare, Op op) {
    RawStream& a = Streams().original;
    RawStream& b = Streams().ours;
    const int start = LiveIosCount();
    LiveIosCount() = start + 2;
    std::memset(&a, 0xA5, sizeof a);
    std::memset(&b, 0xA5, sizeof b);
    prepare(a);
    prepare(b);
    const int before = LiveIosCount();
    const std::string expectedResult = op(a, true);
    const int afterOriginal = LiveIosCount();
    const std::string actualResult = op(b, false);
    const int afterOurs = LiveIosCount();
    const std::string expected = expectedResult + " count " + std::to_string(afterOriginal - before) + " " + Bytes(a);
    const std::string actual = actualResult + " count " + std::to_string(afterOurs - afterOriginal) + " " + Bytes(b);
    if (aliveAfter) {
        DeleteCriticalSection(Section(a.ios.lock));
        DeleteCriticalSection(Section(b.ios.lock));
    }
    LiveIosCount() = start;
    ++s_cases;
    Check(name, true, expected == actual, "%s: %s, ours %s", what.c_str(), expected.c_str(), actual.c_str());
}

// A constructed stream (ours; the original's is compared on its own).
void Construct(RawStream& s) { s.stream.Construct(nullptr, 1); }

// The game's ios copied into a scratch one (its streambuf not owned, so never deleted), with its own lock.
void CopyIos(RawStream& s, const Ios& live) {
    std::memcpy(&s.ios, &live, offsetof(Ios, lock));
    s.ios.deleteBuffer = 0;
    InitializeCriticalSection(Section(s.ios.lock));
}

std::string Pointer(const void* p, const void* self) { return p == self ? "this" : p ? "other" : "null"; }

std::string IosConstructOn(RawStream& s, bool original) {
    return Pointer(original ? s_iosConstruct(&s.ios) : s.ios.Construct(), &s.ios);
}

Ios* __fastcall IosConstructVerify(Ios* self) {
    if (Outermost()) {
        Shadowing shadowing;
        CompareStreams("ios()", "", true, [](RawStream&) {}, &IosConstructOn);
    }
    return self->Construct();
}
EI_VERIFY(0x006FCDB1, IosConstructVerify, 5, s_iosConstruct);

std::string IosDestructOn(RawStream& s, bool original) {
    const int deleted = Recorder::deleted;
    if (original) s_iosDestruct(&s.ios);
    else s.ios.Destruct();
    return "deleted " + Number(Recorder::deleted - deleted);
}

void __fastcall IosDestructVerify(Ios* self) {
    if (Outermost()) {
        Shadowing shadowing;
        CompareStreams("~ios", "", false, [self](RawStream& s) { CopyIos(s, *self); }, &IosDestructOn);
    }
    self->Destruct();
}
EI_VERIFY(0x006FCEDC, IosDestructVerify, 7, s_iosDestruct);

std::string IosInitOn(RawStream& s, bool original, Streambuf* buffer) {
    const int deleted = Recorder::deleted;
    if (original) s_iosInit(&s.ios, nullptr, buffer);
    else s.ios.Init(buffer);
    return "deleted " + Number(Recorder::deleted - deleted);
}

void __fastcall IosInitVerify(Ios* self, void*, Streambuf* buffer) {
    if (Outermost()) {
        Shadowing shadowing;
        CompareStreams("ios::init", "", true, [self](RawStream& s) { CopyIos(s, *self); },
                       [buffer](RawStream& s, bool original) { return IosInitOn(s, original, buffer); });
    }
    self->Init(buffer);
}
EI_VERIFY(0x006FCF26, IosInitVerify, 7, s_iosInit);

std::string OstreamDestructOn(RawStream& s, bool original) {
    if (original) s_ostreamDestruct(&s.ios);
    else s.ios.DestructOstream();
    return std::string();
}

void __fastcall OstreamDestructVerify(Ios* self) {
    if (Outermost()) {
        Shadowing shadowing;
        const Ostream live = *(reinterpret_cast<const Ostream*>(self) - 1);
        CompareStreams("~ostream", "", true, [self, live](RawStream& s) {
            s.stream = live;
            CopyIos(s, *self);
        }, &OstreamDestructOn);
    }
    self->DestructOstream();
}
EI_VERIFY(0x006FC502, OstreamDestructVerify, 6, s_ostreamDestruct);

// Ours only on the game's streams (not reached: they are static); the battery compares it.
Ostream* __fastcall OstreamDeleteVerify(Ios* self, void*, unsigned flags) {
    Ostream* expected = reinterpret_cast<Ostream*>(self) - 1;
    Ostream* result = self->DeleteOstream(flags);
    Check("ostream deleting destructor", static_cast<void*>(expected), static_cast<void*>(result), "%p", result);
    return result;
}
EI_VERIFY(0x006FC404, OstreamDeleteVerify, 5, s_ostreamDelete);

std::string OstreamConstructOn(RawStream& s, bool original, Streambuf* buffer, int mostDerived) {
    return Pointer(original ? s_ostreamConstruct(&s.stream, nullptr, buffer, mostDerived)
                            : s.stream.Construct(buffer, mostDerived), &s.stream);
}

// Shadowed for a most-derived ostream (the static initialisers' calls; the scratch ios is then built by the
// call itself); a base-class call is ours only.
Ostream* __fastcall OstreamConstructVerify(Ostream* self, void*, Streambuf* buffer, int mostDerived) {
    if (mostDerived && Outermost()) {
        Shadowing shadowing;
        CompareStreams("ostream(streambuf*)", "", true, [](RawStream&) {}, [buffer](RawStream& s, bool original) {
            return OstreamConstructOn(s, original, buffer, 1);
        });
    }
    return self->Construct(buffer, mostDerived);
}
EI_VERIFY(0x006FC42F, OstreamConstructVerify, 5, s_ostreamConstruct);

// 006FCE0F (no room: 3 bytes before its CALL): ours; the battery compares it with ~ios plus operator delete.
Ios*(__fastcall* s_unusedIosDeleting)(Ios*, void*, unsigned);
Ios* __fastcall IosDeletingVerify(Ios* self, void*, unsigned flags) {
    Ios* result = self->DeletingDestruct(flags);
    Check("ios deleting destructor", static_cast<void*>(self), static_cast<void*>(result), "%p", result);
    return result;
}
EI_VERIFY(0x006FCE0F, IosDeletingVerify, 0, s_unusedIosDeleting);

// --- The start-up battery. --------------------------------------------------------------------------------

const char kBytes[] = "0123456789ABCDEF";

State Buffered(int pbase, int pptr, int epptr) {
    State s;
    s.base = 0;
    s.ebuf = 16;
    s.pbase = pbase;
    s.pptr = pptr;
    s.epptr = epptr;
    s.bytes = kBytes;
    return s;
}

State WithGet(State s, int eback, int gptr, int egptr) {
    s.eback = eback;
    s.gptr = gptr;
    s.egptr = egptr;
    return s;
}

State Unbuffered(int lastChar, bool reserve) {
    State s = reserve ? Buffered(0, 0, 4) : State{};
    s.unbuffered = 1;
    s.lastChar = lastChar;
    return s;
}

void StreambufBattery() {
    const State states[] = {
        Buffered(0, 0, 4),                 // room in the put area
        Buffered(0, 4, 4),                 // a full put area
        Unbuffered(kEof, false),           // no reserve, nothing looked ahead
        State{},                           // buffered without a reserve area
        WithGet(Buffered(0, 2, 4), 8, 10, 14),
        WithGet(Buffered(0, 0, 4), 8, 8, 12),
        Unbuffered('q', false),            // a byte looked ahead
        Unbuffered(kEof, true),            // a reserve area, but unbuffered
    };
    const Behaviour behaviours[] = {
        {}, {true, false, ""}, {false, true, ""}, {false, false, "abcdefgh"}, {true, true, "xy"},
    };
    static const char* const kPuts[] = {"hello", "", "abcdefghijklmnopqrst", "\xFF\x80"};
    for (const State& state : states) {
        for (const Behaviour& behaviour : behaviours) {
            Compare("streambuf::sync", "", state, behaviour,
                    [](Recorder& r, bool original) { return Number(original ? s_sync(&r) : r.DefaultSync()); });
            for (int setbufCase = 0; setbufCase < 3; ++setbufCase) {
                Compare("streambuf::setbuf", Number(setbufCase), state, behaviour, [setbufCase](Recorder& r, bool original) {
                    char* buffer = setbufCase == 1 ? nullptr : r.area + 32;
                    const int length = setbufCase == 2 ? 0 : 8;
                    return SetbufResult(r, original ? s_setbuf(&r, nullptr, buffer, length) : r.DefaultSetbuf(buffer, length));
                });
            }
            Compare("streambuf::seekoff", "", state, behaviour, [](Recorder& r, bool original) {
                return Number(original ? s_seekoff(&r, nullptr, 3, 1, 2) : r.DefaultSeekoff(3, 1, 2));
            });
            Compare("streambuf::seekpos", "", state, behaviour, [](Recorder& r, bool original) {
                return Number(original ? s_seekpos(&r, nullptr, 7, 1) : r.DefaultSeekpos(7, 1));
            });
            for (const char* text : kPuts) {
                const int count = static_cast<int>(std::strlen(text));
                Compare("streambuf::xsputn", text, state, behaviour, [text, count](Recorder& r, bool original) {
                    return Number(original ? s_xsputn(&r, nullptr, text, count) : r.DefaultXsputn(text, count));
                });
            }
            for (int count : {0, 1, 5, 20}) {
                Compare("streambuf::xsgetn", Number(count), state, behaviour,
                        [count](Recorder& r, bool original) { return XsgetnOn(r, original, count); });
            }
            for (int c : {int{'z'}, 0xC1, -1}) {
                Compare("streambuf::pbackfail", Number(c), state, behaviour, [c](Recorder& r, bool original) {
                    return Number(original ? s_pbackfail(&r, nullptr, c) : r.DefaultPbackfail(c));
                });
            }
            Compare("streambuf::doallocate", "", state, behaviour,
                    [](Recorder& r, bool original) { return DoallocateOn(r, original); });
            Compare("~streambuf", "", state, behaviour, [](Recorder& r, bool original) { return DestructOn(r, original); });
        }
    }
    // The constructor; doallocate and the destructor over an owned reserve (each side's own block).
    for (int constructCase = 0; constructCase < 4; ++constructCase) {
        Compare("streambuf(char*, int)", Number(constructCase), State{}, {}, [constructCase](Recorder& r, bool original) {
            char* buffer = constructCase == 1 ? nullptr : r.area + 5;
            const int length = constructCase == 2 ? 0 : constructCase == 3 ? -3 : 16;
            return ConstructOn(r, original, buffer, length);
        });
    }
    const auto owned = [](Recorder& r) {
        r.base = static_cast<char*>(gamecrt::operator_new(16));
        r.ebuf = r.base + 16;
        r.allocated = 1;
    };
    Compare("streambuf::doallocate", "owned reserve", State{}, {}, [&owned](Recorder& r, bool original) {
        owned(r);
        return DoallocateOn(r, original);
    });
    Compare("~streambuf", "owned reserve", State{}, {}, [&owned](Recorder& r, bool original) {
        owned(r);
        const std::string text = DestructOn(r, original);
        r.allocated = 0;
        r.base = r.ebuf = nullptr;
        return text;
    });
    // The deleting destructor: on a scratch streambuf (flags 0), then on heap ones (flags 1).
    Compare("streambuf deleting destructor", "0", State{}, {}, [](Recorder& r, bool original) {
        const void* own = VtableOf(&r);
        InitializeCriticalSection(Section(r.lock));
        Streambuf* result;
        if (original) {
            s_destruct(&r);
            result = &r;
        } else {
            result = r.DeletingDestruct(0);
        }
        VtableOf(&r) = own;
        return SetbufResult(r, result);
    });
    for (int side = 0; side < 2; ++side) {
        auto* block = static_cast<Streambuf*>(gamecrt::operator_new(sizeof(Streambuf)));
        block->Construct(nullptr, 0);
        Streambuf* result = block;
        if (side == 0) {
            s_destruct(block);
            gamecrt::operator_delete(block);
        } else {
            result = block->DeletingDestruct(1);
        }
        Check("streambuf deleting destructor", static_cast<void*>(block), static_cast<void*>(result), "%p", result);
    }
}

// A streambuf on the heap for an ios to own (deleted through its vtable: Recorder::deleted counts it).
Streambuf* OwnedStreambuf() { return new Recorder; }

void IosBattery() {
    Recorder attached;
    CompareStreams("ios()", "", true, [](RawStream&) {}, &IosConstructOn);
    for (int owned = 0; owned < 2; ++owned) {
        for (int state : {0, 1, 4, 6}) {
            for (int attach = 0; attach < 2; ++attach) {
                const std::string what = "owned " + Number(owned) + " state " + Number(state) + " attach " + Number(attach);
                Streambuf* buffer = attach ? &attached : nullptr;
                const auto prepare = [&](RawStream& s) {
                    Construct(s);
                    s.ios.state = state;
                    s.ios.deleteBuffer = owned;
                    s.ios.bp = owned ? OwnedStreambuf() : &attached;
                };
                CompareStreams("ios::init", what, true, prepare,
                               [buffer](RawStream& s, bool original) { return IosInitOn(s, original, buffer); });
                CompareStreams("~ios", what, false, prepare, &IosDestructOn);
            }
        }
    }
    for (int mostDerived = 0; mostDerived < 2; ++mostDerived) {
        for (int attach = 0; attach < 2; ++attach) {
            Streambuf* buffer = attach ? &attached : nullptr;
            CompareStreams("ostream(streambuf*)", Number(mostDerived) + " attach " + Number(attach), true,
                           [mostDerived](RawStream& s) {
                               if (mostDerived) return;
                               Construct(s);
                               s.stream.floatUsed = 7;
                               s.ios.state = 2;
                           },
                           [buffer, mostDerived](RawStream& s, bool original) {
                               return OstreamConstructOn(s, original, buffer, mostDerived);
                           });
        }
    }
    CompareStreams("~ostream", "", true, &Construct, &OstreamDestructOn);
    CompareStreams("ostream deleting destructor", "0", false, &Construct, [](RawStream& s, bool original) {
        return Pointer(original ? s_ostreamDelete(&s.ios, nullptr, 0) : s.ios.DeleteOstream(0), &s.stream);
    });
    CompareStreams("ios deleting destructor", "0", false, &Construct, [](RawStream& s, bool original) {
        if (!original) return Pointer(s.ios.DeletingDestruct(0), &s.ios);
        s_iosDestruct(&s.ios);
        return Pointer(&s.ios, &s.ios);
    });
    // Flags 1 on heap streams (each freed by the game's operator delete).
    for (int side = 0; side < 2; ++side) {
        auto* block = static_cast<RawStream*>(gamecrt::operator_new(sizeof(RawStream)));
        block->stream.Construct(nullptr, 1);
        Ostream* result = side == 0 ? s_ostreamDelete(&block->ios, nullptr, 1) : block->ios.DeleteOstream(1);
        Check("ostream deleting destructor (heap)", static_cast<void*>(&block->stream), static_cast<void*>(result), "%p", result);
        auto* ios = static_cast<Ios*>(gamecrt::operator_new(sizeof(Ios)));
        ios->Construct();
        Ios* iosResult = ios;
        if (side == 0) {
            s_iosDestruct(ios);
            gamecrt::operator_delete(ios);
        } else {
            iosResult = ios->DeletingDestruct(1);
        }
        Check("ios deleting destructor (heap)", static_cast<void*>(ios), static_cast<void*>(iosResult), "%p", iosResult);
    }
}

void StreamsStartupCheck() {
    s_cases = 0;
    const int live = LiveIosCount();
    {
        Shadowing shadowing;
        StreambufBattery();
        IosBattery();
    }
    Check("crt streams: live ios count", live, LiveIosCount(), "%d", LiveIosCount());
    ei::Log("crt streams: %d case(s) compared, %d ios alive", s_cases, live);
}
EI_STARTUP_CHECK(StreamsStartupCheck);

} // namespace
