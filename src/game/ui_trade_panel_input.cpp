// The trade panel's scroll clamp, its left button and its update:
// scrolling the perk lists and buying school levels, science and perks with experience, a school
// or science level again every quarter second while the button stays down. See docs/notes/ui.md,
// section 5.
#include "ui.h"
#include "ui_camp.h"
#include "ui_screens.h"

#include "unit.h"

#include "ei/hook.h"
#include "unit_db.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <cstdint>

namespace {

const char* const kPerkSound = "buttons\\camp\\perk.wav";
// The skill database (levels by record name).
SkillDatabase* Skills() { return SkillDb(); }

constexpr int kHeldRows = 10;
constexpr int kLearnableRows = 23;
constexpr int kHeldUp = 0, kHeldDown = 1, kLearnableUp = 2, kLearnableDown = 3;
constexpr int kFirstSchool = 4;
constexpr int kScience = 10;
constexpr int kFirstLearnable = 11;
constexpr int kFirstHeld = 34;
constexpr int kMaxLevel = 100;
constexpr float kFirstHold = -0.5f;
constexpr float kHoldRepeat = 0.25f;

CUiMesh* Arrow(CUiWindow* window, int index) { return window->SceneMesh(index); }

// An MFC CString's length (its header before the characters).
int TextLength(const char* text) { return reinterpret_cast<const int*>(text)[-2]; }

} // namespace

// --- 00626D70: the scroll positions clamped ----------------------------------------------------------

void CTradePanel::ClampScroll() {
    if (perkTop_ > static_cast<int>(perks_.size()) - kHeldRows) perkTop_ = static_cast<int>(perks_.size()) - kHeldRows;
    if (perkTop_ < 0) perkTop_ = 0;
    if (learnableTop_ > static_cast<int>(learnable_.size()) - kLearnableRows) learnableTop_ = static_cast<int>(learnable_.size()) - kLearnableRows;
    if (learnableTop_ < 0) learnableTop_ = 0;
    Arrow(this, 0)->visible_ = perkTop_ > 0 ? 1 : 0;
    Arrow(this, 1)->visible_ = perkTop_ < static_cast<int>(perks_.size()) - kHeldRows ? 1 : 0;
    Arrow(this, 2)->visible_ = learnableTop_ > 0 ? 1 : 0;
    Arrow(this, 3)->visible_ = learnableTop_ < static_cast<int>(learnable_.size()) - kLearnableRows ? 1 : 0;
    DrawLists();
}
namespace {
void __fastcall TradeClampScroll(CTradePanel* self) { self->ClampScroll(); }
} // namespace
EI_REWRITTEN(0x00626D70, TradeClampScroll);

// --- 00626920: the left button (input slot 2) --------------------------------------------------------

void CTradePanel::OnLButtonDown(int, int x, int y) {
    int region = RegionAt(x, y);
    switch (region) {
    case kHeldUp: --perkTop_; ClampScroll(); return;
    case kHeldDown: ++perkTop_; ClampScroll(); return;
    case kLearnableUp: --learnableTop_; ClampScroll(); return;
    case kLearnableDown: ++learnableTop_; ClampScroll(); return;
    default: break;
    }
    if (region >= kFirstSchool) interface_->PlaySound(kPerkSound, 0);
    PartyUnitRecord* hero = owner_->HeroRecord();
    auto* stats = &hero->stats;
    TradeRecord& record = records_[recordIndex_];

    const TradeRow* row = nullptr;
    bool hold = false;
    if (region >= kFirstSchool && region < kScience) {
        row = &schools_[region - kFirstSchool];
        if (row->level >= kMaxLevel || row->price > experience_) return;
        ++stats->skills[region - kFirstSchool];
        hold = true;
    } else if (region == kScience) {
        row = &science_[0];
        if (row->level >= kMaxLevel || row->price > experience_) return;
        ++stats->attributes[0];
        hold = true;
    } else if (region >= kFirstLearnable && region < kFirstHeld) {
        std::uint32_t index = static_cast<std::uint32_t>(learnableTop_ + region - kFirstLearnable);
        if (index >= static_cast<std::uint32_t>(static_cast<int>(learnable_.size()))) return;
        row = &learnable_[index];
        if (row->price > experience_ || !row->learnable) return;
        stats->LearnSkill(Skills(), Skills()->FindLevel(row->key));
    } else {
        // 34 and up, and (as unsigned) anything below 0: the next level of a held perk.
        std::uint32_t index = static_cast<std::uint32_t>(perkTop_ + region - kFirstHeld);
        if (index >= static_cast<std::uint32_t>(static_cast<int>(perks_.size()))) return;
        row = &perks_[index];
        if (row->price > experience_ || TextLength(row->nextKey) == 0) return;
        stats->LearnSkill(Skills(), Skills()->FindLevel(row->nextKey));
    }
    stats->spentExperience = static_cast<float>(static_cast<double>(row->price) + stats->spentExperience);
    record.bought = 1;
    BuildLists();
    if (hold) {
        heldRegion_ = region;
        holdTime_ = kFirstHold;
        UiCapture(interface_) = Input();
    }
}
EI_REWRITTEN(0x00626920, CTradePanel::OnLButtonDown);

// --- 00626C30: the left button released (input slot 3) ---------------------------------------------

void CTradePanel::OnLButtonUp(int, int, int) {
    interface_->mouseCapture_ = nullptr;
    heldRegion_ = -1;
    holdTime_ = 0.0f;
}
EI_REWRITTEN(0x00626C30, CTradePanel::OnLButtonUp);   // through LeftUp (input slot 3, RET 0xC; 0 in EAX)

// --- 00626C50: the held school / science button (slot 24) -------------------------------------------

void CTradePanel::UpdateHeld() {
    if (heldRegion_ < 0) return;
    // The comparison uses the unrounded sum; the field keeps it rounded.
    double held = CUiContainer::FrameTime() + holdTime_;
    holdTime_ = static_cast<float>(held);
    EI_NAN_AUDIT(held);
    if (held < kHoldRepeat) return;
    int region = heldRegion_;
    PartyUnitRecord* hero = owner_->HeroRecord();
    auto* stats = &hero->stats;
    const TradeRow* row;
    std::uint8_t* level;
    if (region >= kFirstSchool && region < kScience) {
        row = &schools_[region - kFirstSchool];
        level = &stats->skills[region - kFirstSchool];
    } else if (region == kScience) {
        row = &science_[0];
        level = &stats->attributes[0];
    } else {
        return;
    }
    if (row->level < kMaxLevel && row->price <= experience_) {
        ++*level;
        stats->spentExperience = static_cast<float>(static_cast<double>(row->price) + stats->spentExperience);
        BuildLists();
        return;
    }
    interface_->mouseCapture_ = nullptr;
    heldRegion_ = -1;
}
EI_REWRITTEN(0x00626C50, CTradePanel::UpdateHeld);
