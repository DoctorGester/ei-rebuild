// The statistics panel's -/+ buttons on the new hero's page (00624790, 00624A60) and its hero
// arrows. See docs/notes/ui.md, section 5.
#include "ui_camp.h"

#include "unit.h"

#include "ei/hook.h"
#include "originals_ui.h"
#include "lib/nan_audit.h"

#include <cstdint>

namespace {

const char* const kPerkSound = "buttons\\camp\\perk.wav";
const char* const kHeroSound = "buttons\\camp\\hero.wav";

constexpr int kNewHeroPage = 10;
constexpr int kFirstButton = 6;              // regions 6-13
constexpr int kButtonEnd = 14;
constexpr int kPreviousHero = 0;
constexpr int kNextHero = 1;
constexpr float kMinAttribute = 15.0f;
constexpr float kMaxAttribute = 35.0f;
constexpr float kMaxTotal = 75.0f;
constexpr float kStep = 1.0f;
constexpr int kMaxHeight = 99;
constexpr float kHeightScale = 0.01f;
constexpr double kRepeatDelay = 0.25;
constexpr float kFirstHold = -0.5f;

// Owner slots.
constexpr int kOwnerHero = 51;
constexpr int kOwnerRefresh = 47;
constexpr int kOwnerNextHero = 48;
constexpr int kOwnerPreviousHero = 49;

PartyUnitRecord* OwnerHero(CUiWindow* owner) {
    using Fn = PartyUnitRecord*(__thiscall*)(CUiWindow*);
    return ei::VirtualSlot<Fn>(owner->vtable_, kOwnerHero)(owner);
}
void OwnerCall(CUiWindow* owner, int slot) {
    using Fn = void(__thiscall*)(CUiWindow*);
    ei::VirtualSlot<Fn>(owner->vtable_, slot)(owner);
}
UnitStats* StatsOf(PartyUnitRecord* hero) { return reinterpret_cast<UnitStats*>(hero->stats); }

// An attribute pair (its base and current value, kept equal on this page).
void SetAttribute(float& base, float& current, float value) {
    base = value;
    current = value;
}

} // namespace

bool CHeroStatsPanel::StepPoint(int region) {
    PartyUnitRecord* hero = OwnerHero(owner_);
    UnitStats* stats = StatsOf(hero);
    int total = static_cast<int>(stats->stat10Base) + static_cast<int>(stats->stat20Base) +
                static_cast<int>(stats->stat18Base);
    bool roomLeft = static_cast<float>(total) < kMaxTotal;
    switch (region - kFirstButton) {
    case 0:
        EI_NAN_AUDIT(stats->stat10Base);
        if (stats->stat10Base <= kMinAttribute) return false;
        SetAttribute(stats->stat10Base, stats->stat10, stats->stat10Base - kStep);
        return true;
    case 1:
        EI_NAN_AUDIT(stats->stat18Base);
        if (stats->stat18Base <= kMinAttribute) return false;
        SetAttribute(stats->stat18Base, stats->stat18, stats->stat18Base - kStep);
        return true;
    case 2:
        EI_NAN_AUDIT(stats->stat20Base);
        if (stats->stat20Base <= kMinAttribute) return false;
        SetAttribute(stats->stat20Base, stats->stat20, stats->stat20Base - kStep);
        return true;
    case 3:
        if (points_ < 1) return false;
        --points_;
        hero->height = static_cast<float>(points_) * kHeightScale;
        return true;
    case 4:
        if (stats->stat10Base >= kMaxAttribute || !roomLeft) return false;
        SetAttribute(stats->stat10Base, stats->stat10, stats->stat10Base + kStep);
        return true;
    case 5:
        if (stats->stat18Base >= kMaxAttribute || !roomLeft) return false;
        SetAttribute(stats->stat18Base, stats->stat18, stats->stat18Base + kStep);
        return true;
    case 6:
        if (stats->stat20Base >= kMaxAttribute || !roomLeft) return false;
        SetAttribute(stats->stat20Base, stats->stat20, stats->stat20Base + kStep);
        return true;
    case 7:
        if (points_ > kMaxHeight) return false;
        ++points_;
        hero->height = static_cast<float>(points_) * kHeightScale;
        return true;
    default:
        return false;
    }
}

// --- 00624A60: Update (slot 24) ----------------------------------------------------------------

void CHeroStatsPanel::Update() {
    if (page_ != kNewHeroPage || heldRegion_ < 0) return;
    double held = WindowFrameTimeOriginal(this) + holdTime_;
    holdTime_ = static_cast<float>(held);
    if (held < kRepeatDelay) return;   // TEST AH,1: less (a NaN goes on)
    bool stepped = StepPoint(heldRegion_);
    OwnerCall(owner_, kOwnerRefresh);
    if (!stepped) {
        UiCapture(interface_) = nullptr;
        heldRegion_ = -1;
        holdTime_ = 0.0f;
    }
}
EI_REPLACE_METHOD(0x00624A60, CHeroStatsPanel::Update);

// --- 00624790: the left button (input slot 2) --------------------------------------------------------

void CHeroStatsPanel::OnLButtonDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (page_ == kNewHeroPage) {
        heldRegion_ = -1;
        if (StepPoint(region)) heldRegion_ = region;
        if (region >= kFirstButton && region < kButtonEnd) {
            interface_->PlaySound(kPerkSound, 0);
            OwnerCall(owner_, kOwnerRefresh);
        }
        if (heldRegion_ > 0) {
            holdTime_ = kFirstHold;
            UiCapture(interface_) = CaptureValue(InputOf(this));
        }
        return;
    }
    if (region == kPreviousHero) {
        interface_->PlaySound(kHeroSound, 0);
        OwnerCall(owner_, kOwnerPreviousHero);
    } else if (region == kNextHero) {
        interface_->PlaySound(kHeroSound, 0);
        OwnerCall(owner_, kOwnerNextHero);
    }
}
namespace {
} // namespace
EI_REPLACE_METHOD(0x00624790, InputForward<&CHeroStatsPanel::OnLButtonDown>::Call);
