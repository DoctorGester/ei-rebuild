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
    Append(out, &w.regions_.size_, 4);
    for (int i = regions; i < w.regions_.size_; ++i) {
        const CUiRegion* region = w.regions_.data_[i];
        if (region) Append(out, &region->bounds_, sizeof(RECT));
    }
    Append(out, &w.sceneObjects_.size_, 4);
    for (int i = objects; i < w.sceneObjects_.size_; ++i) {
        auto* sprite = reinterpret_cast<const std::uint8_t*>(w.sceneObjects_.data_[i]);
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
    int regions = w.regions_.size_;
    int objects = w.sceneObjects_.size_;
    s_stripBuild(self, nullptr, top);
    std::vector<std::uint8_t> expected = StripState(self, regions, objects);
    while (w.regions_.size_ > regions) w.RemoveRegionAt(w.regions_.size_ - 1);
    while (w.sceneObjects_.size_ > objects) w.RemoveSceneObjectAt(w.sceneObjects_.size_ - 1);
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
    std::uint32_t wantRight = static_cast<float>(self->list_.size_ - 7) > position ? 0x3E480000u : 0u;
    CEngineObject** sprites = self->window.sceneObjects_.data_;
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
