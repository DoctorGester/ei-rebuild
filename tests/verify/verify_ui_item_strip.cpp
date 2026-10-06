// CUiItemStrip::Build (00620380, ui_item_strip.cpp) runs side by side. It only adds regions and
// sprites to the strip's window and resets the strip's own fields, so the original runs first, the
// fields and what it added are recorded (each new region's rectangle; each new sprite's flags,
// position, orientation, texture corner and colour, +0x220, +0x280 and its quads' corners), the
// added regions and sprites are removed again (RemoveRegionAt / RemoveSceneObjectAt from the end)
// and ours runs over the same state; both records must agree. Reached when the camp (ui_camp,
// ui_describe) and the hero screen's inventory (ui_hero_screen) build their strips.
#include "verify.h"

#include "../../src/game/ui_screens.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void Append(std::vector<std::uint8_t>& out, const void* p, std::size_t n) {
    auto* b = static_cast<const std::uint8_t*>(p);
    out.insert(out.end(), b, b + n);
}

std::vector<std::uint8_t> StripState(CUiItemStrip* strip, int regions, int objects) {
    std::vector<std::uint8_t> out;
    const auto* self = reinterpret_cast<const std::uint8_t*>(strip);
    Append(out, &strip->window.rect_, sizeof(RECT));
    Append(out, self + 0x150, 0x194 - 0x150);   // tab .. hovered
    CUiWindow& w = strip->window;
    Append(out, &w.regions_.size, 4);
    for (int i = regions; i < w.regions_.size; ++i) {
        const CUiRegion* region = w.regions_.data[i];
        if (region) Append(out, &region->bounds_, sizeof(RECT));
    }
    Append(out, &w.sceneObjects_.size, 4);
    for (int i = objects; i < w.sceneObjects_.size; ++i) {
        auto* sprite = reinterpret_cast<const std::uint8_t*>(w.sceneObjects_.data[i]);
        if (!sprite) continue;
        Append(out, sprite + 0x08, 4);     // flags
        Append(out, sprite + 0x1C, 12);    // local position
        Append(out, sprite + 0x34, 16);    // local rotation
        Append(out, sprite + 0xCC, 16);    // texture corner, -1, colour
        Append(out, sprite + 0x220, 4);
        Append(out, sprite + 0x280, 4);
        const CUiMesh* mesh = reinterpret_cast<const CUiMesh*>(sprite);
        Append(out, &mesh->quadCount_, 4);
        for (int q = 0; q < mesh->quadCount_ && mesh->quads_; ++q) Append(out, &mesh->quads_[q], 0x20);
    }
    return out;
}

} // namespace

// 00620380: PUSH ECX; MOV AL,[ESP+8] (5).
static void(__fastcall* s_stripBuild)(CUiItemStrip*, void*, int);
static void __fastcall StripBuildVerify(CUiItemStrip* self, void*, int top) {
    CUiWindow& w = self->window;
    int regions = w.regions_.size;
    int objects = w.sceneObjects_.size;
    s_stripBuild(self, nullptr, top);
    std::vector<std::uint8_t> expected = StripState(self, regions, objects);
    while (w.regions_.size > regions) w.RemoveRegionAt(w.regions_.size - 1);
    while (w.sceneObjects_.size > objects) w.RemoveSceneObjectAt(w.sceneObjects_.size - 1);
    self->Build(static_cast<char>(top));
    std::vector<std::uint8_t> actual = StripState(self, regions, objects);
    int at = -1;
    if (expected.size() != actual.size()) {
        at = -2;
    } else {
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (expected[i] != actual[i]) {
                at = static_cast<int>(i);
                break;
            }
    }
    ei::verify::Check("CUiItemStrip::Build", -1, at, "state byte %d differs (sizes %d/%d, top %d)", at,
                      static_cast<int>(expected.size()), static_cast<int>(actual.size()), top & 0xFF);
}
EI_VERIFY(0x00620380, StripBuildVerify, 5, s_stripBuild);

// CUiItemStrip::Draw (00621B50, the forwarding strip's slot 20): ours only, the original would draw
// the strip (sprites, item models, both text surfaces) a second time. Reached every frame the camp
// shows its strips (ui_camp, ui_describe, frames_basic, render_present). Checked: result 1, the
// end faces' +0xCC dimmed (0x3E480000) exactly when the strip can scroll that way (position > 0;
// list size - 7 > position), and the strip's scroll fields and list unchanged.
static void* s_stripDraw;
static BOOL __fastcall StripDrawVerify(CUiItemStrip* self, void*, int pass) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(self);
    std::uint8_t before[0x1B4 - 0x148];
    std::memcpy(before, bytes + 0x148, sizeof(before));
    BOOL result = self->Draw(pass);
    float position = self->position_[self->tab_];
    std::uint32_t wantLeft = position > 0.0f ? 0x3E480000u : 0u;
    std::uint32_t wantRight = static_cast<float>(self->list_.size - 7) > position ? 0x3E480000u : 0u;
    CEngineObject** sprites = self->window.sceneObjects_.data;
    std::uint32_t left, right;
    std::memcpy(&left, reinterpret_cast<const std::uint8_t*>(sprites[0]) + 0xCC, 4);
    std::memcpy(&right, reinterpret_cast<const std::uint8_t*>(sprites[1]) + 0xCC, 4);
    bool same = std::memcmp(before, bytes + 0x148, sizeof(before)) == 0;
    bool ok = result == 1 && left == wantLeft && right == wantRight && same;
    ei::verify::Check("CUiItemStrip::Draw", true, ok, "result %d, ends %08X/%08X (want %08X/%08X), fields %s", result,
                      left, right, wantLeft, wantRight, same ? "same" : "changed");
    return result;
}
EI_VERIFY(0x00621B50, StripDrawVerify, 0, s_stripDraw);

// CUiItemStrip::Update (00620A50, slot 24 of the strips' vtables 0074197C, 00741A1C, 00741BF4):
// side by side. The original runs first; the scroll fields it wrote (+0x150..+0x17C: timer,
// positions, pending scroll, speed) are recorded and put back as they were, then ours runs (both
// rebuild the list through slot 26, which only depends on the source list and the tab; the list
// itself is not put back since the rebuild may have reallocated it). The frame time (00600270) is
// the same within a frame. Floats compared within a few ULPs (rule 7; a position on a snapping
// edge may rarely settle differently), the visible count exactly. Reached every frame a strip is
// shown: the camp (ui_camp, ui_describe, frames_basic, render_present) and the hero screen's
// inventory (ui_hero_screen). The arrow scroll needs a held arrow (button down on region 0 or 1).
static void(__fastcall* s_stripUpdate)(CUiItemStrip*);
static void __fastcall StripUpdateVerify(CUiItemStrip* self) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(self);
    std::uint8_t before[0x17C - 0x150];
    std::memcpy(before, bytes + 0x150, sizeof(before));
    s_stripUpdate(self);
    std::uint8_t expected[0x17C - 0x150];
    std::memcpy(expected, bytes + 0x150, sizeof(expected));
    int expectedCount = self->count_;
    std::memcpy(bytes + 0x150, before, sizeof(before));
    self->Update();
    // Every float from the timer (+0x158) to the speed (+0x178); the tab and arrow state exactly.
    int at = -1;
    if (std::memcmp(expected, bytes + 0x150, 8) != 0) at = 0;
    for (int offset = 8; offset < static_cast<int>(sizeof(expected)) && at < 0; offset += 4) {
        float a, b;
        std::memcpy(&a, expected + offset, 4);
        std::memcpy(&b, bytes + 0x150 + offset, 4);
        if (!ei::verify::NearlyEqual<float>(a, b, 4, 1e-6f)) at = 0x150 + offset;
    }
    if (at < 0 && expectedCount != self->count_) at = 0x188;
    float wantPosition = 0.0f, gotPosition = 0.0f;
    if (self->tab_ >= 0 && self->tab_ < 6) {
        std::memcpy(&wantPosition, expected + 0xC + 4 * self->tab_, 4);   // position_[tab]
        gotPosition = self->position_[self->tab_];
    }
    ei::verify::Check("CUiItemStrip::Update", -1, at, "field +0x%X differs (tab %d, position %g / %g, count %d / %d)", at,
                      self->tab_, static_cast<double>(wantPosition), static_cast<double>(gotPosition), expectedCount,
                      self->count_);
}
EI_VERIFY(0x00620A50, StripUpdateVerify, 6, s_stripUpdate);
