#pragma once

// MSVC 6's old iostream library (<iostream.h>) as the game's streams use it: streambuf, ios and the
// ostream inserters (S4c), the constructors, destructors and streambuf's default virtuals (S4d;
// src/lib/crt_iostream.cpp). The game's streams are its own objects (the console's eight ostreams and
// their streambufs); these are their layouts. The objects carry the exe's vtables (streambuf 007482AC,
// ios 007482E8, ostream's ios 00748280), which the constructors write.

#include <cstdint>

namespace libcrt {

// streambuf (vtable 007482AC). The slots in the exe's order; overflow and underflow are pure. The
// Default* members are the base class's bodies those slots hold, which a derived streambuf (the console's,
// vtable 00740034) inherits; they call the other slots through the object's own vtable.
class Streambuf {
public:
    virtual ~Streambuf() = default;                                    // 0: scalar deleting destructor
    virtual int Sync() = 0;                                            // 1 (006FCC86)
    virtual Streambuf* Setbuf(char* buffer, int length) = 0;           // 2 (006FCB65)
    virtual long Seekoff(long offset, int direction, int mode) = 0;    // 3 (006FCD18)
    virtual long Seekpos(long position, int mode) = 0;                 // 4 (006FCD1E)
    virtual int Xsputn(const char* text, int count) = 0;               // 5 (006FCB99)
    virtual int Xsgetn(char* text, int count) = 0;                     // 6 (006FCBEF)
    virtual int Overflow(int c) = 0;                                   // 7
    virtual int Underflow() = 0;                                       // 8
    virtual int Pbackfail(int c) = 0;                                  // 9 (006FCD30)
    virtual int Doallocate() = 0;                                      // 10 (006FCCBB)

    // sputc (006FC12C, the header's inline): into the put area, else overflow; the byte, or EOF.
    int Sputc(int c);
    void Lock();     // when lockFlag is negative: the critical section
    void Unlock();

    // streambuf(char*, int) (006FCAD2, RET 8): the reserve area `buffer` of `length` bytes (none, and
    // unbuffered, when either is zero), every get / put pointer null, locked (lockFlag -1).
    Streambuf* Construct(char* buffer, int length);
    // ~streambuf (006FCB35): the vtable back to streambuf's, the lock deleted, the base class's sync
    // (non-virtual), the reserve area freed when the streambuf allocated it.
    void Destruct();
    Streambuf* DeletingDestruct(unsigned flags);                       // 006FCAB6 (slot 0, RET 4)
    int DefaultSync();                                                 // 006FCC86: EOF if anything is pending
    Streambuf* DefaultSetbuf(char* buffer, int length);                // 006FCB65 (RET 8)
    long DefaultSeekoff(long offset, int direction, int mode);         // 006FCD18: EOF (RET 0xC)
    long DefaultSeekpos(long position, int mode);                      // 006FCD1E: seekoff from the start
    int DefaultXsputn(const char* text, int count);                    // 006FCB99 (RET 8)
    int DefaultXsgetn(char* text, int count);                          // 006FCBEF (RET 8)
    int DefaultPbackfail(int c);                                       // 006FCD30 (RET 4)
    int DefaultDoallocate();                                           // 006FCCBB: 0x200 bytes

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
    std::uint8_t lock[0x18];     // +0x34 CRITICAL_SECTION

private:
    void SetVtable(const void* table) { *reinterpret_cast<const void**>(this) = table; }
    void Setb(char* reserve, char* reserveEnd, int owned);   // 006FCCE8
    int Sputbackc(char c);                                   // 006FCD8C
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
    static constexpr long kFloatField = 0x1800;   // ios::floatfield (007482E0)
    static constexpr long kUnitBuf = 0x2000;
    static constexpr long kStdio = 0x4000;

    void Lock();     // when lockFlag is negative: the critical section
    void Unlock();

    // ios() (006FCDB1): no streambuf (badbit), precision 6, fill ' ', locked; the first ios alive also
    // initialises the shared lock 007CAE90 (the count at 007CAEC8). Returns this.
    Ios* Construct();
    // ~ios (006FCEDC): the vtable back to ios's, the locks deleted (the shared one with the last ios), the
    // streambuf deleted when the ios owns it (deleteBuffer), then no streambuf and badbit.
    void Destruct();
    Ios* DeletingDestruct(unsigned deleteFlags);   // 006FCE0F (slot 0, RET 4)
    // ios::init (006FCF26, RET 4): an owned streambuf deleted, `buffer` attached; badbit while it is null.
    void Init(Streambuf* buffer);
    // ostream's destructor and deleting destructor, entered with the ios (the virtual base) as `this`:
    // 006FC502 puts back ostream's vtable for the ios; 006FC404 (slot 0 of 00748280, RET 4) runs it and
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
    std::uint8_t lock[0x18];     // +0x38 CRITICAL_SECTION
};
static_assert(sizeof(Ios) == 0x50);

// ostream: its virtual base table (entry 1: the offset of the ios) and x_floatused (set by the header's
// inline operator<<(float): six digits at most instead of fifteen). Each inserter takes the locks, flushes
// the tie, writes padded to the width (writepad) and clears the width; all return the stream.
struct Ostream {
    // ostream(streambuf*) (006FC42F, RET 8): with `mostDerived`, the virtual base table (00748284) and the
    // ios constructed first; then ostream's vtable on the ios, ios::init(buffer), floatUsed 0. Returns this.
    Ostream* Construct(Streambuf* buffer, int mostDerived);
    Ostream* Apply(Ostream*(__cdecl* manipulator)(Ostream*));   // 00434FE0 operator<<(manipulator) (RET 4)
    Ostream* PutChar(char c);                                    // 006FC0B0 (RET 4)
    Ostream* PutUnsigned(unsigned value);                        // 006FC14E "%u" (RET 4)
    Ostream* PutText(const char* text);                          // 006FC31D (RET 4)
    Ostream* PutDouble(double value);                            // 006FC7FA (RET 8)
    Ostream* PutLong(long value);                                // 006FC91C "%ld" (RET 4)
    Ostream* PutInt(int value);                                  // 006FC9C6 "%d" (RET 4)
    Ostream* Flush();                                            // 006FC346: the streambuf's sync

    Ios& Base() { return *reinterpret_cast<Ios*>(reinterpret_cast<char*>(this) + vbtable[1]); }

    const int* vbtable;
    int floatUsed;
};
static_assert(sizeof(Ostream) == 8);   // the ios follows (Ios::DestructOstream)

// The flush manipulator (00435000): stream->Flush().
Ostream* __cdecl FlushManipulator(Ostream* stream);

} // namespace libcrt
