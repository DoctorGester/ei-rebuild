// The battle HUD's two corner panels, the game clock (HudClock) and the movement panel (HudMovePanel): their setups.
// Rebirth patches both for its WideScreen layout, reproduced behind the flag (ui_widescreen.h): the clock at the screen's
// right edge, the movement panel at its left. See ui.h and docs/notes/ui.md, "Battle HUD corner panels".
#include "ui.h"
#include "world.h"

#include "ui_widescreen.h"

#include "lib/crt.h"
#include "lib/vecmath.h"
#include "world_data.h"

#include <cmath>
#include <cstdint>

namespace {

// The constants the original reads from the exe's data.
constexpr float kSixth = 0.16666667f;             // the clock's sectors: pi / 6
constexpr float kEighth = 0.125f;            // the movement panel's: pi / 8
constexpr double kMiddleRadius = 34.0;
constexpr double kInnerRadius = 35.0;
constexpr double kOuterRadius = 80.0;
constexpr float kSectorRadius = 80.0f;
constexpr float kTexelBottom = 254.0f;
constexpr float kTexelLeft = 2.0f;
constexpr float kLayoutBottomY = 600.0f;
constexpr const char* kArrowSprite = "in25arrow";

constexpr int kLayoutRight = 800;
constexpr int kLayoutBottom = 600;
constexpr int kDiscPoints = 16;
constexpr int kNormalPeriod = 55;    // milliseconds per world update
constexpr int kFastPeriod = 27;
constexpr float kSectorTint = 0.7f;
constexpr float kArrowDepth = 8.2060862f;

// The angle of `turn` steps, as the original computes it (each product a float).
float ArcAngle(int turn, float step) { return (turn * kPi) * step; }
// The cosine / sine as FCOS / FSIN leave them: the precision control does not reach them (only add, subtract,
// multiply, divide and square root), so they stay unrounded and it is their product with the radius that the
// FPU rounds to float at its 24-bit precision. That is what makes sin(pi / 2) x radius the whole radius
// below, and cos(pi / 3) x 34 exactly 17 (a cosine rounded to float first gives 16.999998 and 16).
double ArcCos(float angle) { return std::cos(static_cast<double>(angle)); }
double ArcSin(float angle) { return std::sin(static_cast<double>(angle)); }

// The point at `turn` steps on the circle of `radius` about the origin, truncated (_ftol).
struct ArcPoint {
    int across;
    int up;
};
ArcPoint OnArc(int turn, float step, double radius) {
    const float angle = ArcAngle(turn, step);
    return {static_cast<int>(libcrt::Ftol(static_cast<float>(ArcCos(angle) * radius))),
            static_cast<int>(libcrt::Ftol(static_cast<float>(ArcSin(angle) * radius)))};
}

// The hand of both panels: a small triangle pointing along +x from its first corner.
void SetHand(UiMesh* sprite) {
    sprite->SetTriangle(8.0f, 100.0f, 0.0f, 0.0f, 17.0f, 11.0f, 17.0f, -11.0f, 104.0f, 19.0f, 115.0f, 2.0f, 93.0f, 2.0f);
}

} // namespace

// --- The clock ---------------------------------------------------------------------------

void HudClock::Setup(bool newFast) {
    widescreen::EnterRightPanel();   // addon's 1002332C; 100233E0 at the end
    const int shift = widescreen::EdgeShift();
    rect = RECT{720 + shift, 520, kLayoutRight + shift, kLayoutBottom};    // WideScreen
    AddBlankSprite()->Disc(10.0f, 80.0f, 800.0f, 600.0f, 65.0f, kDiscPoints, 0xAE, 0x51, 0x41);
    SetHand(AddBlankSprite());
    for (int i = 0; i < 4; ++i) {
        UiMesh* arrow = AddSprite(kArrowSprite);
        arrow->textureOffsetU = 0.22265625f;
        arrow->textureOffsetV = 0.140625f;
        arrow->Place(1000.0f, 1000.0f, kArrowDepth);
        arrow->SetScale(0.4f);
    }
    AddBlankSprite()->Disc(8.0f, 80.0f, 800.0f, 600.0f, 32.0f, kDiscPoints, 0xAE, 0x51, 0x20);
    AddBlankSprite()->Disc(12.0f, 80.0f, 800.0f, 600.0f, 80.0f, kDiscPoints, 0xAE, 0x51, 0x50);

    // The pause, normal and fast sectors, each between two radii of the corner.
    for (int i = 0; i < 3; ++i) {
        UiRegion* sector = AddRegion();
        const ArcPoint corners[4] = {OnArc(i + 1, kSixth, kInnerRadius), OnArc(i, kSixth, kInnerRadius),
                                     OnArc(i, kSixth, kOuterRadius), OnArc(i + 1, kSixth, kOuterRadius)};
        for (const ArcPoint& corner : corners) sector->AddPoint(kLayoutRight - corner.across, kLayoutBottom - corner.up);
    }
    regions[0]->SetTip(0x28A0, 1);
    if (!IsMultiplayerClient()) {
        regions[1]->SetTip(0x28A2, 2);
        regions[2]->SetTip(0x28A3, 3);
    }
    // The objectives button: the quarter disc in the corner.
    UiRegion* middle = AddRegion();
    middle->AddPoint(kLayoutRight, kLayoutBottom);
    for (int turn = 0; turn <= 3; ++turn) {
        const ArcPoint point = OnArc(turn, kSixth, kMiddleRadius);
        middle->AddPoint(kLayoutRight - point.across, kLayoutBottom - point.up);
    }
    middle->SetTip(0x28A1, 4);

    fast = newFast;
    blink = 0.0f;
    worlddata::g_updatePeriod = newFast ? kFastPeriod : kNormalPeriod;
    ui->fastGame = fast;
    highlight = false;
    dial = 0.0f;
    widescreen::LeavePanel();
}

// --- The movement panel --------------------------------------------------------------------

void HudMovePanel::Build() {
    widescreen::EnterLeftPanel();    // addon's 10023394; 100233AF at the end
    const int shift = widescreen::EdgeShift();
    rect = RECT{-shift, 520, 80 - shift, kLayoutBottom};    // WideScreen
    // The flag button: the quarter disc in the corner.
    UiRegion* middle = AddRegion();
    middle->AddPoint(0, kLayoutBottom);
    for (int turn = 4; turn >= 0; --turn) {
        const ArcPoint point = OnArc(turn, kEighth, kMiddleRadius);
        middle->AddPoint(point.across, kLayoutBottom - point.up);
    }
    middle->SetTip(0x2904, 0x1B);
    // The four movement modes' sectors.
    for (int moveMode = 0; moveMode < 4; ++moveMode) {
        UiRegion* sector = AddRegion();
        const ArcPoint corners[4] = {OnArc(moveMode, kEighth, kInnerRadius), OnArc(moveMode + 1, kEighth, kInnerRadius),
                                     OnArc(moveMode + 1, kEighth, kOuterRadius), OnArc(moveMode, kEighth, kOuterRadius)};
        for (const ArcPoint& corner : corners) sector->AddPoint(corner.across, kLayoutBottom - corner.up);
        sector->SetTip(0x2905 + moveMode, 0x17 + moveMode);
    }

    SetHand(AddBlankSprite());
    // The sectors' pictures: triangles from the corner, the texels following the layout points.
    for (int moveMode = 0; moveMode < 4; ++moveMode) {
        UiMesh* sector = AddBlankSprite();
        const float endAngle = ArcAngle(moveMode + 1, kEighth);
        const float endUp = static_cast<float>(ArcSin(endAngle) * kSectorRadius);
        const float endAcross = static_cast<float>(ArcCos(endAngle) * kSectorRadius);
        const float startAngle = ArcAngle(moveMode, kEighth);
        const float startUp = static_cast<float>(ArcSin(startAngle) * kSectorRadius);
        const float startAcross = static_cast<float>(ArcCos(startAngle) * kSectorRadius);
        sector->SetTriangle(10.0f, 100.0f, 0.0f, 600.0f, startAcross, kLayoutBottomY - startUp, endAcross,
                            kLayoutBottomY - endUp, 2.0f, 254.0f, startAcross + kTexelLeft,
                            kTexelBottom - startUp, endAcross + kTexelLeft,
                            kTexelBottom - endUp);
        sector->tint[0] = kSectorTint;
        sector->tint[1] = kSectorTint;
        sector->tint[2] = kSectorTint;
    }
    AddBlankSprite()->Disc(8.0f, 80.0f, 0.0f, 600.0f, 32.0f, kDiscPoints, 2, 0xFE, 0x20);
    ShowMode(-1);
    flag = -1;
    SceneMesh(5)->textureOffsetU = 0.48046875f;
    widescreen::LeavePanel();
}
