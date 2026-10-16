// MSVC 6's ostream inserters (S4c) and the streams themselves: streambuf's constructor, destructors and
// default virtuals, ios's, ostream's (S4d). docs/notes/standalone.md; crt_iostream.h. Both builds: they
// keep no state of their own (the streams are the game's; the shared lock and the count of live ios are
// the exe's). opfx / osfx / writepad are the inserters' own helpers; the exe's other callers of them (put,
// write, the streambuf inserter: not reached by the game's code) keep the exe's. The numbers go through
// the game's sprintf, the exponent letter through its toupper. verify_crt_iostream.cpp
// compared each inserter with the original, verify_crt_streams.cpp the rest (until S21d).
#include "crt_iostream.h"

#include "crt.h"
#include "ei/hook.h"
#include "gameapi.h"

#include <cstring>

namespace libcrt {

namespace {

constexpr int kEof = -1;

CRITICAL_SECTION* Section(std::uint8_t* lock) { return reinterpret_cast<CRITICAL_SECTION*>(lock); }

// The exe's vtables the constructors and destructors write: streambuf's, ios's, and ostream's for its ios;
// ostream's virtual base table (the ios 8 bytes in).
const void* StreambufVtable() { return reinterpret_cast<const void*>(0x007482AC); }
const void* IosVtable() { return reinterpret_cast<const void*>(0x007482E8); }
const void* OstreamIosVtable() { return reinterpret_cast<const void*>(0x00748280); }
const int* OstreamVirtualBases() { return reinterpret_cast<const int*>(0x00748284); }

// The lock all streams share (x_lockc) and how many ios are alive (initialised with the first, deleted
// with the last).
CRITICAL_SECTION* SharedLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007CAE90); }
int& LiveIosCount() { return *reinterpret_cast<int*>(0x007CAEC8); }

constexpr int kReserveSize = 0x200;   // doallocate's reserve area

// The game CRT's stdout / stderr (&_iob[1], &_iob[2]), flushed after each insertion under ios::stdio.
gamecrt::CrtFile* GameStdout() { return reinterpret_cast<gamecrt::CrtFile*>(0x00798080); }
gamecrt::CrtFile* GameStderr() { return reinterpret_cast<gamecrt::CrtFile*>(0x007980A0); }

// opfx (006FC1F9): the ios locked; a stream in error gets failbit and nothing is written (0); otherwise
// the tie flushed and the streambuf locked too (1).
int Prefix(Ostream* stream) {
    Ios& ios = stream->Base();
    ios.Lock();
    if (ios.state) {
        ios.state |= Ios::kFail;
        ios.Unlock();
        return 0;
    }
    if (ios.tie) ios.tie->Flush();
    ios.bp->Lock();
    return 1;
}

// osfx (006FC26D): the width cleared, the streambuf synced under unitbuf (failbit | badbit on failure,
// assigned), stdout and stderr flushed under ios::stdio, both locks released.
void Suffix(Ostream* stream) {
    Ios& ios = stream->Base();
    ios.width = 0;
    if ((ios.flags & Ios::kUnitBuf) && ios.bp->Sync() == kEof) ios.state = Ios::kFail | Ios::kBad;
    if (ios.flags & Ios::kStdio) {
        if (gamecrt::fflush(GameStdout()) == kEof) ios.state |= Ios::kFail;
        if (gamecrt::fflush(GameStderr()) == kEof) ios.state |= Ios::kFail;
    }
    ios.bp->Unlock();
    ios.Unlock();
}

// The fill character until the padding is used up (a failed put sets failbit | badbit).
void Pad(Ios& ios, long& padding) {
    while (padding-- > 0)
        if (ios.bp->Sputc(static_cast<unsigned char>(ios.fill)) == kEof) ios.state |= Ios::kFail | Ios::kBad;
}

void Write(Ios& ios, const char* text, unsigned length) {
    if (static_cast<unsigned>(ios.bp->Xsputn(text, static_cast<int>(length))) != length) ios.state |= Ios::kFail | Ios::kBad;
}

// writepad (006FC68E): `leader` (a sign or base prefix) and `value` padded to the width: before both
// (right, the default), between them (internal) or after (left). Once one padding has run, no other does.
void WritePad(Ostream* stream, const char* leader, const char* value) {
    Ios& ios = stream->Base();
    const unsigned leaderLength = static_cast<unsigned>(std::strlen(leader));
    const unsigned length = static_cast<unsigned>(std::strlen(value));
    long padding = static_cast<unsigned>(ios.width) > length + leaderLength
                       ? static_cast<long>(static_cast<unsigned>(ios.width) - (length + leaderLength))
                       : 0;
    if (!(ios.flags & (Ios::kLeft | Ios::kInternal))) Pad(ios, padding);
    if (leaderLength) Write(ios, leader, leaderLength);
    if (ios.flags & Ios::kInternal) Pad(ios, padding);
    Write(ios, value, length);
    if (ios.flags & Ios::kLeft) Pad(ios, padding);
}

// The integer inserters: `format` ("%d", "%ld" or "%u"), its conversion letter at `letter`, changed to
// 'o' / 'x' / 'X' for oct / hex; the leader "0x", "0X" or "0" under showbase, "+" under showpos for a
// positive value (any nonzero one for "%u"). Zero gets neither.
template <typename T>
void PutInteger(Ostream* stream, T value, const char* format, int letter, bool positive) {
    if (!Prefix(stream)) return;
    Ios& ios = stream->Base();
    char conversion[4] = {};
    std::memcpy(conversion, format, std::strlen(format));
    char leader[4] = {};
    if (value != 0) {
        if (ios.flags & (Ios::kHex | Ios::kOct)) {
            if (ios.flags & Ios::kHex) {
                conversion[letter] = (ios.flags & Ios::kUppercase) ? 'X' : 'x';
                leader[1] = conversion[letter];
            } else {
                conversion[letter] = 'o';
            }
            if (ios.flags & Ios::kShowBase) leader[0] = '0';
        } else if (positive && (ios.flags & Ios::kShowPos)) {
            leader[0] = '+';
        }
    }
    char text[12];
    gamecrt::sprintf(text, conversion, value);
    WritePad(stream, leader, text);
    Suffix(stream);
}

} // namespace

int Streambuf::Sputc(int c) {
    if (pptr < epptr) {
        *pptr++ = static_cast<char>(c);
        return c & 0xFF;
    }
    return Overflow(c);
}

void Streambuf::Lock() {
    if (lockFlag < 0) gameapi::EnterCriticalSection(Section(lock));
}

void Streambuf::Unlock() {
    if (lockFlag < 0) gameapi::LeaveCriticalSection(Section(lock));
}

void Ios::Lock() {
    if (lockFlag < 0) gameapi::EnterCriticalSection(Section(lock));
}

void Ios::Unlock() {
    if (lockFlag < 0) gameapi::LeaveCriticalSection(Section(lock));
}

// ---- streambuf -----------------------------------------------------------------------------------------

Streambuf* Streambuf::Construct(char* buffer, int length) {
    lastChar = kEof;
    SetVtable(StreambufVtable());
    allocated = 0;
    unbuffered = 0;
    base = buffer;
    ebuf = buffer + length;
    pbase = pptr = epptr = nullptr;
    eback = gptr = egptr = nullptr;
    if (!buffer || !length) {
        unbuffered = 1;
        base = nullptr;
        ebuf = nullptr;
    }
    lockFlag = -1;
    gameapi::InitializeCriticalSection(Section(lock));
    return this;
}
EI_REPLACE_METHOD(0x006FCAD2, libcrt::Streambuf::Construct);

void Streambuf::Destruct() {
    SetVtable(StreambufVtable());
    gameapi::DeleteCriticalSection(Section(lock));
    DefaultSync();
    if (allocated && base) gamecrt::operator_delete(base);
}
EI_REPLACE_METHOD(0x006FCB35, libcrt::Streambuf::Destruct);

Streambuf* Streambuf::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x006FCAB6, libcrt::Streambuf::DeletingDestruct);

// setb: the reserve area replaced (the old one freed when owned).
void Streambuf::Setb(char* reserve, char* reserveEnd, int owned) {
    if (allocated && base) gamecrt::operator_delete(base);
    base = reserve;
    allocated = owned;
    ebuf = reserveEnd;
}

// sputbackc: back into the get area when there is room before gptr (the byte sign-extended, as the header's
// inline returns it), else pbackfail.
int Streambuf::Sputbackc(char c) {
    if (eback < gptr) {
        *--gptr = c;
        return static_cast<signed char>(c);
    }
    return Pbackfail(static_cast<signed char>(c));
}

// Nothing to do when neither area holds anything; EOF when one does (the base class cannot flush).
int Streambuf::DefaultSync() {
    if (gptr < egptr || pptr > pbase) return kEof;
    return 0;
}
EI_REPLACE_METHOD(0x006FCC86, libcrt::Streambuf::DefaultSync);

// Only while there is no reserve area yet (null otherwise): `buffer` becomes it, or with a null buffer or a
// zero length the streambuf goes unbuffered.
Streambuf* Streambuf::DefaultSetbuf(char* buffer, int length) {
    if (base) return nullptr;
    if (buffer && length) {
        base = buffer;
        unbuffered = 0;
        ebuf = buffer + length;
    } else {
        unbuffered = 1;
    }
    return this;
}
EI_REPLACE_METHOD(0x006FCB65, libcrt::Streambuf::DefaultSetbuf);

long Streambuf::DefaultSeekoff(long, int, int) { return kEof; }
EI_REPLACE_METHOD(0x006FCD18, libcrt::Streambuf::DefaultSeekoff);

long Streambuf::DefaultSeekpos(long position, int mode) { return Seekoff(position, 0, mode); }
EI_REPLACE_METHOD(0x006FCD1E, libcrt::Streambuf::DefaultSeekpos);

// Byte by byte: into the put area while it has room (and the streambuf is buffered), else overflow, until
// overflow fails. The count put.
int Streambuf::DefaultXsputn(const char* text, int count) {
    int put = 0;
    while (count-- != 0) {
        if (!unbuffered && pptr < epptr) {
            *pptr++ = *text;
        } else if (Overflow(static_cast<unsigned char>(*text)) == kEof) {
            break;
        }
        ++text;
        ++put;
    }
    return put;
}
EI_REPLACE_METHOD(0x006FCB99, libcrt::Streambuf::DefaultXsputn);

// Unbuffered: the look-ahead byte (lastChar, fetched by underflow when it is EOF) and the next one after
// each. Buffered: underflow, then what the get area holds, until `count` bytes or EOF. The count got.
int Streambuf::DefaultXsgetn(char* text, int count) {
    int got = 0;
    if (unbuffered) {
        if (lastChar == kEof) lastChar = Underflow();
        while (count-- != 0 && lastChar != kEof) {
            *text++ = static_cast<char>(lastChar);
            ++got;
            lastChar = Underflow();
        }
        return got;
    }
    while (count != 0 && Underflow() != kEof) {
        int available = static_cast<int>(egptr - gptr);
        if (available >= count) available = count;
        if (available > 0) {
            std::memcpy(text, gptr, static_cast<std::size_t>(available));
            text += available;
            gptr += available;
            got += available;
            count -= available;
        }
    }
    return got;
}
EI_REPLACE_METHOD(0x006FCBEF, libcrt::Streambuf::DefaultXsgetn);

// Room before gptr: sputbackc. Otherwise the source is asked to step back one byte (seekoff(-1, cur, in));
// when it can and the streambuf is buffered with a get area, the get area shifts up one byte (its last byte
// dropped; VC6's count, egptr - gptr - 1, as it is) and `c` goes in at gptr. `c`, or EOF.
int Streambuf::DefaultPbackfail(int c) {
    if (eback < gptr) return Sputbackc(static_cast<char>(c));
    if (Seekoff(-1, 1, 1) == kEof) return kEof;
    if (!unbuffered && egptr) {
        std::memmove(gptr + 1, gptr, static_cast<std::size_t>(egptr - gptr - 1));
        *gptr = static_cast<char>(c);
    }
    return c;
}
EI_REPLACE_METHOD(0x006FCD30, libcrt::Streambuf::DefaultPbackfail);

int Streambuf::DefaultDoallocate() {
    char* reserve = static_cast<char*>(gamecrt::operator_new(kReserveSize));
    if (!reserve) return kEof;
    Setb(reserve, reserve + kReserveSize, 1);
    return 1;
}
EI_REPLACE_METHOD(0x006FCCBB, libcrt::Streambuf::DefaultDoallocate);

// ---- ios and ostream ------------------------------------------------------------------------------------

Ios* Ios::Construct() {
    lockFlag = -1;
    bp = nullptr;
    ispecial = 0;
    ospecial = 0;
    tie = nullptr;
    flags = 0;
    width = 0;
    deleteBuffer = 0;
    vtable = IosVtable();
    state = kBad;
    precision = 6;
    fill = ' ';
    gameapi::InitializeCriticalSection(Section(lock));
    if (LiveIosCount()++ == 0) gameapi::InitializeCriticalSection(SharedLock());
    return this;
}
EI_REPLACE_METHOD(0x006FCDB1, libcrt::Ios::Construct);

void Ios::Destruct() {
    lockFlag = -1;
    vtable = IosVtable();
    if (--LiveIosCount() == 0) gameapi::DeleteCriticalSection(SharedLock());
    gameapi::DeleteCriticalSection(Section(lock));
    if (deleteBuffer && bp) delete bp;
    bp = nullptr;
    state = kBad;
}
EI_REPLACE_METHOD(0x006FCEDC, libcrt::Ios::Destruct);

Ios* Ios::DeletingDestruct(unsigned deleteFlags) {
    Destruct();
    if (deleteFlags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x006FCE0F, libcrt::Ios::DeletingDestruct);

void Ios::Init(Streambuf* buffer) {
    if (deleteBuffer && bp) delete bp;
    bp = buffer;
    if (buffer) state &= ~kBad;
    else state |= kBad;
}
EI_REPLACE_METHOD(0x006FCF26, libcrt::Ios::Init);

void Ios::DestructOstream() {
    Ostream* stream = reinterpret_cast<Ostream*>(this) - 1;
    stream->Base().vtable = OstreamIosVtable();
}
EI_REPLACE_METHOD(0x006FC502, libcrt::Ios::DestructOstream);

Ostream* Ios::DeleteOstream(unsigned deleteFlags) {
    Ostream* stream = reinterpret_cast<Ostream*>(this) - 1;
    DestructOstream();
    Destruct();
    if (deleteFlags & 1) gamecrt::operator_delete(stream);
    return stream;
}
EI_REPLACE_METHOD(0x006FC404, libcrt::Ios::DeleteOstream);

Ostream* Ostream::Construct(Streambuf* buffer, int mostDerived) {
    if (mostDerived) {
        vbtable = OstreamVirtualBases();
        Base().Construct();
    }
    Base().vtable = OstreamIosVtable();
    Base().Init(buffer);
    floatUsed = 0;
    return this;
}
EI_REPLACE_METHOD(0x006FC42F, libcrt::Ostream::Construct);

Ostream* Ostream::Apply(Ostream*(__cdecl* manipulator)(Ostream*)) {
    manipulator(this);
    return this;
}
EI_REPLACE_METHOD(0x00434FE0, libcrt::Ostream::Apply);

// With a width, through writepad as a one-byte string (a zero byte writes only the padding); otherwise
// put, and on EOF overflow is asked once more before failbit | badbit.
Ostream* Ostream::PutChar(char c) {
    if (!Prefix(this)) return this;
    Ios& ios = Base();
    if (ios.width) {
        const char text[2] = {c, '\0'};
        WritePad(this, "", text);
    } else {
        const int byte = static_cast<unsigned char>(c);
        if (ios.bp->Sputc(byte) == kEof && ios.bp->Overflow(byte) == kEof) ios.state |= Ios::kFail | Ios::kBad;
    }
    Suffix(this);
    return this;
}
EI_REPLACE_METHOD(0x006FC0B0, libcrt::Ostream::PutChar);

Ostream* Ostream::PutUnsigned(unsigned value) {
    PutInteger(this, value, "%u", 1, true);
    return this;
}
EI_REPLACE_METHOD(0x006FC14E, libcrt::Ostream::PutUnsigned);

Ostream* Ostream::PutText(const char* text) {
    if (Prefix(this)) {
        WritePad(this, "", text);
        Suffix(this);
    }
    return this;
}
EI_REPLACE_METHOD(0x006FC31D, libcrt::Ostream::PutText);

// "%[+][#].<precision>g", the letter 'f' under fixed, 'e' under scientific (upper case under uppercase,
// except 'f'); the precision at most 15, or 6 after the inline float inserter (floatUsed, cleared). The
// sign goes out as the leader, so internal padding falls between it and the digits.
Ostream* Ostream::PutDouble(double value) {
    const bool afterFloat = floatUsed != 0;
    floatUsed = 0;
    const unsigned limit = afterFloat ? 6 : 15;
    unsigned precision = static_cast<unsigned>(Base().precision);
    if (precision >= limit) precision = limit;
    if (!Prefix(this)) return this;
    Ios& ios = Base();
    char flagsText[4] = {};
    int n = 0;
    if (ios.flags & Ios::kShowPos) flagsText[n++] = '+';
    if (ios.flags & Ios::kShowPoint) flagsText[n++] = '#';
    char format[12];
    const int last = gamecrt::sprintf(format, "%%%s.%.0ug", flagsText, precision) - 1;
    const long field = ios.flags & Ios::kFloatField;
    if (field == Ios::kFixed) {
        format[last] = 'f';
    } else {
        if (field == Ios::kScientific) format[last] = 'e';
        if (ios.flags & Ios::kUppercase) format[last] = static_cast<char>(gamecrt::toupper(format[last]));
    }
    // 24 bytes in VC6 (a fixed value past 10^22 overruns its stack); room here.
    char text[512];
    gamecrt::sprintf(text, format, value);
    char leader[2] = {};
    const char* digits = text;
    if (text[0] == '+' || text[0] == '-') {
        leader[0] = text[0];
        digits = text + 1;
    }
    WritePad(this, leader, digits);
    Suffix(this);
    return this;
}
EI_REPLACE_METHOD(0x006FC7FA, libcrt::Ostream::PutDouble);

Ostream* Ostream::PutLong(long value) {
    PutInteger(this, value, "%ld", 2, value > 0);
    return this;
}
EI_REPLACE_METHOD(0x006FC91C, libcrt::Ostream::PutLong);

Ostream* Ostream::PutInt(int value) {
    PutInteger(this, value, "%d", 1, value > 0);
    return this;
}
EI_REPLACE_METHOD(0x006FC9C6, libcrt::Ostream::PutInt);

Ostream* Ostream::Flush() {
    Ios& ios = Base();
    ios.Lock();
    ios.bp->Lock();
    if (ios.bp->Sync() == kEof) ios.state |= Ios::kFail;
    ios.bp->Unlock();
    ios.Unlock();
    return this;
}
EI_REPLACE_METHOD(0x006FC346, libcrt::Ostream::Flush);

Ostream* __cdecl FlushManipulator(Ostream* stream) { return stream->Flush(); }
EI_REPLACE(0x00435000, libcrt::FlushManipulator);

Ostream* __cdecl EndlManipulator(Ostream* stream) { return stream->PutChar('\n')->Flush(); }
EI_REPLACE(0x004BA9F0, libcrt::EndlManipulator);

} // namespace libcrt
