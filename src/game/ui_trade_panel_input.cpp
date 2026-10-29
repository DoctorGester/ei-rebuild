// The trade panel's scroll clamp, its left button and its update:
// scrolling the perk lists and buying school levels, science and perks with experience, a school
// or science level again every frame from 750 ms of holding on while the button stays down (whole
// milliseconds). See docs/notes/ui.md, section 5.
#include "ui.h"
#include "ui_camp.h"
#include "ui_screens.h"

#include "unit.h"

#include "unit_db.h"

#include <cstdint>

namespace {

const char* const kPerkSound = "buttons\\camp\\perk.wav";

constexpr int kHeldRows = 10;
constexpr int kLearnableRows = 23;
constexpr int kHeldUp = 0, kHeldDown = 1, kLearnableUp = 2, kLearnableDown = 3;
constexpr int kFirstSchool = 4;
constexpr int kScience = 10;
constexpr int kFirstLearnable = 11;
constexpr int kFirstHeld = 34;
constexpr int kMaxLevel = 100;
constexpr int kFirstHoldTicks = -500;    // milliseconds: the first repeat waits 750 ms
constexpr int kRepeatTicks = 250;        // then a level every frame

} // namespace

// --- The scroll positions clamped ----------------------------------------------------------------------

void TradePanel::ClampScroll() {
    if (perkTop > static_cast<int>(perks.size()) - kHeldRows) perkTop = static_cast<int>(perks.size()) - kHeldRows;
    if (perkTop < 0) perkTop = 0;
    if (learnableTop > static_cast<int>(learnable.size()) - kLearnableRows) learnableTop = static_cast<int>(learnable.size()) - kLearnableRows;
    if (learnableTop < 0) learnableTop = 0;
    SceneMesh(0)->visible = perkTop > 0;
    SceneMesh(1)->visible = perkTop < static_cast<int>(perks.size()) - kHeldRows;
    SceneMesh(2)->visible = learnableTop > 0;
    SceneMesh(3)->visible = learnableTop < static_cast<int>(learnable.size()) - kLearnableRows;
    DrawLists();
}

// --- The left button (input click) ---------------------------------------------------------------------

void TradePanel::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    switch (region) {
    case kHeldUp: --perkTop; ClampScroll(); return;
    case kHeldDown: ++perkTop; ClampScroll(); return;
    case kLearnableUp: --learnableTop; ClampScroll(); return;
    case kLearnableDown: ++learnableTop; ClampScroll(); return;
    default: break;
    }
    if (region >= kFirstSchool) ui->PlaySound(kPerkSound);
    UnitStats* stats = &owner->HeroRecord()->stats;
    TradeStatsRecord& record = records[recordIndex];

    const TradeRow* row = nullptr;
    bool hold = false;
    if (region >= kFirstSchool && region < kScience) {
        row = &schools[region - kFirstSchool];
        if (row->level >= kMaxLevel || row->price > experience) return;
        ++stats->skills[region - kFirstSchool];
        hold = true;
    } else if (region == kScience) {
        row = &science[0];
        if (row->level >= kMaxLevel || row->price > experience) return;
        ++stats->attributes[0];
        hold = true;
    } else if (region >= kFirstLearnable && region < kFirstHeld) {
        std::uint32_t index = static_cast<std::uint32_t>(learnableTop + region - kFirstLearnable);
        if (index >= learnable.size()) return;
        row = &learnable[index];
        if (row->price > experience || !row->learnable) return;
        stats->LearnSkill(&g_skillDatabase, g_skillDatabase.FindLevel(row->key));
    } else {
        // 34 and up, and (as unsigned) anything below 0: the next level of a held perk.
        std::uint32_t index = static_cast<std::uint32_t>(perkTop + region - kFirstHeld);
        if (index >= perks.size()) return;
        row = &perks[index];
        if (row->price > experience || row->nextKey.empty()) return;
        stats->LearnSkill(&g_skillDatabase, g_skillDatabase.FindLevel(row->nextKey));
    }
    stats->spentExperience = row->price + stats->spentExperience;
    record.bought = true;
    BuildLists();
    if (hold) {
        heldRegion = region;
        holdTicks = kFirstHoldTicks;
        CaptureMouse();
    }
}

// --- The left button released (input release) -----------------------------------------------------------

void TradePanel::LeftUp(int, int, int) {
    ui->mouseCapture = nullptr;
    heldRegion = -1;
    holdTicks = 0;
}

// --- The held school / science button (update) ---------------------------------------------------------

void TradePanel::Update() {
    if (heldRegion < 0) return;
    holdTicks += UiContainer::FrameTicks();
    if (holdTicks < kRepeatTicks) return;
    int region = heldRegion;
    UnitStats* stats = &owner->HeroRecord()->stats;
    const TradeRow* row;
    std::uint8_t* level;
    if (region >= kFirstSchool && region < kScience) {
        row = &schools[region - kFirstSchool];
        level = &stats->skills[region - kFirstSchool];
    } else if (region == kScience) {
        row = &science[0];
        level = &stats->attributes[0];
    } else {
        return;
    }
    if (row->level < kMaxLevel && row->price <= experience) {
        ++*level;
        stats->spentExperience = row->price + stats->spentExperience;
        BuildLists();
        return;
    }
    ui->mouseCapture = nullptr;
    heldRegion = -1;
}
