#pragma once

// The byte buffers, the bit cursors over them and the LZ coder that compresses save games, multiplayer map
// transfers and network messages. See docs/notes/network.md, "Compression".

#include <cstddef>
#include <cstdint>
#include <cstring>

// A byte buffer: GrowingBuffer, BufferView (a fixed view over memory owned elsewhere) and BufferedStream (a
// window onto a file).
namespace stlp {
class string;
}

class GameBuffer {
public:
    GameBuffer(const GameBuffer&) = delete;
    GameBuffer& operator=(const GameBuffer&) = delete;

    virtual void ReserveRead(std::int32_t size) = 0;                // room for `size` more bytes to read
    virtual void ReserveWrite(std::int32_t size) = 0;               // room for `size` more bytes to write
    virtual std::uint32_t Commit() { return 0; }                    // no caller reads its result
    virtual int ReadMore(void* to, std::uint32_t size) = 0;         // a read past the end (0 in both classes)
    virtual int WriteMore(const void* from, std::uint32_t size) = 0;   // a write past the end
    virtual ~GameBuffer() {}
    virtual std::uint8_t* Seek(std::int32_t position) = 0;          // the cursor to `position` bytes from the start

    unsigned ReadBytes(void* to, unsigned size);
    GameBuffer* CopyOut(void* to, unsigned size);
    // Moves the contents to a new block of size * 2 + 0x40 bytes.
    void Grow(std::uint32_t size);
    // Length-prefixed strings: the length * 2 in one byte, or length * 2 + 1 in four bytes from 0x80 on.
    // Reads one into the STLport string `s` (assign): false with error bit 3 when it is longer than
    // `maxLength` (> 0). The bytes are read in place (ReserveRead first when they run past `end`), then
    // Commit.
    bool ReadString(stlp::string* s, int maxLength);
    // Writes `s`: in place when it fits before `end` (flags bit 0), else through WriteMore.
    void WriteString(const stlp::string* s);
    // A read of `size` bytes past `high`: error bit 3 set, `to` zeroed, then what is left before `high`
    // (through ReadMore when that crosses `end`); the cursor ends at `high`. Returns the bytes read.
    std::uint32_t ReadPastEnd(void* to, std::uint32_t size);
    // The block replaced by one of `capacity` bytes with the old capacity's bytes; the pointers move with it.
    void Reallocate(std::uint32_t capacity);
    // Before `size` bytes are written in place: flags bit 0 set, room made (ReserveWrite) when the block ends
    // before them. Returns the cursor.
    std::uint8_t* BeginWrite(std::int32_t size);
    // Copies `size` bytes to the write position and moves it on (no room check).
    GameBuffer* BufferWriteRaw(const void* from, unsigned size);
    // Writes `size` bytes: in place (flags bit 0 set) when they fit before `end`, otherwise through WriteMore.
    // Returns `size` or what WriteMore left.
    unsigned BufferWrite(const void* from, unsigned size);

    // The stream position of `at` in the block: base plus its offset from begin.
    std::int32_t PositionOf(const std::uint8_t* at) const { return base + static_cast<std::int32_t>(at - begin); }

    std::uint8_t* begin;
    std::uint8_t* cur;
    std::uint8_t* end;         // capacity
    std::uint8_t* high;        // highest position written (updated lazily)
    std::int32_t base;         // position of `begin` in the whole stream (0 for these)
    std::uint32_t flags;       // bit 0: written through a cursor

protected:
    GameBuffer() {}
};
// tests/vectors/record_coder.cpp reads game.exe's buffers through this layout.
static_assert(offsetof(GameBuffer, begin) == 0x04);
static_assert(offsetof(GameBuffer, flags) == 0x18);
static_assert(sizeof(GameBuffer) == 0x1C);

class GrowingBuffer final : public GameBuffer {
public:
    // Base 0, flags 6, a 0x20-byte block (cursor and high at its start).
    GrowingBuffer();
    // Base 0, a block of `from`'s capacity with all of its bytes, the same cursor / high offsets and flags.
    GrowingBuffer(const GrowingBuffer& from);
    // The block freed, then copied as the copy constructor (base kept).
    GrowingBuffer& operator=(const GrowingBuffer& from);
    ~GrowingBuffer() override;

    // Both always move the contents to a new block of (base - begin + cur + size) * 2 + 0x40 bytes (callers
    // test the room first).
    void ReserveRead(std::int32_t size) override;
    void ReserveWrite(std::int32_t size) override;
    int ReadMore(void*, std::uint32_t) override;
    // Grows as ReserveWrite, then copies `size` bytes to the cursor; returns `size`.
    int WriteMore(const void* from, std::uint32_t size) override;
    // `high` raised to the cursor, the cursor moved to begin + position; past `end` the contents move to a
    // block of position * 2 + 0x40 bytes. Returns the cursor, or the new capacity when it grew.
    std::uint8_t* Seek(std::int32_t position) override;
};

// A fixed view (flags 2) over [first, last), as the coder's callers build it.
class BufferView final : public GameBuffer {
public:
    BufferView(std::uint8_t* first, std::uint8_t* last) {
        begin = first;
        cur = first;
        end = last;
        high = last;
        base = 0;
        flags = 2;
    }
    ~BufferView() override;

    void ReserveRead(std::int32_t) override;    // nothing to reserve
    void ReserveWrite(std::int32_t) override;
    int ReadMore(void*, std::uint32_t) override;
    int WriteMore(const void*, std::uint32_t) override;
    // The cursor moved to `position` bytes from the start; returns it.
    std::uint8_t* Seek(std::int32_t position) override;
};

// A cursor over a GameBuffer that reads and writes bytes and LSB-first bit fields. Bits are collected in
// `acc`; a byte is reserved at `bytePtr` when a field starts in a new byte and stored there once more than 8
// bits are pending (exactly 8 stay pending).
struct BufferCursor {
    std::uint8_t* p;           // next byte
    std::uint8_t* bytePtr;     // the reserved byte the pending bits go to
    std::uint32_t acc;         // pending bits
    std::uint8_t bits;         // number of pending bits
    std::uint8_t unused_0D[3];
    GameBuffer* sink;          // the buffer `p` is committed to at the end (or null)
    std::uint8_t* start;       // `p` when the cursor began

    // Appends the low `count` bits of `value` (writing).
    void PutBits(std::uint32_t value, std::uint8_t count);
    // Appends one bit (`bit` is added unmasked).
    void PutBit(std::uint32_t bit);
    std::uint32_t GetBit();
    // Next `count` bits (reading, 1..32), masked with kCoderBitMasks.
    std::uint32_t GetBits(std::uint32_t count);
    // Copies `size` bytes to `p`.
    void WriteBytes(const void* from, std::uint32_t size);
    // Copies `size` bytes from `p`.
    void ReadBytes(void* to, std::uint32_t size);
    // Starts reading at `at` (the other two arguments are not used).
    void Reset(std::uint8_t* at, std::uint32_t unused1, std::uint32_t unused2);
    // Stores the pending bits in the reserved byte.
    void FlushBits();
    // FlushBits, then commits `p` to the sink (which must exist) and drops it. Returns the commit's result.
    std::uint32_t EndWrite();
    // FlushBits, then commits `p` to the sink if any. Returns the commit's result, 0 without a sink.
    std::uint32_t End();

    // Bind the cursor to a buffer with room for `size` more bytes (ReserveRead / ReserveWrite when short):
    // sink, start and p set, no pending bits (acc is left); the write variant first sets flags bit 0.
    void BeginRead(GameBuffer* buffer, std::int32_t size);
    void BeginWrite(GameBuffer* buffer, std::int32_t size);
};
static_assert(sizeof(BufferCursor) == 0x18);
static_assert(offsetof(BufferCursor, bits) == 0x0C);
static_assert(offsetof(BufferCursor, sink) == 0x10);

struct CoderState;

// The LZ coder state. The window keeps the last 0x400 bytes twice (at 0 and 0x400) so that comparisons can
// run past the wrap. Compressed format: the uncompressed size (int32), then per item a flag bit: 0 and a
// literal byte (byte aligned at the output position), or 1, a length code and an offset code (bit fields).
struct CoderState {
    std::uint8_t window[0x800];
    std::int32_t chain[0x400];       // distance to the previous position with the same hash
    std::int32_t head[0x400];        // last position per hash ((previous byte << 2) ^ byte)
    BufferCursor out;
    std::uint32_t pos;               // window position
    std::int32_t matchPos;           // -1: no match in progress
    std::int32_t matchLen;
    std::uint32_t prev;              // the previous input byte

    CoderState* Construct();
    // Compresses everything in `in` (from its start) to `out` at its cursor.
    void Compress(GameBuffer* in, GameBuffer* out);
    // Decompresses `in` from its cursor to `out` at its cursor.
    void Decompress(GameBuffer* in, GameBuffer* out);
    // Writes the match in progress (length and offset codes); ends it.
    void EncodeMatch(std::int32_t at);
    // Ends the output (out.End: pending bits stored, the sink committed), then Construct.
    void Reset();
};
static_assert(offsetof(CoderState, chain) == 0x800);
static_assert(offsetof(CoderState, head) == 0x1800);
static_assert(offsetof(CoderState, out) == 0x2800);
static_assert(offsetof(CoderState, pos) == 0x2818);
static_assert(offsetof(CoderState, prev) == 0x2824);
static_assert(sizeof(CoderState) == 0x2828);

// The code tables of the coder's lengths and distances, filled at start-up by InitCodeTables: the code and
// its bit count of each match length 1..0x22 (at length - 1) and of each distance bucket 0..0x21.
extern std::uint32_t g_offsetBits[34];
extern std::uint32_t g_offsetCodes[34];
extern std::uint32_t g_lengthBits[34];
extern std::uint32_t g_lengthCodes[34];

// (1 << n) - 1 for n = 1..32, at n - 1 (BufferCursor::GetBits).
inline constexpr std::uint32_t kCoderBitMasks[32] = /*0078DB28*/ {
    0x1,       0x3,       0x7,       0xF,       0x1F,       0x3F,       0x7F,       0xFF,
    0x1FF,     0x3FF,     0x7FF,     0xFFF,     0x1FFF,     0x3FFF,     0x7FFF,     0xFFFF,
    0x1FFFF,   0x3FFFF,   0x7FFFF,   0xFFFFF,   0x1FFFFF,   0x3FFFFF,   0x7FFFFF,   0xFFFFFF,
    0x1FFFFFF, 0x3FFFFFF, 0x7FFFFFF, 0xFFFFFFF, 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF};
