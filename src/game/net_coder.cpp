// Byte-buffer cursors, the LZ coder (save games, map transfers, network messages) and the fixed
// buffer view's slots. See net_coder.h and docs/notes/network.md.
#include "net_coder.h"

#include "game_data.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

namespace {

// Tables filled at startup by 0042FAE0 (code bits and bit counts, indexed by value): the lengths' by
// length 1..0x22, the offsets' by distance bucket 0..0x21. In the game's memory (ei::GameData).
using LengthTable = std::uint32_t[0x23];
using OffsetTable = std::uint32_t[0x22];
LengthTable& LengthCodes() { return ei::GameData<LengthTable>(0x0079B844); }
LengthTable& LengthBits() { return ei::GameData<LengthTable>(0x0079B7BC); }
OffsetTable& OffsetCodes() { return ei::GameData<OffsetTable>(0x0079B738); }
OffsetTable& OffsetBits() { return ei::GameData<OffsetTable>(0x0079B6B0); }
// 0078DB24: (1 << n) - 1 for n = 0..32.
const std::uint32_t* BitMasks() { return ei::GameData<const std::uint32_t[33]>(0x0078DB24); }

// The compressor looks for matches at most this far back (in the hash chains' distances).
constexpr std::int32_t kMaxDistance = 0x31E;
// A match is not extended beyond this length.
constexpr std::int32_t kMaxMatchLength = 0xE1;
constexpr std::uint32_t kWindowMask = 0x3FF;

} // namespace

// --- BufferCursor ---------------------------------------------------------------------------------

void BufferCursor::PutBits(std::uint32_t value, std::uint8_t count) {
    if (bits == 0) {
        bytePtr = p;
        ++p;
        acc = value;
        bits = count;
    } else {
        acc += value << bits;
        bits = static_cast<std::uint8_t>(bits + count);
    }
    while (bits > 8) {
        *bytePtr = static_cast<std::uint8_t>(acc);
        bits = static_cast<std::uint8_t>(bits - 8);
        bytePtr = p;
        acc >>= 8;
        ++p;
    }
}
EI_REPLACE_METHOD(0x004301E0, BufferCursor::PutBits);

std::uint32_t BufferCursor::GetBit() {
    if (bits < 1) {
        acc = *p++;
        bits = 8;
    }
    std::uint32_t bit = acc & 1;
    acc >>= 1;
    --bits;
    return bit;
}
EI_REPLACE_METHOD(0x00431040, BufferCursor::GetBit);

std::uint32_t BufferCursor::GetBits(std::uint32_t count) {
    while (bits < count) {
        acc += static_cast<std::uint32_t>(*p) << bits;
        bits = static_cast<std::uint8_t>(bits + 8);
        ++p;
    }
    std::uint32_t value = acc & BitMasks()[count];
    acc >>= (count & 0x1F);
    bits = static_cast<std::uint8_t>(bits - static_cast<std::uint8_t>(count));
    return value;
}
EI_REPLACE_METHOD(0x00430FE0, BufferCursor::GetBits);

void BufferCursor::WriteBytes(const void* from, std::uint32_t size) {
    std::memcpy(p, from, size);
    p += size;
}
EI_REPLACE_METHOD(0x00430930, BufferCursor::WriteBytes);

void BufferCursor::ReadBytes(void* to, std::uint32_t size) {
    std::memcpy(to, p, size);
    p += size;
}
EI_REPLACE_METHOD(0x00430F60, BufferCursor::ReadBytes);

void BufferCursor::Reset(std::uint8_t* at, std::uint32_t, std::uint32_t) {
    p = at;
    bits = 0;
    bytePtr = nullptr;
}

void BufferCursor::FlushBits() {
    if (bits != 0) {
        bits = 0;
        if (bytePtr) *bytePtr = static_cast<std::uint8_t>(acc);
    }
}

std::uint32_t BufferCursor::EndWrite() {
    FlushBits();
    sink->cur = p;
    std::uint32_t result = sink->Commit();
    sink = nullptr;
    return result;
}
EI_REPLACE_METHOD(0x00430F90, BufferCursor::EndWrite);

std::uint32_t BufferCursor::End() {
    FlushBits();
    if (!sink) return reinterpret_cast<std::uint32_t>(this);   // EAX keeps `this`
    sink->cur = p;
    return sink->Commit();
}
EI_REPLACE_METHOD(0x0041D930, BufferCursor::End);

std::uint32_t __fastcall CoderEndOutput(CoderState* coder) {
    BufferCursor& out = coder->out;
    out.FlushBits();
    if (!out.sink) return reinterpret_cast<std::uint32_t>(coder);
    out.sink->cur = out.p;
    return out.sink->Commit();
}
EI_REPLACE(0x0041D960, CoderEndOutput);

// --- Code tables ----------------------------------------------------------------------------------

// 0042FAE0: the startup initialiser (from the .data list at 007830C0) of the code tables: for match
// lengths 1..0x22 the code and its bit count (0079B844 / 0079B7BC, indexed by length), for distance
// buckets 0..0x21 (every 16 bytes from -0x5C) the code and bit count (0079B738 / 0079B6B0). It runs
// before the hooks are installed; the rewrite is for the standalone build.
void __cdecl InitCodeTables() {
    LengthTable& lengthCodes = LengthCodes();
    LengthTable& lengthBits = LengthBits();
    for (int length = 1; length <= 0x22; ++length) {
        int j = length - 1;
        lengthCodes[length] = 0;
        if (length <= 1) {
            lengthCodes[length] = 0;
            lengthBits[length] = 1;
        } else if (length <= 2) {
            lengthCodes[length] = 1;
            lengthBits[length] = 3;
        } else if (length <= 3) {
            lengthCodes[length] = 3;
            lengthBits[length] = 5;
        } else if (length <= 4) {
            lengthCodes[length] = 5;
            lengthBits[length] = 3;
        } else if (length <= 5) {
            lengthCodes[length] = 7;
            lengthBits[length] = 5;
        } else if (length <= 6) {
            lengthCodes[length] = 0xF;
            lengthBits[length] = 5;
        } else if (length <= 10) {
            lengthBits[length] = 6;
            lengthCodes[length] = 16 * j - 0x55;
        } else if (length <= 18) {
            lengthBits[length] = 8;
            lengthCodes[length] = 32 * j - 0x12D;
        } else {
            lengthBits[length] = 10;
            lengthCodes[length] = 64 * j - 0x461;
        }
    }
    OffsetTable& offsetCodes = OffsetCodes();
    OffsetTable& offsetBits = OffsetBits();
    for (int k = 0; k < 0x22; ++k) {
        int v = 16 * k - 0x5C;
        int high = 0x1C + 32 * k;
        offsetCodes[k] = 0;
        if (v < -0x3C) {
            offsetBits[k] = 6;
            offsetCodes[k] = high;
        } else if (v < 4) {
            offsetBits[k] = 6;
            offsetCodes[k] = v + 0x44;
        } else if (v < 0x44) {
            offsetBits[k] = 6;
            offsetCodes[k] = v;
        } else if (v < 0xC4) {
            offsetBits[k] = 7;
            offsetCodes[k] = v - 0x44;
        } else {
            offsetBits[k] = 9;
            offsetCodes[k] = high - 0x250;
        }
    }
}
EI_REPLACE(0x0042FAE0, InitCodeTables);

// --- Compression ----------------------------------------------------------------------------------

void CoderState::EncodeMatch(std::int32_t at) {
    std::uint32_t offset = (pos - static_cast<std::uint32_t>(matchLen) - static_cast<std::uint32_t>(at) - 1) & kWindowMask;
    matchPos = -1;
    // Length: 2..0x22 from the table, then two escape codes with 6 and 7 more bits.
    std::int32_t length = matchLen;
    if (length <= 0x22) {
        out.PutBits(LengthCodes()[length], static_cast<std::uint8_t>(LengthBits()[length]));
    } else if (length <= 0x62) {
        out.PutBits(0x17, 5);
        out.PutBits(static_cast<std::uint32_t>(length - 0x23), 6);
    } else if (length <= 0xE2) {
        out.PutBits(0x3F, 6);
        out.PutBits(static_cast<std::uint32_t>(length - 0x63), 7);
    }
    // Offset (distance - 1): 0..0x21 from the table, then a 2- or 3-bit prefix and 6..12 bits.
    std::int32_t value = static_cast<std::int32_t>(offset);
    if (value < 0x22) {
        out.PutBits(OffsetCodes()[value], static_cast<std::uint8_t>(OffsetBits()[value]));
    } else if (value < 0x62) {
        out.PutBits(2, 3);
        out.PutBits(static_cast<std::uint32_t>(value - 0x22), 6);
    } else if (value < 0x162) {
        out.PutBits(6, 3);
        out.PutBits(static_cast<std::uint32_t>(value - 0x62), 8);
    } else if (value < 0x562) {
        out.PutBits(3, 2);
        out.PutBits(static_cast<std::uint32_t>(value - 0x162), 10);
    } else if (value < 0x1562) {   // unreachable: offsets are below 0x400
        out.PutBits(1, 2);
        out.PutBits(static_cast<std::uint32_t>(value - 0x562), 12);
    }
}
EI_REPLACE_METHOD(0x0042FD10, CoderState::EncodeMatch);

namespace {

// The match in progress stopped matching at the newest byte: walks the hash chain of the match's
// start (`a`, a distance back from the match's start `t`) in step with the chain of the newest
// position (`e`, a distance back from `cur`) and takes the first earlier string that repeats the
// match plus the newest byte. Inline in 00430450.
bool ExtendMatch(CoderState& c, std::uint32_t cur, std::int32_t e) {
    std::uint32_t t = c.pos - static_cast<std::uint32_t>(c.matchLen);
    std::int32_t a = c.chain[c.matchPos] + static_cast<std::int32_t>((t - static_cast<std::uint32_t>(c.matchPos)) & kWindowMask);
    const std::uint8_t* target = c.window + (t & kWindowMask);
    while (a < kMaxDistance) {
        while (a > e) e += c.chain[(cur - static_cast<std::uint32_t>(e)) & kWindowMask];
        if (a < e) {
            a += c.chain[(t - static_cast<std::uint32_t>(a)) & kWindowMask];
            continue;
        }
        std::uint32_t q = (t - static_cast<std::uint32_t>(a)) & kWindowMask;
        if (std::memcmp(c.window + q, target, static_cast<std::size_t>(c.matchLen + 1)) == 0) {
            c.matchPos = static_cast<std::int32_t>(q);
            ++c.matchLen;
            return true;
        }
        a += c.chain[q];
        e += c.chain[(cur - static_cast<std::uint32_t>(e)) & kWindowMask];
    }
    return false;
}

// Starts a match at the byte before the newest one: the first position along the newest byte's
// hash chain (distance `d` back from `cur`) whose two bytes equal them. Inline in 00430450.
bool StartMatch(CoderState& c, std::uint32_t cur, std::int32_t d, const std::uint8_t* at) {
    std::uint16_t pair;
    std::memcpy(&pair, at - 1, 2);
    c.matchPos = static_cast<std::int32_t>(cur);
    while (d < kMaxDistance) {
        std::uint32_t q = (cur - static_cast<std::uint32_t>(d)) & kWindowMask;
        std::uint16_t seen;
        std::memcpy(&seen, c.window + q, 2);
        if (seen == pair) {
            c.matchPos = static_cast<std::int32_t>(q);
            c.matchLen = 2;
            return true;
        }
        d += c.chain[q];
    }
    c.matchPos = -1;
    return false;
}

} // namespace

void CoderState::Compress(GameBuffer* in, GameBuffer* outBuffer) {
    BufferCursor reader;
    reader.p = nullptr;
    reader.bits = 0;
    reader.bytePtr = nullptr;
    reader.sink = nullptr;
    in->Seek(0);
    if (in->cur > in->high) in->high = in->cur;
    std::int32_t size = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(in->base) - reinterpret_cast<std::uint32_t>(in->begin) +
        reinterpret_cast<std::uint32_t>(in->high));
    reader.BeginRead(in, size);
    const std::uint8_t* end = reader.p + size;
    out.BeginWrite(outBuffer, size * 2 + 10);
    std::memcpy(out.p, &size, 4);
    out.p += 4;
    if (size > 0) {
        const std::uint8_t* at = reader.p;
        std::uint8_t byte = *at;
        chain[pos] = 0x400;
        pos = (pos + 1) & kWindowMask;
        window[pos] = byte;
        window[pos + 0x400] = byte;
        prev = byte;
        for (++at; at < end; ++at) {
            byte = *at;
            std::uint32_t hash = (prev << 2) ^ byte;
            std::uint32_t cur = pos;
            std::int32_t last = head[hash];
            head[hash] = static_cast<std::int32_t>(cur);
            std::int32_t d = static_cast<std::int32_t>((cur - static_cast<std::uint32_t>(last)) & kWindowMask);
            if (d == 0) d = 0x400;
            chain[pos] = d;
            pos = (cur + 1) & kWindowMask;
            window[pos] = byte;
            window[pos + 0x400] = byte;
            if (matchPos < 0) {
                if (!StartMatch(*this, cur, d, at)) {
                    // Literal: a 0 flag bit, then the previous byte.
                    out.PutBits(0, 1);
                    *out.p++ = static_cast<std::uint8_t>(prev);
                }
            } else if (matchLen < kMaxMatchLength &&
                       window[matchPos + matchLen] == byte) {
                ++matchLen;
            } else if (!(matchLen < kMaxMatchLength && ExtendMatch(*this, cur, d))) {
                EncodeMatch(matchPos);
            }
            prev = byte;
        }
        // The last byte: closes the match (one position earlier, as `pos` did not move) or is a
        // literal.
        if (matchPos >= 0) {
            EncodeMatch(matchPos - 1);
        } else {
            matchPos = -1;
            out.PutBits(0, 1);
            out.WriteBytes(&prev, 1);
        }
    }
    out.EndWrite();
    reader.End();
}
EI_REPLACE_METHOD(0x00430450, CoderState::Compress);

// --- Decompression --------------------------------------------------------------------------------

namespace {

std::int32_t DecodeLength(BufferCursor& r) {
    if (!r.GetBit()) return r.GetBit() ? 4 : 2;
    if (!r.GetBit()) {
        if (r.GetBit()) return static_cast<std::int32_t>(r.GetBits(2)) + 7;
        if (r.GetBit()) return static_cast<std::int32_t>(r.GetBits(3)) + 0xB;
        return 3;
    }
    if (!r.GetBit()) {
        if (r.GetBit()) return static_cast<std::int32_t>(r.GetBits(6)) + 0x23;
        return 5;
    }
    if (!r.GetBit()) return 6;
    if (!r.GetBit()) return static_cast<std::int32_t>(r.GetBits(4)) + 0x13;
    return static_cast<std::int32_t>(r.GetBits(7)) + 0x63;
}

std::int32_t DecodeOffset(BufferCursor& r) {
    if (r.GetBit()) {
        if (r.GetBit()) return static_cast<std::int32_t>(r.GetBits(10)) + 0x162;
        return static_cast<std::int32_t>(r.GetBits(12)) + 0x562;
    }
    if (r.GetBit()) {
        if (r.GetBit()) return static_cast<std::int32_t>(r.GetBits(8)) + 0x62;
        return static_cast<std::int32_t>(r.GetBits(6)) + 0x22;
    }
    if (r.GetBit()) {
        if (r.GetBit()) {
            if (r.GetBit()) return static_cast<std::int32_t>(r.GetBits(1));
            return static_cast<std::int32_t>(r.GetBits(4)) + 0x12;
        }
        return static_cast<std::int32_t>(r.GetBits(2)) + 6;
    }
    if (r.GetBit()) return static_cast<std::int32_t>(r.GetBits(2)) + 2;
    return static_cast<std::int32_t>(r.GetBits(3)) + 0xA;
}

} // namespace

void CoderState::Decompress(GameBuffer* in, GameBuffer* outBuffer) {
    BufferCursor reader;
    reader.Reset(nullptr, 0, 0);
    reader.sink = nullptr;
    if (in->cur > in->high) in->high = in->cur;
    reader.BeginRead(in, static_cast<std::int32_t>(in->high - in->cur));
    std::int32_t remaining;
    std::memcpy(&remaining, reader.p, 4);
    reader.p += 4;
    out.BeginWrite(outBuffer, remaining);
    while (remaining > 0) {
        if (!reader.GetBit()) {
            std::uint8_t byte;
            reader.ReadBytes(&byte, 1);
            out.WriteBytes(&byte, 1);
            --remaining;
            window[pos] = byte;
            pos = (pos + 1) & kWindowMask;
            continue;
        }
        std::int32_t length = DecodeLength(reader);
        std::int32_t offset = DecodeOffset(reader);
        if (!(length < remaining)) length = remaining;
        for (std::int32_t i = 0; i < length; ++i) {
            std::uint8_t byte = window[(pos - static_cast<std::uint32_t>(offset) - 1) & kWindowMask];
            out.WriteBytes(&byte, 1);
            window[pos] = byte;
            pos = (pos + 1) & kWindowMask;
        }
        remaining -= length;
    }
    out.EndWrite();
    reader.End();
}
EI_REPLACE_METHOD(0x00430960, CoderState::Decompress);

// --- The fixed view (0073CA64) and its base (0073CA48) --------------------------------------------

namespace {
void SetVtable(GameBuffer* b, std::uintptr_t vtable) { ei::SetVtable(b, vtable); }
} // namespace

std::uint8_t* GameBuffer::SeekView(std::int32_t position) {
    cur = begin + position;
    return cur;
}
EI_REPLACE_METHOD(0x0047D930, GameBuffer::SeekView);

void __fastcall BufferViewDestruct(GameBuffer* self) { SetVtable(self, kBufferViewBaseVtable); }
EI_REPLACE(0x0047D960, BufferViewDestruct);
EI_REPLACE(0x004DA850, BufferViewDestruct);   // byte-identical copy (reached when unwinding)

void* GameBuffer::DeleteView(unsigned deleteFlags) {
    BufferViewDestruct(this);
    if (deleteFlags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0047D940, GameBuffer::DeleteView);

void* GameBuffer::DeleteViewBase(unsigned deleteFlags) {
    SetVtable(this, kBufferViewBaseVtable);
    if (deleteFlags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0047D910, GameBuffer::DeleteViewBase);
