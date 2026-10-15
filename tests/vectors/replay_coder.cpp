// Replay of the "coder" family (coder_vectors.h) against src/game/net_coder.cpp and the cursor binding in
// game_buffer.cpp. The buffers are ReplayBuffers (our own vtable): Seek is the growing buffer's or the
// view's slot as the recorded buffer's class, and a reserve (room the recorded buffer had to make)
// cannot happen, as each buffer is given room for the whole record.
#include "coder_vectors.h"
#include "vectors.h"

#include "../../src/game/game_data.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

void __cdecl InitCodeTables();   // net_coder.cpp

namespace {

using namespace ei::vectors;
using namespace ei::vectors::coder;

class ReplayBuffer final : public GameBuffer {
public:
    // A buffer like `state` (offsets from its start) over `capacity` bytes of zeros.
    ReplayBuffer(const BufferState& state, std::size_t capacity) : bytes_(capacity + kSlack), growing_(state.growing != 0) {
        begin = bytes_.data();
        cur = begin + state.cur;
        high = begin + state.high;
        end = begin + capacity;
        base = 0;
        flags = state.flags;
    }

    void ReserveRead(std::int32_t) override { reserved_ = true; }
    void ReserveWrite(std::int32_t) override { reserved_ = true; }
    std::uint32_t Commit() override { return 0; }
    int ReadMore(void*, std::uint32_t) override { return 0; }
    int WriteMore(const void*, std::uint32_t) override {
        reserved_ = true;
        return 0;
    }
    std::uint8_t* Seek(std::int32_t position) override {
        return growing_ ? GrowingBufferSeek(this, nullptr, position) : BufferViewSeek(this, nullptr, position);
    }

    bool Reserved() const { return reserved_; }
    BufferState State() const {
        auto s = Zeroed<BufferState>();
        s.cur = static_cast<std::int32_t>(cur - begin);
        s.high = static_cast<std::int32_t>(high - begin);
        s.end = static_cast<std::int32_t>(end - begin);
        s.flags = flags;
        s.growing = growing_;
        return s;
    }

private:
    static constexpr std::size_t kSlack = 64;   // a corrupt stream's reads past the end stay inside
    std::vector<std::uint8_t> bytes_;
    bool growing_;
    bool reserved_ = false;
};

// The room a recorded buffer needs for `extra` bytes at its cursor.
std::size_t Room(const BufferState& s, std::uint32_t extra) {
    std::size_t needed = static_cast<std::size_t>(std::max({s.end, s.high, s.cur, 0}));
    return std::max(needed, static_cast<std::size_t>(s.cur) + extra * 2 + 16);
}

// Cursor, high and flags (the capacity is the replay's own).
bool SameState(const BufferState& a, const BufferState& b) {
    return a.cur == b.cur && a.high == b.high && a.flags == b.flags;
}

void Describe(char* d, std::size_t n, const char* what, const BufferState& ours, const BufferState& original) {
    std::snprintf(d, n, "%s: ours cur %d high %d flags %X, original cur %d high %d flags %X", what, ours.cur, ours.high,
                  ours.flags, original.cur, original.high, original.flags);
}

// The fixture: our InitCodeTables against the game's tables, then the game's left for the coder checks.
bool CheckTables(const Tables& v, char* d, std::size_t n) {
    InitCodeTables();
    const auto* lengthCodes = &ei::GameData<std::uint32_t[0x23]>(kLengthCodes)[0];
    const auto* lengthBits = &ei::GameData<std::uint32_t[0x23]>(kLengthBits)[0];
    const auto* offsetCodes = &ei::GameData<std::uint32_t[0x22]>(kOffsetCodes)[0];
    const auto* offsetBits = &ei::GameData<std::uint32_t[0x22]>(kOffsetBits)[0];
    bool same = std::memcmp(lengthCodes + 1, v.lengthCodes + 1, 0x22 * 4) == 0 &&
                std::memcmp(lengthBits + 1, v.lengthBits + 1, 0x22 * 4) == 0 &&
                std::memcmp(offsetCodes, v.offsetCodes, sizeof(v.offsetCodes)) == 0 &&
                std::memcmp(offsetBits, v.offsetBits, sizeof(v.offsetBits)) == 0;
    std::memcpy(ei::GameData<std::uint32_t[0x23]>(kLengthCodes), v.lengthCodes, sizeof(v.lengthCodes));
    std::memcpy(ei::GameData<std::uint32_t[0x23]>(kLengthBits), v.lengthBits, sizeof(v.lengthBits));
    std::memcpy(ei::GameData<std::uint32_t[0x22]>(kOffsetCodes), v.offsetCodes, sizeof(v.offsetCodes));
    std::memcpy(ei::GameData<std::uint32_t[0x22]>(kOffsetBits), v.offsetBits, sizeof(v.offsetBits));
    std::memcpy(ei::GameData<std::uint32_t[33]>(kBitMasks), v.masks, sizeof(v.masks));
    if (!same) std::snprintf(d, n, "InitCodeTables fills other tables than the game's");
    return same;
}
EI_VECTOR_CHECK("@CoderTables", Tables, CheckTables);

bool CheckCompress(Reader& r, char* d, std::size_t n) {
    const auto head = std::make_unique<CompressHead>(r.Get<CompressHead>());
    const unsigned char* input = r.Bytes(head->inSize);
    const unsigned char* output = r.Bytes(head->outSize);
    if (!input || !output) return true;   // RunBytes reports the short payload

    static CoderState coder;   // 10 KB
    std::memset(&coder, 0, sizeof(coder));
    std::memcpy(&coder, head->tablesBefore, kTablesSize);
    coder.pos = head->before.pos;
    coder.matchPos = head->before.matchPos;
    coder.matchLen = head->before.matchLen;
    coder.prev = head->before.prev;
    coder.out.acc = head->before.acc;
    coder.out.bits = head->before.bits;

    ReplayBuffer in(head->inBefore, Room(head->inBefore, head->inSize));
    std::memcpy(in.begin, input, head->inSize);
    ReplayBuffer out(head->outBefore, Room(head->outBefore, head->inSize * 2 + 10));
    coder.Compress(&in, &out);

    Scalars after = Zeroed<Scalars>();
    after.pos = coder.pos;
    after.matchPos = coder.matchPos;
    after.matchLen = coder.matchLen;
    after.prev = coder.prev;
    after.acc = coder.out.acc;
    after.bits = coder.out.bits;
    const std::uint32_t written = static_cast<std::uint32_t>(out.cur - out.begin - head->outBefore.cur);
    if (in.Reserved() || out.Reserved()) {
        std::snprintf(d, n, "%u bytes: ours made room in a buffer the replay sized for the call", head->inSize);
        return false;
    }
    if (written != head->outSize || std::memcmp(out.begin + head->outBefore.cur, output, written) != 0) {
        std::uint32_t at = 0;
        while (at < written && at < head->outSize && out.begin[head->outBefore.cur + at] == output[at]) ++at;
        std::snprintf(d, n, "%u bytes in: ours wrote %u bytes, original %u; first difference at %u", head->inSize,
                      written, head->outSize, at);
        return false;
    }
    if (!Same(after, head->after) || HashBytes(&coder, kTablesSize) != head->tablesAfter) {
        std::snprintf(d, n, "%u bytes in: coder after: pos %u/%u matchPos %d/%d matchLen %d/%d prev %u/%u acc %X/%X bits %u/%u, "
                      "tables %s", head->inSize, after.pos, head->after.pos, after.matchPos, head->after.matchPos,
                      after.matchLen, head->after.matchLen, after.prev, head->after.prev, after.acc, head->after.acc,
                      after.bits, head->after.bits, HashBytes(&coder, kTablesSize) == head->tablesAfter ? "same" : "differ");
        return false;
    }
    if (!SameState(in.State(), head->inAfter)) {
        Describe(d, n, "input", in.State(), head->inAfter);
        return false;
    }
    if (!SameState(out.State(), head->outAfter)) {
        Describe(d, n, "output", out.State(), head->outAfter);
        return false;
    }
    return true;
}
EI_VECTOR_CHECK_BYTES("CoderState::Compress", CheckCompress);

bool CheckDecompress(Reader& r, char* d, std::size_t n) {
    const auto head = r.Get<DecompressHead>();
    const unsigned char* input = r.Bytes(head.inSize);
    const unsigned char* output = r.Bytes(head.outSize);
    if (!input || !output) return true;

    static CoderState coder;
    std::memset(&coder, 0, sizeof(coder));
    std::memcpy(coder.window, head.windowBefore, sizeof(head.windowBefore));
    coder.pos = head.posBefore;

    ReplayBuffer in(head.inBefore, Room(head.inBefore, head.inSize));
    std::memcpy(in.begin + head.inBefore.cur, input, head.inSize);
    ReplayBuffer out(head.outBefore, Room(head.outBefore, head.outSize));
    coder.Decompress(&in, &out);

    const std::uint32_t written = static_cast<std::uint32_t>(out.cur - out.begin - head.outBefore.cur);
    if (in.Reserved() || out.Reserved()) {
        std::snprintf(d, n, "%u bytes: ours made room in a buffer the replay sized for the call", head.inSize);
        return false;
    }
    if (written != head.outSize || std::memcmp(out.begin + head.outBefore.cur, output, written) != 0) {
        std::uint32_t at = 0;
        while (at < written && at < head.outSize && out.begin[head.outBefore.cur + at] == output[at]) ++at;
        std::snprintf(d, n, "%u bytes in: ours wrote %u bytes, original %u; first difference at %u", head.inSize, written,
                      head.outSize, at);
        return false;
    }
    if (coder.pos != head.posAfter || std::memcmp(coder.window, head.windowAfter, sizeof(head.windowAfter)) != 0) {
        std::snprintf(d, n, "%u bytes in: window position ours %u, original %u; window %s", head.inSize, coder.pos,
                      head.posAfter,
                      std::memcmp(coder.window, head.windowAfter, sizeof(head.windowAfter)) == 0 ? "same" : "differs");
        return false;
    }
    if (coder.out.bits != 0) {
        std::snprintf(d, n, "%u bytes in: %u output bits left pending", head.inSize, coder.out.bits);
        return false;
    }
    if (!SameState(in.State(), head.inAfter)) {
        Describe(d, n, "input", in.State(), head.inAfter);
        return false;
    }
    if (!SameState(out.State(), head.outAfter)) {
        Describe(d, n, "output", out.State(), head.outAfter);
        return false;
    }
    return true;
}
EI_VECTOR_CHECK_BYTES("CoderState::Decompress", CheckDecompress);

} // namespace
