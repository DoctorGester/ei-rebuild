// Checks for the HUD panels' setups (A-H26): the clock's and the movement panel's (00648520,
// 00649190; ui_hud_setup.cpp), the unit panel's and the map's (006456E0, 00641F20;
// ui_hud_slide.cpp). All are Rebirth-patched, so they are hooked in the vanilla launch only.
// Each runs side by side on the real panel, which is new and empty when the battle screen sets it
// up: the original first, its regions (vertices, bounds, flags, offsets, tooltip lines and
// arguments), its sprites (position, orientation, texture frame, tint, scale, flags, plain colour,
// texture, array sizes, a rectangle's corners and texels) and the panel's fields recorded;
// then the regions and sprites dropped (ClearRegions / ClearSceneObjects, as the HUD's rebuilt
// panels do), the fields put back, and ours. A panel that is not empty runs ours alone.
// The regions' vertices are integers truncated from sines and cosines: compared exactly (they
// depend on the FPU's 24-bit precision, see OnArc). The sprites' floats within a few ULPs.
// Reached by every scenario that starts a game (the battle screen's build, 00635250).
#include "verify.h"

#include "ui_panel_shot.h"

#include "../../src/game/ui.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace ui_shot;

void DropContents(CUiWindow* w) {
    w->ClearRegions();
    w->ClearSceneObjects();
}

int& UpdatePeriod() { return *reinterpret_cast<int*>(std::uintptr_t(0x0078FF34)); }

} // namespace

// 00648520: SUB ESP,8; PUSH EBX; PUSH EBP; PUSH ESI (6 bytes).
static void(__fastcall* s_clockSetup)(CHudClock*, void*, int);
static void __fastcall ClockSetupVerify(CHudClock* self, void*, int fast) {
    if (self->regions_.size != 0 || self->sceneObjects_.size != 0) {
        self->CHudClock::SetupVanilla(fast);
        ei::verify::Check("CHudClock::Setup", fast, self->fast_, "ours only: the panel was not empty");
        return;
    }
    const RECT rect = self->rect_;
    const float blink = self->blink_;
    const float dial = self->dial_;
    const int wasFast = self->fast_;
    const bool highlight = self->highlight_;
    const int period = UpdatePeriod();
    const int interfaceFast = self->interface_->fastGame_;

    s_clockSetup(self, nullptr, fast);
    const PanelShot expected = TakePanel(self);
    const float expectedBlink = self->blink_;
    const float expectedDial = self->dial_;
    const int expectedFast = self->fast_;
    const bool expectedHighlight = self->highlight_;
    const int expectedPeriod = UpdatePeriod();
    const int expectedInterfaceFast = self->interface_->fastGame_;

    DropContents(self);
    self->rect_ = rect;
    self->blink_ = blink;
    self->dial_ = dial;
    self->fast_ = wasFast;
    self->highlight_ = highlight;
    UpdatePeriod() = period;
    self->interface_->fastGame_ = interfaceFast;

    self->CHudClock::SetupVanilla(fast);
    const PanelShot actual = TakePanel(self);
    const int region = FirstRegionDifference(expected, actual);
    const int sprite = FirstSpriteDifference(expected, actual);
    const bool fields = std::memcmp(&expected.rect, &actual.rect, sizeof(RECT)) == 0 && self->blink_ == expectedBlink &&
                        self->dial_ == expectedDial && self->fast_ == expectedFast && self->highlight_ == expectedHighlight &&
                        UpdatePeriod() == expectedPeriod && self->interface_->fastGame_ == expectedInterfaceFast;
    ei::verify::Check("CHudClock::Setup", true, region < 0 && sprite < 0 && fields,
                      "fast %d: regions %d / %d (first difference %d), sprites %d / %d (first difference %d), fields %d; "
                      "differs (the original's / ours) in %s; in %s",
                      fast, static_cast<int>(expected.regions.size()), static_cast<int>(actual.regions.size()), region,
                      static_cast<int>(expected.sprites.size()), static_cast<int>(actual.sprites.size()), sprite, fields,
                      DescribeRegionDifference(expected, actual).c_str(), DescribeSpriteDifference(expected, actual).c_str());
}
EI_VERIFY(0x00648520, ClockSetupVerify, 6, s_clockSetup);

// 00649190: SUB ESP,0x1C; PUSH EBX; PUSH EBP (5 bytes). The setup ends in ShowMode (00649910),
// whose own side-by-side wrapper runs inside both runs.
static void(__fastcall* s_movePanelSetup)(CHudMovePanel*);
static void __fastcall MovePanelSetupVerify(CHudMovePanel* self) {
    if (self->regions_.size != 0 || self->sceneObjects_.size != 0) {
        self->CHudMovePanel::SetupVanilla();
        ei::verify::Check("CHudMovePanel::Setup", -1, self->flag_, "ours only: the panel was not empty");
        return;
    }
    const RECT rect = self->rect_;
    const int mode = self->mode_;
    const int flag = self->flag_;

    s_movePanelSetup(self);
    const PanelShot expected = TakePanel(self);
    const int expectedMode = self->mode_;
    const int expectedFlag = self->flag_;

    DropContents(self);
    self->rect_ = rect;
    self->mode_ = mode;
    self->flag_ = flag;

    self->CHudMovePanel::SetupVanilla();
    const PanelShot actual = TakePanel(self);
    const int region = FirstRegionDifference(expected, actual);
    const int sprite = FirstSpriteDifference(expected, actual);
    const bool fields = std::memcmp(&expected.rect, &actual.rect, sizeof(RECT)) == 0 && self->mode_ == expectedMode &&
                        self->flag_ == expectedFlag;
    ei::verify::Check("CHudMovePanel::Setup", true, region < 0 && sprite < 0 && fields,
                      "regions %d / %d (first difference %d), sprites %d / %d (first difference %d), fields %d; differs (the "
                      "original's / ours) in %s; in %s",
                      static_cast<int>(expected.regions.size()), static_cast<int>(actual.regions.size()), region,
                      static_cast<int>(expected.sprites.size()), static_cast<int>(actual.sprites.size()), sprite, fields,
                      DescribeRegionDifference(expected, actual).c_str(), DescribeSpriteDifference(expected, actual).c_str());
}
EI_VERIFY(0x00649190, MovePanelSetupVerify, 5, s_movePanelSetup);

// --- 006456E0: the unit panel's setup (ui_hud_slide.cpp) ------------------------------------------

namespace {

struct UnitPanelFields {
    int sliding;
    int offset;
    int travel;
    float slide;
    float direction;
    int bodyPart;
    std::uint8_t unknown14C[4];
    CUnit* hoverUnit;
    int effectIcons[6];
};

UnitPanelFields TakeFields(const CHudUnitPanel* p) {
    UnitPanelFields f = {};
    f.sliding = p->sliding_;
    f.offset = p->offset_;
    f.travel = p->travel_;
    f.slide = p->slide_;
    f.direction = p->slideDirection_;
    f.bodyPart = p->bodyPart_;
    std::memcpy(f.unknown14C, p->unknown_14C, sizeof(f.unknown14C));
    f.hoverUnit = p->hoverUnit_;
    std::memcpy(f.effectIcons, p->effectIcons_, sizeof(f.effectIcons));
    return f;
}

void PutFields(CHudUnitPanel* p, const UnitPanelFields& f) {
    p->sliding_ = f.sliding;
    p->offset_ = f.offset;
    p->travel_ = f.travel;
    p->slide_ = f.slide;
    p->slideDirection_ = f.direction;
    p->bodyPart_ = f.bodyPart;
    std::memcpy(p->unknown_14C, f.unknown14C, sizeof(f.unknown14C));
    p->hoverUnit_ = f.hoverUnit;
    std::memcpy(p->effectIcons_, f.effectIcons, sizeof(f.effectIcons));
}

std::vector<std::string> StringsOf(const mfc::CStringArray* array) {
    std::vector<std::string> strings;
    for (int i = 0; i < array->size; ++i) strings.emplace_back(static_cast<const char*>((*array)[i]));
    return strings;
}

mfc::CStringArray* InfoTexts(CHudUnitPanel* p) { return reinterpret_cast<mfc::CStringArray*>(&p->textsVtable_); }
mfc::CStringArray* PartLabels(CHudUnitPanel* p) { return reinterpret_cast<mfc::CStringArray*>(&p->partLabelsVtable_); }

} // namespace

// 006456E0: PUSH -1; PUSH 0x72E28B (7 bytes). Side by side as the two setups above; the panel's
// text surface and the strings it appends to its two arrays are dropped as well between the runs.
// The setup ends in Layout (00645F80), whose own side-by-side wrapper runs inside both runs.
static void(__fastcall* s_unitPanelSetup)(CHudUnitPanel*, void*, int, std::uint8_t);
static void __fastcall UnitPanelSetupVerify(CHudUnitPanel* self, void*, int bodyPart, std::uint8_t slid) {
    if (self->regions_.size != 0 || self->sceneObjects_.size != 0 || self->surfaces_.size != 0 ||
        InfoTexts(self)->size != 0 || PartLabels(self)->size != 0) {
        self->CHudUnitPanel::SetupVanilla(bodyPart, slid);
        ei::verify::Check("CHudUnitPanel::Setup", bodyPart, self->bodyPart_, "ours only: the panel was not empty");
        return;
    }
    const RECT rect = self->rect_;
    const UnitPanelFields before = TakeFields(self);

    s_unitPanelSetup(self, nullptr, bodyPart, slid);
    const PanelShot expected = TakePanel(self);
    const UnitPanelFields expectedFields = TakeFields(self);
    const int expectedSurfaces = self->surfaces_.size;
    const std::vector<std::string> expectedTexts = StringsOf(InfoTexts(self));
    const std::vector<std::string> expectedLabels = StringsOf(PartLabels(self));

    DropContents(self);
    self->ClearSurfaces();
    InfoTexts(self)->SetSize(0);
    PartLabels(self)->SetSize(0);
    self->rect_ = rect;
    PutFields(self, before);

    self->CHudUnitPanel::SetupVanilla(bodyPart, slid);
    const PanelShot actual = TakePanel(self);
    const UnitPanelFields actualFields = TakeFields(self);
    const int region = FirstRegionDifference(expected, actual);
    const int sprite = FirstSpriteDifference(expected, actual);
    const bool fields = std::memcmp(&expected.rect, &actual.rect, sizeof(RECT)) == 0 &&
                        expectedFields.sliding == actualFields.sliding && expectedFields.offset == actualFields.offset &&
                        expectedFields.travel == actualFields.travel && expectedFields.slide == actualFields.slide &&
                        expectedFields.direction == actualFields.direction && expectedFields.bodyPart == actualFields.bodyPart &&
                        std::memcmp(expectedFields.unknown14C, actualFields.unknown14C, sizeof(actualFields.unknown14C)) == 0 &&
                        expectedFields.hoverUnit == actualFields.hoverUnit &&
                        std::memcmp(expectedFields.effectIcons, actualFields.effectIcons, sizeof(actualFields.effectIcons)) == 0;
    const bool rest = self->surfaces_.size == expectedSurfaces && StringsOf(InfoTexts(self)) == expectedTexts &&
                      StringsOf(PartLabels(self)) == expectedLabels;
    ei::verify::Check("CHudUnitPanel::Setup", true, region < 0 && sprite < 0 && fields && rest,
                      "part %d slid %d: regions %d / %d (first difference %d), sprites %d / %d (first difference %d), fields %d, "
                      "surfaces and texts %d; differs (the original's / ours) in %s; in %s",
                      bodyPart, slid, static_cast<int>(expected.regions.size()), static_cast<int>(actual.regions.size()), region,
                      static_cast<int>(expected.sprites.size()), static_cast<int>(actual.sprites.size()), sprite, fields, rest,
                      DescribeRegionDifference(expected, actual).c_str(), DescribeSpriteDifference(expected, actual).c_str());
}
EI_VERIFY(0x006456E0, UnitPanelSetupVerify, 7, s_unitPanelSetup);

// --- 00641F20: the map's setup (ui_hud_slide.cpp) -------------------------------------------------

namespace {

struct MapFields {
    float numbers[11];   // centreX_ .. halfSide_
    int sliding;
    int offset;
    int travel;
    float slide;
    float direction;
    int view;
    float northTime;
};
static_assert(offsetof(CHudMap, halfSide_) - offsetof(CHudMap, centreX_) == 10 * sizeof(float));

MapFields TakeFields(const CHudMap* p) {
    MapFields f = {};
    std::memcpy(f.numbers, &p->centreX_, sizeof(f.numbers));
    f.sliding = p->sliding_;
    f.offset = p->offset_;
    f.travel = p->travel_;
    f.slide = p->slide_;
    f.direction = p->slideDirection_;
    f.view = p->view_;
    f.northTime = p->unknown_1B0;
    return f;
}

void PutFields(CHudMap* p, const MapFields& f) {
    std::memcpy(&p->centreX_, f.numbers, sizeof(f.numbers));
    p->sliding_ = f.sliding;
    p->offset_ = f.offset;
    p->travel_ = f.travel;
    p->slide_ = f.slide;
    p->slideDirection_ = f.direction;
    p->view_ = f.view;
    p->unknown_1B0 = f.northTime;
}

bool SameFields(const MapFields& a, const MapFields& b) {
    for (int i = 0; i < 11; ++i)
        if (!Near(a.numbers[i], b.numbers[i])) return false;
    return a.sliding == b.sliding && a.offset == b.offset && a.travel == b.travel && a.slide == b.slide &&
           a.direction == b.direction && a.view == b.view && a.northTime == b.northTime;
}

std::vector<UiPointF> PointsOf(const UiPointArray& points) {
    return std::vector<UiPointF>(points.data, points.data + points.size);
}

bool SamePoints(const std::vector<UiPointF>& a, const std::vector<UiPointF>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!Near(a[i].x, b[i].x) || !Near(a[i].y, b[i].y)) return false;
    return true;
}

} // namespace

// 00641F20: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes). Side by side as the setups above. Between the
// runs the panel's surface and its two point arrays are dropped as well, and the map's picture the
// first run loaded is taken out of the referenced objects with the reference LoadTexture added
// (not released: the texture manager keeps the object, so the second run finds the same one and
// its load is the manager's "already loaded" case). The setup ends in UpdateView, BuildMarkers and
// Layout (00642E80, 00643DC0, 00642930), whose own side-by-side wrappers run inside both runs.
static void(__fastcall* s_mapSetup)(CHudMap*, void*, float, std::uint8_t);
static void __fastcall MapSetupVerify(CHudMap* self, void*, float zoom, std::uint8_t slid) {
    if (self->regions_.size != 0 || self->sceneObjects_.size != 0 || self->surfaces_.size != 0 ||
        self->viewPolygon_.size != 0 || self->visibleArea_.size != 0) {
        self->CHudMap::SetupVanilla(zoom, slid);
        ei::verify::Check("CHudMap::Setup", 0, self->view_, "ours only: the panel was not empty");
        return;
    }
    const RECT rect = self->rect_;
    const MapFields before = TakeFields(self);
    const int objects = self->objects_.size;

    s_mapSetup(self, nullptr, zoom, slid);
    const PanelShot expected = TakePanel(self);
    const MapFields expectedFields = TakeFields(self);
    const int expectedSurfaces = self->surfaces_.size;
    const int expectedObjects = self->objects_.size;
    const CEngineObject* expectedPicture = expectedObjects > objects ? self->objects_.data[objects] : nullptr;
    const std::vector<UiPointF> expectedPolygon = PointsOf(self->viewPolygon_);
    const std::vector<UiPointF> expectedVisible = PointsOf(self->visibleArea_);

    DropContents(self);
    self->ClearSurfaces();
    self->viewPolygon_.RemoveAll();
    self->visibleArea_.RemoveAll();
    while (self->objects_.size > objects) {
        --self->objects_.data[self->objects_.size - 1]->refCount_;
        --self->objects_.size;
    }
    self->rect_ = rect;
    PutFields(self, before);

    self->CHudMap::SetupVanilla(zoom, slid);
    const PanelShot actual = TakePanel(self);
    const int region = FirstRegionDifference(expected, actual);
    const int sprite = FirstSpriteDifference(expected, actual);
    const bool fields = std::memcmp(&expected.rect, &actual.rect, sizeof(RECT)) == 0 && SameFields(expectedFields, TakeFields(self));
    const CEngineObject* picture = self->objects_.size > objects ? self->objects_.data[objects] : nullptr;
    const bool rest = self->surfaces_.size == expectedSurfaces && self->objects_.size == expectedObjects &&
                      picture == expectedPicture && SamePoints(expectedPolygon, PointsOf(self->viewPolygon_)) &&
                      SamePoints(expectedVisible, PointsOf(self->visibleArea_));
    ei::verify::Check("CHudMap::Setup", true, region < 0 && sprite < 0 && fields && rest,
                      "zoom %g slid %d: regions %d / %d (first difference %d), sprites %d / %d (first difference %d), fields %d, "
                      "surface, picture and polygons %d; differs (the original's / ours) in %s; in %s",
                      zoom, slid, static_cast<int>(expected.regions.size()), static_cast<int>(actual.regions.size()), region,
                      static_cast<int>(expected.sprites.size()), static_cast<int>(actual.sprites.size()), sprite, fields, rest,
                      DescribeRegionDifference(expected, actual).c_str(), DescribeSpriteDifference(expected, actual).c_str());
}
EI_VERIFY(0x00641F20, MapSetupVerify, 5, s_mapSetup);
