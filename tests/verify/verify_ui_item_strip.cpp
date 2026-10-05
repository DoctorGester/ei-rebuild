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
