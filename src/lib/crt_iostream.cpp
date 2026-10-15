// MSVC 6's ostream inserters (S4c, docs/notes/standalone.md; crt_iostream.h). Both builds: they keep no
// state of their own (the streams are the game's). opfx / osfx / writepad are the bodies' own helpers;
// the exe's other callers of them (put, write, the streambuf inserter: not reached by the game's code)
// keep the exe's. The numbers go through the game's sprintf, the exponent letter through its toupper.
// tests/verify/verify_crt_iostream.cpp compares each inserter with the original.
#include "crt_iostream.h"

#include "crt.h"
#include "ei/hook.h"
#include "gameapi.h"

#include <cstring>

namespace libcrt {

namespace {

constexpr int kEof = -1;

CRITICAL_SECTION* Section(std::uint8_t* lock) { return reinterpret_cast<CRITICAL_SECTION*>(lock); }

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

} // namespace libcrt
