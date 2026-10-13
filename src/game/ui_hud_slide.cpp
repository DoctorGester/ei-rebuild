// The battle HUD's two sliding side panels, the map (CHudMap) and the unit panel (CHudUnitPanel):
// their layouts at the current slide offset (00642930, 00645F80), their per-frame updates
// (00642B40, 00647FB0, slot 24) and the unit panel's setup (006456E0). A-H26.
// Rebirth patches all of them (its widescreen layout), so these are the vanilla bodies and install
// in the vanilla launch only (rule 9); the callees Rebirth patches too (the sprites' Place
// 005E6FA0 and shapes 005E70E0 / 005E7680 / 005E7D60, the regions' SetRect 005FC9E0) are called
// through their addresses. See ui.h ("Battle screen and its HUD panels") and
// docs/notes/items_spells.md ("A-H26").
#include "ui.h"

#include "item_db_text.h"       // GameTexts
#include "map_object.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"
#include "originals_render.h"   // QuaternionSetAxisAngleOriginal
#include "originals_ui.h"       // SpritePlaceOriginal, WindowFrameTimeOriginal

#include <cmath>
#include <cstdint>
#include <cstring>

__declspec(naked) void CHudMap::CallLayout() EI_ORIGINAL(0x00642930)
__declspec(naked) void CHudUnitPanel::CallLayout() EI_ORIGINAL(0x00645F80)

namespace {

// The map's two per-frame rebuilds (rewritten in ui_hud_map.cpp), through their addresses so that
// their verify wrappers run.
__declspec(naked) void __fastcall MapUpdateViewAt(CHudMap*) EI_ORIGINAL(0x00642E80)
__declspec(naked) void __fastcall MapBuildMarkersAt(CHudMap*) EI_ORIGINAL(0x00643DC0)

// The layouts' x coordinates and the slide's time are read where the original reads them.
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }
float MapCentreX() { return GameFloat(0x00742AAC); }      // 700
float MapButtonsX() { return GameFloat(0x00742AA8); }     // 790
float MapFrameX() { return GameFloat(0x00742AA4); }       // 710
float MapNeedleX() { return GameFloat(0x00742AA0); }      // 694
float UnitBackX() { return GameFloat(0x0073BAB0); }       // 100
float UnitEdgeX() { return GameFloat(0x0073BAAC); }       // 10
float UnitMiddleX() { return GameFloat(0x00742AE0); }     // 90
float UnitMoreX() { return GameFloat(0x00742ADC); }       // 172
float UnitPartDown() { return GameFloat(0x00742AD8); }    // 17.5
float UnitEffectX() { return GameFloat(0x0073D72C); }     // 40
float SlideTime() { return GameFloat(0x00742428); }       // 0.3 seconds for the whole way
float SlingTurn() { return GameFloat(0x007C8144); }       // the sling buttons' turn (a global angle)
// The unit panel's six body parts: where each one's pair of sprites goes (two tables of six ints).
const int* PartX() { return reinterpret_cast<const int*>(std::uintptr_t(0x00742434)); }
const int* PartY() { return reinterpret_cast<const int*>(std::uintptr_t(0x0074244C)); }
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

void Place(CUiWindow* window, int index, float x, float y, float depth) {
    SpritePlaceOriginal(window->sceneObjects_.data[index], nullptr, x, y, depth);
}

void SetTint(CEngineObject* object, float value) {
    float* tint = reinterpret_cast<CUiMesh*>(object)->tint_;
    tint[0] = tint[1] = tint[2] = value;
}

// The regions after the first follow the panel.
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
        WindowFrameTimeOriginal(panel) * panel->slideDirection_ / SlideTime() + panel->slide_;
    panel->slide_ = RoundToFloat(moved);
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

// A sling button turned by `angle` about z and put at `x`.
void PlaceSling(CEngineObject* sling, float angle, float x) {
    const Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion turn;
    reinterpret_cast<CUiMesh*>(sling)->orientation_ = *QuaternionSetAxisAngleOriginal(&turn, nullptr, &axis, angle);
    SpritePlaceOriginal(sling, nullptr, x, kSlingY, FromBits(kSlingDepthBits));
}

} // namespace

// --- The map -------------------------------------------------------------------------------------

void CHudMap::LayoutVanilla() {
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
    surfaces_.data[0]->MoveTo(offset_ + 700, 80);
    MoveRegions(this, offset_);
}
EI_REPLACE_METHOD(0x00642930, CHudMap::LayoutVanilla);

void CHudMap::UpdateVanilla() {
    // The zoom runs towards the chosen view's end: up to zoomMax_ (view 1) or down to 1 (view 2).
    if (view_ == 1) {
        const double zoom = std::pow(kZoomRate, WindowFrameTimeOriginal(this)) * zoom_;
        zoom_ = RoundToFloat(zoom);
        if (zoom > zoomMax_) zoom_ = zoomMax_;
    }
    if (view_ == 2) {
        const double zoom = zoom_ / std::pow(kZoomRate, WindowFrameTimeOriginal(this));
        zoom_ = RoundToFloat(zoom);
        if (zoom < 1.0) zoom_ = 1.0f;
    }
    MapUpdateViewAt(this);
    MapBuildMarkersAt(this);

    if (sliding_) {
        AdvanceSlide(this);
        CallLayout();
        // The sling button (scene object 5).
        if (slideDirection_ == 1.0f)
            PlaceSling(sceneObjects_.data[5], SlingTurn(), 713.0f);
        else
            PlaceSling(sceneObjects_.data[5], -SlingTurn(), 850.0f);
    }
    interface_->unknown_89[2] = slideDirection_ > 0.0f ? 1 : 0;

    // The "camera north" button (scene object 11) is lit while its time runs.
    if (unknown_1B0 > 0.0f) unknown_1B0 = RoundToFloat(unknown_1B0 - WindowFrameTimeOriginal(this));
    SetTint(sceneObjects_.data[11], unknown_1B0 > 0.0f ? 1.0f : 0.5f);
}
EI_REPLACE_METHOD(0x00642B40, CHudMap::UpdateVanilla);

// --- The unit panel ------------------------------------------------------------------------------

void CHudUnitPanel::LayoutVanilla() {
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
    // The body parts: a pair of sprites each (scene objects 14 + 2i and 15 + 2i).
    for (int i = 0; i < kParts; ++i) {
        const float y = static_cast<float>(PartY()[i]) + UnitPartDown();
        const float x = static_cast<float>(PartX()[i] + 50) - offset;
        Place(this, 14 + 2 * i, x, y, 4.0f);
        Place(this, 15 + 2 * i, x, y, 5.0f);
    }
    // The effects' pictures (scene objects 26-31), 30 apart downwards.
    for (int i = 0; i < kEffects; ++i) Place(this, 26 + i, UnitEffectX() - offset, static_cast<float>(50 + 30 * i), 5.0f);
    surfaces_.data[0]->MoveTo(20 - offset_, 0);
    MoveRegions(this, -offset_);
}
EI_REPLACE_METHOD(0x00645F80, CHudUnitPanel::LayoutVanilla);

void CHudUnitPanel::UpdateVanilla() {
    if (sliding_) {
        AdvanceSlide(this);
        CallLayout();
        // The sling button (scene object 2).
        if (slideDirection_ == -1.0f)
            PlaceSling(sceneObjects_.data[2], SlingTurn(), -50.0f);
        else
            PlaceSling(sceneObjects_.data[2], -SlingTurn(), 87.0f);
    }
    if (hoverUnit_ && ObjectSlots(hoverUnit_)->IsDead() != 0) hoverUnit_ = nullptr;
    interface_->unknown_89[0] = slideDirection_ > 0.0f ? 1 : 0;
    interface_->unknown_8C = bodyPart_;
}
EI_REPLACE_METHOD(0x00647FB0, CHudUnitPanel::UpdateVanilla);

// --- 006456E0: the unit panel's setup ----------------------------------------------------------------

namespace {
const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
constexpr std::uintptr_t kArrowSprite = std::uintptr_t(0x007945B0);     // "in25arrow"
constexpr std::uintptr_t kInfoTextFormat = std::uintptr_t(0x007945CC);  // "infounit_%d"
constexpr std::uintptr_t kPartLabelKeys[4] = {
    std::uintptr_t(0x00793E38),   // "hl_skull"
    std::uintptr_t(0x00793E24),   // "hl_torso"
    std::uintptr_t(0x007945C4),   // "hl_leg"
    std::uintptr_t(0x007945BC),   // "hl_arm"
};
// The strip's scale: 100 / 0.48157 (the layout's half height at depth 1), both read from the exe.
float StripScale() { return RoundToFloat(GameFloat(0x0073BAB0) / GameFloat(0x007423FC)); }
constexpr int kUnitTravel = 180;
constexpr int kInfoTexts = 18;
constexpr std::uint32_t kPlainColour = 0x100;             // CUiMesh::flags_: drawn in plainColour_
} // namespace

void CHudUnitPanel::SetupVanilla(int bodyPart, std::uint8_t slid) {
    offset_ = slid ? kUnitTravel : 0;
    slide_ = slid ? 1.0f : 0.0f;
    slideDirection_ = slid ? 1.0f : -1.0f;
    rect_ = RECT{0, 0, 185, 225};
    sliding_ = 1;
    travel_ = kUnitTravel;

    // Regions: the sling button, the four body part buttons, the whole panel.
    CUiRegion* sling = AddRegion();
    sling->CallSetRect(0, 0, 20, 30);
    sling->SetTip(0x2774, 1, 0);
    for (int i = 0; i < 4; ++i) {
        CUiRegion* part = AddRegion();
        part->CallSetRect(0, 100 + 30 * i, 20, 130 + 30 * i);
        part->SetTip(0x2775 + i, 1, 0x2E + i);
    }
    AddRegion()->CallSetRect(0, 0, 185, 225);

    // Scene objects 0 and 1: the panel's and its edge's plain backgrounds.
    CUiMesh* back = AddSprite(nullptr);
    back->SetRectangle(60.0f, 100.0f, 20.0f, 0.0f, 180.0f, 220.0f, 0, 0, 256, 256);
    back->texture_ = nullptr;
    back->flags_ |= kPlainColour;
    back->plainColour_ = 0x50000000;
    back->visible_ = 0;
    CUiMesh* edge = AddSprite(nullptr);
    edge->SetRectangle(20.0f, 100.0f, 0.0f, 0.0f, 20.0f, 220.0f, 0, 0, 256, 256);
    edge->texture_ = nullptr;
    edge->flags_ |= kPlainColour;
    edge->plainColour_ = 0xB4000000;
    // 2: the sling button.
    CUiMesh* slingButton = AddSprite(GameText(kArrowSprite));
    slingButton->uOffset_ = 0.22265625f;
    slingButton->vOffset_ = 0.140625f;
    PlaceSling(reinterpret_cast<CEngineObject*>(slingButton), SlingTurn(), -50.0f);
    slingButton->SetScale(0.8f);
    // 3-6: the body part buttons; 7: the frame.
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 0.0f, 100.0f, 20.0f, 130.0f, 0x2F, 0x3E, 0x43, 0x5C);
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 0.0f, 130.0f, 20.0f, 160.0f, 0x45, 0x3E, 0x59, 0x5C);
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 0.0f, 160.0f, 20.0f, 190.0f, 3, 0x5E, 0x17, 0x7C);
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 0.0f, 190.0f, 20.0f, 220.0f, 0xE9, 0x70, 0xFD, 0x8E);
    AddSprite(nullptr)->SetFrame(10.0f, 80.0f, -5.0f, -5.0f, 185.0f, 225.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);
    // 8: a strip of twenty cells down the edge; 9: the "more effects" mark. Both hidden.
    CUiMesh* strip = AddSprite(nullptr);
    UiMeshCellsOriginal(strip, nullptr, 4.0f, StripScale(), 20.0f, 0.0f, 25.0f, 220.0f, 0x55, 2, 0x5A, 0x2A, 1, 20);
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
        const int x = PartX()[i];
        const int y = PartY()[i];
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

    // The text surface.
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 20, 0, 180, 220, 5, 1) : nullptr);

    bodyPart_ = bodyPart;
    for (int i = 0; i < 4; ++i) SetTint(sceneObjects_.data[3 + i], bodyPart_ == i ? 1.0f : 0.5f);
    CallLayout();
    std::memset(unknown_14C, 0, sizeof(unknown_14C));
    hoverUnit_ = nullptr;

    // The panel's texts "infounit_0".."infounit_17" and the four body part labels.
    CTextManager* texts = GameTexts();
    mfc::CString text;
    mfc::CString key;
    auto* infoTexts = reinterpret_cast<mfc::CStringArray*>(&textsVtable_);
    for (int i = 0; i < kInfoTexts; ++i) {
        key.Format(GameText(kInfoTextFormat), i);
        text.Empty();
        texts->GetString(key, &text);
        infoTexts->SetAtGrow(infoTexts->size, text);
    }
    // A missing label keeps the text before it.
    auto* labels = reinterpret_cast<mfc::CStringArray*>(&partLabelsVtable_);
    for (std::uintptr_t labelKey : kPartLabelKeys) {
        texts->GetString(GameText(labelKey), &text);
        labels->SetAtGrow(labels->size, text);
    }
    for (int& icon : effectIcons_) icon = -1;
}
EI_REPLACE_METHOD(0x006456E0, CHudUnitPanel::SetupVanilla);
