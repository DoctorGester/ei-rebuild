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
constexpr int kRepeatTicks = 250;            // a step every frame from 250 ms
constexpr int kFirstHoldTicks = -500;        // the first step waits 750 ms

// An attribute pair (its base and current value, kept equal on this page).
void SetAttribute(float& base, float& current, float value) {
    base = value;
    current = value;
}

} // namespace

bool HeroStatsPanel::StepPoint(int region) {
    PartyUnitRecord* hero = owner->HeroRecord();
    UnitStats* stats = &hero->stats;
    int total = static_cast<int>(stats->strengthBase) + static_cast<int>(stats->intelligenceBase) +
                static_cast<int>(stats->dexterityBase);
    bool roomLeft = total < kMaxTotal;
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
        if (points < 1) return false;
        --points;
        hero->height = points * kHeightScale;
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
        if (points > kMaxHeight) return false;
        ++points;
        hero->height = points * kHeightScale;
        return true;
    default:
        return false;
    }
}

// --- Update --------------------------------------------------------------------------------------

void HeroStatsPanel::Update() {
    if (page != kNewHeroPage || heldRegion < 0) return;
    holdTicks += UiContainer::FrameTicks();
    if (holdTicks < kRepeatTicks) return;
    bool stepped = StepPoint(heldRegion);
    owner->RefreshStats();
    if (!stepped) {
        ui->mouseCapture = nullptr;
        heldRegion = -1;
        holdTicks = 0;
    }
}

// --- The left button (input click) -----------------------------------------------------------------

void HeroStatsPanel::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (page == kNewHeroPage) {
        heldRegion = -1;
        if (StepPoint(region)) heldRegion = region;
        if (region >= kFirstButton && region < kButtonEnd) {
            ui->PlayInterfaceSound(kPerkSound);
            owner->RefreshStats();
        }
        if (heldRegion > 0) {
            holdTicks = kFirstHoldTicks;
            CaptureMouse();
        }
        return;
    }
    if (region == kPreviousHero) {
        ui->PlayInterfaceSound(kHeroSound);
        owner->PreviousHero();
    } else if (region == kNextHero) {
        ui->PlayInterfaceSound(kHeroSound);
        owner->NextHero();
    }
}

void HeroStatsPanel::LeftUp(int, int, int) {
    ui->mouseCapture = nullptr;
    heldRegion = -1;
    holdTicks = 0;
}
