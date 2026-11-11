// The battle HUD's clock, movement panel and top panel (ui_hud.h): their mouse handlers, setups and per-frame updates,
// the clock's hands and arcs, the movement mode and flag button, and the top panel's pages, picked-up items and money
// lines (the message log's frame). See docs/notes/ui.md, section 7 ("Battle HUD corner panels").
#include "game/ui/ui_hud.h"
#include "game/app/app.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_texts.h"
#include "game/objects/map_object.h"
#include "game/players/player_client.h"
#include "game/ui/screen_layout.h"
#include "engine/data/text_manager.h"
#include "game/players/player.h"
#include "game/ui/ui.h"
#include "game/ui/ui_battle.h"
#include "game/ui/ui_hud_pieces.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_objectives.h"
#include "game/ui/ui_scroll_text.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <format>
#include <iterator>
#include <list>
#include <memory>
#include <string>

namespace {
constexpr const char* kSoundClock = "buttons\\battle\\clock.wav";
// The world's update period in milliseconds: the game's speed.
constexpr int kNormalPeriod = 55;
constexpr int kFastPeriod = 27;

// The constants the movement panel's marker is placed with, read where the game reads them.
constexpr float kSixteenth = 0.0625f;
constexpr float kModeMarkerRadius = 73.0f;
constexpr float kModeMarkerCentreY = 600.0f;
constexpr float kModeMarkerDepth = 8.0f;

// The constants are read from memory at run time; Rebirth rewrites one of them (the clock's x,
// screen_layout::g_layoutWidth, for its widescreen layout).
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

constexpr float kHandDepth = 8.0f;
constexpr float kSpriteDepth = 8.106086f;   // 8.106: the dial marks, the top panel sling

// The time of day in hours: (client ticks * scale + offset) mod 24 (_CIfmod), stored.
float TimeOfDay() {
    const WorldClient* world = &g_clientWorld;
    return std::fmod(world->tick * world->secondsPerTick + world->timeBase, 24.0f);
}

// Turns a sprite about `axis` (z or -z) by `angle`.
void Turn(UiMesh* object, float axisZ, float angle) {
    Vector3 axis = {0.0f, 0.0f, axisZ};
    Quaternion q = Quaternion::AxisAngle(axis, angle);
    object->localRotation = q;
}

constexpr float kSlingX = 400.0f;               // the sling button's place: x
constexpr float kSlingInY = -52.0f;             // y while the panel slides out (turned over)
constexpr float kSlingOutY = 83.0f;             // y otherwise

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

constexpr int kLayoutRight = 800;
constexpr int kLayoutBottom = 600;
constexpr int kDiscPoints = 16;
constexpr float kSectorTint = 0.7f;
constexpr float kArrowDepth = 8.2060862f;

// The angle of `turn` steps, as the original computes it (each product a float).
float ArcAngle(int turn, float step) { return (turn * kPi) * step; }
// The cosine / sine of the angle.
float ArcCos(float angle) { return lib::Cos(angle); }
float ArcSin(float angle) { return lib::Sin(angle); }

// The point at `turn` steps on the circle of `radius` about the origin, truncated (_ftol).
struct ArcPoint {
    int across = 0;
    int up = 0;
};
ArcPoint OnArc(int turn, float step, float radius) {
    const float angle = ArcAngle(turn, step);
    return {static_cast<int>((ArcCos(angle) * radius)),
            static_cast<int>((ArcSin(angle) * radius))};
}

// The hand of both panels: a small triangle pointing along +x from its first corner, that corner at layout x `x`.
void SetHand(UiMesh* sprite, float x) {
    sprite->SetTriangle(8.0f, 100.0f, x, 0.0f, x + 17.0f, 11.0f, x + 17.0f, -11.0f, 104.0f, 19.0f, 115.0f, 2.0f, 93.0f,
                        2.0f);
}

constexpr char kNewLine[] = {'\n', '\0'};
} // namespace

// --- Clock -----------------------------------------------------------------------------------------------------------

void HudClock::OnEvent(UiEvent& event) {
    DispatchMouse(event, [&] { PressRegion(event); }, [&] { LeftUp(event); }, [&] { HitRegion(event); });
}

void HudClock::LeftUp(UiEvent& event) {
    const UiRegion* region = PressedRegion(event);
    if (region == pauseButton) {
        TogglePause();
    } else if (region == normalButton || region == fastButton) {
        // A network client too (Rebirth's MPSpeedControl; the original ignored the buttons there): the host's sets
        // the server's speed, everyone's game.
        if (g_clientWorld.paused) g_clientWorld.SendPause(0);
        SetSpeed(region == fastButton);
    } else if (region == objectivesButton) {
        OpenObjectives();
    }
}

void HudClock::TogglePause() {
    g_interface.PlayInterfaceSound(kSoundClock);
    const bool paused = g_clientWorld.paused;
    g_clientWorld.SendPause(paused ? 0 : 1);
}

void HudClock::SetSpeed(bool newFast) {
    fast = newFast;
    app::g_updatePeriod = newFast ? kFastPeriod : kNormalPeriod;
    g_interface.fastGame = newFast;
    g_interface.PlayInterfaceSound(kSoundClock);
}

void HudClock::OpenObjectives() {
    highlight = false;
    g_interface.PlayInterfaceSound(kSoundClick);
    auto window = std::make_unique<ObjectivesWindow>();
    window->Setup(LocalPlayerZone(), nullptr, false, false);
    g_interface.PushWindow(std::move(window), WindowBackdrop::FirstWindow);
}

void HudClock::Update() {
    g_interface.fastGame = fast;

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
    if (!GamePaused()) {
        const float step = fast != 0 ? UiContainer::FrameTime() : UiContainer::FrameTime() * 0.5f;
        dial = step + dial;
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
    Show();
}

void HudClock::Show() {
    float hours = TimeOfDay();
    float pulse = lib::Cos(kHudFullTurn * blink) * kPulseAmplitude + kPulseMiddle;

    float handAngle;
    if (GamePaused()) {
        SetTint(face, kBlinkBase - pulse);
        handAngle = kPi * kPausedHand;
        hand->visible = blink > 0.5f;
    } else {
        SetTint(face, 1.0f);
        handAngle = fast != 0 ? kPi * kFastHand : kPi * kNormalHand;
        hand->visible = true;
    }

    // The hand about -z, placed on its circle.
    Turn(hand, -1.0f, handAngle);
    float handSin = lib::Sin(handAngle);
    float handY = kCentreY - handSin * kHandRadius;
    float handX = lib::Cos(handAngle) * kHandRadius + screen_layout::g_layoutWidth;
    hand->Place(handX, handY, kHandDepth);

    // The face turns with the time of day.
    Turn(face, 1.0f, (hours + kFaceOffset) * kPi * kFaceStep);

    // The four dial marks, a notch apart.
    float angle = dial - (kPi * kFaceStep);
    for (UiMesh* mark : dialMarks) {
        Turn(mark, 1.0f, angle);
        float c = lib::Cos(angle);
        float y = kCentreY - c * kDialRadius;
        float x = lib::Sin(angle) * kDialRadius + screen_layout::g_layoutWidth;
        mark->Place(x, y, kSpriteDepth);
        angle -= kPi * kDialStep;
    }

    SetTint(objectivesIcon, highlight ? pulse : 1.0f);
}

void HudClock::Setup(bool newFast) {
    originX = screen_layout::EdgeShift();
    rect = Rect{720 + originX, 520, kLayoutRight + originX, kLayoutBottom};
    face = AddBlankSprite();
    face->Disc(10.0f, 80.0f, 800.0f + originX, 600.0f, 65.0f, kDiscPoints, 0xAE, 0x51, 0x41);
    hand = AddBlankSprite();
    SetHand(hand, static_cast<float>(originX));
    for (UiMesh*& mark : dialMarks) {
        mark = AddSprite(kArrowSprite);
        mark->textureOffsetU = 0.22265625f;
        mark->textureOffsetV = 0.140625f;
        mark->Place(1000.0f + originX, 1000.0f, kArrowDepth);
        mark->SetScale(0.4f);
    }
    objectivesIcon = AddBlankSprite();
    objectivesIcon->Disc(8.0f, 80.0f, 800.0f + originX, 600.0f, 32.0f, kDiscPoints, 0xAE, 0x51, 0x20);
    rim = AddBlankSprite();
    rim->Disc(12.0f, 80.0f, 800.0f + originX, 600.0f, 80.0f, kDiscPoints, 0xAE, 0x51, 0x50);

    // The pause, normal and fast sectors, each between two radii of the corner.
    auto addSector = [this](int i) {
        UiRegion* sector = AddRegion();
        const ArcPoint corners[4] = {OnArc(i + 1, kSixth, kInnerRadius), OnArc(i, kSixth, kInnerRadius),
                                     OnArc(i, kSixth, kOuterRadius), OnArc(i + 1, kSixth, kOuterRadius)};
        for (const ArcPoint& corner : corners)
            sector->AddPoint(kLayoutRight - corner.across + originX, kLayoutBottom - corner.up);
        return sector;
    };
    pauseButton = addSector(0);
    normalButton = addSector(1);
    fastButton = addSector(2);
    pauseButton->SetTip(0x28A0, 1);
    if (!IsMultiplayerClient()) {
        normalButton->SetTip(0x28A2, 2);
        fastButton->SetTip(0x28A3, 3);
    }
    // The objectives button: the quarter disc in the corner.
    objectivesButton = AddRegion();
    objectivesButton->AddPoint(kLayoutRight + originX, kLayoutBottom);
    for (int turn = 0; turn <= 3; ++turn) {
        const ArcPoint point = OnArc(turn, kSixth, kMiddleRadius);
        objectivesButton->AddPoint(kLayoutRight - point.across + originX, kLayoutBottom - point.up);
    }
    objectivesButton->SetTip(0x28A1, 4);

    fast = newFast;
    blink = 0.0f;
    app::g_updatePeriod = newFast ? kFastPeriod : kNormalPeriod;
    g_interface.fastGame = fast;
    highlight = false;
    dial = 0.0f;
}

// --- Movement panel --------------------------------------------------------------------------------------------------

void HudMovePanel::OnEvent(UiEvent& event) {
    DispatchMouse(event, [&] { PressRegion(event); }, [&] { LeftUp(event); }, [&] { HitRegion(event); });
}

void HudMovePanel::LeftUp(UiEvent& event) {
    const UiRegion* region = PressedRegion(event);
    if (region == flagButton)
        ToggleFlag();
    else if (const int moveMode = RegionIndex(modeButtons, region); moveMode >= 0)
        SetMode(moveMode);
}

void HudMovePanel::SetMode(int newMode) {
    PlayerClient* view = g_clientWorld.LocalView();
    if (!view) return;
    if (newMode >= 0 && newMode < 4) {
        g_interface.PlayInterfaceSound(kSoundClick);
        for (Unit* unit : owner->party) {
            if (unit->flags & MapObjectFlag::Selected) view->SetMoveMode(unit, newMode);
        }
    }
    ShowMode(newMode);
}

void HudMovePanel::ShowMode(int newMode) {
    mode = newMode;
    marker->visible = newMode >= 0;
    for (int i = 0; i < 4; ++i) {
        UiMesh* button = modeIcons[i];
        if (mode != i) {
            SetTint(button, kTintDim);
            continue;
        }
        // The marker points at the mode's button: turned about -z and put on its circle.
        const float angle = static_cast<float>(2 * i + 1) * kPi * kSixteenth;
        const Vector3 axis = {0.0f, 0.0f, -1.0f};
        marker->localRotation = Quaternion::AxisAngle(axis, angle);
        const float y = kModeMarkerCentreY - (lib::Sin(angle) * kModeMarkerRadius);
        const float x = lib::Cos(angle) * kModeMarkerRadius;
        marker->Place(x + originX, y, kModeMarkerDepth);
        SetTint(button, kTintOn);
    }
}

void HudMovePanel::Update() {
    if (!g_clientWorld.LocalView()) return;
    // The common value over the selected units: -1 none selected, -2 mixed.
    int newMode = -1;
    int newFlag = -1;
    for (Unit* unit : owner->party) {
        if (!(unit->flags & MapObjectFlag::Selected)) continue;
        if (newMode == -1)
            newMode = static_cast<int>(unit->moveMode2);
        else if (newMode != static_cast<int>(unit->moveMode2))
            newMode = -2;
        if (newFlag == -1)
            newFlag = unit->fightAtWill;
        else if (newFlag != unit->fightAtWill)
            newFlag = -2;
    }
    ShowMode(newMode);
    flag = newFlag;
    std::uint32_t bits = newFlag == 0 ? 0x3EAC0000u : newFlag == 1 ? 0u : 0x3EF60000u;
    flagIcon->textureOffsetU = std::bit_cast<float>(bits);
}

void HudMovePanel::Build() {
    originX = -screen_layout::EdgeShift();
    rect = Rect{originX, 520, 80 + originX, kLayoutBottom};
    // The flag button: the quarter disc in the corner.
    flagButton = AddRegion();
    flagButton->AddPoint(originX, kLayoutBottom);
    for (int turn = 4; turn >= 0; --turn) {
        const ArcPoint point = OnArc(turn, kEighth, kMiddleRadius);
        flagButton->AddPoint(point.across + originX, kLayoutBottom - point.up);
    }
    flagButton->SetTip(0x2904, 0x1B);
    // The four movement modes' sectors.
    for (int moveMode = 0; moveMode < 4; ++moveMode) {
        UiRegion* sector = AddRegion();
        const ArcPoint corners[4] = {OnArc(moveMode, kEighth, kInnerRadius), OnArc(moveMode + 1, kEighth, kInnerRadius),
                                     OnArc(moveMode + 1, kEighth, kOuterRadius), OnArc(moveMode, kEighth, kOuterRadius)};
        for (const ArcPoint& corner : corners) sector->AddPoint(corner.across + originX, kLayoutBottom - corner.up);
        sector->SetTip(0x2905 + moveMode, 0x17 + moveMode);
        modeButtons[moveMode] = sector;
    }

    marker = AddBlankSprite();
    SetHand(marker, static_cast<float>(originX));
    // The sectors' pictures: triangles from the corner, the texels following the layout points; each shaped about
    // the layout's corner, then placed at the window's (the points' offsets from the first one stay as computed).
    for (int moveMode = 0; moveMode < 4; ++moveMode) {
        UiMesh* sector = AddBlankSprite();
        modeIcons[moveMode] = sector;
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
        sector->Place(static_cast<float>(originX), kLayoutBottomY, 10.0f);
        sector->tint[0] = kSectorTint;
        sector->tint[1] = kSectorTint;
        sector->tint[2] = kSectorTint;
    }
    flagIcon = AddBlankSprite();
    flagIcon->Disc(8.0f, 80.0f, static_cast<float>(originX), 600.0f, 32.0f, kDiscPoints, 2, 0xFE, 0x20);
    ShowMode(-1);
    flag = -1;
    flagIcon->textureOffsetU = 0.48046875f;
}

void HudMovePanel::ToggleFlag() {
    g_interface.PlayInterfaceSound(kSoundClick);
    // The selected party units' common flag: -1 none selected, -2 mixed.
    int common = -1;
    for (Unit* unit : owner->party) {
        if (!(unit->flags & MapObjectFlag::Selected)) continue;
        if (common == -1)
            common = unit->fightAtWill;
        else if (common != unit->fightAtWill)
            common = -2;
    }
    int value = !owner->party.empty() && common == 1 ? 0 : 1;
    for (Unit* unit : owner->party) {
        if (unit->flags & MapObjectFlag::Selected) g_clientWorld.LocalView()->SetUnitFightAtWill(unit, value);
    }
    flag = value;
    // The button's frame (the original also has 0.48046875 for any other value; `value` is 0 or 1).
    flagIcon->textureOffsetU = value == 0 ? 0.3359375f : 0.0f;
}

// --- Top panel -------------------------------------------------------------------------------------------------------

void HudTopPanel::OnEvent(UiEvent& event) {
    DispatchMouse(event, [&] { PressRegion(event); }, [&] { LeftUp(event); }, [&] { HitRegion(event); }, [&] { HitRegion(event); });
}

void HudTopPanel::LeftUp(UiEvent& event) {
    const UiRegion* region = PressedRegion(event);
    if (ToggleSling(region, slingButton, slide)) return;
    if (const int newPage = RegionIndex(pageButtons, region); newPage >= 0) {
        page = newPage;
        ShowPage();
        g_interface.PlayInterfaceSound(kSoundClick);
    }
}

void HudTopPanel::Update() {
    if (slide.moving) {
        slide.Step();
        Layout();
        float y;
        if (slide.direction == -1.0f) {    // sliding out
            Turn(slingArrow, 1.0f, kPi);
            y = kSlingInY;
        } else {
            slingArrow->localRotation = kHudRestRotation;
            y = kSlingOutY;
        }
        slingArrow->Place(kSlingX, y, kSpriteDepth);
    }
    g_interface.hudPanelsIn[1] = slide.direction > 0.0f ? 1 : 0;             // the panel is (coming) in
    g_interface.hudTopPage = page;
}

void HudTopPanel::Setup(int newPage, bool shown) {
    rect.left = 0xB4;
    rect.top = 0;
    slide.t = shown ? 1.0f : 0.0f;
    rect.right = 0x26C;
    rect.bottom = 100;
    slide.moving = true;
    slide.travel = 0x5F;
    slide.direction = shown ? 1.0f : -1.0f;

    slingButton = AddRegion();
    slingButton->SetRect(0x181, 0, 0x19F, 0x14);
    slingButton->SetTip(0x27D8);
    pageButtons[0] = AddRegion();
    pageButtons[0]->SetRect(0xB9, 0, 0xD7, 0x14);
    pageButtons[0]->SetTip(0x27D9, 0x32);
    pageButtons[1] = AddRegion();
    pageButtons[1]->SetRect(0xD7, 0, 0xF5, 0x14);
    pageButtons[1]->SetTip(0x27DA, 0x33);
    AddRegion()->SetRect(0xB4, 0, 0x26C, 100);

    back = AddBlankSprite();
    back->SetRectangle(20.0f, kSpriteScale, 180.0f, 20.0f, 620.0f, 95.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    back->texture = nullptr;
    back->flags |= FigureMeshFlag::Flat;
    back->flatColour = 0x50000000;
    bar = AddBlankSprite();
    bar->SetRectangle(20.0f, kSpriteScale, 180.0f, 0.0f, 620.0f, 20.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    bar->texture = nullptr;
    bar->flags |= FigureMeshFlag::Flat;
    bar->flatColour = 0xB4000000;

    slingArrow = AddSprite("in25arrow");
    slingArrow->textureOffsetU = 0.22265625f;
    slingArrow->textureOffsetV = 0.140625f;
    slingArrow->localRotation = Quaternion::AxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi);
    slingArrow->Place(400.0f, -52.0f, 8.106086f);
    slingArrow->SetScale(0.8f);

    pageIcons[0] = AddBlankSprite();
    pageIcons[0]->SetRectangle(10.0f, 80.0f, 185.0f, 0.0f, 215.0f, 20.0f, 2, 0x80, 0x20, 0x94);
    pageIcons[1] = AddBlankSprite();
    pageIcons[1]->SetRectangle(10.0f, 80.0f, 215.0f, 0.0f, 245.0f, 20.0f, 2, 0x96, 0x20, 0xAA);
    frame = AddBlankSprite();
    frame->SetFrame(18.0f, 80.0f, 180.0f, -5.0f, 620.0f, 100.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);

    page = newPage;
    ShowPage();
    Layout();
}

void HudTopPanel::AddPickedUp(Item* item, bool crippled) {
    std::string name;
    std::string kind;
    name = FormatItemName(g_clientWorld.texts.get(), item);
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

void HudTopPanel::ShowPage() {
    for (int i = 0; i < 2; ++i) {
        float tint = page == i ? 1.0f : 0.5f;
        UiMesh* button = pageIcons[i];
        button->tint[0] = tint;
        button->tint[1] = tint;
        button->tint[2] = tint;
    }
    UiScrollText* log = owner->messageLog;
    if (page == 0) {
        log->SetText(pageTexts[0]);
        log->bar.SetPosition(log->bar.maxPosition);
        log->bar.moved = true;
    } else {
        log->SetText(pageTexts[1]);
        log->bar.SetPosition(0);
    }
}

void HudTopPanel::ShowItems() {
    TextManager* texts = g_clientWorld.texts.get();
    // A local list of the world's copies.
    std::vector<Item*> merged{};
    Player* player = g_clientWorld.LocalView()->player;
    std::string line;
    std::string format;
    for (auto node = player->items.begin(); node != player->items.end();) {
        Item* item = *node;
        ++node;
        Item* same = nullptr;
        for (auto entry = merged.begin(); entry != merged.end() && !same; ++entry)
            if ((*entry)->IsSameAs(item)) same = *entry;
        if (same) {
            same->count += item->count;
            continue;
        }
        Item* copy = g_clientWorld.MakeCopyOf(item);
        copy->count = item->count;
        copy->AddRef();
        merged.push_back(copy);
    }

    auto& text = pageTexts[1];
    text.clear();
    auto* party = LocalPlayerData();
    if (static_cast<int>(party->money) > 0) {
        format = texts->TextManager::GetString("format_money");
        line = str::Format(format, static_cast<int>(party->money));
        text += line;
        text += kNewLine;
    }
    for (auto node = merged.begin(); node != merged.end();) {
        Item* item = *node;
        ++node;
        line = FormatItemName(texts, item);
        text += line;
        text += kNewLine;
        item->Release();
    }
    merged.clear();
    if (page == 1) {
        UiScrollText* log = owner->messageLog;
        log->SetText(text);
        log->bar.SetPosition(0);
    }
    merged.clear();
}

void HudTopPanel::Layout() {
    // Place is Rebirth-patched.
    const int offset = slide.Offset();
    float slideOffset = static_cast<float>(offset);
    back->Place(400.0f, 57.5f - slideOffset, 20.0f);
    bar->Place(400.0f, 10.0f - slideOffset, 20.0f);
    pageIcons[0]->Place(200.0f, 10.0f - slideOffset, 10.0f);
    pageIcons[1]->Place(230.0f, 10.0f - slideOffset, 10.0f);
    frame->Place(400.0f, 47.5f - slideOffset, 18.0f);
    Rect area{190, 25 - offset, 610, 89 - offset};
    owner->messageLog->SetRect(area);
    // Every region but the sling button moves with the panel.
    for (const auto& region : regions) {
        if (region.get() == slingButton) continue;
        region->offsetX = 0;
        region->offsetY = -offset;
    }
}
