// The battle HUD's two sliding side panels, the map (CHudMap) and the unit panel (CHudUnitPanel): their layouts at the
// current slide offset, their per-frame updates and their setups. Rebirth patches all of them for its WideScreen layout,
// reproduced behind the flag (ui_widescreen.h). See ui.h and docs/notes/ui.md, "Battle HUD sliding panels".
#include "ui.h"
#include "game_strings.h"

#include "item_db_text.h"
#include "map_object.h"
#include "object_tables.h"
#include "ui_gdi.h"
#include "ui_widescreen.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "game_data.h"
#include "game_singletons.h"

#include <concepts>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

float MapCentreX() { return 700.0f /*00742AAC*/; }
float MapButtonsX() { return 790.0f /*00742AA8*/; }
float MapFrameX() { return 710.0f /*00742AA4*/; }
float MapNeedleX() { return 694.0f /*00742AA0*/; }
float UnitBackX() { return 100.0f /*0073BAB0*/; }
float UnitEdgeX() { return 10.0f /*0073BAAC*/; }
float UnitMiddleX() { return 90.0f /*00742AE0*/; }
float UnitMoreX() { return 172.0f /*00742ADC*/; }
float UnitPartDown() { return 17.5f /*00742AD8*/; }
float UnitEffectX() { return 40.0f /*0073D72C*/; }
float SlideTime() { return 0.3f /*00742428*/; }       // 0.3 seconds for the whole way
// The unit panel's six body parts: where each one's pair of sprites goes (two tables of six ints).
constexpr int kPartX[6] = /*00742434*/ {50, 50, 95, 5, 95, 5};
constexpr int kPartY[6] = /*0074244C*/ {35, 105, 70, 70, 140, 140};
constexpr int kParts = 6;
constexpr int kEffects = 6;

constexpr double kZoomRate = 2.5;                   // the map's zoom changes 2.5 times a second
constexpr std::uint32_t kSlingDepthBits = 0x4101B287;   // 8.106: the sling buttons and the needle
constexpr float kSlingY = 20.0f;

float FromBits(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FromBits(T) = delete;

// WideScreen's layout constants (addon's operands, exact integers as floats): moved right for the
// map, left for the unit panel.
float RightEdge(float x) { return x + static_cast<float>(widescreen::EdgeShift()); }
float LeftEdge(float x) { return x - static_cast<float>(widescreen::EdgeShift()); }

void Place(CUiWindow* window, int index, float x, float y, float depth) {
    AsUiMesh(window->sceneObjects_.data[index])->PlaceBody(x, y, depth);
}

void SetTint(CEngineObject* object, float value) {
    float* tint = AsUiMesh(object)->tint_;
    tint[0] = tint[1] = tint[2] = value;
}

void MoveRegions(CUiWindow* window, int offsetX) {
    for (int i = 1; i < window->regions_.size; ++i) {
        window->regions_.data[i]->offsetX_ = offsetX;
        window->regions_.data[i]->offsetY_ = 0;
    }
}

// A running slide advanced by the frame's time: slide_ moves by time * direction / 0.3 and stays
// within 0..1; offset_ is travel_ eased (2 s^2 up to half way, 1 - 2 (1 - s)^2 after), truncated;
// the slide ends at either end. The original compares on the FPU stack; an offset on a rounding
// edge may differ by one for a frame.
template <typename Panel>
void AdvanceSlide(Panel* panel) {
    const double moved =
        CUiContainer::FrameTime() * panel->slideDirection_ / SlideTime() + panel->slide_;
    panel->slide_ = static_cast<float>(moved);
    if (moved > 1.0) panel->slide_ = 1.0f;
    if (panel->slide_ < 0.0f) panel->slide_ = 0.0f;
    const double s = panel->slide_;
    double eased;
    if (panel->slide_ < 0.5f) {
        eased = 2.0 * (s * s);
    } else {
        const double rest = 1.0 - s;
        eased = 1.0 - 2.0 * (rest * rest);
    }
    panel->offset_ = static_cast<int>(gamecrt::Ftol(panel->travel_ * eased));
    if (panel->slide_ == 1.0f || panel->slide_ == 0.0f) panel->sliding_ = 0;
}

void PlaceSling(CEngineObject* sling, float angle, float x) {
    const Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion turn;
    AsUiMesh(sling)->localRotation_ = *turn.SetAxisAngle(axis, angle);
    AsUiMesh(sling)->PlaceBody(x, kSlingY, FromBits(kSlingDepthBits));
}

} // namespace

// --- The map -------------------------------------------------------------------------------------

void CHudMap::Layout() {
    widescreen::EnterRightPanel();   // addon's 10023366
    const float offset = static_cast<float>(offset_);
    const float centre = offset + MapCentreX();
    const float buttons = offset + MapButtonsX();
    Place(this, 0, centre, 80.0f, 18.0f);
    Place(this, 1, centre, 80.0f, 14.0f);
    Place(this, 2, centre, 80.0f, 12.0f);
    Place(this, 3, centre, 80.0f, 20.0f);
    Place(this, 4, buttons, 80.0f, 20.0f);
    Place(this, 6, buttons, 115.0f, 10.0f);
    Place(this, 7, buttons, 145.0f, 10.0f);
    Place(this, 8, centre, 80.0f, 10.0f);
    Place(this, 9, offset + MapFrameX(), 80.0f, 10.0f);
    Place(this, 10, offset + MapNeedleX(), 84.0f, FromBits(kSlingDepthBits));
    Place(this, 11, buttons, 85.0f, 10.0f);
    surfaces_.data[0]->MoveTo(offset_ + 700 + widescreen::EdgeShift(), 80);   // WideScreen
    widescreen::LeavePanel();        // addon's 1002337E, before the regions
    MoveRegions(this, offset_);
}
EI_REWRITTEN(0x00642930, CHudMap::Layout);

void CHudMap::UpdateVanilla() {
    // The zoom runs towards the chosen view's end: up to zoomMax_ (view 1) or down to 1 (view 2).
    if (view_ == 1) {
        const double zoom = std::pow(kZoomRate, CUiContainer::FrameTime()) * zoom_;
        zoom_ = static_cast<float>(zoom);
        if (zoom > zoomMax_) zoom_ = zoomMax_;
    }
    if (view_ == 2) {
        const double zoom = zoom_ / std::pow(kZoomRate, CUiContainer::FrameTime());
        zoom_ = static_cast<float>(zoom);
        if (zoom < 1.0) zoom_ = 1.0f;
    }
    UpdateView();
    BuildMarkers();

    if (sliding_) {
        AdvanceSlide(this);
        Layout();
        // WideScreen.
        if (slideDirection_ == 1.0f)
            PlaceSling(sceneObjects_.data[5], kHudQuarterTurn, RightEdge(713.0f));
        else
            PlaceSling(sceneObjects_.data[5], -kHudQuarterTurn, RightEdge(850.0f));
    }
    interface_->hudPanelsIn_[2] = slideDirection_ > 0.0f ? 1 : 0;

    if (northTime_ > 0.0f) northTime_ = static_cast<float>(northTime_ - CUiContainer::FrameTime());
    SetTint(sceneObjects_.data[11], northTime_ > 0.0f ? 1.0f : 0.5f);
}
EI_REWRITTEN(0x00642B40, CHudMap::UpdateVanilla);

// --- The unit panel ------------------------------------------------------------------------------

void CHudUnitPanel::Layout() {
    widescreen::EnterLeftPanel();    // addon's 100232AF
    const float offset = static_cast<float>(offset_);
    const float edge = UnitEdgeX() - offset;
    const float middle = UnitMiddleX() - offset;
    Place(this, 0, UnitBackX() - offset, 110.0f, 60.0f);
    Place(this, 1, edge, 110.0f, 20.0f);
    Place(this, 3, edge, 115.0f, 10.0f);
    Place(this, 4, edge, 145.0f, 10.0f);
    Place(this, 5, edge, 175.0f, 10.0f);
    Place(this, 6, edge, 205.0f, 10.0f);
    Place(this, 7, middle, 110.0f, 10.0f);
    Place(this, 9, UnitMoreX() - offset, 212.0f, 4.0f);
    Place(this, 10, middle, 192.5f, 4.0f);
    Place(this, 11, middle, 192.5f, 5.0f);
    Place(this, 12, middle, 207.5f, 4.0f);
    Place(this, 13, middle, 207.5f, 5.0f);
    for (int i = 0; i < kParts; ++i) {
        const float y = static_cast<float>(kPartY[i]) + UnitPartDown();
        const float x = static_cast<float>(kPartX[i] + 50) - offset;
        Place(this, 14 + 2 * i, x, y, 4.0f);
        Place(this, 15 + 2 * i, x, y, 5.0f);
    }
    for (int i = 0; i < kEffects; ++i) Place(this, 26 + i, UnitEffectX() - offset, static_cast<float>(50 + 30 * i), 5.0f);
    surfaces_.data[0]->MoveTo(20 - widescreen::EdgeShift() - offset_, 0);   // WideScreen
    MoveRegions(this, -offset_);
    widescreen::LeavePanel();        // addon's 100232E9
}
EI_REWRITTEN(0x00645F80, CHudUnitPanel::Layout);

void CHudUnitPanel::UpdateVanilla() {
    if (sliding_) {
        AdvanceSlide(this);
        Layout();
        // WideScreen.
        if (slideDirection_ == -1.0f)
            PlaceSling(sceneObjects_.data[2], kHudQuarterTurn, LeftEdge(-50.0f));
        else
            PlaceSling(sceneObjects_.data[2], -kHudQuarterTurn, LeftEdge(87.0f));
    }
    if (hoverUnit_ && ObjectSlots(hoverUnit_)->IsDead() != 0) hoverUnit_ = nullptr;
    interface_->hudPanelsIn_[0] = slideDirection_ > 0.0f ? 1 : 0;
    interface_->hudBodyPart_ = bodyPart_;
}
EI_REWRITTEN(0x00647FB0, CHudUnitPanel::UpdateVanilla);

// --- The unit panel's setup ----------------------------------------------------------------

namespace {
constexpr const char* kArrowSprite = "in25arrow";
constexpr const char* kInfoTextFormat = "infounit_%d";
constexpr const char* kPartLabelKeys[4] = {"hl_skull", "hl_torso", "hl_leg", "hl_arm"};
float StripScale() { return static_cast<float>(100.0f /*0073BAB0*/ / 0.48157462f /*007423FC*/); }
constexpr int kUnitTravel = 180;
constexpr int kWideUnitLeft = -133;   // WideScreen's rectangle (addon's constants, whatever the margin)
constexpr int kWideUnitRight = 52;
constexpr int kInfoTexts = 18;
constexpr std::uint32_t kPlainColour = 0x100;             // CUiMesh::flags_: drawn in flatColour_
} // namespace

void CHudUnitPanel::SetupVanilla(int bodyPart, std::uint8_t slid) {
    offset_ = slid ? kUnitTravel : 0;
    slide_ = slid ? 1.0f : 0.0f;
    slideDirection_ = slid ? 1.0f : -1.0f;
    if (widescreen::Active()) {
        // addon's handler (over the fields' stores): the panel offset, and its own
        // rectangle and slide state.
        widescreen::EnterLeftPanel();
        rect_ = RECT{kWideUnitLeft, 0, kWideUnitRight, 225};
        sliding_ = 0;
    } else {
        rect_ = RECT{0, 0, 185, 225};
        sliding_ = 1;
    }
    travel_ = kUnitTravel;

    CUiRegion* sling = AddRegion();
    sling->SetRect(0, 0, 20, 30);
    sling->SetTip(0x2774, 1, 0);
    for (int i = 0; i < 4; ++i) {
        CUiRegion* part = AddRegion();
        part->SetRect(0, 100 + 30 * i, 20, 130 + 30 * i);
        part->SetTip(0x2775 + i, 1, 0x2E + i);
    }
    AddRegion()->SetRect(0, 0, 185, 225);

    CUiMesh* back = AddSprite(nullptr);
    back->SetRectangle(60.0f, 100.0f, 20.0f, 0.0f, 180.0f, 220.0f, 0, 0, 256, 256);
    back->texture_ = nullptr;
    back->flags_ |= kPlainColour;
    back->flatColour_ = 0x50000000;
    back->visible_ = 0;
    CUiMesh* edge = AddSprite(nullptr);
    edge->SetRectangle(20.0f, 100.0f, 0.0f, 0.0f, 20.0f, 220.0f, 0, 0, 256, 256);
    edge->texture_ = nullptr;
    edge->flags_ |= kPlainColour;
    edge->flatColour_ = 0xB4000000;
    CUiMesh* slingButton = AddSprite(kArrowSprite);
    slingButton->textureOffsetU_ = 0.22265625f;
    slingButton->textureOffsetV_ = 0.140625f;
    PlaceSling(slingButton, kHudQuarterTurn, -50.0f);
    slingButton->SetScale(0.8f);
    // 3-6: the body part buttons; 7: the frame.
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 0.0f, 100.0f, 20.0f, 130.0f, 0x2F, 0x3E, 0x43, 0x5C);
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 0.0f, 130.0f, 20.0f, 160.0f, 0x45, 0x3E, 0x59, 0x5C);
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 0.0f, 160.0f, 20.0f, 190.0f, 3, 0x5E, 0x17, 0x7C);
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 0.0f, 190.0f, 20.0f, 220.0f, 0xE9, 0x70, 0xFD, 0x8E);
    AddSprite(nullptr)->SetFrame(10.0f, 80.0f, -5.0f, -5.0f, 185.0f, 225.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);
    // 8: a strip of twenty cells down the edge; 9: the "more effects" mark. Both hidden.
    CUiMesh* strip = AddSprite(nullptr);
    strip->SetGridVanilla(4.0f, StripScale(), 20.0f, 0.0f, 25.0f, 220.0f, 0x55, 2, 0x5A, 0x2A, 1, 20);
    strip->visible_ = 0;
    CUiMesh* more = AddSprite(nullptr);
    more->SetRectangle(4.0f, 100.0f, 164.0f, 204.0f, 180.0f, 220.0f, 0xD3, 2, 0xE3, 0x12);
    more->visible_ = 0;
    // 10-13: the health and the mana bar, each over its frame.
    AddSprite(nullptr)->SetRectangle(4.0f, 100.0f, 40.0f, 190.0f, 140.0f, 195.0f, 0x40, 0x9A, 0xA4, 0x9F);
    AddSprite(nullptr)->SetRectangle(5.0f, 100.0f, 35.0f, 190.0f, 145.0f, 195.0f, 0x3B, 0x93, 0xA9, 0x98);
    AddSprite(nullptr)->SetRectangle(4.0f, 100.0f, 40.0f, 205.0f, 140.0f, 210.0f, 0x40, 0xA1, 0xA4, 0xA6);
    AddSprite(nullptr)->SetRectangle(5.0f, 100.0f, 35.0f, 205.0f, 145.0f, 210.0f, 0x3B, 0x93, 0xA9, 0x98);
    // 14-25: the six body parts' bars, each over its frame, at the tables' places.
    for (int i = 0; i < kParts; ++i) {
        const int x = kPartX[i];
        const int y = kPartY[i];
        AddSprite(nullptr)->SetRectangle(4.0f, 50.0f, static_cast<float>(x + 25), static_cast<float>(y + 15),
                                         static_cast<float>(x + 75), static_cast<float>(y + 20), 0x40, 0x7E, 0x72, 0x83);
        AddSprite(nullptr)->SetRectangle(5.0f, 50.0f, static_cast<float>(x + 20), static_cast<float>(y + 15),
                                         static_cast<float>(x + 80), static_cast<float>(y + 20), 0x3B, 0x77, 0x77, 0x7C);
    }
    // 26-31: the effects' pictures, from the panel's icon texture.
    for (int top = 38; top < 218; top += 30) {
        CUiMesh* icon = AddSprite(nullptr);
        icon->SetRectangle(5.0f, 30.0f, 28.0f, static_cast<float>(top), 52.0f, static_cast<float>(top + 24), 0, 0, 64, 64);
        icon->texture_ = objects_.data[1];
    }

    AddSurface(new CUiSurface(interface_, 20, 0, 180, 220, 5, 1));

    bodyPart_ = bodyPart;
    for (int i = 0; i < 4; ++i) SetTint(sceneObjects_.data[3 + i], bodyPart_ == i ? 1.0f : 0.5f);
    Layout();
    shownUnit_ = nullptr;
    hoverUnit_ = nullptr;

    CTextManager* texts = TheTextManager();
    mfc::CString text;
    std::string key;
    mfc::CStringArray* infoTexts = &texts_;
    for (int i = 0; i < kInfoTexts; ++i) {
        key = str::Format(kInfoTextFormat, i);
        text.Empty();
        texts->GetString(key.c_str(), &text);
        infoTexts->SetAtGrow(infoTexts->size, text);
    }
    // A missing label keeps the text before it.
    mfc::CStringArray* labels = &partLabels_;
    for (const char* labelKey : kPartLabelKeys) {
        texts->GetString(labelKey, &text);
        labels->SetAtGrow(labels->size, text);
    }
    for (int& icon : effectIcons_) icon = -1;
    widescreen::LeavePanel();        // addon's 100230E2
}
EI_REWRITTEN(0x006456E0, CHudUnitPanel::SetupVanilla);

// --- The map's setup ----------------------------------------------------------------------

namespace {
constexpr const char* kDefaultPicture = "battle00";
float ZoneUnit() { return 32.0f /*0073CA90*/; }            // world units per unit of a zone's size
float MapSide() { return 2.0f /*0073CDB8*/; }             // the map's side in map units
float ZoomRange() { return 0.05f /*0074134C*/; }           // the largest zoom per world unit
float Pi() { return 3.1415927f /*007423F8*/; }
float OutlineStep() { return 0.125f /*0073C520*/; }         // sixteen points round the map
double OutlineRadius() { return 65.0 /*00742A98*/; }
double OutlineCentreX() { return 700.0 /*00742A88*/; }
double OutlineCentreY() { return 80.0 /*00742A90*/; }
float DepthUnit() { return 0.48157462f /*007423FC*/; }           // the layout's half height at depth 1
float BoundsScale() { return 0.045f /*00742A80*/; }
float MarkersScale() { return 0.03f /*00742A7C*/; }
float SlingQuarter() { return (-0.5f /*0073CDC8*/); }        // the sling starts a quarter turn back
constexpr int kMapTextureKind = 3;
constexpr int kMapTravel = 180;
constexpr int kOutlinePoints = 16;

const MapRecord* LocalZone() {
    mfc::CString name(mfc::CString::kUnset);
    return TheMapDatabase()->FindMap(*LocalPlayerName(&name));
}

void SetPlain(CUiMesh* sprite, std::uint32_t colour) {
    sprite->texture_ = nullptr;
    sprite->flags_ |= kPlainColour;
    sprite->flatColour_ = colour;
}
} // namespace

void CHudMap::SetupVanilla(float zoom, std::uint8_t slid) {
    widescreen::EnterRightPanel();   // addon's 10023344
    const int shift = widescreen::EdgeShift();
    rect_ = RECT{615 + shift, 0, 800 + shift, 165};   // WideScreen
    halfSide_ = 80.0f;

    // The world's extent and the map's picture (referenced object 1), from the zone's record.
    if (const MapRecord* zone = LocalZone()) {
        worldWidth_ = static_cast<float>(zone->sizeX) * ZoneUnit();
        worldHeight_ = static_cast<float>(zone->sizeY) * ZoneUnit();
        LoadTexture(zone->picture, kMapTextureKind, nullptr);
    } else {
        worldWidth_ = 32.0f;
        worldHeight_ = 32.0f;
        LoadTexture(kDefaultPicture, kMapTextureKind, nullptr);
    }
    textureSize_ = worldWidth_ < worldHeight_ ? worldHeight_ : worldWidth_;
    scale_ = MapSide() / textureSize_;
    zoomMax_ = textureSize_ * ZoomRange();
    zoom_ = zoomMax_ < zoom ? zoomMax_ : zoom;
    centreX_ = 0.0f;
    centreY_ = 0.0f;
    facingX_ = 0.0f;
    facingY_ = 1.0f;
    offset_ = slid ? kMapTravel : 0;
    slide_ = slid ? 1.0f : 0.0f;
    sliding_ = 1;
    travel_ = kMapTravel;
    slideDirection_ = slid ? 1.0f : -1.0f;

    // Regions: 0 the sling button, 1 the round map, 2 the map's square, 3 and 4 the view buttons,
    // 5 "camera north", 6 the whole panel.
    CUiRegion* sling = AddRegion();
    sling->SetRect(780, 0, 800, 30);
    sling->SetTip(0x283C, 1, 0x34);
    // The round map's outline: each product and sum is a float on the game's FPU (24-bit precision; FCOS /
    // FSIN themselves are not rounded), so the point under the centre is 80 down, not 79 (see OnArc).
    CUiRegion* round = AddRegion();
    for (int i = 0; i < kOutlinePoints; ++i) {
        const float angle = static_cast<float>(static_cast<float>(static_cast<float>(i) * Pi()) * OutlineStep());
        const float down = static_cast<float>(x87::fcos(static_cast<double>(angle)) * OutlineRadius());
        const int y = static_cast<int>(gamecrt::Ftol(static_cast<float>(down + OutlineCentreY())));
        const float across = static_cast<float>(x87::fsin(static_cast<double>(angle)) * OutlineRadius());
        const int x = static_cast<int>(gamecrt::Ftol(static_cast<float>(OutlineCentreX() - across)));
        round->AddPoint(x, y);
    }
    AddRegion()->SetRect(620, 0, 780, 160);
    CUiRegion* zoomIn = AddRegion();
    zoomIn->SetRect(780, 100, 800, 130);
    zoomIn->SetTip(0x283D, 1, 0);
    CUiRegion* zoomOut = AddRegion();
    zoomOut->SetRect(780, 130, 800, 160);
    zoomOut->SetTip(0x283E, 1, 0);
    CUiRegion* north = AddRegion();
    north->SetRect(780, 70, 800, 100);
    north->SetTip(0x283F, 1, 0xC);
    AddRegion()->SetRect(615, 0, 800, 165);

    // Scene object 0: the world's bounds, in the map's picture; 1: the visible area's disc, in the
    // picture too; 2: the markers, four texels of one colour each.
    CUiMesh* bounds = AddSprite(nullptr);
    bounds->SetRectangle(18.0f, 100.0f, 620.0f, 0.0f, 780.0f, 160.0f, 0, 0, 256, 256);
    bounds->texture_ = objects_.data[1];
    bounds->tint_[0] = bounds->tint_[1] = bounds->tint_[2] = 0.8f;
    bounds->SetScale(static_cast<float>(static_cast<float>(DepthUnit() * halfSide_) * BoundsScale()));
    CUiMesh* area = AddSprite(nullptr);
    area->Disc(14.0f, 80.0f, 700.0f, 80.0f, 64.0f, kOutlinePoints, 0x80, 0x80, 0x80);
    area->texture_ = objects_.data[1];
    area->tint_[0] = area->tint_[1] = area->tint_[2] = 1.0f;
    CUiMesh* markers = AddSprite(nullptr);
    markers->PlaceBody(700.0f, 80.0f, 12.0f);
    markers->SetScale(static_cast<float>(static_cast<float>(DepthUnit() * halfSide_) * MarkersScale()));
    markers->ResetColours();
    markers->SetUvCount(4);
    const UiTexel markerTexels[4] = {
        {0.9375f, 0.984375f}, {0.953125f, 0.984375f}, {0.96875f, 0.984375f}, {0.984375f, 0.984375f}};
    for (int i = 0; i < 4; ++i) markers->Texels().data[i] = markerTexels[i];
    // 3 and 4: the map's and the buttons' plain backgrounds.
    CUiMesh* back = AddSprite(nullptr);
    back->SetRectangle(20.0f, 100.0f, 620.0f, 0.0f, 780.0f, 160.0f, 0, 0, 256, 256);
    SetPlain(back, 0x50000000);
    CUiMesh* edge = AddSprite(nullptr);
    edge->SetRectangle(20.0f, 100.0f, 780.0f, 0.0f, 800.0f, 160.0f, 0, 0, 256, 256);
    SetPlain(edge, 0xB4000000);
    CUiMesh* slingButton = AddSprite(kArrowSprite);
    slingButton->textureOffsetU_ = 0.22265625f;
    slingButton->textureOffsetV_ = 0.140625f;
    PlaceSling(slingButton, static_cast<float>(Pi() * SlingQuarter()), 850.0f);
    slingButton->SetScale(0.8f);
    // 6 and 7: the view buttons; 8: the ring round the map; 9: the frame; 10: the compass needle;
    // 11: "camera north".
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 780.0f, 100.0f, 800.0f, 130.0f, 0x19, 0x3E, 0x2D, 0x5C);
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 780.0f, 130.0f, 800.0f, 160.0f, 3, 0x3E, 0x17, 0x5C);
    AddSprite(nullptr)->Ring(10.0f, 60.0f, 700.0f, 80.0f, 66.0f, kOutlinePoints, 3.0f, 0x1B, 0x6B, 0x5B, 0x70);
    AddSprite(nullptr)->SetFrame(10.0f, 80.0f, 615.0f, -5.0f, 805.0f, 165.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);
    CUiMesh* needle = AddSprite(kArrowSprite);
    needle->textureOffsetU_ = 0.22265625f;
    needle->textureOffsetV_ = 0.140625f;
    needle->PlaceBody(900.0f, 80.0f, FromBits(kSlingDepthBits));
    needle->SetScale(0.8f);
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 780.0f, 70.0f, 800.0f, 100.0f, 0x22, 0x76, 0x36, 0x94);

    CUiSurface* dot = new CUiSurface(interface_, 700, 80, 705, 85, 5, 1);
    AddSurface(dot);
    mfc::impl::CDC* dc = UiSurfaceGetDC(dot);
    gameapi::SetPixel(dc->hdc, 0, 0, 0xFFFFFF);
    UiSurfaceReleaseDC(dot);

    const CUiMesh* disc = SceneMesh(1);
    for (int block = 0; block < kOutlinePoints / 4; ++block) {
        for (int corner = 0; corner < 4; ++corner)
            viewPolygon_.Add(UiPointF{disc->Quads()[block].x[corner], disc->Quads()[block].y[corner]});
    }
    UpdateView();
    BuildMarkers();

    view_ = 0;
    for (int i = 0; i < 2; ++i) SetTint(sceneObjects_.data[6 + i], view_ == i + 1 ? 1.0f : 0.5f);
    Layout();
    northTime_ = 0.0f;
    widescreen::LeavePanel();        // addon's 10023359
}
EI_REWRITTEN(0x00641F20, CHudMap::SetupVanilla);
