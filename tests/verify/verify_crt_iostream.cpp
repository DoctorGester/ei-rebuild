// Checks for MSVC 6's ostream inserters (src/lib/crt_iostream.cpp, S4c).
// Each call the game makes (the console and the logs; smoke reaches the console's) is shadowed: the stream's
// format state (flags, width, precision, fill, state, x_floatused) is copied into two scratch streams
// with recording streambufs, the original runs on one and ours on the other, and what each wrote (the
// bytes, and the streambuf calls in order), the state, the width and x_floatused must be the same; then
// ours runs on the game's stream. A start-up battery does the same over values x formats x streambuf
// behaviours (a full put area, failing overflow / xsputn / sync, a tie, the locks, a stream in error).
// The original of operator<<(const char*) has no room for a trampoline (3 bytes before a CALL): its body
// is rebuilt from the exe's opfx / writepad / osfx (006FC1F9 / 006FC68E / 006FC26D, not replaced) as it
// calls them. flush, the flush manipulator and operator<<(manipulator) have no room either and are
// ours only (the battery checks the manipulator runs once and the tie's flush syncs).
#include "verify.h"

#include "../../src/hook/log.h"
#include "../../src/lib/crt_iostream.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>

namespace {

using ei::verify::Check;
using libcrt::Ios;
using libcrt::Ostream;

// A streambuf that records what it is given: the bytes written (the put area drained into them first)
// and its calls in order. Its put area holds `room` bytes (0: unbuffered, every byte through overflow).
class RecordingStreambuf : public libcrt::Streambuf {
public:
    std::string output;
    std::string calls;
    bool failOverflow = false;
    bool shortWrites = false;
    bool failSync = false;
    char area[4] = {};

    void Reset(int room) {
        output.clear();
        calls.clear();
        allocated = unbuffered = lastChar = 0;
        base = ebuf = eback = gptr = egptr = nullptr;
        pbase = pptr = area;
        epptr = area + room;
        lockFlag = 0;
    }
    std::string Result() const { return output + std::string(pbase, pptr) + "|" + calls; }

    int Sync() override {
        calls += 'S';
        Drain();
        return failSync ? -1 : 0;
    }
    Streambuf* Setbuf(char*, int) override { return this; }
    long Seekoff(long, int, int) override { return -1; }
    long Seekpos(long, int) override { return -1; }
    int Xsputn(const char* text, int count) override {
        calls += 'W' + std::to_string(count);
        Drain();
        const int written = shortWrites && count > 0 ? count - 1 : count;
        output.append(text, written);
        return written;
    }
    int Xsgetn(char*, int) override { return 0; }
    int Overflow(int c) override {
        calls += 'O';
        Drain();
        if (failOverflow) return -1;
        output += static_cast<char>(c);
        return c & 0xFF;
    }
    int Underflow() override { return -1; }
    int Pbackfail(int) override { return -1; }
    int Doallocate() override { return 0; }

private:
    void Drain() {
        output.append(pbase, pptr);
        pptr = pbase;
    }
};

// An ostream with its ios (the virtual base right after it) and a recording streambuf.
const int kVirtualBaseTable[2] = {0, 8};
struct ScratchStream {
    Ostream stream;
    Ios ios;
    RecordingStreambuf buffer;

    ScratchStream() {
        std::memset(&ios, 0, sizeof ios);
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(ios.lock));
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(buffer.lock));
    }
    ~ScratchStream() {
        DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(ios.lock));
        DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(buffer.lock));
    }
    bool Unlocked() {
        return reinterpret_cast<CRITICAL_SECTION*>(ios.lock)->RecursionCount == 0 &&
               reinterpret_cast<CRITICAL_SECTION*>(buffer.lock)->RecursionCount == 0;
    }
};
static_assert(offsetof(ScratchStream, ios) == 8);

// What a stream's format is (copied from the game's stream, or a battery case) and how its streambuf
// behaves.
struct Format {
    long flags;
    int width;
    int precision;
    char fill;
    int state;
    int floatUsed;
};
struct Behaviour {
    int room = 4;
    bool failOverflow = false;
    bool shortWrites = false;
    bool failSync = false;
    bool tie = false;
    bool locked = false;
};

// The scratch streams (one set, shared by the game's threads under a lock).
struct Scratch {
    ScratchStream original;
    ScratchStream ours;
    ScratchStream tieOriginal;
    ScratchStream tieOurs;
    std::mutex mutex;
};
Scratch& Streams() {
    static Scratch* scratch = new Scratch;   // never destroyed: the game's streams outlive the statics
    return *scratch;
}

void Prepare(ScratchStream& s, ScratchStream& tie, const Format& f, const Behaviour& b) {
    s.stream.vbtable = kVirtualBaseTable;
    s.stream.floatUsed = f.floatUsed;
    s.ios.bp = &s.buffer;
    s.ios.state = f.state;
    s.ios.flags = f.flags;
    s.ios.width = f.width;
    s.ios.precision = f.precision;
    s.ios.fill = f.fill;
    s.ios.lockFlag = b.locked ? -1 : 0;
    s.buffer.Reset(b.room);
    s.buffer.lockFlag = b.locked ? -1 : 0;
    s.buffer.failOverflow = b.failOverflow;
    s.buffer.shortWrites = b.shortWrites;
    s.buffer.failSync = b.failSync;
    tie.stream.vbtable = kVirtualBaseTable;
    tie.ios.bp = &tie.buffer;
    tie.ios.state = 0;
    tie.buffer.Reset(4);
    tie.buffer.area[0] = 'T';
    tie.buffer.pptr = tie.buffer.area + 1;   // something pending for the tie's flush
    s.ios.tie = b.tie ? &tie.stream : nullptr;
}

int s_cases;

void Compare(const char* name, const std::string& value, Ostream* originalResult, Ostream* ourResult) {
    ++s_cases;
    ScratchStream& a = Streams().original;
    ScratchStream& b = Streams().ours;
    const bool same = a.buffer.Result() == b.buffer.Result() && a.ios.state == b.ios.state &&
                      a.ios.width == b.ios.width && a.stream.floatUsed == b.stream.floatUsed &&
                      Streams().tieOriginal.buffer.Result() == Streams().tieOurs.buffer.Result() &&
                      originalResult == &a.stream && ourResult == &b.stream && a.Unlocked() && b.Unlocked();
    Check(name, true, same, "%s flags %lX width %d precision %d fill %02X: '%s' state %d width %d, ours '%s' state %d width %d",
          value.c_str(), a.ios.flags, a.ios.width, a.ios.precision, static_cast<unsigned char>(a.ios.fill),
          a.buffer.Result().c_str(), a.ios.state, a.ios.width, b.buffer.Result().c_str(), b.ios.state, b.ios.width);
}

// --- The originals. ----------------------------------------------------------------------------------------

Ostream*(__fastcall* s_putChar)(Ostream*, void*, char);
Ostream*(__fastcall* s_putUnsigned)(Ostream*, void*, unsigned);
Ostream*(__fastcall* s_putDouble)(Ostream*, void*, double);
Ostream*(__fastcall* s_putLong)(Ostream*, void*, long);
Ostream*(__fastcall* s_putInt)(Ostream*, void*, int);

// operator<<(const char*) as 006FC31D: opfx, writepad("" at 0079B298, text), osfx.
__declspec(naked) int __fastcall OriginalPrefix(Ostream*) EI_ORIGINAL(0x006FC1F9)
__declspec(naked) Ostream* __fastcall OriginalWritePad(Ostream*, void*, const char*, const char*) EI_ORIGINAL(0x006FC68E)
__declspec(naked) void __fastcall OriginalSuffix(Ostream*) EI_ORIGINAL(0x006FC26D)
Ostream* OriginalPutText(Ostream* stream, const char* text) {
    if (OriginalPrefix(stream)) {
        OriginalWritePad(stream, nullptr, reinterpret_cast<const char*>(0x0079B298), text);
        OriginalSuffix(stream);
    }
    return stream;
}

// One insertion compared: `original(stream)` and `ours(stream)` on the scratch streams from `format`.
template <typename Original, typename Ours>
void Shadow(const char* name, const std::string& value, const Format& format, const Behaviour& behaviour,
            Original original, Ours ours) {
    Scratch& scratch = Streams();
    std::lock_guard<std::mutex> hold(scratch.mutex);
    Prepare(scratch.original, scratch.tieOriginal, format, behaviour);
    Prepare(scratch.ours, scratch.tieOurs, format, behaviour);
    Ostream* expected = original(&scratch.original.stream);
    Ostream* actual = ours(&scratch.ours.stream);
    Compare(name, value, expected, actual);
}

Format FormatOf(Ostream* stream) {
    const Ios& ios = stream->Base();
    return {ios.flags, ios.width, ios.precision, ios.fill, ios.state, stream->floatUsed};
}

// --- The wrappers: the game's call shadowed, then ours on its stream. ---------------------------------------

// 006FC0B0: PUSH EBP; MOV EBP,ESP; PUSH ESI; MOV ESI,ECX (6 bytes).
Ostream* __fastcall PutCharVerify(Ostream* self, void*, char c) {
    Shadow("ostream << char", std::to_string(static_cast<unsigned char>(c)), FormatOf(self), {},
           [c](Ostream* s) { return s_putChar(s, nullptr, c); }, [c](Ostream* s) { return s->PutChar(c); });
    return self->PutChar(c);
}
EI_VERIFY(0x006FC0B0, PutCharVerify, 6, s_putChar);

// 006FC14E / 006FC91C / 006FC9C6: PUSH EBP; MOV EBP,ESP; SUB ESP,0x14 (6 bytes).
Ostream* __fastcall PutUnsignedVerify(Ostream* self, void*, unsigned value) {
    Shadow("ostream << unsigned", std::to_string(value), FormatOf(self), {},
           [value](Ostream* s) { return s_putUnsigned(s, nullptr, value); },
           [value](Ostream* s) { return s->PutUnsigned(value); });
    return self->PutUnsigned(value);
}
EI_VERIFY(0x006FC14E, PutUnsignedVerify, 6, s_putUnsigned);

Ostream* __fastcall PutLongVerify(Ostream* self, void*, long value) {
    Shadow("ostream << long", std::to_string(value), FormatOf(self), {},
           [value](Ostream* s) { return s_putLong(s, nullptr, value); }, [value](Ostream* s) { return s->PutLong(value); });
    return self->PutLong(value);
}
EI_VERIFY(0x006FC91C, PutLongVerify, 6, s_putLong);

Ostream* __fastcall PutIntVerify(Ostream* self, void*, int value) {
    Shadow("ostream << int", std::to_string(value), FormatOf(self), {},
           [value](Ostream* s) { return s_putInt(s, nullptr, value); }, [value](Ostream* s) { return s->PutInt(value); });
    return self->PutInt(value);
}
EI_VERIFY(0x006FC9C6, PutIntVerify, 6, s_putInt);

// 006FC7FA: PUSH EBP; MOV EBP,ESP; PUSH 0x28 (5 bytes).
std::string DoubleText(double value) {
    char text[64];
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    std::snprintf(text, sizeof text, "%.17g (%016llX)", value, static_cast<unsigned long long>(bits));
    return text;
}

Ostream* __fastcall PutDoubleVerify(Ostream* self, void*, double value) {
    Shadow("ostream << double", DoubleText(value), FormatOf(self), {},
           [value](Ostream* s) { return s_putDouble(s, nullptr, value); },
           [value](Ostream* s) { return s->PutDouble(value); });
    return self->PutDouble(value);
}
EI_VERIFY(0x006FC7FA, PutDoubleVerify, 5, s_putDouble);

Ostream*(__fastcall* s_unusedPutText)(Ostream*, void*, const char*);
Ostream* __fastcall PutTextVerify(Ostream* self, void*, const char* text) {
    Shadow("ostream << text", text, FormatOf(self), {}, [text](Ostream* s) { return OriginalPutText(s, text); },
           [text](Ostream* s) { return s->PutText(text); });
    return self->PutText(text);
}
EI_VERIFY(0x006FC31D, PutTextVerify, 0, s_unusedPutText);

// 00434FE0: ours only (4 bytes before its CALL; the manipulator runs once).
Ostream*(__fastcall* s_unusedApply)(Ostream*, void*, Ostream*(__cdecl*)(Ostream*));
Ostream* __fastcall ApplyVerify(Ostream* self, void*, Ostream*(__cdecl* manipulator)(Ostream*)) {
    Ostream* result = self->Apply(manipulator);
    Check("ostream << manipulator", static_cast<void*>(self), static_cast<void*>(result), "%p", result);
    return result;
}
EI_VERIFY(0x00434FE0, ApplyVerify, 0, s_unusedApply);

// The manipulator applied as 00434FE0 does: called with the stream, the stream returned.
Ostream* OriginalApply(Ostream* stream, Ostream*(__cdecl* manipulator)(Ostream*)) {
    manipulator(stream);
    return stream;
}

Ostream*(__fastcall* s_unusedFlush)(Ostream*);
Ostream* __fastcall FlushVerify(Ostream* self) {
    Ostream* result = self->Flush();
    Check("ostream::flush", static_cast<void*>(self), static_cast<void*>(result), "%p", result);
    return result;
}
EI_VERIFY(0x006FC346, FlushVerify, 0, s_unusedFlush);

// --- The start-up battery. -----------------------------------------------------------------------------

__declspec(naked) Ostream* __cdecl FlushManipulatorAt(Ostream*) EI_ORIGINAL(0x00435000)
__declspec(naked) Ostream* __cdecl EndlManipulatorAt(Ostream*) EI_ORIGINAL(0x004BA9F0)

const Format kFormats[] = {
    {0, 0, 6, ' ', 0, 0},
    {Ios::kShowPos, 0, 6, ' ', 0, 0},
    {Ios::kHex, 0, 6, ' ', 0, 0},
    {Ios::kOct, 0, 6, ' ', 0, 0},
    {Ios::kHex | Ios::kShowBase, 10, 6, '*', 0, 0},
    {Ios::kHex | Ios::kShowBase | Ios::kUppercase | Ios::kInternal, 12, 6, '0', 0, 0},
    {Ios::kOct | Ios::kShowBase | Ios::kLeft, 9, 6, '.', 0, 0},
    {Ios::kHex | Ios::kOct | Ios::kShowPos, 0, 6, ' ', 0, 0},
    {Ios::kLeft, 8, 3, '_', 0, 0},
    {Ios::kInternal | Ios::kShowPos, 15, 10, '0', 0, 0},
    {Ios::kLeft | Ios::kInternal, 15, 6, '#', 0, 0},
    {0, 1, 0, ' ', 0, 0},
    {0, 40, 20, 0, 0, 1},
    {0, -1, 6, ' ', 0, 0},
    {Ios::kShowPoint, 0, 0, ' ', 0, 0},
    {Ios::kShowPoint | Ios::kShowPos, 12, 4, ' ', 0, 1},
    {Ios::kScientific, 0, 6, ' ', 0, 0},
    {Ios::kScientific | Ios::kUppercase, 0, 15, ' ', 0, 0},
    {Ios::kFixed, 0, 2, ' ', 0, 0},
    {Ios::kFixed | Ios::kUppercase | Ios::kShowPos, 20, 9, ' ', 0, 1},
    {Ios::kFloatField | Ios::kUppercase, 0, 6, ' ', 0, 0},
    {Ios::kUppercase, 0, 30, ' ', 0, 0},
    {Ios::kUnitBuf, 5, 6, ' ', 0, 0},
    {0, 7, 6, ' ', Ios::kFail, 0},
    {0, 0, 6, ' ', 1, 1},
};

const Behaviour kBehaviours[] = {
    {},
    {0},
    {1, true},
    {2, false, true},
    {4, false, false, true},
    {4, false, false, false, true},
    {0, true, true, true, true, true},
};

int s_manipulatorCalls;
Ostream* __cdecl CountingManipulator(Ostream* stream) {
    ++s_manipulatorCalls;
    return stream;
}

void IostreamStartupCheck() {
    s_cases = 0;

    static const char kChars[] = {'a', '\0', '\xFF', '-', ' '};
    static const unsigned kUnsigned[] = {0, 1, 42, 255, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
    static const long kSigned[] = {0, 1, -1, 42, -42, 255, 0x7FFFFFFF, -0x7FFFFFFF - 1};
    static const char* const kTexts[] = {"", "x", "abc", "-12", "a longer line of text for the padding"};
    static const double kDoubles[] = {0.0, -0.0, 1.0, -1.0, 0.1, 1.0 / 3.0, -2.5, 123456789.123, 1e-5, 1.5e15,
                                      1e20, -2.5e-300, 4.9e-324, 1e300, 0.000123456789,
                                      std::numeric_limits<double>::infinity(),
                                      -std::numeric_limits<double>::infinity(),
                                      std::numeric_limits<double>::quiet_NaN()};
    for (const Format& format : kFormats) {
        for (const Behaviour& behaviour : kBehaviours) {
            for (char c : kChars)
                Shadow("ostream << char", std::to_string(static_cast<unsigned char>(c)), format, behaviour,
                       [c](Ostream* s) { return s_putChar(s, nullptr, c); }, [c](Ostream* s) { return s->PutChar(c); });
            for (unsigned v : kUnsigned)
                Shadow("ostream << unsigned", std::to_string(v), format, behaviour,
                       [v](Ostream* s) { return s_putUnsigned(s, nullptr, v); }, [v](Ostream* s) { return s->PutUnsigned(v); });
            for (long v : kSigned) {
                Shadow("ostream << long", std::to_string(v), format, behaviour,
                       [v](Ostream* s) { return s_putLong(s, nullptr, v); }, [v](Ostream* s) { return s->PutLong(v); });
                const int i = static_cast<int>(v);
                Shadow("ostream << int", std::to_string(i), format, behaviour,
                       [i](Ostream* s) { return s_putInt(s, nullptr, i); }, [i](Ostream* s) { return s->PutInt(i); });
            }
            for (const char* text : kTexts)
                Shadow("ostream << text", text, format, behaviour,
                       [text](Ostream* s) { return OriginalPutText(s, text); },
                       [text](Ostream* s) { return s->PutText(text); });
            for (double v : kDoubles) {
                // A fixed value past 10^22 overruns the original's 24-byte buffer.
                if ((format.flags & Ios::kFloatField) == Ios::kFixed && !(std::fabs(v) < 1e15)) continue;
                Shadow("ostream << double", DoubleText(v), format, behaviour,
                       [v](Ostream* s) { return s_putDouble(s, nullptr, v); }, [v](Ostream* s) { return s->PutDouble(v); });
            }
            s_manipulatorCalls = 0;
            Shadow("ostream << manipulator", "counting", format, behaviour,
                   [](Ostream* s) { return OriginalApply(s, &CountingManipulator); },
                   [](Ostream* s) { return s->Apply(&CountingManipulator); });
            Check("ostream << manipulator calls", 2, s_manipulatorCalls, "%d", s_manipulatorCalls);
            Shadow("ostream << flush", "flush", format, behaviour,
                   [](Ostream* s) { return OriginalApply(s, &FlushManipulatorAt); },
                   [](Ostream* s) { return s->Apply(&libcrt::FlushManipulator); });
            Shadow("ostream << endl", "endl", format, behaviour,
                   [](Ostream* s) { return OriginalApply(s, &EndlManipulatorAt); },
                   [](Ostream* s) { return s->Apply(&libcrt::EndlManipulator); });
        }
    }
    ei::Log("crt iostream: %d insertion(s) compared", s_cases);
}
EI_STARTUP_CHECK(IostreamStartupCheck);

} // namespace
