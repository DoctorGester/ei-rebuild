// The battle HUD's two corner panels, the game clock (CHudClock) and the movement panel
// (CHudMovePanel): their setups (00648520, 00649190), which make the panels' sprites and their
// regions about the layout's bottom corners. A-H26.
// Rebirth patches both for its WideScreen layout, reproduced behind the flag (W3, ui_widescreen.h):
// the clock at the screen's right edge, the movement panel at its left (the panel offset while
// they run, their rectangles moved by EdgeShift). The callees Rebirth patches too (the sprites'
// Place 005E6FA0 and SetTriangle 005E99D0, the regions' AddPoint 005FCA70, the panel's ShowMode
// 00649910) are called through their addresses. See ui.h ("Battle screen and its HUD panels"),
// docs/notes/items_spells.md ("A-H26") and docs/notes/rebirth.md ("WideScreen").
#include "ui.h"

#include "ui_mesh_build.h"
#include "ui_widescreen.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "originals_ui.h"       // SpritePlaceOriginal
#include "originals_world.h"    // IsNetworkClientOriginal

#include <cmath>
#include <cstdint>

namespace {

// The constants the original reads from the exe's data.
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }
double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const double*>(address); }
float Pi() { return GameFloat(0x007423F8); }
float Sixth() { return GameFloat(0x0073CE40); }             // the clock's sectors: pi / 6
float Eighth() { return GameFloat(0x0073C520); }            // the movement panel's: pi / 8
double MiddleRadius() { return GameDouble(0x00742AF8); }    // 34
double InnerRadius() { return GameDouble(0x00742B00); }     // 35
double OuterRadius() { return GameDouble(0x00742A90); }     // 80
float SectorRadius() { return GameFloat(0x00741F1C); }      // 80
float TexelBottom() { return GameFloat(0x0073D03C); }       // 254
float TexelLeft() { return GameFloat(0x0073CDB8); }         // 2
float LayoutBottom() { return GameFloat(0x00742B0C); }      // 600
int& UpdatePeriod() { return *reinterpret_cast<int*>(std::uintptr_t(0x0078FF34)); }
const char* ArrowSprite() { return reinterpret_cast<const char*>(std::uintptr_t(0x007945B0)); }   // "in25arrow"

constexpr int kLayoutRight = 800;
constexpr int kLayoutBottom = 600;
constexpr int kDiscPoints = 16;
constexpr int kNormalPeriod = 55;    // milliseconds per world update
constexpr int kFastPeriod = 27;
constexpr float kSectorTint = 0.7f;
constexpr float kArrowDepth = 8.2060862f;

// The angle of `turn` steps, as the original computes it (each product a float).
float ArcAngle(int turn, float step) { return RoundToFloat(RoundToFloat(static_cast<float>(turn) * Pi()) * step); }
// The cosine / sine as FCOS / FSIN leave them: the precision control does not reach them (only
// add, subtract, multiply, divide and square root), so they stay unrounded and it is their product
// with the radius that the FPU rounds to float at its 24-bit precision. That is what makes
// sin(pi / 2) x radius the whole radius below, and cos(pi / 3) x 34 exactly 17 (a cosine rounded
// to float first gives 16.999998 and 16: X33).
double ArcCos(float angle) { return std::cos(static_cast<double>(angle)); }
double ArcSin(float angle) { return std::sin(static_cast<double>(angle)); }

// The point at `turn` steps on the circle of `radius` about the origin, truncated (_ftol).
struct ArcPoint {
    int across;
    int up;
};
ArcPoint OnArc(int turn, float step, double radius) {
    const float angle = ArcAngle(turn, step);
    return {static_cast<int>(gamecrt::Ftol(RoundToFloat(ArcCos(angle) * radius))),
            static_cast<int>(gamecrt::Ftol(RoundToFloat(ArcSin(angle) * radius)))};
}

CUiMeshBuilder* Builder(CUiMesh* sprite) { return static_cast<CUiMeshBuilder*>(sprite); }

// The hand of both panels: a small triangle pointing along +x from its first corner.
void SetHand(CUiMesh* sprite) {
    sprite->SetTriangle(8.0f, 100.0f, 0.0f, 0.0f, 17.0f, 11.0f, 17.0f, -11.0f, 104.0f, 19.0f, 115.0f, 2.0f, 93.0f, 2.0f);
}

} // namespace

// --- 00648520: the clock ---------------------------------------------------------------------------

void CHudClock::SetupVanilla(int fast) {
    widescreen::EnterRightPanel();   // addon's 1002332C; 100233E0 at the end
    const int shift = widescreen::EdgeShift();
    rect_ = RECT{720 + shift, 520, kLayoutRight + shift, kLayoutBottom};   // WideScreen: 00648531 / 00648545
    Builder(AddSprite(nullptr))->Disc(10.0f, 80.0f, 800.0f, 600.0f, 65.0f, kDiscPoints, 0xAE, 0x51, 0x41);
    SetHand(AddSprite(nullptr));
    for (int i = 0; i < 4; ++i) {
        CUiMesh* arrow = AddSprite(ArrowSprite());
        arrow->uOffset_ = 0.22265625f;
        arrow->vOffset_ = 0.140625f;
        SpritePlaceOriginal(arrow, nullptr, 1000.0f, 1000.0f, kArrowDepth);
        arrow->SetScale(0.4f);
    }
    Builder(AddSprite(nullptr))->Disc(8.0f, 80.0f, 800.0f, 600.0f, 32.0f, kDiscPoints, 0xAE, 0x51, 0x20);
    Builder(AddSprite(nullptr))->Disc(12.0f, 80.0f, 800.0f, 600.0f, 80.0f, kDiscPoints, 0xAE, 0x51, 0x50);

    // The pause, normal and fast sectors, each between two radii of the corner.
    for (int i = 0; i < 3; ++i) {
        CUiRegion* sector = AddRegion();
        const ArcPoint corners[4] = {OnArc(i + 1, Sixth(), InnerRadius()), OnArc(i, Sixth(), InnerRadius()),
                                     OnArc(i, Sixth(), OuterRadius()), OnArc(i + 1, Sixth(), OuterRadius())};
        for (const ArcPoint& corner : corners) sector->CallAddPoint(kLayoutRight - corner.across, kLayoutBottom - corner.up);
    }
    regions_.data[0]->SetTip(0x28A0, 1, 1);
    if (!IsNetworkClientOriginal()) {
        regions_.data[1]->SetTip(0x28A2, 1, 2);
        regions_.data[2]->SetTip(0x28A3, 1, 3);
    }
    // The objectives button: the quarter disc in the corner.
    CUiRegion* middle = AddRegion();
    middle->CallAddPoint(kLayoutRight, kLayoutBottom);
    for (int turn = 0; turn <= 3; ++turn) {
        const ArcPoint point = OnArc(turn, Sixth(), MiddleRadius());
        middle->CallAddPoint(kLayoutRight - point.across, kLayoutBottom - point.up);
    }
    middle->SetTip(0x28A1, 1, 4);

    fast_ = fast;
    blink_ = 0.0f;
    UpdatePeriod() = fast ? kFastPeriod : kNormalPeriod;
    interface_->fastGame_ = fast_;
    highlight_ = false;
    dial_ = 0.0f;
    widescreen::LeavePanel();
}
EI_REPLACE_METHOD(0x00648520, CHudClock::SetupVanilla);
EI_REBIRTH_IMPLEMENTS(0x00648520, ei::rebirth::Patch::WideScreen);

// --- 00649190: the movement panel --------------------------------------------------------------------

void CHudMovePanel::SetupVanilla() {
    widescreen::EnterLeftPanel();    // addon's 10023394; 100233AF at the end
    const int shift = widescreen::EdgeShift();
    rect_ = RECT{-shift, 520, 80 - shift, kLayoutBottom};   // WideScreen: 0064919F / 006491B3
    // The flag button: the quarter disc in the corner.
    CUiRegion* middle = AddRegion();
    middle->CallAddPoint(0, kLayoutBottom);
    for (int turn = 4; turn >= 0; --turn) {
        const ArcPoint point = OnArc(turn, Eighth(), MiddleRadius());
        middle->CallAddPoint(point.across, kLayoutBottom - point.up);
    }
    middle->SetTip(0x2904, 1, 0x1B);
    // The four movement modes' sectors.
    for (int mode = 0; mode < 4; ++mode) {
        CUiRegion* sector = AddRegion();
        const ArcPoint corners[4] = {OnArc(mode, Eighth(), InnerRadius()), OnArc(mode + 1, Eighth(), InnerRadius()),
                                     OnArc(mode + 1, Eighth(), OuterRadius()), OnArc(mode, Eighth(), OuterRadius())};
        for (const ArcPoint& corner : corners) sector->CallAddPoint(corner.across, kLayoutBottom - corner.up);
        sector->SetTip(0x2905 + mode, 1, 0x17 + mode);
    }

    SetHand(AddSprite(nullptr));
    // The sectors' pictures: triangles from the corner, the texels following the layout points.
    for (int mode = 0; mode < 4; ++mode) {
        CUiMesh* sector = AddSprite(nullptr);
        const float endAngle = ArcAngle(mode + 1, Eighth());
        const float endUp = RoundToFloat(ArcSin(endAngle) * SectorRadius());
        const float endAcross = RoundToFloat(ArcCos(endAngle) * SectorRadius());
        const float startAngle = ArcAngle(mode, Eighth());
        const float startUp = RoundToFloat(ArcSin(startAngle) * SectorRadius());
        const float startAcross = RoundToFloat(ArcCos(startAngle) * SectorRadius());
        sector->SetTriangle(10.0f, 100.0f, 0.0f, 600.0f, startAcross, RoundToFloat(LayoutBottom() - startUp), endAcross,
                            RoundToFloat(LayoutBottom() - endUp), 2.0f, 254.0f, RoundToFloat(startAcross + TexelLeft()),
                            RoundToFloat(TexelBottom() - startUp), RoundToFloat(endAcross + TexelLeft()),
                            RoundToFloat(TexelBottom() - endUp));
        sector->tint_[0] = kSectorTint;
        sector->tint_[1] = kSectorTint;
        sector->tint_[2] = kSectorTint;
    }
    Builder(AddSprite(nullptr))->Disc(8.0f, 80.0f, 0.0f, 600.0f, 32.0f, kDiscPoints, 2, 0xFE, 0x20);
    ShowMode(-1);
    flag_ = -1;
    reinterpret_cast<CUiMesh*>(sceneObjects_.data[5])->uOffset_ = 0.48046875f;
    widescreen::LeavePanel();
}
EI_REPLACE_METHOD(0x00649190, CHudMovePanel::SetupVanilla);
EI_REBIRTH_IMPLEMENTS(0x00649190, ei::rebirth::Patch::WideScreen);
