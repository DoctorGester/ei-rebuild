// Record mode for the "coder" family (coder_vectors.h). Stolen bytes as in verify_net_coder.cpp. Every
// scenario compresses and decompresses (the hero's profile, saves and the client-server messages).
#include "coder_vectors.h"
#include "vectors.h"

#include "ei/hook.h"

#include <cstring>
#include <memory>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::coder;

constexpr std::uint32_t kCap = 8;

Sampler g_tables("coder", "@CoderTables", sizeof(Tables), 1);
Sampler g_compress("coder", "CoderState::Compress", kVariableSize, kCap, 3);
Sampler g_decompress("coder", "CoderState::Decompress", kVariableSize, kCap, 3);

// The game's tables, once (they are filled before the hooks are installed and never change).
void OfferTables() {
    static bool offered = false;
    if (offered) return;
    offered = true;
    auto v = Zeroed<Tables>();
    std::memcpy(v.lengthCodes, reinterpret_cast<const void*>(kLengthCodes), sizeof(v.lengthCodes));
    std::memcpy(v.lengthBits, reinterpret_cast<const void*>(kLengthBits), sizeof(v.lengthBits));
    std::memcpy(v.offsetCodes, reinterpret_cast<const void*>(kOffsetCodes), sizeof(v.offsetCodes));
    std::memcpy(v.offsetBits, reinterpret_cast<const void*>(kOffsetBits), sizeof(v.offsetBits));
    std::memcpy(v.masks, reinterpret_cast<const void*>(kBitMasks), sizeof(v.masks));
    g_tables.Offer(v, ControlWord());
}

BufferState StateOf(const GameBuffer* b) {
    auto s = Zeroed<BufferState>();
    s.cur = static_cast<std::int32_t>(b->cur - b->begin);
    s.high = static_cast<std::int32_t>(b->high - b->begin);
    s.end = static_cast<std::int32_t>(b->end - b->begin);
    s.flags = b->flags;
    std::uintptr_t vtable;
    std::memcpy(&vtable, b, sizeof(vtable));
    s.growing = vtable == GameBuffer::kGrowingVtableAddress;
    return s;
}

bool Recordable(const GameBuffer* in, const GameBuffer* out) {
    std::uintptr_t vin, vout;
    std::memcpy(&vin, in, sizeof(vin));
    std::memcpy(&vout, out, sizeof(vout));
    auto known = [](std::uintptr_t v) { return v == GameBuffer::kGrowingVtableAddress || v == kBufferViewVtable; };
    return in != out && in->base == 0 && out->base == 0 && known(vin) && known(vout);
}

Scalars ScalarsOf(const CoderState* c) {
    auto s = Zeroed<Scalars>();
    s.pos = c->pos;
    s.matchPos = c->matchPos;
    s.matchLen = c->matchLen;
    s.prev = c->prev;
    s.acc = c->out.acc;
    s.bits = c->out.bits;
    return s;
}

using CoderFn = void(__fastcall*)(CoderState*, void*, GameBuffer*, GameBuffer*);

// 00430450. The input is everything from its start (the size Compress writes first); the output is
// what it wrote at the output's cursor.
CoderFn s_compress;
void __fastcall CompressRecord(CoderState* self, void*, GameBuffer* in, GameBuffer* out) {
    OfferTables();
    if (!Recordable(in, out)) {
        s_compress(self, nullptr, in, out);
        return;
    }
    auto owned = std::make_unique<CompressHead>();   // 10 KB, zeroed
    CompressHead& head = *owned;
    std::memcpy(head.tablesBefore, self, kTablesSize);
    head.before = ScalarsOf(self);
    head.inBefore = StateOf(in);
    head.outBefore = StateOf(out);
    std::uint16_t cw = ControlWord();
    s_compress(self, nullptr, in, out);
    head.after = ScalarsOf(self);
    head.tablesAfter = HashBytes(self, kTablesSize);
    head.inAfter = StateOf(in);
    head.outAfter = StateOf(out);
    const std::uint8_t* written = out->begin + head.outBefore.cur;
    std::int32_t size;
    std::memcpy(&size, written, sizeof(size));
    if (size < 0 || static_cast<std::uint32_t>(size) > kMaxBytes) return;
    head.inSize = static_cast<std::uint32_t>(size);
    head.outSize = static_cast<std::uint32_t>(head.outAfter.cur - head.outBefore.cur);
    Writer w;
    w.Put(head);
    w.PutBytes(in->begin, head.inSize);
    w.PutBytes(written, head.outSize);
    g_compress.OfferBytes(w, cw, SizeBucket(head.inSize));
}

// 00430960. The input is what it read from the input's cursor; the output what it wrote.
CoderFn s_decompress;
void __fastcall DecompressRecord(CoderState* self, void*, GameBuffer* in, GameBuffer* out) {
    OfferTables();
    if (!Recordable(in, out)) {
        s_decompress(self, nullptr, in, out);
        return;
    }
    auto head = Zeroed<DecompressHead>();
    std::memcpy(head.windowBefore, self->window, sizeof(head.windowBefore));
    head.posBefore = self->pos;
    head.inBefore = StateOf(in);
    head.outBefore = StateOf(out);
    std::uint16_t cw = ControlWord();
    s_decompress(self, nullptr, in, out);
    std::memcpy(head.windowAfter, self->window, sizeof(head.windowAfter));
    head.posAfter = self->pos;
    head.inAfter = StateOf(in);
    head.outAfter = StateOf(out);
    if (head.inAfter.cur < head.inBefore.cur || head.outAfter.cur < head.outBefore.cur) return;
    head.inSize = static_cast<std::uint32_t>(head.inAfter.cur - head.inBefore.cur);
    head.outSize = static_cast<std::uint32_t>(head.outAfter.cur - head.outBefore.cur);
    if (head.outSize > kMaxBytes) return;
    Writer w;
    w.Put(head);
    w.PutBytes(in->begin + head.inBefore.cur, head.inSize);
    w.PutBytes(out->begin + head.outBefore.cur, head.outSize);
    g_decompress.OfferBytes(w, cw, SizeBucket(head.outSize));
}

} // namespace

// The save games' callers are ours (world_savefile.cpp).
EI_RECORD_OURS(0x00430450, CompressRecord, 7, s_compress);
EI_RECORD_OURS(0x00430960, DecompressRecord, 7, s_decompress);
