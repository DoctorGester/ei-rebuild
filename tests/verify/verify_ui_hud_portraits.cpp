// Checks for the party portraits' per-frame update (ui_hud_portraits.cpp).
//
// CHudPortraits::Update runs side by side. Its state lives in the portrait slots and in the
// game's random number generator (it rolls for the face wobble), so both are copied before the
// original runs and put back before ours runs; the RNG therefore advances once per frame as in a
// normal game. Compared: the slots, the RNG, every portrait's face rotation, bars, flash, and the
// selection marker. The face picture (00640670) and a face model change (005FF460) run in both;
// they redraw or reload the same thing.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/random.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
using UpdateFn = void(__fastcall*)(CHudPortraits*, void*);
UpdateFn s_portraitsUpdate;

IsaacRandom* GameRandom() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }

std::uint8_t* Bytes(CEngineObject* object) { return reinterpret_cast<std::uint8_t*>(object); }

struct PortraitsCopy {
    std::vector<std::uint8_t> slots;
    std::vector<std::uint8_t> random;
    std::vector<std::uint8_t> objects;   // per portrait: face rotation, two bars, flash
    std::uint8_t marker[16];             // visible flag and local position
};

int PortraitCount(CHudPortraits* p) { return (std::min)(p->count_, p->owner_->partyCount_); }

PortraitsCopy CopyPortraits(CHudPortraits* p) {
    PortraitsCopy c;
    int n = (std::max)(PortraitCount(p), 0);
    c.slots.resize(n * sizeof(HudPortraitSlot));
    if (n) std::memcpy(c.slots.data(), p->slots_, c.slots.size());
    c.random.resize(sizeof(IsaacRandom));
    std::memcpy(c.random.data(), GameRandom(), sizeof(IsaacRandom));
    for (int i = 0; i < n; ++i) {
        auto put = [&](const void* src, std::size_t size) {
            const auto* b = static_cast<const std::uint8_t*>(src);
            c.objects.insert(c.objects.end(), b, b + size);
        };
        CEngineObject** o = p->sceneObjects_.data_ + i * 5;
        put(Bytes(o[1]) + 0x34, 16);
        for (int k = 2; k <= 3; ++k) {
            auto* bar = reinterpret_cast<CUiMesh*>(o[k]);
            put(&bar->quads_[0].x[2], 8);
            put(bar->uvs_ + 4, 16);
        }
        put(Bytes(o[5]) + 0x220, 4);
        put(Bytes(o[5]) + 0xD4, 4);
    }
    std::memcpy(c.marker, Bytes(p->sceneObjects_.data_[0]) + 0x220, 4);
    std::memcpy(c.marker + 4, Bytes(p->sceneObjects_.data_[0]) + 0x1C, 12);
    return c;
}

int FirstDifference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) return 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return static_cast<int>(i);
    return -1;
}
} // namespace

static void __fastcall PortraitsUpdateVerify(CHudPortraits* self, void*) {
    PortraitsCopy before = CopyPortraits(self);
    s_portraitsUpdate(self, nullptr);
    PortraitsCopy expected = CopyPortraits(self);
    if (!before.slots.empty()) std::memcpy(self->slots_, before.slots.data(), before.slots.size());
    std::memcpy(GameRandom(), before.random.data(), sizeof(IsaacRandom));
    self->CHudPortraits::Update();
    PortraitsCopy actual = CopyPortraits(self);

    const char* part = "";
    int offset = -1;
    if ((offset = FirstDifference(expected.slots, actual.slots)) >= 0)
        part = "slots";
    else if ((offset = FirstDifference(expected.random, actual.random)) >= 0)
        part = "random";
    else if ((offset = FirstDifference(expected.objects, actual.objects)) >= 0)
        part = "objects";
    else if (std::memcmp(expected.marker, actual.marker, sizeof(expected.marker)) != 0) {
        part = "marker";
        offset = 0;
    }
    // Both values of the differing dword; for the slots also the slot and the offset in it.
    std::uint32_t was = 0, now = 0;
    const std::vector<std::uint8_t>* e = nullptr;
    const std::vector<std::uint8_t>* a = nullptr;
    if (part == std::string("slots")) e = &expected.slots, a = &actual.slots;
    else if (part == std::string("random")) e = &expected.random, a = &actual.random;
    else if (part == std::string("objects")) e = &expected.objects, a = &actual.objects;
    int aligned = offset & ~3;
    if (e && aligned >= 0 && static_cast<std::size_t>(aligned) + 4 <= e->size() &&
        static_cast<std::size_t>(aligned) + 4 <= a->size()) {
        std::memcpy(&was, e->data() + aligned, 4);
        std::memcpy(&now, a->data() + aligned, 4);
    }
    int slot = offset >= 0 ? offset / static_cast<int>(sizeof(HudPortraitSlot)) : -1;
    ei::verify::Check("CHudPortraits::Update", -1, offset, "%s differ at +0x%X (slot %d +0x%X) %08X/%08X", part, offset,
                      slot, offset >= 0 ? offset % static_cast<int>(sizeof(HudPortraitSlot)) : 0, was, now);
}
// PUSH EBP; MOV EBP,ESP; PUSH -1: 5 bytes.
EI_VERIFY(0x00641080, PortraitsUpdateVerify, 5, s_portraitsUpdate);
