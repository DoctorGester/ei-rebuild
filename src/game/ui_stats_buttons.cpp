// The statistics panel's -/+ buttons on the new hero's page and its hero
// arrows. See docs/notes/ui.md, section 5.
#include "ui.h"
#include "ui_camp.h"

#include "unit.h"

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

// An attribute pair (its base and current value, kept equal on this page).
void SetAttribute(float& base, float& current, float value) {
    base = value;
    current = value;
}

} // namespace

bool CHeroStatsPanel::StepPoint(int region) {
    PartyUnitRecord* hero = owner_->HeroRecord();
    UnitStats* stats = &hero->stats;
    int total = static_cast<int>(stats->strengthBase) + static_cast<int>(stats->intelligenceBase) +
                static_cast<int>(stats->dexterityBase);
    bool roomLeft = static_cast<float>(total) < kMaxTotal;
    switch (region - kFirstButton) {
    case 0:
        EI_NAN_AUDIT(stats->strengthBase);
        if (stats->strengthBase <= kMinAttribute) return false;
        SetAttribute(stats->strengthBase, stats->strength, stats->strengthBase - kStep);
        return true;
    case 1:
        EI_NAN_AUDIT(stats->dexterityBase);
        if (stats->dexterityBase <= kMinAttribute) return false;
        SetAttribute(stats->dexterityBase, stats->dexterity, stats->dexterityBase - kStep);
        return true;
    case 2:
        EI_NAN_AUDIT(stats->intelligenceBase);
        if (stats->intelligenceBase <= kMinAttribute) return false;
        SetAttribute(stats->intelligenceBase, stats->intelligence, stats->intelligenceBase - kStep);
        return true;
    case 3:
        if (points_ < 1) return false;
        --points_;
        hero->height = static_cast<float>(points_) * kHeightScale;
        return true;
    case 4:
        if (stats->strengthBase >= kMaxAttribute || !roomLeft) return false;
        SetAttribute(stats->strengthBase, stats->strength, stats->strengthBase + kStep);
        return true;
    case 5:
        if (stats->dexterityBase >= kMaxAttribute || !roomLeft) return false;
        SetAttribute(stats->dexterityBase, stats->dexterity, stats->dexterityBase + kStep);
        return true;
    case 6:
        if (stats->intelligenceBase >= kMaxAttribute || !roomLeft) return false;
        SetAttribute(stats->intelligenceBase, stats->intelligence, stats->intelligenceBase + kStep);
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

// --- Update --------------------------------------------------------------------------------------

void CHeroStatsPanel::Update() {
    if (page_ != kNewHeroPage || heldRegion_ < 0) return;
    double held = CUiContainer::FrameTime() + holdTime_;
    holdTime_ = static_cast<float>(held);
    if (held < kRepeatDelay) return;   // less (a NaN goes on)
    bool stepped = StepPoint(heldRegion_);
    owner_->RefreshStats();
    if (!stepped) {
        interface_->mouseCapture_ = nullptr;
        heldRegion_ = -1;
        holdTime_ = 0.0f;
    }
}

// --- The left button (input click) -----------------------------------------------------------------

void CHeroStatsPanel::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (page_ == kNewHeroPage) {
        heldRegion_ = -1;
        if (StepPoint(region)) heldRegion_ = region;
        if (region >= kFirstButton && region < kButtonEnd) {
            interface_->PlaySound(kPerkSound);
            owner_->RefreshStats();
        }
        if (heldRegion_ > 0) {
            holdTime_ = kFirstHold;
            CaptureMouse();
        }
        return;
    }
    if (region == kPreviousHero) {
        interface_->PlaySound(kHeroSound);
        owner_->PreviousHero();
    } else if (region == kNextHero) {
        interface_->PlaySound(kHeroSound);
        owner_->NextHero();
    }
}
