#pragma once

// The byte buffers, the bit cursors over them and the LZ coder that compresses save games, multiplayer map
// transfers and network messages. See docs/notes/network.md, "Compression".

#include "lib/bytes.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// A byte buffer: GrowingBuffer and BufferView (a fixed view over memory owned elsewhere). `cur` is where the next
// byte is read or written; `high` is the highest position written, raised to `cur` lazily (Size). `cur` and `high`
// never pass `end`.
class GameBuffer {
public:
    GameBuffer(const GameBuffer&) = delete;
    GameBuffer& operator=(const GameBuffer&) = delete;
    virtual ~GameBuffer() {}

    // Room for `size` more bytes from the cursor: the growing buffer grows, a view has what it has.
    virtual void Reserve(std::int32_t size) = 0;
    // A write of `size` bytes that do not fit before `end`: the growing buffer grows, a view drops them.
    virtual void WriteMore(const void* from, std::uint32_t size) = 0;
    // The cursor to `position` bytes from the start.
    virtual void Seek(std::int32_t position) = 0;

    // The bytes written so far (`high` raised to the cursor first).
    std::int32_t Size() {
        if (cur > high) high = cur;
        return PositionOf(high);
    }
    // The bytes from the cursor to the end of what was written.
    std::int32_t Unread() { return Size() - PositionOf(cur); }
    // Emptied: the cursor and `high` at the start, no read error.
    void Clear() {
        cur = begin;
        high = begin;
        readPastEnd = false;
    }

    // `size` bytes at the cursor: in place when they fit before `end`, else through WriteMore.
    void Write(const void* from, std::uint32_t size);
    // `size` bytes from the cursor. Past `high`: readPastEnd set, `to` zeroed, then what is left before `high`
    // read (the cursor ends at `high`).
    void Read(void* to, std::uint32_t size);
    template <typename T>
    void Put(const T& value) {
        static_assert(lib::kFormatValue<T>, "a message value: plain bytes without padding, one size in every build");
        Write(&value, sizeof(T));
    }
    template <typename T>
    void Get(T& value) {
        static_assert(lib::kFormatValue<T>, "a message value: plain bytes without padding, one size in every build");
        Read(&value, sizeof(T));
    }
    // Length-prefixed strings: the length * 2 in one byte, or length * 2 + 1 in four bytes from 0x80 on.
    // Reads one into `s`: false with readPastEnd set when it is longer than `maxLength` (> 0). The bytes are read
    // in place (Reserve first when they run past `end`).
    bool ReadString(std::string* s, int maxLength);
    void WriteString(std::string_view s);

    // The position of `at` from the start.
    std::int32_t PositionOf(const std::uint8_t* at) const { return static_cast<std::int32_t>(at - begin); }

    std::uint8_t* begin = nullptr;
    std::uint8_t* cur = nullptr;
    std::uint8_t* end = nullptr;  // capacity
    std::uint8_t* high = nullptr;  // highest position written (raised lazily)
    bool readPastEnd = false;   // a read ran past `high` (or a string past its limit)

protected:
    GameBuffer() {}
    // `block` (the derived class's, which [begin, end) views) resized to `capacity` bytes, keeping the bytes
    // both sizes hold; the pointers keep their offsets.
    void ResizeBlock(std::vector<std::uint8_t>& block, std::uint32_t capacity);
};

class GrowingBuffer final : public GameBuffer {
public:
    // A 0x20-byte block (cursor and high at its start).
    GrowingBuffer();
    GrowingBuffer(const GrowingBuffer& from);
    // A block of `from`'s capacity with all of its bytes, the same cursor / high offsets and read error.
    GrowingBuffer& operator=(const GrowingBuffer& from);

    // Always moves the contents to a new block of (cur + size) * 2 + 0x40 bytes (callers test the room first).
    void Reserve(std::int32_t size) override;
    // Grows as Reserve, then copies `size` bytes to the cursor.
    void WriteMore(const void* from, std::uint32_t size) override;
    // `high` raised to the cursor, the cursor moved to begin + position; past `end` the contents move to a
    // block of position * 2 + 0x40 bytes.
    void Seek(std::int32_t position) override;
    // The cursor at the start and `size` bytes held (`high` after them); the block grows to size * 2 + 0x40 bytes
    // when they pass its end.
    void SetSize(std::uint32_t size);

private:
    void Reallocate(std::uint32_t capacity) { ResizeBlock(block, capacity); }

    std::vector<std::uint8_t> block;   // the bytes [begin, end)
};

// A fixed view over [first, last), all of it written.
class BufferView final : public GameBuffer {
public:
    BufferView(std::uint8_t* first, std::uint8_t* last) {
        begin = first;
        cur = first;
        end = last;
        high = last;
    }

    void Reserve(std::int32_t) override {}
    void WriteMore(const void*, std::uint32_t) override {}
    void Seek(std::int32_t position) override { cur = begin + position; }
};

// A cursor over a GameBuffer that reads and writes bytes and LSB-first bit fields. Bits are collected in
// `acc`; a byte is reserved at `bytePtr` when a field starts in a new byte and stored there once more than 8
// bits are pending (exactly 8 stay pending). Reads stop at `limit`: bytes and bits past it read as zero and `p`
// stays at `limit`.
struct BufferCursor {
    std::uint8_t* p = nullptr;           // next byte
    std::uint8_t* bytePtr = nullptr;     // the reserved byte the pending bits go to
    std::uint32_t acc = 0;               // pending bits
    std::uint8_t bits = 0;               // number of pending bits
    GameBuffer* sink = nullptr;          // the buffer `p` is committed to at the end (or null)
    const std::uint8_t* limit = nullptr; // reading: one past the last byte there is to read

    // Bind the cursor to a buffer with room for `size` more bytes (Reserve when short): sink and p set, no
    // pending bits (acc is left); reading also sets the limit `size` bytes on.
    void BeginRead(GameBuffer* buffer, std::int32_t size);
    void BeginWrite(GameBuffer* buffer, std::int32_t size);
    // Stores the pending bits in the reserved byte, then commits `p` to the sink if any and drops it.
    void End();

    // Appends the low `count` bits of `value` (`value` is added unmasked).
    void PutBits(std::uint32_t value, std::uint8_t count);
    std::uint32_t GetBit();
    // Next `count` bits (1..32), masked with kCoderBitMasks.
    std::uint32_t GetBits(std::uint32_t count);

    void WriteBytes(const void* from, std::uint32_t size);
    void ReadBytes(void* to, std::uint32_t size);
    template <typename T>
    void Put(const T& value) {
        static_assert(lib::kFormatValue<T>, "a message value: plain bytes without padding, one size in every build");
        WriteBytes(&value, sizeof(T));
    }
    template <typename T>
    void Take(T& value) {
        static_assert(lib::kFormatValue<T>, "a message value: plain bytes without padding, one size in every build");
        ReadBytes(&value, sizeof(T));
    }
    // A zero-terminated text, in the asset code page on the wire (asset_text.h) as vanilla peers send it.
    void PutString(const std::string& s);
    // The text runs to its zero byte, or to the limit without one (the original read on past a packet's end
    // until it met a zero byte).
    void TakeString(std::string& s);

private:
    std::size_t Room() const { return p < limit ? static_cast<std::size_t>(limit - p) : 0; }
    std::uint8_t NextByte() { return p < limit ? *p++ : 0; }
};

struct CoderState;

// The LZ coder state. The window keeps the last 0x400 bytes twice (at 0 and 0x400) so that comparisons can
// run past the wrap. Compressed format: the uncompressed size (int32), then per item a flag bit: 0 and a
// literal byte (byte aligned at the output position), or 1, a length code and an offset code (bit fields).
struct CoderState {
    // Start's state (the window's other bytes are not written).
    CoderState() { Start(); }
    ~CoderState() { out.End(); }

    std::uint8_t window[0x800] = {};
    std::int32_t chain[0x400] = {};  // distance to the previous position with the same hash
    std::int32_t head[0x400] = {};   // last position per hash ((previous byte << 2) ^ byte)
    BufferCursor out;
    std::uint32_t pos = 0;           // window position
    std::int32_t matchPos = 0;       // -1: no match in progress
    std::int32_t matchLen = 0;
    std::uint32_t prev = 0;          // the previous input byte

    // Compresses everything in `in` (from its start) to `out` at its cursor.
    void Compress(GameBuffer* in, GameBuffer* out);
    // Decompresses `in` from its cursor to `out` at its cursor.
    void Decompress(GameBuffer* in, GameBuffer* out);
    // Writes the match in progress (length and offset codes); ends it.
    void EncodeMatch(std::int32_t at);
    // Ends the output (pending bits stored, the sink committed), then Start.
    void Reset();

private:
    // The output cursor unbound with no pending bits, no previous byte, no match, window position 0, every hash
    // head 0, chain[0] 0x400 and the window's first and last byte of each half 0.
    void Start();
};

// The code tables of the coder's lengths and distances, filled at start-up by InitCodeTables: the code and
// its bit count of each match length 1..0x22 (at length - 1) and of each distance bucket 0..0x21.
extern std::uint32_t g_offsetBits[34];
extern std::uint32_t g_offsetCodes[34];
extern std::uint32_t g_lengthBits[34];
extern std::uint32_t g_lengthCodes[34];
// Fills the code tables (start-up, GameMain).
void InitCodeTables();

// (1 << n) - 1 for n = 1..32, at n - 1 (BufferCursor::GetBits).
inline constexpr std::uint32_t kCoderBitMasks[32] = /*0078DB28*/ {
    0x1,       0x3,       0x7,       0xF,       0x1F,       0x3F,       0x7F,       0xFF,
    0x1FF,     0x3FF,     0x7FF,     0xFFF,     0x1FFF,     0x3FFF,     0x7FFF,     0xFFFF,
    0x1FFFF,   0x3FFFF,   0x7FFFF,   0xFFFFF,   0x1FFFFF,   0x3FFFFF,   0x7FFFFF,   0xFFFFFF,
    0x1FFFFFF, 0x3FFFFFF, 0x7FFFFFF, 0xFFFFFFF, 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF};
