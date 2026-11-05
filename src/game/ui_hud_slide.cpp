// The battle HUD's two sliding side panels, the map (HudMap) and the unit panel (HudUnitPanel): their layouts at the
// current slide offset, their per-frame updates and their setups. Rebirth patches all of them for its WideScreen layout,
// reproduced for every window (screen_layout.h). See ui_hud.h and docs/notes/ui.md, "Battle HUD sliding panels".
#include "ui.h"
#include "ui_mesh.h"
#include "ui_hud.h"

#include "text_manager.h"
#include "map_object.h"
#include "object_tables.h"
#include "screen_layout.h"
#include "unit.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/maths.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "game_singletons.h"

#include <concepts>
#include <cmath>

namespace {

constexpr const char* kSoundSling = "buttons\\battle\\sling.wav";
constexpr float kMapCentreX = 700.0f;
constexpr float kMapButtonsX = 790.0f;
constexpr float kMapFrameX = 710.0f;
constexpr float kMapNeedleX = 694.0f;
constexpr float kUnitBackX = 100.0f;
constexpr float kUnitEdgeX = 10.0f;
constexpr float kUnitMiddleX = 90.0f;
constexpr float kUnitMoreX = 172.0f;
constexpr float kUnitPartDown = 17.5f;
constexpr float kUnitEffectX = 40.0f;
constexpr float kSlideTime = 0.3f;       // 0.3 seconds for the whole way
// The unit panel's six body parts: where each one's pair of sprites goes (two tables of six ints).
constexpr int kPartX[6] = /*00742434*/ {50, 50, 95, 5, 95, 5};
constexpr int kPartY[6] = /*0074244C*/ {35, 105, 70, 70, 140, 140};
constexpr int kParts = 6;
constexpr int kEffects = 6;

constexpr float kZoomRate = 2.5f;                   // the map's zoom changes 2.5 times a second
constexpr float kSlingDepthBits = 8.106086f;   // 8.106: the sling buttons and the needle
constexpr float kSlingY = 20.0f;

// WideScreen's layout constants (addon's operands, exact integers as floats): moved right for the
// map, left for the unit panel.
float RightEdge(float x) { return x + screen_layout::EdgeShift(); }
float LeftEdge(float x) { return x - screen_layout::EdgeShift(); }

void SetTint(UiMesh* object, float value) {
    float* tint = object->tint;
    tint[0] = tint[1] = tint[2] = value;
}

// Every region but the sling button moves with the panel.
void MoveRegions(UiWindow* window, const UiRegion* sling, int offsetX) {
    for (const auto& region : window->regions) {
        if (region.get() == sling) continue;
        region->offsetX = offsetX;
        region->offsetY = 0;
    }
}

void PlaceSling(UiMesh* sling, float angle, float x) {
    const Vector3 axis = {0.0f, 0.0f, 1.0f};
    sling->localRotation = Quaternion::AxisAngle(axis, angle);
    sling->Place(x, kSlingY, kSlingDepthBits);
}

constexpr const char* kArrowSprite = "in25arrow";
constexpr const char* kInfoTextFormat = "infounit_%d";
constexpr const char* kPartLabelKeys[4] = {"hl_skull", "hl_torso", "hl_leg", "hl_arm"};
float StripScale() { return 100.0f / 0.48157462f; }
constexpr int kUnitTravel = 180;
constexpr int kInfoTexts = 18;

constexpr const char* kDefaultPicture = "battle00";
constexpr float kZoneUnit = 32.0f;            // world units per unit of a zone's size
constexpr float kMapSide = 2.0f;             // the map's side in map units
constexpr float kZoomRange = 0.05f;           // the largest zoom per world unit
constexpr float kOutlineStep = 0.125f;         // sixteen points round the map
constexpr float kOutlineRadius = 65.0f;
constexpr float kOutlineCentreX = 700.0f;
constexpr float kOutlineCentreY = 80.0f;
constexpr float kDepthUnit = 0.48157462f;           // the layout's half height at depth 1
constexpr float kBoundsScale = 0.045f;
constexpr float kMarkersScale = 0.03f;
constexpr float kSlingQuarter = -0.5f;        // the sling starts a quarter turn back
constexpr int kMapTravel = 180;
constexpr int kOutlinePoints = 16;

const MapRecord* LocalZone() {
    return g_mapDatabase.FindMap(LocalPlayerZone());
}

void SetPlain(UiMesh* sprite, std::uint32_t colour) {
    sprite->texture = nullptr;
    sprite->flags |= FigureMeshFlag::Flat;
    sprite->flatColour = colour;
}

} // namespace

// --- The slide -----------------------------------------------------------------------------------

void PanelSlide::Step() {
    t = UiContainer::FrameTime() * direction / kSlideTime + t;
    if (t > 1.0f) t = 1.0f;
    t = MaxKeepB(t, 0.0f);      // NaN too
    if (t == 1.0f || t == 0.0f) moving = false;
}

void PanelSlide::Toggle(Interface3D* ui) {
    ui->PlayInterfaceSound(kSoundSling);
    direction = -direction;
    moving = true;
}

int PanelSlide::Offset() const {
    float eased;
    if (t < 0.5f) {
        eased = 2.0f * (t * t);
    } else {
        const float rest = 1.0f - t;
        eased = 1.0f - 2.0f * (rest * rest);
    }
    return static_cast<int>(travel * eased);
}

// --- The map -------------------------------------------------------------------------------------

void HudMap::Layout() {
    screen_layout::EnterRightPanel();   // addon's 10023366
    const int offset = slide.Offset();
    const float slideOffset = static_cast<float>(offset);
    const float middle = slideOffset + kMapCentreX;
    const float buttons = slideOffset + kMapButtonsX;
    bounds->Place(middle, 80.0f, 18.0f);
    visibleDisc->Place(middle, 80.0f, 14.0f);
    markers->Place(middle, 80.0f, 12.0f);
    mapBack->Place(middle, 80.0f, 20.0f);
    buttonsBack->Place(buttons, 80.0f, 20.0f);
    viewIcons[0]->Place(buttons, 115.0f, 10.0f);
    viewIcons[1]->Place(buttons, 145.0f, 10.0f);
    ring->Place(middle, 80.0f, 10.0f);
    frame->Place(slideOffset + kMapFrameX, 80.0f, 10.0f);
    needle->Place(slideOffset + kMapNeedleX, 84.0f, kSlingDepthBits);
    northIcon->Place(buttons, 85.0f, 10.0f);
    surfaces[0]->MoveTo({offset + 700 + screen_layout::EdgeShift(), 80});     // WideScreen
    screen_layout::LeavePanel();        // addon's 1002337E, before the regions
    MoveRegions(this, slingButton, offset);
}

void HudMap::Update() {
    // The zoom runs towards the chosen view's end: up to zoomMax (view 1) or down to 1 (view 2).
    if (view == 1) {
        const float newZoom = lib::Pow(kZoomRate, UiContainer::FrameTime()) * zoom;
        zoom = newZoom;
        if (newZoom > zoomMax) zoom = zoomMax;
    }
    if (view == 2) {
        const float newZoom = zoom / lib::Pow(kZoomRate, UiContainer::FrameTime());
        zoom = newZoom;
        if (newZoom < 1.0f) zoom = 1.0f;
    }
    UpdateView();
    BuildMarkers();

    if (slide.moving) {
        slide.Step();
        Layout();
        // WideScreen.
        if (slide.direction == 1.0f)
            PlaceSling(slingArrow, kHudQuarterTurn, RightEdge(713.0f));
        else
            PlaceSling(slingArrow, -kHudQuarterTurn, RightEdge(850.0f));
    }
    ui->hudPanelsIn[2] = slide.direction > 0.0f ? 1 : 0;

    if (northTime > 0.0f) northTime = northTime - UiContainer::FrameTime();
    SetTint(northIcon, northTime > 0.0f ? 1.0f : 0.5f);
}

// --- The unit panel ------------------------------------------------------------------------------

void HudUnitPanel::Layout() {
    screen_layout::EnterLeftPanel();    // addon's 100232AF
    const int offset = slide.Offset();
    const float slideOffset = static_cast<float>(offset);
    const float edgeX = kUnitEdgeX - slideOffset;
    const float middle = kUnitMiddleX - slideOffset;
    background->Place(kUnitBackX - slideOffset, 110.0f, 60.0f);
    edge->Place(edgeX, 110.0f, 20.0f);
    partIcons[0]->Place(edgeX, 115.0f, 10.0f);
    partIcons[1]->Place(edgeX, 145.0f, 10.0f);
    partIcons[2]->Place(edgeX, 175.0f, 10.0f);
    partIcons[3]->Place(edgeX, 205.0f, 10.0f);
    frame->Place(middle, 110.0f, 10.0f);
    moreMark->Place(kUnitMoreX - slideOffset, 212.0f, 4.0f);
    healthBar->Place(middle, 192.5f, 4.0f);
    healthFrame->Place(middle, 192.5f, 5.0f);
    manaBar->Place(middle, 207.5f, 4.0f);
    manaFrame->Place(middle, 207.5f, 5.0f);
    for (int i = 0; i < kParts; ++i) {
        const float y = kPartY[i] + kUnitPartDown;
        const float x = (kPartX[i] + 50) - slideOffset;
        bodyBars[i]->Place(x, y, 4.0f);
        bodyFrames[i]->Place(x, y, 5.0f);
    }
    for (int i = 0; i < kEffects; ++i)
        effectSprites[i]->Place(kUnitEffectX - slideOffset, static_cast<float>(50 + 30 * i), 5.0f);
    surfaces[0]->MoveTo({20 - screen_layout::EdgeShift() - offset, 0});     // WideScreen
    MoveRegions(this, slingButton, -offset);
    screen_layout::LeavePanel();        // addon's 100232E9
}

void HudUnitPanel::Update() {
    if (slide.moving) {
        slide.Step();
        Layout();
        // WideScreen.
        if (slide.direction == -1.0f)
            PlaceSling(slingArrow, kHudQuarterTurn, LeftEdge(-50.0f));
        else
            PlaceSling(slingArrow, -kHudQuarterTurn, LeftEdge(87.0f));
    }
    if (hoverUnit && hoverUnit->IsDead() != 0) hoverUnit = nullptr;
    ui->hudPanelsIn[0] = slide.direction > 0.0f ? 1 : 0;
    ui->hudBodyPart = bodyPart;
}

// --- The unit panel's setup ----------------------------------------------------------------

void HudUnitPanel::Setup(int newBodyPart, bool slid) {
    slide.t = slid ? 1.0f : 0.0f;
    slide.direction = slid ? 1.0f : -1.0f;
    // The panel offset from here on (addon's handler over the fields' stores) and the original's rectangle moved to the
    // window's left edge. addon wrote (-133, 0)-(52, 225) and `moving` 0 whatever the margin: its rectangle is this one
    // at 16:9 but a unit to the right, and without the first update's slide a panel set up slid out kept its sling
    // button where it was made, turned the other way.
    screen_layout::EnterLeftPanel();
    const int shift = screen_layout::EdgeShift();
    rect = Rect{-shift, 0, 185 - shift, 225};
    slide.moving = true;
    slide.travel = kUnitTravel;

    slingButton = AddRegion();
    slingButton->SetRect(0, 0, 20, 30);
    slingButton->SetTip(0x2774);
    for (int i = 0; i < 4; ++i) {
        partButtons[i] = AddRegion();
        partButtons[i]->SetRect(0, 100 + 30 * i, 20, 130 + 30 * i);
        partButtons[i]->SetTip(0x2775 + i, 0x2E + i);
    }
    AddRegion()->SetRect(0, 0, 185, 225);

    background = AddBlankSprite();
    background->SetRectangle(60.0f, kSpriteScale, 20.0f, 0.0f, 180.0f, 220.0f, 0, 0, 256, 256);
    background->texture = nullptr;
    background->flags |= FigureMeshFlag::Flat;
    background->flatColour = 0x50000000;
    background->visible = false;
    edge = AddBlankSprite();
    edge->SetRectangle(20.0f, kSpriteScale, 0.0f, 0.0f, 20.0f, 220.0f, 0, 0, 256, 256);
    edge->texture = nullptr;
    edge->flags |= FigureMeshFlag::Flat;
    edge->flatColour = 0xB4000000;
    slingArrow = AddSprite(kArrowSprite);
    slingArrow->textureOffsetU = 0.22265625f;
    slingArrow->textureOffsetV = 0.140625f;
    PlaceSling(slingArrow, kHudQuarterTurn, -50.0f);
    slingArrow->SetScale(0.8f);
    partIcons[0] = AddBlankSprite();
    partIcons[0]->SetRectangle(10.0f, 80.0f, 0.0f, 100.0f, 20.0f, 130.0f, 0x2F, 0x3E, 0x43, 0x5C);
    partIcons[1] = AddBlankSprite();
    partIcons[1]->SetRectangle(10.0f, 80.0f, 0.0f, 130.0f, 20.0f, 160.0f, 0x45, 0x3E, 0x59, 0x5C);
    partIcons[2] = AddBlankSprite();
    partIcons[2]->SetRectangle(10.0f, 80.0f, 0.0f, 160.0f, 20.0f, 190.0f, 3, 0x5E, 0x17, 0x7C);
    partIcons[3] = AddBlankSprite();
    partIcons[3]->SetRectangle(10.0f, 80.0f, 0.0f, 190.0f, 20.0f, 220.0f, 0xE9, 0x70, 0xFD, 0x8E);
    frame = AddBlankSprite();
    frame->SetFrame(10.0f, 80.0f, -5.0f, -5.0f, 185.0f, 225.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);
    heightBar = AddBlankSprite();
    heightBar->SetGrid(4.0f, StripScale(), 20.0f, 0.0f, 25.0f, 220.0f, 0x55, 2, 0x5A, 0x2A, 1, 20);
    heightBar->visible = false;
    moreMark = AddBlankSprite();
    moreMark->SetRectangle(4.0f, kSpriteScale, 164.0f, 204.0f, 180.0f, 220.0f, 0xD3, 2, 0xE3, 0x12);
    moreMark->visible = false;
    healthBar = AddBlankSprite();
    healthBar->SetRectangle(4.0f, kSpriteScale, 40.0f, 190.0f, 140.0f, 195.0f, 0x40, 0x9A, 0xA4, 0x9F);
    healthFrame = AddBlankSprite();
    healthFrame->SetRectangle(5.0f, kSpriteScale, 35.0f, 190.0f, 145.0f, 195.0f, 0x3B, 0x93, 0xA9, 0x98);
    manaBar = AddBlankSprite();
    manaBar->SetRectangle(4.0f, kSpriteScale, 40.0f, 205.0f, 140.0f, 210.0f, 0x40, 0xA1, 0xA4, 0xA6);
    manaFrame = AddBlankSprite();
    manaFrame->SetRectangle(5.0f, kSpriteScale, 35.0f, 205.0f, 145.0f, 210.0f, 0x3B, 0x93, 0xA9, 0x98);
    // The body parts' bars at the tables' places.
    for (int i = 0; i < kParts; ++i) {
        const int x = kPartX[i];
        const int y = kPartY[i];
        bodyBars[i] = AddBlankSprite();
        bodyBars[i]->SetRectangle(4.0f, 50.0f, static_cast<float>(x + 25), static_cast<float>(y + 15),
                                  static_cast<float>(x + 75), static_cast<float>(y + 20), 0x40, 0x7E, 0x72, 0x83);
        bodyFrames[i] = AddBlankSprite();
        bodyFrames[i]->SetRectangle(5.0f, 50.0f, static_cast<float>(x + 20), static_cast<float>(y + 15),
                                    static_cast<float>(x + 80), static_cast<float>(y + 20), 0x3B, 0x77, 0x77, 0x7C);
    }
    for (int i = 0; i < kEffects; ++i) {
        const int top = 38 + 30 * i;
        effectSprites[i] = AddBlankSprite();
        effectSprites[i]->SetRectangle(5.0f, 30.0f, 28.0f, static_cast<float>(top), 52.0f, static_cast<float>(top + 24),
                                       0, 0, 64, 64);
        effectSprites[i]->texture = objects[1];
    }

    UiSurface* panel = AddSurface(std::make_unique<UiSurface>(ui, 20, 0, 180, 220, SurfaceBlend::Keyed, true));
    panel->PaintWith([this](UiSurface& out) { PaintTexts(out); });

    bodyPart = newBodyPart;
    for (int i = 0; i < 4; ++i) SetTint(partIcons[i], bodyPart == i ? 1.0f : 0.5f);
    Layout();
    shownUnit = nullptr;
    hoverUnit = nullptr;

    TextManager* clientWorldTexts = g_clientWorld.texts.get();
    std::string text;
    std::string key;
    for (int i = 0; i < kInfoTexts; ++i) {
        key = str::Format(kInfoTextFormat, i);
        text.clear();
        text = clientWorldTexts->GetString(key);
        texts.push_back(text);
    }
    // A missing label keeps the text before it.
    for (const char* labelKey : kPartLabelKeys) {
        text = clientWorldTexts->GetString(labelKey);
        partLabels.push_back(text);
    }
    for (int& icon : effectIcons) icon = -1;
    screen_layout::LeavePanel();        // addon's 100230E2
}

// --- The map's setup ----------------------------------------------------------------------

void HudMap::Setup(float newZoom, bool slid) {
    screen_layout::EnterRightPanel();   // addon's 10023344
    const int shift = screen_layout::EdgeShift();
    rect = Rect{615 + shift, 0, 800 + shift, 165};    // WideScreen
    halfSide = 80.0f;

    // The world's extent and the map's picture (referenced object 1), from the zone's record.
    if (const MapRecord* zone = LocalZone()) {
        worldWidth = zone->sizeX * kZoneUnit;
        worldHeight = zone->sizeY * kZoneUnit;
        LoadTexture(zone->picture, TextureKind::Interface);
    } else {
        worldWidth = 32.0f;
        worldHeight = 32.0f;
        LoadTexture(kDefaultPicture, TextureKind::Interface);
    }
    textureSize = worldWidth < worldHeight ? worldHeight : worldWidth;
    scale = kMapSide / textureSize;
    zoomMax = textureSize * kZoomRange;
    zoom = zoomMax < newZoom ? zoomMax : newZoom;
    centre = {0.0f, 0.0f};
    facing = {0.0f, 1.0f};
    slide.t = slid ? 1.0f : 0.0f;
    slide.moving = true;
    slide.travel = kMapTravel;
    slide.direction = slid ? 1.0f : -1.0f;

    slingButton = AddRegion();
    slingButton->SetRect(780, 0, 800, 30);
    slingButton->SetTip(0x283C, 0x34);
    // The round map's outline (a point whose product falls just short of a whole pixel may truncate one
    // pixel lower than the original's, see OnArc).
    roundMap = AddRegion();
    for (int i = 0; i < kOutlinePoints; ++i) {
        const float angle = (i * kPi) * kOutlineStep;
        const float down = lib::Cos(angle) * kOutlineRadius;
        const int y = static_cast<int>((down + kOutlineCentreY));
        const float across = lib::Sin(angle) * kOutlineRadius;
        const int x = static_cast<int>((kOutlineCentreX - across));
        roundMap->AddPoint(x, y);
    }
    mapSquare = AddRegion();
    mapSquare->SetRect(620, 0, 780, 160);
    viewButtons[0] = AddRegion();
    viewButtons[0]->SetRect(780, 100, 800, 130);
    viewButtons[0]->SetTip(0x283D);
    viewButtons[1] = AddRegion();
    viewButtons[1]->SetRect(780, 130, 800, 160);
    viewButtons[1]->SetTip(0x283E);
    northButton = AddRegion();
    northButton->SetRect(780, 70, 800, 100);
    northButton->SetTip(0x283F, 0xC);
    AddRegion()->SetRect(615, 0, 800, 165);

    bounds = AddBlankSprite();
    bounds->SetRectangle(18.0f, kSpriteScale, 620.0f, 0.0f, 780.0f, 160.0f, 0, 0, 256, 256);
    bounds->texture = objects[1];
    bounds->tint[0] = bounds->tint[1] = bounds->tint[2] = 0.8f;
    bounds->SetScale((kDepthUnit * halfSide) * kBoundsScale);
    visibleDisc = AddBlankSprite();
    visibleDisc->Disc(14.0f, 80.0f, 700.0f, 80.0f, 64.0f, kOutlinePoints, 0x80, 0x80, 0x80);
    visibleDisc->texture = objects[1];
    visibleDisc->tint[0] = visibleDisc->tint[1] = visibleDisc->tint[2] = 1.0f;
    markers = AddBlankSprite();
    markers->Place(700.0f, 80.0f, 12.0f);
    markers->SetScale((kDepthUnit * halfSide) * kMarkersScale);
    markers->ResetColours();
    markers->SetUvCount(4);
    const TexCoord markerTexels[4] = {
        {0.9375f, 0.984375f}, {0.953125f, 0.984375f}, {0.96875f, 0.984375f}, {0.984375f, 0.984375f}};
    for (int i = 0; i < 4; ++i) markers->texCoords[i] = markerTexels[i];
    mapBack = AddBlankSprite();
    mapBack->SetRectangle(20.0f, kSpriteScale, 620.0f, 0.0f, 780.0f, 160.0f, 0, 0, 256, 256);
    SetPlain(mapBack, 0x50000000);
    buttonsBack = AddBlankSprite();
    buttonsBack->SetRectangle(20.0f, kSpriteScale, 780.0f, 0.0f, 800.0f, 160.0f, 0, 0, 256, 256);
    SetPlain(buttonsBack, 0xB4000000);
    slingArrow = AddSprite(kArrowSprite);
    slingArrow->textureOffsetU = 0.22265625f;
    slingArrow->textureOffsetV = 0.140625f;
    PlaceSling(slingArrow, kPi * kSlingQuarter, 850.0f);
    slingArrow->SetScale(0.8f);
    viewIcons[0] = AddBlankSprite();
    viewIcons[0]->SetRectangle(10.0f, 80.0f, 780.0f, 100.0f, 800.0f, 130.0f, 0x19, 0x3E, 0x2D, 0x5C);
    viewIcons[1] = AddBlankSprite();
    viewIcons[1]->SetRectangle(10.0f, 80.0f, 780.0f, 130.0f, 800.0f, 160.0f, 3, 0x3E, 0x17, 0x5C);
    ring = AddBlankSprite();
    ring->Ring(10.0f, 60.0f, 700.0f, 80.0f, 66.0f, kOutlinePoints, 3.0f, 0x1B, 0x6B, 0x5B, 0x70);
    frame = AddBlankSprite();
    frame->SetFrame(10.0f, 80.0f, 615.0f, -5.0f, 805.0f, 165.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);
    needle = AddSprite(kArrowSprite);
    needle->textureOffsetU = 0.22265625f;
    needle->textureOffsetV = 0.140625f;
    needle->Place(900.0f, 80.0f, kSlingDepthBits);
    needle->SetScale(0.8f);
    northIcon = AddBlankSprite();
    northIcon->SetRectangle(10.0f, 80.0f, 780.0f, 70.0f, 800.0f, 100.0f, 0x22, 0x76, 0x36, 0x94);

    UiSurface* dot = AddSurface(std::make_unique<UiSurface>(ui, 700, 80, 705, 85, SurfaceBlend::Keyed, true));
    dot->PaintWith([](UiSurface& out) { out.Dot({0, 0}, 0xFFFFFF); });

    for (int block = 0; block < kOutlinePoints / 4; ++block) {
        const VertexPacket& quad = visibleDisc->vertices[block];
        for (int corner = 0; corner < 4; ++corner) viewPolygon.push_back(Vector2{quad.x()[corner], quad.y()[corner]});
    }
    UpdateView();
    BuildMarkers();

    view = 0;
    for (int i = 0; i < 2; ++i) SetTint(viewIcons[i], view == i + 1 ? 1.0f : 0.5f);
    Layout();
    northTime = 0.0f;
    screen_layout::LeavePanel();        // addon's 10023359
}
