// Checks for ui_hud_setup.cpp (A-H26): the clock's and the movement panel's setups (00648520,
// 00649190). Both are Rebirth-patched, so they are hooked in the vanilla launch only.
// Both run side by side on the real panel, which is new and empty when the battle screen sets it
// up: the original first, its regions (vertices, bounds, flags, tooltip lines and arguments), its
// sprites (position, texture frame, tint, scale, array sizes) and the panel's fields recorded;
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
};

struct SpriteShot {
    Vector3 position;
    float frame[2];
    float tint[3];
    float scale;
    int sizes[4];
    int visible;
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
                    x.tip == y.tip && x.lines == y.lines && x.tipFlag == y.tipFlag && x.tipExtra == y.tipExtra;
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
                    std::memcmp(x.sizes, y.sizes, sizeof(x.sizes)) == 0 && x.visible == y.visible;
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
