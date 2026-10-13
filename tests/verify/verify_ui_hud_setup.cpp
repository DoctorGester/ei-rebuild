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

#include "../../src/game/ui.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct RegionShot {
    std::vector<UiPoint> points;
    RECT bounds;
    bool rectOnly;
    bool enabled;
    bool tip;
    std::vector<std::string> lines;
    int tipFlag;
    int tipExtra;
    int offset[2];
};

struct SpriteShot {
    Vector3 position;
    Quaternion orientation;
    float frame[2];
    float tint[3];
    float scale;
    int sizes[4];
    int visible;
    std::uint32_t flags;
    std::uint32_t plainColour;
    const void* texture;
    // A rectangle's four corners and texels (every element of both is written); empty for the
    // other shapes, whose arrays may keep unwritten elements.
    std::vector<float> shape;
};

struct PanelShot {
    RECT rect;
    std::vector<RegionShot> regions;
    std::vector<SpriteShot> sprites;
};

PanelShot TakePanel(CUiWindow* w) {
    PanelShot shot;
    shot.rect = w->rect_;
    for (int i = 0; i < w->regions_.size; ++i) {
        const CUiRegion* region = w->regions_.data[i];
        RegionShot r = {};
        r.points.assign(region->points_.data, region->points_.data + region->points_.size);
        r.bounds = region->bounds_;
        r.rectOnly = region->rectOnly_;
        r.enabled = region->enabled_;
        r.tip = region->tip_ != nullptr;
        if (const CUiTooltip* tip = region->tip_) {
            for (int line = 0; line < tip->lines_.size; ++line) r.lines.emplace_back(static_cast<const char*>(tip->lines_[line]));
            // An empty tooltip leaves its two arguments unwritten.
            if (tip->lines_.size > 0) {
                r.tipFlag = tip->flag_;
                r.tipExtra = tip->extra_;
            }
        }
        r.offset[0] = region->offsetX_;
        r.offset[1] = region->offsetY_;
        shot.regions.push_back(r);
    }
    for (int i = 0; i < w->sceneObjects_.size; ++i) {
        CUiMesh* sprite = reinterpret_cast<CUiMesh*>(w->sceneObjects_.data[i]);
        SpriteShot s = {};
        s.position = sprite->position_;
        s.frame[0] = sprite->uOffset_;
        s.frame[1] = sprite->vOffset_;
        std::memcpy(s.tint, sprite->tint_, sizeof(s.tint));
        s.scale = sprite->ScaleX();
        s.sizes[0] = sprite->quadCount_;
        s.sizes[1] = sprite->vertexCount_;
        s.sizes[2] = sprite->indexCount_;
        s.sizes[3] = sprite->uvCount_;
        s.visible = sprite->visible_;
        s.orientation = sprite->orientation_;
        s.flags = sprite->flags_;
        s.plainColour = sprite->plainColour_;
        s.texture = sprite->texture_;
        if (sprite->quadCount_ == 1 && sprite->vertexCount_ == 4 && sprite->indexCount_ == 6 && sprite->uvCount_ == 4) {
            const float* corners = reinterpret_cast<const float*>(sprite->quads_);
            s.shape.assign(corners, corners + sizeof(UiQuadBlock) / sizeof(float));
            s.shape.insert(s.shape.end(), sprite->uvs_, sprite->uvs_ + 8);
        }
        shot.sprites.push_back(s);
    }
    return shot;
}

bool Near(float a, float b) { return ei::verify::NearlyEqual<float>(a, b, 4, 1e-5f); }

// The index of the first region that differs (-1: none), and the same for the sprites.
int FirstRegionDifference(const PanelShot& a, const PanelShot& b) {
    if (a.regions.size() != b.regions.size()) return static_cast<int>(a.regions.size() < b.regions.size() ? a.regions.size() : b.regions.size());
    for (std::size_t i = 0; i < a.regions.size(); ++i) {
        const RegionShot& x = a.regions[i];
        const RegionShot& y = b.regions[i];
        bool same = x.points.size() == y.points.size() &&
                    (x.points.empty() || std::memcmp(x.points.data(), y.points.data(), x.points.size() * sizeof(UiPoint)) == 0) &&
                    std::memcmp(&x.bounds, &y.bounds, sizeof(RECT)) == 0 && x.rectOnly == y.rectOnly && x.enabled == y.enabled &&
                    x.tip == y.tip && x.lines == y.lines && x.tipFlag == y.tipFlag && x.tipExtra == y.tipExtra &&
                    x.offset[0] == y.offset[0] && x.offset[1] == y.offset[1];
        if (!same) return static_cast<int>(i);
    }
    return -1;
}

int FirstSpriteDifference(const PanelShot& a, const PanelShot& b) {
    if (a.sprites.size() != b.sprites.size()) return static_cast<int>(a.sprites.size() < b.sprites.size() ? a.sprites.size() : b.sprites.size());
    for (std::size_t i = 0; i < a.sprites.size(); ++i) {
        const SpriteShot& x = a.sprites[i];
        const SpriteShot& y = b.sprites[i];
        bool same = Near(x.position.x, y.position.x) && Near(x.position.y, y.position.y) && Near(x.position.z, y.position.z) &&
                    Near(x.frame[0], y.frame[0]) && Near(x.frame[1], y.frame[1]) && Near(x.tint[0], y.tint[0]) &&
                    Near(x.tint[1], y.tint[1]) && Near(x.tint[2], y.tint[2]) && Near(x.scale, y.scale) &&
                    std::memcmp(x.sizes, y.sizes, sizeof(x.sizes)) == 0 && x.visible == y.visible &&
                    Near(x.orientation.w, y.orientation.w) && Near(x.orientation.x, y.orientation.x) &&
                    Near(x.orientation.y, y.orientation.y) && Near(x.orientation.z, y.orientation.z) && x.flags == y.flags &&
                    x.plainColour == y.plainColour && x.texture == y.texture && x.shape.size() == y.shape.size();
        for (std::size_t k = 0; same && k < x.shape.size(); ++k) same = Near(x.shape[k], y.shape[k]);
        if (!same) return static_cast<int>(i);
    }
    return -1;
}

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
                      "fast %d: regions %d / %d (first difference %d), sprites %d / %d (first difference %d), fields %d", fast,
                      static_cast<int>(expected.regions.size()), static_cast<int>(actual.regions.size()), region,
                      static_cast<int>(expected.sprites.size()), static_cast<int>(actual.sprites.size()), sprite, fields);
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
                      "regions %d / %d (first difference %d), sprites %d / %d (first difference %d), fields %d",
                      static_cast<int>(expected.regions.size()), static_cast<int>(actual.regions.size()), region,
                      static_cast<int>(expected.sprites.size()), static_cast<int>(actual.sprites.size()), sprite, fields);
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
                      "surfaces and texts %d",
                      bodyPart, slid, static_cast<int>(expected.regions.size()), static_cast<int>(actual.regions.size()), region,
                      static_cast<int>(expected.sprites.size()), static_cast<int>(actual.sprites.size()), sprite, fields, rest);
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
                      "surface, picture and polygons %d",
                      zoom, slid, static_cast<int>(expected.regions.size()), static_cast<int>(actual.regions.size()), region,
                      static_cast<int>(expected.sprites.size()), static_cast<int>(actual.sprites.size()), sprite, fields, rest);
}
EI_VERIFY(0x00641F20, MapSetupVerify, 5, s_mapSetup);
