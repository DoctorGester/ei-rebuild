// Checks for the network state writers (src/game/net_state.cpp). The object's DeltaState (+0x1C4)
// is deep-copied (tests/verify/delta_snapshot.h), the original writes, its result is copied, the
// saved state is put back and ours writes; the states and the delta globals are compared. A state
// with a non-memory stream runs ours only. The unit writer's class-0x3E controller refresh (slot
// 29: the item spells recomputed) runs once per side.
#include "verify.h"

#include "delta_snapshot.h"

#include <cstdint>
#include <cstring>
#include <vector>

void __fastcall MapObjectWriteNetState(std::uint8_t* object);
void __fastcall UnitWriteNetState(std::uint8_t* unit);

namespace {

using WriteFn = void(__fastcall*)(std::uint8_t*);

// The first 512 calls of each, then every 16th, are compared: copying every stream of a state on
// each tick of every replicated object would slow the game down. The others run ours alone.
bool Sampled(unsigned& counter) {
    unsigned n = counter++;
    return n < 512 || (n & 15) == 0;
}

void WriteChecked(const char* name, WriteFn original, WriteFn ours, std::uint8_t* object, unsigned& calls) {
    if (!Sampled(calls)) {
        ours(object);
        return;
    }
    auto* state = reinterpret_cast<DeltaState*>(object + 0x1C4);
    deltasnap::StateCopy saved(state);
    if (!saved.valid()) {
        ours(object);
        return;
    }
    const deltasnap::Globals before = deltasnap::Globals::Save();
    original(object);
    deltasnap::StateCopy theirs(state);
    const deltasnap::Globals theirsGlobals = deltasnap::Globals::Save();
    saved.MoveInto(state);
    before.Restore();
    ours(object);
    bool ok = theirs.Same(state) && theirsGlobals == deltasnap::Globals::Save();
    ei::verify::Check(name, true, ok, "object %d, mode %d, mask %04X", *reinterpret_cast<int*>(object + 0xC),
                      state->mode_, state->mask_);
}

} // namespace

// 0050EAF0: SUB ESP,8; PUSH EBX; PUSH EBP (5 bytes).
static WriteFn s_mapWrite;
static void __fastcall MapObjectWriteNetStateVerify(std::uint8_t* object) {
    static unsigned calls;
    WriteChecked("MapObject::WriteNetState", s_mapWrite, MapObjectWriteNetState, object, calls);
}
EI_VERIFY(0x0050EAF0, MapObjectWriteNetStateVerify, 5, s_mapWrite);

// ---- Readers: ours reads first (its side effects are the game's); the DeltaState, the received
// stream and the globals are then put back and the original reads the same data again; the states,
// the stream, the globals, the result and the fields the reader decodes are compared. The
// original's run repeats idempotent side effects (the position, the effect array) and skips the
// change-driven ones (stat reactions, a new player), whose old values ours already updated. ------

std::uint32_t __fastcall MapObjectReadNetState(std::uint8_t* object, void*, int first);
std::uint32_t __fastcall UnitReadNetState(std::uint8_t* unit, void*, int first);

namespace {

using ReadFn = std::uint32_t(__fastcall*)(std::uint8_t*, void*, int);

// The decoded fields: raw bytes of the given ranges, the effect array's contents and (units) the
// CString at +0x238.
struct Decoded {
    std::vector<std::uint8_t> bytes;
    void Add(const std::uint8_t* object, int offset, int size) {
        bytes.insert(bytes.end(), object + offset, object + offset + size);
    }
};

Decoded Decode(std::uint8_t* object, bool unit) {
    Decoded d;
    d.Add(object, 0x17C, 0x18);
    d.Add(object, 0x1C, 0xC);
    d.Add(object, 0x34, 0x10);
    std::uint8_t hidden = (object[8] >> 3) & 1;
    d.bytes.push_back(hidden);
    int count = *reinterpret_cast<int*>(object + 0x19C);
    d.Add(object, 0x19C, 4);
    if (count > 0 && count < 256) d.bytes.insert(d.bytes.end(), *reinterpret_cast<std::uint8_t**>(object + 0x198),
                                                 *reinterpret_cast<std::uint8_t**>(object + 0x198) + count * 0x14);
    if (unit) {
        d.Add(object, 0x330, 0x108);
        d.Add(object, 0x244, 4);
        d.Add(object, 0x24C, 4);
        d.Add(object, 0x250, 4);
        d.Add(object, 0x254, 8);
        d.Add(object, 0x25C, 8);
        d.Add(object, 0x264, 8);
        d.Add(object, 0x270, 4);
        d.Add(object, 0x274, 8);
        d.Add(object, 0x438, 4);
        d.Add(object, 0x4C8, 0x14);
        const char* text = *reinterpret_cast<const char* const*>(object + 0x238);
        if (text) d.bytes.insert(d.bytes.end(), text, text + std::strlen(text) + 1);
    }
    return d;
}

std::uint32_t ReadChecked(const char* name, ReadFn original, ReadFn ours, bool unit, std::uint8_t* object, int first,
                          unsigned& calls) {
    if (!Sampled(calls)) return ours(object, nullptr, first);
    auto* state = reinterpret_cast<DeltaState*>(object + 0x1C4);
    BitStream* reader = state->reader_;
    deltasnap::StateCopy saved(state);
    if (!saved.valid() || !reader || !deltasnap::IsMemoryStream(reader)) return ours(object, nullptr, first);
    BitStream savedReader;
    deltasnap::CloneStream(&savedReader, reader);
    const deltasnap::Globals before = deltasnap::Globals::Save();

    std::uint32_t mine = ours(object, nullptr, first);
    deltasnap::StateCopy mineState(state);
    BitStream mineReader;
    deltasnap::CloneStream(&mineReader, reader);
    const deltasnap::Globals mineGlobals = deltasnap::Globals::Save();
    Decoded mineFields = Decode(object, unit);

    saved.MoveInto(state);
    deltasnap::FreeStream(reader);
    std::memcpy(reader, &savedReader, sizeof(BitStream));
    before.Restore();
    std::uint32_t theirs = original(object, nullptr, first);

    bool ok = theirs == mine && mineState.Same(state) && deltasnap::SameStream(&mineReader, reader) &&
              mineGlobals == deltasnap::Globals::Save() && Decode(object, unit).bytes == mineFields.bytes;
    deltasnap::FreeStream(&mineReader);
    ei::verify::Check(name, true, ok, "object %d, first %d, result %u vs %u", *reinterpret_cast<int*>(object + 0xC),
                      first, theirs, mine);
    return theirs;
}

} // namespace

// 0050EC30: SUB ESP,0x24; PUSH EBX; PUSH EBP (5 bytes).
static ReadFn s_mapRead;
static std::uint32_t __fastcall MapObjectReadNetStateVerify(std::uint8_t* object, void*, int first) {
    static unsigned calls;
    return ReadChecked("MapObject::ReadNetState", s_mapRead, MapObjectReadNetState, false, object, first, calls);
}
EI_VERIFY(0x0050EC30, MapObjectReadNetStateVerify, 5, s_mapRead);

// 0052EF90: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes).
static ReadFn s_unitRead;
static std::uint32_t __fastcall UnitReadNetStateVerify(std::uint8_t* unit, void*, int first) {
    static unsigned calls;
    return ReadChecked("Unit::ReadNetState", s_unitRead, UnitReadNetState, true, unit, first, calls);
}
EI_VERIFY(0x0052EF90, UnitReadNetStateVerify, 5, s_unitRead);

// 0052EC70: SUB ESP,0x34; PUSH EBX; PUSH EBP (5 bytes).
static WriteFn s_unitWrite;
static void __fastcall UnitWriteNetStateVerify(std::uint8_t* unit) {
    static unsigned calls;
    WriteChecked("Unit::WriteNetState", s_unitWrite, UnitWriteNetState, unit, calls);
}
EI_VERIFY(0x0052EC70, UnitWriteNetStateVerify, 5, s_unitWrite);
