#pragma once

// MSVC 6's old iostream library (<iostream.h>) as the game's streams use it: streambuf, ios and the
// ostream inserters, the constructors, destructors and streambuf's default virtuals (
// src/lib/crt_iostream.cpp). The game's streams are its own objects (the console's eight ostreams and
// their streambufs); these are their layouts. streambuf is a real class; ios and ostream's ios carry
// compiled stand-ins of the exe's tables, which the constructors write: the console's
// ostreams are constant-initialised statics that game.exe's data points at, which a class with a virtual
// base cannot be.

#include <cstddef>
#include <cstdint>
#include <windows.h>   // CRITICAL_SECTION

namespace libcrt {

// streambuf. The slots in the exe's order; overflow and underflow are pure, the rest the
// base class's bodies, which a derived streambuf (the console's) inherits.
class Streambuf {
public:
    // The lock deleted, the base class's sync (non-virtual), the reserve area freed
    // when the streambuf allocated it.
    virtual ~Streambuf();
    virtual int Sync();                                                // EOF if anything is pending
    virtual Streambuf* Setbuf(char* buffer, int length);
    virtual long Seekoff(long offset, int direction, int mode);        // EOF
    virtual long Seekpos(long position, int mode);                     // seekoff from the start
    virtual int Xsputn(const char* text, int count);
    virtual int Xsgetn(char* text, int count);
    virtual int Overflow(int c) = 0;                                   // 7
    virtual int Underflow() = 0;                                       // 8
    virtual int Pbackfail(int c);
    virtual int Doallocate();                                          // 0x200 bytes

    // sputc (the header's inline): into the put area, else overflow; the byte, or EOF.
    int Sputc(int c);
    void Lock();     // when lockFlag is negative: the critical section
    void Unlock();

    // The game's heap (an ios that owns its streambuf deletes it).
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    int allocated;               // +0x04
    int unbuffered;              // +0x08
    int lastChar;                // +0x0C
    char* base;                  // +0x10
    char* ebuf;                  // +0x14
    char* pbase;                 // +0x18
    char* pptr;                  // +0x1C
    char* epptr;                 // +0x20
    char* eback;                 // +0x24
    char* gptr;                  // +0x28
    char* egptr;                 // +0x2C
    int lockFlag;                // +0x30
    CRITICAL_SECTION lock;       // +0x34

protected:
    // streambuf(char*, int): the reserve area `buffer` of `length` bytes (none, and unbuffered,
    // when either is zero), every get / put pointer null, locked (lockFlag -1).
    Streambuf(char* buffer, int length);

private:
    void Setb(char* reserve, char* reserveEnd, int owned);
    int Sputbackc(char c);
};
static_assert(sizeof(Streambuf) == 0x4C);

struct Ostream;

// ios, the ostream's virtual base.
struct Ios {
    // state
    static constexpr int kFail = 0x2;
    static constexpr int kBad = 0x4;
    // flags
    static constexpr long kLeft = 0x2;
    static constexpr long kInternal = 0x8;
    static constexpr long kOct = 0x20;
    static constexpr long kHex = 0x40;
    static constexpr long kShowBase = 0x80;
    static constexpr long kShowPoint = 0x100;
    static constexpr long kUppercase = 0x200;
    static constexpr long kShowPos = 0x400;
    static constexpr long kScientific = 0x800;
    static constexpr long kFixed = 0x1000;
    static constexpr long kFloatField = 0x1800;   // ios::floatfield
    static constexpr long kUnitBuf = 0x2000;
    static constexpr long kStdio = 0x4000;

    void Lock();     // when lockFlag is negative: the critical section
    void Unlock();

    // ios(): no streambuf (badbit), precision 6, fill ' ', locked; the first ios alive also
    // initialises the shared lock (the count of live ios beside it). Returns this.
    Ios* Construct();
    // ~ios: the vtable back to ios's, the locks deleted (the shared one with the last ios), the
    // streambuf deleted when the ios owns it (deleteBuffer), then no streambuf and badbit.
    void Destruct();
    Ios* DeletingDestruct(unsigned deleteFlags);
    // ios::init: an owned streambuf deleted, `buffer` attached; badbit while it is null.
    void Init(Streambuf* buffer);
    // ostream's destructor and deleting destructor, entered with the ios (the virtual base) as `this`:
    // puts back ostream's vtable for the ios; runs it and
    // ~ios, then frees the ostream (8 bytes before the ios) under bit 0; it returns the ostream.
    void DestructOstream();
    Ostream* DeleteOstream(unsigned deleteFlags);

    const void* vtable;          // +0x00
    Streambuf* bp;               // +0x04
    int state;                   // +0x08
    int ispecial;                // +0x0C
    int ospecial;                // +0x10
    int isfxSpecial;             // +0x14
    int osfxSpecial;             // +0x18
    int deleteBuffer;            // +0x1C
    Ostream* tie;                // +0x20
    long flags;                  // +0x24
    int precision;               // +0x28
    char fill;                   // +0x2C
    int width;                   // +0x30
    int lockFlag;                // +0x34
    CRITICAL_SECTION lock;       // +0x38
};
static_assert(sizeof(Ios) == 0x50);

// ostream: its virtual base table (entry 1: the offset of the ios) and x_floatused (set by the header's
// inline operator<<(float): six digits at most instead of fifteen). Each inserter takes the locks, flushes
// the tie, writes padded to the width (writepad) and clears the width; all return the stream.
// A manipulator (ostream& f(ostream&)), as operator<< takes it.
using Manipulator = Ostream*(__cdecl*)(Ostream*);

struct Ostream {
    // ostream(streambuf*): with `mostDerived`, the virtual base table {0, 8} and the
    // ios constructed first; then ostream's vtable on the ios, ios::init(buffer), floatUsed 0. Returns this.
    Ostream* Construct(Streambuf* buffer, int mostDerived);
    Ostream* Apply(Ostream*(__cdecl* manipulator)(Ostream*));   // operator<<(manipulator)
    Ostream* PutChar(char c);
    Ostream* PutUnsigned(unsigned value);                        // "%u"
    Ostream* PutText(const char* text);
    Ostream* PutDouble(double value);
    Ostream* PutLong(long value);                                // "%ld"
    Ostream* PutInt(int value);                                  // "%d"
    Ostream* Flush();                                            // the streambuf's sync

    Ios& Base() { return *reinterpret_cast<Ios*>(reinterpret_cast<char*>(this) + vbtable[1]); }

    const int* vbtable;
    int floatUsed;
};
static_assert(sizeof(Ostream) == 8);   // the ios follows (Ios::DestructOstream)

// The flush manipulator: stream->Flush().
Ostream* __cdecl FlushManipulator(Ostream* stream);
// The game's endl (no Ghidra function; the console's lines): '\n', then Flush.
Ostream* __cdecl EndlManipulator(Ostream* stream);

} // namespace libcrt
