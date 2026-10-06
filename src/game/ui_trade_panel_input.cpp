// The trade panel's scroll clamp (00626D70) and its left button (00626920): scrolling the perk
// lists and buying school levels, science and perks with experience. See docs/notes/ui.md,
// section 5.
#include "ui_camp.h"

#include "unit.h"

#include "ei/hook.h"
#include "originals_ui.h"
#include "originals_units.h"

#include <cstdint>

namespace {

const char* const kPerkSound = reinterpret_cast<const char*>(0x00793DF0);   // "buttons\camp\perk.wav"
// The skill database (levels by record name).
void* SkillDatabase() { return reinterpret_cast<void*>(0x007AF268); }

constexpr int kHeldRows = 10;
constexpr int kLearnableRows = 23;
constexpr int kHeldUp = 0, kHeldDown = 1, kLearnableUp = 2, kLearnableDown = 3;
constexpr int kFirstSchool = 4;
constexpr int kScience = 10;
constexpr int kFirstLearnable = 11;
constexpr int kFirstHeld = 34;
constexpr int kMaxLevel = 100;
constexpr float kFirstHold = -0.5f;

CUiMesh* Arrow(CUiWindow* window, int index) { return reinterpret_cast<CUiMesh*>(window->sceneObjects_.data_[index]); }

// An MFC CString's length (its header before the characters).
int TextLength(const char* text) { return reinterpret_cast<const int*>(text)[-2]; }

} // namespace

// --- 00626D70: the scroll positions clamped ----------------------------------------------------------

void CTradePanel::ClampScroll() {
    if (perkTop_ > perks_.Count() - kHeldRows) perkTop_ = perks_.Count() - kHeldRows;
    if (perkTop_ < 0) perkTop_ = 0;
    if (learnableTop_ > learnable_.Count() - kLearnableRows) learnableTop_ = learnable_.Count() - kLearnableRows;
    if (learnableTop_ < 0) learnableTop_ = 0;
    Arrow(this, 0)->visible_ = perkTop_ > 0 ? 1 : 0;
    Arrow(this, 1)->visible_ = perkTop_ < perks_.Count() - kHeldRows ? 1 : 0;
    Arrow(this, 2)->visible_ = learnableTop_ > 0 ? 1 : 0;
    Arrow(this, 3)->visible_ = learnableTop_ < learnable_.Count() - kLearnableRows ? 1 : 0;
    TradePanelDrawListsOriginal(this);
}
namespace {
void __fastcall TradeClampScroll(CTradePanel* self) { self->ClampScroll(); }
} // namespace
EI_REPLACE(0x00626D70, TradeClampScroll);

// --- 00626920: the left button (input slot 2) --------------------------------------------------------

void CTradePanel::OnLButtonDown(int, int x, int y) {
    int region = RegionAt(x, y);
    switch (region) {
    case kHeldUp: --perkTop_; TradePanelClampScrollOriginal(this); return;
    case kHeldDown: ++perkTop_; TradePanelClampScrollOriginal(this); return;
    case kLearnableUp: --learnableTop_; TradePanelClampScrollOriginal(this); return;
    case kLearnableDown: ++learnableTop_; TradePanelClampScrollOriginal(this); return;
    default: break;
    }
    if (region >= kFirstSchool) interface_->PlaySound(kPerkSound, 0);
    using HeroFn = PartyUnitRecord*(__thiscall*)(CUiWindow*);
    PartyUnitRecord* hero = reinterpret_cast<HeroFn>(owner_->vtable_[51])(owner_);
    auto* stats = reinterpret_cast<UnitStats*>(hero->stats);
    TradeRecord& record = records_.begin[recordIndex_];

    const TradeRow* row = nullptr;
    bool hold = false;
    if (region >= kFirstSchool && region < kScience) {
        row = &schools_.begin[region - kFirstSchool];
        if (row->level >= kMaxLevel || row->price > experience_) return;
        ++stats->skills[region - kFirstSchool];
        hold = true;
    } else if (region == kScience) {
        row = &science_.begin[0];
        if (row->level >= kMaxLevel || row->price > experience_) return;
        ++stats->attributes[0];
        hold = true;
    } else if (region >= kFirstLearnable && region < kFirstHeld) {
        std::uint32_t index = static_cast<std::uint32_t>(learnableTop_ + region - kFirstLearnable);
        if (index >= static_cast<std::uint32_t>(learnable_.Count())) return;
        row = &learnable_.begin[index];
        if (row->price > experience_ || !row->learnable) return;
        StatsLearnSkillOriginal(stats, nullptr, SkillDatabase(), SkillFindOriginal(SkillDatabase(), nullptr, row->key));
    } else {
        // 34 and up, and (as unsigned) anything below 0: the next level of a held perk.
        std::uint32_t index = static_cast<std::uint32_t>(perkTop_ + region - kFirstHeld);
        if (index >= static_cast<std::uint32_t>(perks_.Count())) return;
        row = &perks_.begin[index];
        if (row->price > experience_ || TextLength(row->nextKey) == 0) return;
        StatsLearnSkillOriginal(stats, nullptr, SkillDatabase(),
                                SkillFindOriginal(SkillDatabase(), nullptr, row->nextKey));
    }
    stats->spentExperience = static_cast<float>(static_cast<double>(row->price) + stats->spentExperience);
    record.bought = 1;
    TradePanelBuildListsOriginal(this);
    if (hold) {
        heldRegion_ = region;
        holdTime_ = kFirstHold;
        UiCapture(interface_) = CaptureValue(InputOf(this));
    }
}
namespace {
void __fastcall TradeLButtonDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CTradePanel>(input)->OnLButtonDown(buttons, x, y);
}
} // namespace
EI_REPLACE(0x00626920, TradeLButtonDown);
