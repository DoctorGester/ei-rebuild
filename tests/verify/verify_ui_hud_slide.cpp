// Checks for ui_hud_slide.cpp (A-H26): the map's and the unit panel's layouts (00642930, 00645F80)
// and per-frame updates (00642B40, 00647FB0). All four are Rebirth-patched, so they are hooked in
// the vanilla launch only.
// - The layouts run side by side: they write the sprites' positions, the first surface's rectangle
//   and the regions' offsets, all from the slide offset; the original runs first, what it wrote is
//   recorded and put back, then ours. Reached when the battle screen is built (the panels' setups
//   00641F20 / 006456E0 call them: every scenario that starts a game) and on every frame of a slide.
// - The unit panel's update runs side by side too (it reads the frame's time, which does not change
//   within the call, and writes the slide's state, the layout, the hover unit and two of the
//   interface's saved fields). Reached every frame of the battle screen; the slide by ui_hud (the
//   panel's sling button, twice).
// - The map's update rebuilds the map's meshes (UpdateView, BuildMarkers, each checked side by side
//   by its own wrapper), so it runs ours only: the wrapper works out the zoom, the slide's state,
//   the "camera north" time and the flags from the state before the call. Reached every frame of
//   the battle screen; the zoom by ui_hud (the view buttons); no scenario slides the map yet (its
//   sling button or the map slide key).
// The floats are compared within a few ULPs (rule 7); a slide offset on a rounding edge may differ
// by one for a frame, and the layout is then not compared.
#include "verify.h"

#include "../../src/game/originals_ui.h"
#include "../../src/game/ui.h"
#include "../../src/game/unit.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr int kMaxSprites = 40;
constexpr int kMaxRegions = 16;
constexpr std::size_t kSurfaceSize = 0x38;

// What a layout writes.
struct LayoutState {
    int sprites;
    int regions;
    Vector3 positions[kMaxSprites];
    Quaternion turns[kMaxSprites];   // the updates turn the sling button
    int offsets[kMaxRegions][2];
    std::uint8_t surface[kSurfaceSize];
};

CUiMesh* Sprite(CUiWindow* w, int index) { return reinterpret_cast<CUiMesh*>(w->sceneObjects_.data[index]); }

LayoutState CaptureLayout(CUiWindow* w) {
    LayoutState s = {};
    s.sprites = w->sceneObjects_.size < kMaxSprites ? w->sceneObjects_.size : kMaxSprites;
    s.regions = w->regions_.size < kMaxRegions ? w->regions_.size : kMaxRegions;
    for (int i = 0; i < s.sprites; ++i) {
        s.positions[i] = Sprite(w, i)->position_;
        s.turns[i] = Sprite(w, i)->orientation_;
    }
    for (int i = 0; i < s.regions; ++i) {
        s.offsets[i][0] = w->regions_.data[i]->offsetX_;
        s.offsets[i][1] = w->regions_.data[i]->offsetY_;
    }
    std::memcpy(s.surface, w->surfaces_.data[0], kSurfaceSize);
    return s;
}

void RestoreLayout(CUiWindow* w, const LayoutState& s) {
    for (int i = 0; i < s.sprites; ++i) {
        Sprite(w, i)->position_ = s.positions[i];
        Sprite(w, i)->orientation_ = s.turns[i];
    }
    for (int i = 0; i < s.regions; ++i) {
        w->regions_.data[i]->offsetX_ = s.offsets[i][0];
        w->regions_.data[i]->offsetY_ = s.offsets[i][1];
    }
    std::memcpy(w->surfaces_.data[0], s.surface, kSurfaceSize);
}

bool Near(float expected, float actual) { return ei::verify::NearlyEqual<float>(expected, actual, 4, 1e-6f); }

// How many sprites, regions and surfaces differ; `first` is the first differing sprite.
int LayoutDifferences(const LayoutState& e, const LayoutState& a, int* first) {
    int differences = 0;
    *first = -1;
    if (e.sprites != a.sprites || e.regions != a.regions) return 1000;
    for (int i = 0; i < e.sprites; ++i) {
        const bool same = Near(e.positions[i].x, a.positions[i].x) && Near(e.positions[i].y, a.positions[i].y) &&
                          Near(e.positions[i].z, a.positions[i].z) && Near(e.turns[i].w, a.turns[i].w) &&
                          Near(e.turns[i].x, a.turns[i].x) && Near(e.turns[i].y, a.turns[i].y) &&
                          Near(e.turns[i].z, a.turns[i].z);
        if (same) continue;
        if (*first < 0) *first = i;
        ++differences;
    }
    for (int i = 0; i < e.regions; ++i)
        if (e.offsets[i][0] != a.offsets[i][0] || e.offsets[i][1] != a.offsets[i][1]) ++differences;
    if (std::memcmp(e.surface, a.surface, kSurfaceSize) != 0) ++differences;
    return differences;
}

template <typename Panel>
void LayoutSideBySide(const char* name, Panel* self, void(__fastcall* original)(Panel*)) {
    const LayoutState before = CaptureLayout(self);
    original(self);
    const LayoutState expected = CaptureLayout(self);
    RestoreLayout(self, before);
    self->LayoutVanilla();
    const LayoutState actual = CaptureLayout(self);
    int first;
    const int differences = LayoutDifferences(expected, actual, &first);
    ei::verify::Check(name, 0, differences, "offset %d, %d sprites, first differing sprite %d", self->offset_,
                      expected.sprites, first);
}

// 00642930: PUSH ESI (1), MOV ESI,ECX (2), PUSH imm32 (5).
void(__fastcall* s_mapLayout)(CHudMap*);
void __fastcall MapLayoutVerify(CHudMap* self) { LayoutSideBySide("CHudMap::Layout", self, s_mapLayout); }

// 00645F80: PUSH ECX, PUSH EBX, PUSH ESI (3), MOV ESI,ECX (2), PUSH EDI (1).
void(__fastcall* s_unitLayout)(CHudUnitPanel*);
void __fastcall UnitLayoutVerify(CHudUnitPanel* self) { LayoutSideBySide("CHudUnitPanel::Layout", self, s_unitLayout); }

// --- The unit panel's update: side by side --------------------------------------------------------

struct UnitPanelState {
    LayoutState layout;
    int sliding;
    int offset;
    float slide;
    CUnit* hover;
    std::uint8_t shown;
    int part;
};

UnitPanelState CaptureUnitPanel(CHudUnitPanel* w) {
    UnitPanelState s;
    s.layout = CaptureLayout(w);
    s.sliding = w->sliding_;
    s.offset = w->offset_;
    s.slide = w->slide_;
    s.hover = w->hoverUnit_;
    s.shown = w->interface_->unknown_89[0];
    s.part = w->interface_->unknown_8C;
    return s;
}

void RestoreUnitPanel(CHudUnitPanel* w, const UnitPanelState& s) {
    RestoreLayout(w, s.layout);
    w->sliding_ = s.sliding;
    w->offset_ = s.offset;
    w->slide_ = s.slide;
    w->hoverUnit_ = s.hover;
    w->interface_->unknown_89[0] = s.shown;
    w->interface_->unknown_8C = s.part;
}

// 00647FB0: SUB ESP,0x1C (3), PUSH ESI (1), MOV ESI,ECX (2).
void(__fastcall* s_unitUpdate)(CHudUnitPanel*);
void __fastcall UnitUpdateVerify(CHudUnitPanel* self) {
    const UnitPanelState before = CaptureUnitPanel(self);
    s_unitUpdate(self);
    const UnitPanelState expected = CaptureUnitPanel(self);
    RestoreUnitPanel(self, before);
    self->UpdateVanilla();
    const UnitPanelState actual = CaptureUnitPanel(self);
    const int offsetGap = expected.offset > actual.offset ? expected.offset - actual.offset : actual.offset - expected.offset;
    const bool same = expected.sliding == actual.sliding && offsetGap <= 1 && Near(expected.slide, actual.slide) &&
                      expected.hover == actual.hover && expected.shown == actual.shown && expected.part == actual.part;
    ei::verify::Check("CHudUnitPanel::Update", true, same,
                      "sliding %d: %d/%d offset %d/%d slide %g/%g hover %p/%p shown %d/%d part %d/%d", before.sliding,
                      expected.sliding, actual.sliding, expected.offset, actual.offset, static_cast<double>(expected.slide),
                      static_cast<double>(actual.slide), static_cast<void*>(expected.hover),
                      static_cast<void*>(actual.hover), expected.shown, actual.shown, expected.part, actual.part);
    if (offsetGap != 0) return;
    int first;
    const int differences = LayoutDifferences(expected.layout, actual.layout, &first);
    ei::verify::Check("CHudUnitPanel::Update", 0, differences, "layout at offset %d, first differing sprite %d",
                      actual.offset, first);
}

// --- The map's update: ours only -------------------------------------------------------------------

float SlideTime() { return *reinterpret_cast<const float*>(0x00742428); }

void __fastcall MapUpdateVerify(CHudMap* self) {
    const int view = self->view_;
    const float zoom = self->zoom_;
    const int sliding = self->sliding_;
    const int offset = self->offset_;
    const float slide = self->slide_;
    const float north = self->unknown_1B0;
    const double time = WindowFrameTimeOriginal(self);

    self->UpdateVanilla();

    double wantZoom = zoom;
    if (view == 1) {
        wantZoom = std::pow(2.5, time) * zoom;
        if (wantZoom > self->zoomMax_) wantZoom = self->zoomMax_;
    } else if (view == 2) {
        wantZoom = zoom / std::pow(2.5, time);
        if (wantZoom < 1.0) wantZoom = 1.0;
    }
    ei::verify::CheckNear("CHudMap::Update", static_cast<float>(wantZoom), self->zoom_, "view %d zoom %g", view,
                          static_cast<double>(zoom));

    bool ok = true;
    if (sliding) {
        double s = time * self->slideDirection_ / SlideTime() + slide;
        if (s > 1.0) s = 1.0;
        if (s < 0.0) s = 0.0;
        const float wantSlide = static_cast<float>(s);
        const double f = wantSlide;
        const double eased = wantSlide < 0.5f ? 2.0 * f * f : 1.0 - 2.0 * (1.0 - f) * (1.0 - f);
        const int wantOffset = static_cast<int>(self->travel_ * eased);
        const int gap = wantOffset > self->offset_ ? wantOffset - self->offset_ : self->offset_ - wantOffset;
        const int wantSliding = (self->slide_ == 1.0f || self->slide_ == 0.0f) ? 0 : 1;
        ok = Near(wantSlide, self->slide_) && gap <= 1 && self->sliding_ == wantSliding;
    } else {
        ok = self->sliding_ == 0 && self->offset_ == offset && self->slide_ == slide;
    }
    ok = ok && self->interface_->unknown_89[2] == (self->slideDirection_ > 0.0f ? 1 : 0);
    ei::verify::Check("CHudMap::Update", true, ok, "sliding %d -> %d, slide %g -> %g, offset %d -> %d, direction %g",
                      sliding, self->sliding_, static_cast<double>(slide), static_cast<double>(self->slide_), offset,
                      self->offset_, static_cast<double>(self->slideDirection_));

    const float wantNorth = north > 0.0f ? static_cast<float>(north - time) : north;
    ei::verify::CheckNear("CHudMap::Update", wantNorth, self->unknown_1B0, "the north time from %g",
                          static_cast<double>(north));
    const float tint = self->unknown_1B0 > 0.0f ? 1.0f : 0.5f;
    const float* shown = Sprite(self, 11)->tint_;
    ei::verify::Check("CHudMap::Update", true, shown[0] == tint && shown[1] == tint && shown[2] == tint,
                      "the north button's tint %g", static_cast<double>(shown[0]));
}

void* s_unused;

} // namespace

EI_VERIFY(0x00642930, MapLayoutVerify, 8, s_mapLayout);
EI_VERIFY(0x00645F80, UnitLayoutVerify, 6, s_unitLayout);
EI_VERIFY(0x00647FB0, UnitUpdateVerify, 6, s_unitUpdate);
EI_VERIFY(0x00642B40, MapUpdateVerify, 0, s_unused);
