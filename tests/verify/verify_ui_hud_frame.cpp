// Checks for the battle HUD panels' per-frame updates (ui_hud_frame.cpp).
//
// CHudClock::Update runs side by side: the original first (through the trampoline), then what
// it wrote is copied (its two phases, the interface's speed field, the sprites' rotations,
// positions, tints and the hand's visible flag), the two phases are put back, and ours runs
// from the same state. The frame time and the game clock do not change within the frame.
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstring>

namespace {
using UpdateFn = void(__fastcall*)(CHudClock*, void*);
UpdateFn s_clockUpdate;

struct ClockCopy {
    float blink, dial;
    int interfaceSpeed;
    std::uint8_t rotation[6][16];   // objects 0-5, +0x34
    std::uint8_t position[6][12];   // objects 1-5, +0x1C (index 0 unused)
    std::uint8_t faceTint[12];      // object 0, +0x224
    std::uint8_t buttonTint[12];    // object 6
    int handVisible;                // object 1, +0x220
};

ClockCopy CopyClock(CHudClock* clock) {
    ClockCopy c;
    std::memset(&c, 0, sizeof(c));
    std::memcpy(&c.blink, &clock->blink_, 4);
    std::memcpy(&c.dial, &clock->dial_, 4);
    std::memcpy(&c.interfaceSpeed, reinterpret_cast<std::uint8_t*>(clock->interface_) + 0x94, 4);
    auto bytes = [&](int k) { return reinterpret_cast<std::uint8_t*>(clock->sceneObjects_.data_[k]); };
    for (int k = 0; k < 6; ++k) std::memcpy(c.rotation[k], bytes(k) + 0x34, 16);
    for (int k = 1; k < 6; ++k) std::memcpy(c.position[k], bytes(k) + 0x1C, 12);
    std::memcpy(c.faceTint, bytes(0) + 0x224, 12);
    std::memcpy(c.buttonTint, bytes(6) + 0x224, 12);
    std::memcpy(&c.handVisible, bytes(1) + 0x220, 4);
    return c;
}
} // namespace

static void __fastcall ClockUpdateVerify(CHudClock* self, void*) {
    float phases[2];
    std::memcpy(phases, &self->blink_, sizeof(phases));   // blink_, dial_
    s_clockUpdate(self, nullptr);
    ClockCopy expected = CopyClock(self);
    std::memcpy(&self->blink_, phases, sizeof(phases));
    self->CHudClock::Update();
    ClockCopy actual = CopyClock(self);
    ei::verify::Check("CHudClock::Update", expected, actual, "blink %08X/%08X dial %08X/%08X",
                      *reinterpret_cast<std::uint32_t*>(&expected.blink),
                      *reinterpret_cast<std::uint32_t*>(&actual.blink),
                      *reinterpret_cast<std::uint32_t*>(&expected.dial),
                      *reinterpret_cast<std::uint32_t*>(&actual.dial));
}
// SUB ESP,0x2C; PUSH EBX; PUSH ESI: 5 bytes.
EI_VERIFY(0x006488E0, ClockUpdateVerify, 5, s_clockUpdate);

// CHudTopPanel::Update, side by side: the slide state is put back after the original ran (its
// Layout call depends only on that state, so running it twice is harmless).
namespace {
using TopUpdateFn = void(__fastcall*)(CHudTopPanel*, void*);
TopUpdateFn s_topUpdate;

struct TopCopy {
    int sliding, offset;
    float slide;
    std::uint8_t slingRotation[16];
    std::uint8_t slingPosition[12];
    int shown;
    int page;
};

TopCopy CopyTop(CHudTopPanel* panel) {
    TopCopy c;
    std::memset(&c, 0, sizeof(c));
    c.sliding = panel->sliding_;
    c.offset = panel->offset_;
    std::memcpy(&c.slide, &panel->slide_, 4);
    auto* sling = reinterpret_cast<std::uint8_t*>(panel->sceneObjects_.data_[2]);
    std::memcpy(c.slingRotation, sling + 0x34, 16);
    std::memcpy(c.slingPosition, sling + 0x1C, 12);
    auto* shared = reinterpret_cast<std::uint8_t*>(panel->interface_);
    c.shown = shared[0x8A];
    std::memcpy(&c.page, shared + 0x90, 4);
    return c;
}
} // namespace

static void __fastcall TopPanelUpdateVerify(CHudTopPanel* self, void*) {
    int sliding = self->sliding_;
    int offset = self->offset_;
    float slide;
    std::memcpy(&slide, &self->slide_, 4);
    s_topUpdate(self, nullptr);
    TopCopy expected = CopyTop(self);
    self->sliding_ = sliding;
    self->offset_ = offset;
    std::memcpy(&self->slide_, &slide, 4);
    self->CHudTopPanel::Update();
    TopCopy actual = CopyTop(self);
    ei::verify::Check("CHudTopPanel::Update", expected, actual, "sliding %d/%d offset %d/%d", expected.sliding,
                      actual.sliding, expected.offset, actual.offset);
}
// SUB ESP,0x1C; PUSH ESI; MOV ESI,ECX: 6 bytes.
EI_VERIFY(0x00649FF0, TopPanelUpdateVerify, 6, s_topUpdate);
