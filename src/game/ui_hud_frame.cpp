// Per-frame updates of the battle HUD panels: the clock (HudClock::Update) turns its
// face with the time of day, moves the hand and the speed dial and makes the pause button blink
// while the game is paused; the top panel (HudTopPanel::Update) slides in and out.
// See docs/notes/ui.md.
#include "ui.h"

#include "game_singletons.h"
#include "maths.h"
#include "text_manager.h"
#include "ui_float.h"
#include "ui_widescreen.h"
#include "world.h"
#include "world_data.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

// The constants are read from memory at run time; Rebirth rewrites one of them (the clock's x,
// widescreen::g_layoutWidth, for its widescreen layout).
constexpr float kPausedHand = 0.9166667f;   // hand angle while paused, in pi
constexpr float kFastHand = 0.5833333f;   // at fast speed
constexpr float kNormalHand = 0.75f;   // at normal speed
constexpr float kDialStep = 0.16666667f;   // dial notches, in pi
constexpr float kFaceStep = 0.083333336f;   // one hour of the face, in pi
constexpr float kFaceOffset = 15.0f;   // 15 hours
constexpr float kHandRadius = 73.0f;
constexpr float kDialRadius = 30.0f;
constexpr float kCentreY = 600.0f;
constexpr float kBlinkBase = 1.4f;
constexpr float kPulseAmplitude = 0.3f;
constexpr float kPulseMiddle = 0.7f;
constexpr float kSlideTime = 0.3f;   // 0.3 seconds for a full slide
bool GamePaused() { return g_clientWorld.paused != 0; }

constexpr float kHandDepth = 8.0f;
constexpr float kSpriteDepth = 8.106086f;   // 8.106: the dial marks, the top panel sling

// The time of day in hours: (client ticks * scale + offset) mod 24 (_CIfmod), stored.
float TimeOfDay() {
    const WorldClient* world = &g_clientWorld;
    return std::fmod(world->tick * world->secondsPerTick + world->timeBase, 24.0f);
}

void SetTint(EngineObject* object, float value) {
    float* tint = AsUiMesh(object)->tint;
    tint[0] = value;
    tint[1] = value;
    tint[2] = value;
}

// Turns a sprite about `axis` (z or -z) by `angle`.
void Turn(EngineObject* object, float axisZ, float angle) {
    Vector3 axis = {0.0f, 0.0f, axisZ};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    AsUiMesh(object)->localRotation = q;
}

std::uint32_t Bits(float f) {
    std::uint32_t u;
    u = std::bit_cast<std::uint32_t>(f);
    return u;
}

constexpr float kSlingX = 400.0f;               // the sling button's place: x
constexpr float kSlingInY = -52.0f;             // y while the panel slides out (turned over)
constexpr float kSlingOutY = 83.0f;             // y otherwise

} // namespace

void HudClock::Update() {
    ui->fastGame = fast;
    float hours = TimeOfDay();
    EngineObject** scene = sceneObjects.data();
    EngineObject* face = scene[0];

    // The blink phase runs from 0 to 1 once a second.
    float blinkAt = UiContainer::FrameTime() + blink;
    blink = blinkAt;
    if (blinkAt >= 1.0f) {
        float next;
        do {
            next = blink - 1.0f;
            blink = next;
        } while (next >= 1.0f);
    }
    float pulse = std::cos(kHudFullTurn * blink) * kPulseAmplitude + kPulseMiddle;

    float handAngle;
    if (GamePaused()) {
        SetTint(face, kBlinkBase - pulse);
        handAngle = kPi * kPausedHand;
        AsUiMesh(scene[1])->visible = blink > 0.5f;
    } else {
        SetTint(face, 1.0f);
        float step;
        if (fast != 0) {
            handAngle = kPi * kFastHand;
            step = UiContainer::FrameTime();
        } else {
            handAngle = kPi * kNormalHand;
            step = UiContainer::FrameTime() * 0.5f;
        }
        dial = step + dial;
        AsUiMesh(scene[1])->visible = true;
    }

    // The dial turns by whole notches' worth and wraps.
    float notch = kPi * kDialStep;
    if (dial > notch) {
        float next;
        do {
            next = dial - notch;
            dial = next;
            notch = kPi * kDialStep;
        } while (next > notch);
    }

    // The hand (object 1) about -z, placed on its circle.
    Turn(scene[1], -1.0f, handAngle);
    float handSin = std::sin(handAngle);
    float handY = kCentreY - handSin * kHandRadius;
    float handX = std::cos(handAngle) * kHandRadius + widescreen::g_layoutWidth;
    AsUiMesh(scene[1])->Place(handX, handY, kHandDepth);

    // The face turns with the time of day.
    Turn(face, 1.0f, (hours + kFaceOffset) * kPi * kFaceStep);

    // The four dial marks (objects 2-5), a notch apart.
    float angle = dial - (kPi * kFaceStep);
    for (int k = 2; k < 6; ++k) {
        EngineObject* mark = scene[k];
        Turn(mark, 1.0f, angle);
        float c = std::cos(angle);
        float y = kCentreY - c * kDialRadius;
        float x = std::sin(angle) * kDialRadius + widescreen::g_layoutWidth;
        AsUiMesh(mark)->Place(x, y, kSpriteDepth);
        angle = angle - (kPi * kDialStep);
    }

    SetTint(scene[6], highlight ? pulse : 1.0f);
}

// --- Top panel ---------------------------------------------------------------------------------

void HudTopPanel::Update() {
    if (sliding) {
        float slideAt = UiContainer::FrameTime() * slideDirection / kSlideTime + slide;
        slide = slideAt;
        if (slideAt > 1.0f) slide = 1.0f;
        slide = MaxKeepB(slide, 0.0f);      // NaN too
        // Ease in and out: 2s^2 for the first half, 1 - 2(1-s)^2 for the second.
        float eased;
        if (slide < 0.5f) {
            float s = slide;
            eased = s * s;
            eased = eased + eased;
        } else {
            float r = 1.0f - slide;
            float r2 = r * r;
            eased = 1.0f - (r2 + r2);
        }
        offset = static_cast<int>(travel * eased);
        EI_NAN_AUDIT(slide);
        if (Bits(slide) == 0x3F800000 || slide == 0.0f) sliding = false;
        Layout();
        EngineObject* sling = sceneObjects[2];
        float y;
        if (Bits(slideDirection) == 0xBF800000) {    // -1: sliding out
            Turn(sling, 1.0f, kPi);
            y = kSlingInY;
        } else {
            AsUiMesh(sling)->localRotation = kHudRestRotation;
            y = kSlingOutY;
        }
        AsUiMesh(sling)->Place(kSlingX, y, kSpriteDepth);
    }
    ui->hudPanelsIn[1] = slideDirection > 0.0f ? 1 : 0;             // the panel is (coming) in
    ui->hudTopPage = page;
}

// --- Ui hud top panel --------------------------------------------------------------------------------

// The battle HUD's top panel (the message log's frame): its setup and the lines it adds
// to page 0 for picked-up items and money. The page display and layout are
// in ui_hud_panels.cpp. See ui.h and docs/notes/items_spells.md.

void HudTopPanel::Setup(int newPage, bool shown) {
    rect.left = 0xB4;
    offset = shown ? 0x5F : 0;
    rect.top = 0;
    slide = shown ? 1.0f : 0.0f;
    rect.right = 0x26C;
    rect.bottom = 100;
    sliding = true;
    travel = 0x5F;
    slideDirection = shown ? 1.0f : -1.0f;

    UiRegion* region = AddRegion();
    region->SetRect(0x181, 0, 0x19F, 0x14);   // the slide button
    region->SetTip(0x27D8);
    region = AddRegion();
    region->SetRect(0xB9, 0, 0xD7, 0x14);     // page 0
    region->SetTip(0x27D9, 0x32);
    region = AddRegion();
    region->SetRect(0xD7, 0, 0xF5, 0x14);     // page 1
    region->SetTip(0x27DA, 0x33);
    AddRegion()->SetRect(0xB4, 0, 0x26C, 100);

    UiMesh* back = AddBlankSprite();
    back->SetRectangle(20.0f, 100.0f, 180.0f, 20.0f, 620.0f, 95.0f, 0, 0, 0x100, 0x100);
    back->texture = nullptr;
    back->flags |= FigureMeshFlag::Flat;
    back->flatColour = 0x50000000;
    UiMesh* bar = AddBlankSprite();
    bar->SetRectangle(20.0f, 100.0f, 180.0f, 0.0f, 620.0f, 20.0f, 0, 0, 0x100, 0x100);
    bar->texture = nullptr;
    bar->flags |= FigureMeshFlag::Flat;
    bar->flatColour = 0xB4000000;

    UiMesh* arrow = AddSprite("in25arrow");
    arrow->textureOffsetU = 0.22265625f;
    arrow->textureOffsetV = 0.140625f;
    Quaternion turn;
    arrow->localRotation = *turn.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi);
    arrow->Place(400.0f, -52.0f, FloatFromBits(0x4101B287));
    arrow->SetScale(0.8f);

    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 185.0f, 0.0f, 215.0f, 20.0f, 2, 0x80, 0x20, 0x94);
    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 215.0f, 0.0f, 245.0f, 20.0f, 2, 0x96, 0x20, 0xAA);
    AddBlankSprite()->SetFrame(18.0f, 80.0f, 180.0f, -5.0f, 620.0f, 100.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);

    page = newPage;
    ShowPage();
    Layout();
}

void HudTopPanel::AddPickedUp(Item* item, bool crippled) {
    std::string name;
    std::string kind;
    name = g_clientWorld.texts->FormatItemName(item);
    const char* key = crippled ? "combat_crippled" : "combat_picked_up";
    kind = g_clientWorld.texts->GetString(key);
    std::string line = kind;
    line += " ";
    line += name;
    line += "\n";   // "\n"
    name = line;
    pageTexts[0] += name;
    if (page == 0) owner->messageLog->AddText(name);
}

void HudTopPanel::AddMoney(int money) {
    if (money <= 0) return;
    std::string text;
    std::string picked;
    std::string format;
    picked = g_clientWorld.texts->GetString("combat_picked_up");
    format = g_clientWorld.texts->GetString("format_money");
    text = str::Format(format, money);
    std::string line = picked;
    line += " ";
    line += text;
    line += "\n";   // "\n"
    text = line;
    pageTexts[0] += text;
    if (page == 0) owner->messageLog->AddText(text);
}

// --- Ui hud setup ------------------------------------------------------------------------------------

// The battle HUD's two corner panels, the game clock (HudClock) and the movement panel (HudMovePanel): their setups.
// Rebirth patches both for its WideScreen layout, reproduced behind the flag (ui_widescreen.h): the clock at the screen's
// right edge, the movement panel at its left. See ui.h and docs/notes/ui.md, "Battle HUD corner panels".

namespace {

// The constants the original reads from the exe's data.
constexpr float kSixth = 0.16666667f;             // the clock's sectors: pi / 6
constexpr float kEighth = 0.125f;            // the movement panel's: pi / 8
constexpr float kMiddleRadius = 34.0f;
constexpr float kInnerRadius = 35.0f;
constexpr float kOuterRadius = 80.0f;
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
// The cosine / sine of the angle. In float, cos(pi / 3) x 34 is 16.999998 and truncates to 16 where the
// original's unrounded cosine times 34 at 24 bits gave 17: a region's corner one pixel off.
float ArcCos(float angle) { return std::cos(angle); }
float ArcSin(float angle) { return std::sin(angle); }

// The point at `turn` steps on the circle of `radius` about the origin, truncated (_ftol).
struct ArcPoint {
    int across;
    int up;
};
ArcPoint OnArc(int turn, float step, float radius) {
    const float angle = ArcAngle(turn, step);
    return {static_cast<int>((ArcCos(angle) * radius)),
            static_cast<int>((ArcSin(angle) * radius))};
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
    rect = Rect{720 + shift, 520, kLayoutRight + shift, kLayoutBottom};    // WideScreen
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
    rect = Rect{-shift, 520, 80 - shift, kLayoutBottom};    // WideScreen
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
        const float endUp = ArcSin(endAngle) * kSectorRadius;
        const float endAcross = ArcCos(endAngle) * kSectorRadius;
        const float startAngle = ArcAngle(moveMode, kEighth);
        const float startUp = ArcSin(startAngle) * kSectorRadius;
        const float startAcross = ArcCos(startAngle) * kSectorRadius;
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
