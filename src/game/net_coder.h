#pragma once

// The byte buffers, the bit cursors over them and the LZ coder used to compress save games
// (00579B80 / 00579830), multiplayer map transfers (00662AC0) and network messages (0047D7F0 /
// 0047D970, 0041D600). See docs/notes/network.md, "Compression". Names provisional.

#include "ei/vcall.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// A byte buffer (0x1C bytes). Two classes share the layout:
//   0073D624  growing buffer (constructor 004DA7F0, destructor 004DA910): slot 0 / 1 make room
//             for n more bytes to read / write (004DA630 / 004DA6B0: a new block of
//             (base - begin + cur + n) * 2 + 0x40 bytes), slot 2 commit (empty), slot 3 (0), slot 4
//             writes past the end, growing (004DA740), slot 5 deleting destructor (004DA8F0), slot 6
//             Seek (004DA860, grows when needed).
//   0073CA64  fixed view over memory owned elsewhere: slots 0 / 1 / 2 empty, 3 / 4 (0), 5 deleting
//             destructor (0047D940, body 0047D960), 6 Seek (0047D930). Its base 0073CA48 has only
//             slot 2 (empty, 0047D900) and slot 5 (deleting destructor 0047D910); the views live on
//             the stack and are destroyed by setting the base's vtable (inlined, or 0047D960 when
//             unwinding).
// Like the streams, the class is __declspec(novtable): the game's constructors (or InitView) store
// the vtables, and C++ never does. Copies are byte copies, the vtable included, as they were when
// this was a plain struct (the verify wrappers copy buffers).
namespace net {
struct StringRep;
}

class __declspec(novtable) GameBuffer {
public:
    GameBuffer() {}
    GameBuffer(const GameBuffer& other) { std::memcpy(static_cast<void*>(this), &other, sizeof(GameBuffer)); }
    GameBuffer& operator=(const GameBuffer& other) {
        std::memcpy(static_cast<void*>(this), &other, sizeof(GameBuffer));
        return *this;
    }

    virtual void ReserveRead(std::int32_t size);                    // 0: room for `size` more bytes to read
    virtual void ReserveWrite(std::int32_t size);                   // 1: room for `size` more bytes to write
    virtual std::uint32_t Commit();                                 // 2: after a cursor stored its position in `cur` (empty; EAX as it was)
    virtual int ReadMore(void* to, std::uint32_t size);             // 3: a read past the end (0 in both classes)
    virtual int WriteMore(const void* from, std::uint32_t size);    // 4: a write past the end (the growing buffer grows)
    virtual ~GameBuffer() {}                                        // 5: deleting destructor
    virtual std::uint8_t* Seek(std::int32_t position);              // 6: the cursor to `position` bytes from the start

    // A fixed view (vtable 0073CA64, flags 2) over [first, last), as the coder's callers build it.
    void InitView(std::uint8_t* first, std::uint8_t* last) {
        ei::SetVtable(this, kViewVtableAddress);
        begin = first;
        cur = first;
        end = last;
        high = last;
        base = 0;
        flags = 2;
    }

    static constexpr std::uintptr_t kViewVtableAddress = 0x0073CA64;

    // The growing buffer's non-virtual members (game_buffer.cpp).
    // 004DA7F0. Constructor: growing vtable, base 0, flags 6, a 0x20-byte block (cursor and high at
    // its start). Returns this.
    GameBuffer* ConstructGrowing();
    // 004DA9E0 (RET 4). Copy constructor: growing vtable, base 0, a block of `from`'s capacity with
    // all of its bytes, the same cursor / high offsets and flags. Returns this.
    GameBuffer* ConstructCopy(const GameBuffer* from);
    // 004DAA80 (RET 4). Assignment: frees the block, then copies as ConstructCopy (vtable and base
    // kept). Returns this.
    GameBuffer* AssignFrom(const GameBuffer* from);
    // 004DA910. Destructor body: growing vtable, the block freed, the view base's vtable left.
    void DestructGrowing();
    // 004DA5C0 (RET 4). Moves the contents to a new block of size * 2 + 0x40 bytes.
    void Grow(std::uint32_t size);
    // game_buffer_strings.cpp (A-S4). Length-prefixed strings: the length * 2 in one byte, or
    // length * 2 + 1 in four bytes from 0x80 on.
    // 004DA200 (RET 8, AL). Reads one into the STLport string `s` (assign): false with error bit 3
    // when it is longer than `maxLength` (> 0). The bytes are read in place (slot 0 first when they
    // run past `end`), then slot 2.
    bool ReadString(net::StringRep* s, int maxLength);
    // 004DA380 (RET 4). Writes `s`: in place when it fits before `end` (flags bit 0), else
    // through slot 4.
    void WriteString(const net::StringRep* s);
    // 004DA170 (RET 8). A read of `size` bytes past `high`: error bit 3 set, `to` zeroed, then what
    // is left before `high` (through slot 3 when that crosses `end`); the cursor ends at `high`.
    // Returns the bytes read.
    std::uint32_t ReadPastEnd(void* to, std::uint32_t size);
    // The block replaced by one of `capacity` bytes with the old capacity's bytes; the pointers
    // move with it (inlined in every growing slot).
    void Reallocate(std::uint32_t capacity);
    // 00580EE0 (RET 4; buffered_file.cpp). Before `size` bytes are written in place: flags bit 0
    // set, room made (slot 1) when the block ends before them. Returns the cursor.
    std::uint8_t* BeginWrite(std::int32_t size);
    // 00436C30 (net_session_tick.cpp). Copies `size` bytes to the write position and moves it on (no
    // room check). Returns this.
    GameBuffer* BufferWriteRaw(const void* from, unsigned size);
    // 00436C60 (RET 8; net_session_tick.cpp). Writes `size` bytes: in place (flags bit 0 set) when
    // they fit before `end`, otherwise through slot 4. Returns `size` or what slot 4 left.
    unsigned BufferWrite(const void* from, unsigned size);

    std::uint8_t* begin;       // +0x04
    std::uint8_t* cur;         // +0x08
    std::uint8_t* end;         // +0x0C capacity
    std::uint8_t* high;        // +0x10 highest position written (updated lazily)
    std::int32_t base;         // +0x14 position of `begin` in the whole stream (0 for these)
    std::uint32_t flags;       // +0x18 (bit 0: written through a cursor)
};
static_assert(offsetof(GameBuffer, begin) == 0x04);
static_assert(offsetof(GameBuffer, flags) == 0x18);
static_assert(sizeof(GameBuffer) == 0x1C);

constexpr std::uintptr_t kGrowingBufferVtable = 0x0073D624;
constexpr std::uintptr_t kBufferViewVtable = GameBuffer::kViewVtableAddress;
constexpr std::uintptr_t kBufferViewBaseVtable = 0x0073CA48;

// The fixed view's own slots (free functions registered at their addresses, rewrite-guide rule 4).
// 0047D930 (slot 6, RET 4): the cursor moved to `position` bytes from the start; returns it.
std::uint8_t* __fastcall BufferViewSeek(GameBuffer* self, void*, std::int32_t position);
// 0047D960: the base's vtable left (the view owns nothing).
void __fastcall BufferViewDestruct(GameBuffer* self);
// 0047D940 (slot 5, RET 4) and the base's 0047D910 (slot 5 of 0073CA48, RET 4): deleting
// destructors (the base's vtable left; freed when bit 0 of `flags` is set). Return `self`.
void* __fastcall BufferViewDeletingDestructor(GameBuffer* self, void*, unsigned flags);
void* __fastcall BufferViewBaseDeletingDestructor(GameBuffer* self, void*, unsigned flags);
// 004DA5A0 / 004DA5B0 (view slots 3 / 4) and 004DA730 (growing slot 3), RET 8: 0.
int __fastcall BufferNothingMore(GameBuffer* self, void*, const void* data, std::uint32_t size);

// The growing buffer's slots (game_buffer.cpp).
// 004DA630 / 004DA6B0 (slots 0 / 1, RET 4, byte-identical): always moves the contents to a new
// block of (base - begin + cur + size) * 2 + 0x40 bytes (callers test the room first).
void __fastcall GrowingBufferReserve(GameBuffer* self, void*, std::int32_t size);
// 004DA740 (slot 4, RET 8): grows as Reserve, then copies `size` bytes to the cursor; returns `size`.
int __fastcall GrowingBufferWriteMore(GameBuffer* self, void*, const void* from, std::uint32_t size);
// 004DA860 (slot 6, RET 4): `high` raised to the cursor, the cursor moved to begin + position; past
// `end` the contents move to a block of position * 2 + 0x40 bytes. Returns the cursor, or the new
// capacity when it grew (EAX as the original leaves it).
std::uint8_t* __fastcall GrowingBufferSeek(GameBuffer* self, void*, std::int32_t position);
// 004DA8F0 (slot 5, RET 4): DestructGrowing, freed when bit 0 of `flags` is set; returns `self`.
GameBuffer* __fastcall GrowingBufferDeletingDestructor(GameBuffer* self, void*, unsigned flags);

// A cursor over a GameBuffer (0x18 bytes) that reads and writes bytes and LSB-first bit fields.
// Bits are collected in `acc`; a byte is reserved at `bytePtr` when a field starts in a new
// byte and stored there once more than 8 bits are pending (exactly 8 stay pending).
struct BufferCursor {
    std::uint8_t* p;           // +0x00 next byte
    std::uint8_t* bytePtr;     // +0x04 the reserved byte the pending bits go to
    std::uint32_t acc;         // +0x08 pending bits
    std::uint8_t bits;         // +0x0C number of pending bits
    std::uint8_t unused_0D[3];
    GameBuffer* sink;          // +0x10 the buffer `p` is committed to at the end (or null)
    std::uint8_t* start;       // +0x14 `p` when the cursor began

    // 004301E0. Appends the low `count` bits of `value` (writing).
    void PutBits(std::uint32_t value, std::uint8_t count);
    // 004357C0 (net_connection.cpp). Appends one bit (`bit` is added unmasked, as the original).
    void PutBit(std::uint32_t bit);
    // 00431040. Next bit (reading).
    std::uint32_t GetBit();
    // 00430FE0. Next `count` bits (reading), masked with the table at 0078DB24.
    std::uint32_t GetBits(std::uint32_t count);
    // 00430930. Copies `size` bytes to `p`.
    void WriteBytes(const void* from, std::uint32_t size);
    // 00430F60. Copies `size` bytes from `p`.
    void ReadBytes(void* to, std::uint32_t size);
    // 00430FC0. Starts reading at `at` (the other two arguments are not used).
    void Reset(std::uint8_t* at, std::uint32_t unused1, std::uint32_t unused2);
    // 00442570. Stores the pending bits in the reserved byte.
    void FlushBits();
    // 00430F90. FlushBits, then commits `p` to the sink (which must exist) and drops it. Returns
    // what the sink's commit left in EAX.
    std::uint32_t EndWrite();
    // 0041D930 (also inlined in many places). FlushBits, then commits `p` to the sink if any.
    // Returns the commit's EAX, or `this` without a sink (00579830 returns it with AL cleared).
    std::uint32_t End();

    // 004DA450 / 004DA490 (game_buffer.cpp, RET 8) bind the cursor to a buffer with room for
    // `size` more bytes (slot 0 / slot 1 of the buffer when short): sink, start and p set, no
    // pending bits (acc is left); the write variant first sets the buffer's flag bit 0.
    void BeginRead(GameBuffer* buffer, std::int32_t size);
    void BeginWrite(GameBuffer* buffer, std::int32_t size);
};
static_assert(sizeof(BufferCursor) == 0x18);
static_assert(offsetof(BufferCursor, bits) == 0x0C);
static_assert(offsetof(BufferCursor, sink) == 0x10);

// 0041D960: the same as BufferCursor::End for the cursor at +0x2800 of a CoderState (returns the
// coder without a sink).
struct CoderState;
std::uint32_t __fastcall CoderEndOutput(CoderState* coder);

// The 0x2828-byte LZ coder state (constructor 0042FCA0 in core_streams.cpp). The window keeps the
// last 0x400 bytes twice (+0 and +0x400) so that comparisons can run past the wrap. Compressed
// format: the uncompressed size (int32), then per item a flag bit: 0 and a literal byte (byte
// aligned at the output position), or 1, a length code and an offset code (bit fields).
struct CoderState {
    std::uint8_t window[0x800];      // +0x000
    std::int32_t chain[0x400];       // +0x800 distance to the previous position with the same hash
    std::int32_t head[0x400];        // +0x1800 last position per hash ((previous byte << 2) ^ byte)
    BufferCursor out;                // +0x2800
    std::uint32_t pos;               // +0x2818 window position
    std::int32_t matchPos;           // +0x281C -1: no match in progress
    std::int32_t matchLen;           // +0x2820
    std::uint32_t prev;              // +0x2824 the previous input byte

    // 0042FCA0 (core_streams.cpp). Returns this.
    CoderState* Construct();
    // 00430450. Compresses everything in `in` (from its start) to `out` at its cursor.
    void Compress(GameBuffer* in, GameBuffer* out);
    // 00430960. Decompresses `in` from its cursor to `out` at its cursor.
    void Decompress(GameBuffer* in, GameBuffer* out);
    // 0042FD10. Writes the match in progress (length and offset codes); ends it.
    void EncodeMatch(std::int32_t at);
    // 006671A0 (player_client.cpp, A-H10). Ends the output (out.End: pending bits stored, the sink
    // committed), then Construct.
    void Reset();
};
static_assert(offsetof(CoderState, chain) == 0x800);
static_assert(offsetof(CoderState, head) == 0x1800);
static_assert(offsetof(CoderState, out) == 0x2800);
static_assert(offsetof(CoderState, pos) == 0x2818);
static_assert(offsetof(CoderState, prev) == 0x2824);
static_assert(sizeof(CoderState) == 0x2828);
