// The camp's trade panel (the level-up page): its layout, its lists of schools and perks and their drawing.
// See docs/notes/ui.md ("Camp, global map, hero screens").
#include "text_manager.h"
#include "world.h"
#include "ui_camp.h"
#include "party.h"
#include "unit.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <string>

#include "core_maths.h"
#include "unit_db.h"
#include "world_glue.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include <format>

namespace {

// Opens text "perk <name>" (its first line is the row's shown name).
bool OpenPerkText(CTextManager* texts, std::string_view name) {
    std::string key("perk ");
    key += name;
    return texts->CTextManager::Open(key);
}
// The open perk text's line; the cursor moves to the next line.
std::string ReadPerkLine(CTextManager* texts) {
    std::string line;
    texts->ReadLine(&line, 1);
    return line;
}

// A perk record's text key: its name without trailing level digits, plus "0".
std::string PerkBaseName(const SkillLevelRecord* record) {
    std::string text = record->name;
    str::TrimRight(text, "123");
    text += "0";
    return text;
}

void Down(RECT* rect) { gameapi::OffsetRect(rect, 0, 15); }

constexpr std::uint32_t kBright = 0xA7D7E4;
constexpr std::uint32_t kGrey = 0x828282;
std::uint32_t PriceColour(int price, int experience) { return price > experience ? kGrey : kBright; }

// The coin sprite after a price: texels (0x7C, 0x8A)-(0xA4, 0x99) between x `left` and `right`,
// the row's height lower by 100 (the panel's layout offset).
void PriceSprite(CTradePanel* panel, const RECT& row, float left, float right) {
    CUiMesh* sprite = panel->AddBlankSprite();
    sprite->SetRectangle(6.0f, 100.0f, left, static_cast<float>(row.top + 100), right,
                         static_cast<float>(row.bottom + 100), 0x7C, 0x8A, 0xA4, 0x99);
}

void DrawSchool(CTradePanel* panel, CUiSurface* draw, RECT* row, const TradeRow& school) {
    draw->DrawTextIn(school.name, row, 0, kBright, 0, 1);
    RECT levelRect = *row;
    gameapi::OffsetRect(&levelRect, -40, 0);
    draw->DrawTextIn(std::format("{}", school.level), &levelRect, 0, kBright, 2, 1);
    if (school.level < 100) {
        draw->DrawTextIn(CTradePanel::FormatPrice(school.price), row, 0, PriceColour(school.price, panel->experience_), 2, 1);
        PriceSprite(panel, *row, 350.0f, 390.0f);
    }
}

UnitStats* PartyMemberStats(int index) { return &LocalPlayerData()->members_[index].stats; }

} // namespace

void CTradePanel::BuildLists() {
    UnitStats* stats = &owner_->HeroRecord()->stats;
    stats->ResetToBase(0.0f);
    owner_->RefreshStats();
    CTextManager* texts = g_clientWorld.texts_;
    std::vector<SkillLevelRecord>& levels = g_skillDatabase.levels;
    perks_.clear();
    learnable_.clear();
    schools_.clear();
    science_.clear();
    // The rows are pushed empty. The original's row has uninitialised price, level and flag (stack bytes, all
    // overwritten except the flag of the non-learnable rows); ours are 0.
    const char* const* names = SkillNames();
    for (int i = 0; i < 6; ++i) {
        TradeRow& row = schools_.emplace_back();
        row.name = OpenPerkText(texts, names[i]) ? ReadPerkLine(texts) : names[i];
        row.level = stats->skills[i];
        row.price = SchoolPrice(row.level, row.level + 1);
    }
    {
        TradeRow& row = science_.emplace_back();
        row.name = OpenPerkText(texts, names[6]) ? ReadPerkLine(texts) : names[6];
        row.level = stats->attributes[0];
        row.price = SchoolPrice(row.level, row.level + 1);
    }

    std::string perkLevel;
    texts->CTextManager::GetString("perk_level", &perkLevel);

    for (unsigned group = 0; group < 0x6C; group += 3) {
        int level = stats->skillLevels[group / 3];
        if (level <= 0) continue;
        unsigned index = group + level - 1;
        if (index >= levels.size()) continue;
        SkillLevelRecord* record = &levels[index];
        TradeRow& row = perks_.emplace_back();
        std::string text = PerkBaseName(record);
        if (OpenPerkText(texts, text))
            texts->ReadLine(&text, 1);
        else
            text = record->displayName;
        row.name = level > 1 ? std::format("{} ({}{})", text, perkLevel, level) : text;
        if (level < 3) {
            // The next level's record (null past the end: the original reads its name through
            // null then, as here).
            unsigned next = index + 1;
            SkillLevelRecord* nextRecord = next < levels.size() ? &levels[next] : nullptr;
            row.nextKey = nextRecord->name;
            row.price = stats->SkillPrice(nextRecord);
        } else {
            row.price = 0;
        }
        row.key = record->name;
        row.level = level;
    }

    for (unsigned index = 0; index < levels.size(); index += 3) {
        SkillLevelRecord* record = &levels[index];
        if (!stats->CanLearn(&g_skillDatabase, record)) continue;
        TradeRow& row = learnable_.emplace_back();
        row.name = OpenPerkText(texts, PerkBaseName(record)) ? ReadPerkLine(texts) : record->displayName;
        row.price = stats->SkillPrice(record);
        row.key = record->name;
        row.learnable = true;
        row.level = 0;
    }

    // Experience left: pending plus earned minus spent, truncated (the game's _ftol).
    experience_ = static_cast<int>(static_cast<double>(stats->pendingExperience) + stats->experience -
                                   stats->spentExperience);

    ClampScroll();
    DrawLists();
}

void CTradePanel::DrawLists() {
    CUiSurface* draw = surfaces_[0];
    draw->Clear();
    while (std::ssize(sceneObjects_) > 4) RemoveSceneObjectAt(std::ssize(sceneObjects_) - 1);

    RECT title = {0, 10, 200, 25};
    draw->DrawTextIn(texts_[0], &title, 0, 0xFFFFFF, 1, 1);
    title = {0, 205, 200, 220};
    draw->DrawTextIn(texts_[1], &title, 0, 0xFFFFFF, 1, 1);
    title = {200, 10, 400, 25};
    draw->DrawTextIn(texts_[2], &title, 0, 0xFFFFFF, 1, 1);

    RECT row = {15, 35, 180, 50};
    for (unsigned i = 0; i < 6; ++i) {
        if (i == 2) {
            Down(&row);
            Down(&row);
            continue;
        }
        DrawSchool(this, draw, &row, schools_[i]);
        Down(&row);
    }
    row = {15, 65, 180, 80};
    DrawSchool(this, draw, &row, science_[0]);

    row = {15, 230, 180, 245};
    unsigned index = static_cast<unsigned>(perkTop_);
    for (int shown = 0; shown < 10 && index < perks_.size(); ++shown, ++index) {
        const TradeRow& perk = perks_[index];
        draw->DrawTextIn(perk.name, &row, 0, kBright, 0, 1);
        if (!perk.nextKey.empty()) {
            draw->DrawTextIn(FormatPrice(perk.price), &row, 0, PriceColour(perk.price, experience_), 2, 1);
            PriceSprite(this, row, 350.0f, 390.0f);
        }
        Down(&row);
    }

    row = {215, 35, 380, 50};
    index = static_cast<unsigned>(learnableTop_);
    for (int shown = 0; shown < 23 && index < learnable_.size(); ++shown, ++index) {
        const TradeRow& perk = learnable_[index];
        std::uint32_t textColour = kGrey;
        std::uint32_t priceColour = kGrey;
        if (perk.learnable) {
            textColour = kBright;
            priceColour = PriceColour(perk.price, experience_);
        }
        draw->DrawTextIn(perk.name, &row, 0, textColour, 0, 1);
        draw->DrawTextIn(FormatPrice(perk.price), &row, 0, priceColour, 2, 1);
        PriceSprite(this, row, 550.0f, 590.0f);
        Down(&row);
    }
}

void CTradePanel::Build() {
    rect_ = RECT{200, 100, 600, 500};
    AddSurface(new CUiSurface(interface_, 200, 100, 600, 500, SurfaceBlend::Keyed, true));

    AddRegion()->SetRect(360, 320, 380, 330);
    AddRegion()->SetRect(360, 480, 380, 490);
    AddRegion()->SetRect(560, 125, 580, 135);
    AddRegion()->SetRect(560, 480, 580, 490);
    RECT row = {215, 135, 385, 150};
    for (int i = 0; i < 2; ++i) {
        AddRegion()->SetRectFrom(row);
        Down(&row);
    }
    Down(&row);
    Down(&row);
    AddRegion()->SetRect(0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        AddRegion()->SetRectFrom(row);
        Down(&row);
    }
    Down(&row);
    AddRegion()->SetRect(215, 165, 385, 180);
    for (int y = 135; y < 480; y += 15) AddRegion()->SetRect(415, y, 585, y + 15);
    for (int y = 330; y < 480; y += 15) AddRegion()->SetRect(215, y, 385, y + 15);

    AddBlankSprite()->SetRectangle(6.0f, 100.0f, 360.0f, 320.0f, 380.0f, 330.0f, 0x82, 3, 0x96, 0xD);
    AddBlankSprite()->SetRectangle(6.0f, 100.0f, 360.0f, 480.0f, 380.0f, 490.0f, 0x82, 0xD, 0x96, 3);
    AddBlankSprite()->SetRectangle(6.0f, 100.0f, 560.0f, 125.0f, 580.0f, 135.0f, 0x82, 3, 0x96, 0xD);
    AddBlankSprite()->SetRectangle(6.0f, 100.0f, 560.0f, 480.0f, 580.0f, 490.0f, 0x82, 0xD, 0x96, 3);

    CTextManager* texts = g_clientWorld.texts_;
    std::string text;
    for (const char* key : {"skills", "perks", "perks_av"}) {
        texts->CTextManager::GetString(key, &text);
        texts_.push_back(text);
    }
    ResetRecords();
}

// --- The records again --------------------------------------------------------------------------------

void CTradePanel::ResetRecords() {
    // The vector emptied first (the records hold no resources). The original pushes uninitialised stack bytes;
    // its record filler writes all but a few bytes. Ours pushes zeros.
    records_.clear();
    for (int i = 0; i < std::ssize(LocalPlayerData()->members_); ++i) {
        records_.emplace_back();
        recordIndex_ = i;
        SnapshotRecord();
    }
}

// --- The record from the hero ------------------------------------------------------------------------

void CTradePanel::SnapshotRecord() {
    if (recordIndex_ < 0) return;
    TradeStatsRecord& record = records_[recordIndex_];
    const UnitStats* stats = PartyMemberStats(recordIndex_);
    for (int i = 0; i < 6; ++i) record.schools[i] = stats->skills[i];
    record.science = stats->attributes[0];
    for (int i = 0; i < 36; ++i) record.perks[i] = stats->skillLevels[i];
    // Copied bit for bit, as the original does.
    record.experience = stats->experience;
    record.pendingExperience = stats->pendingExperience;
    record.spentExperience = stats->spentExperience;
    record.bought = false;
}

// --- The hero's statistics back from the record -------------------------------------------------------

void CTradePanel::RestoreRecord() {
    if (recordIndex_ < 0) return;
    TradeStatsRecord& record = records_[recordIndex_];
    UnitStats* stats = PartyMemberStats(recordIndex_);
    // The hit point maximum and the hit points the slots allow, before (both stored as float).
    const float oldMaximum = static_cast<float>(stats->HitPointCurve());
    const float oldHitPoints = static_cast<float>(stats->Reduce(oldMaximum));
    for (int i = 0; i < 6; ++i) stats->skills[i] = record.schools[i];
    stats->attributes[0] = record.science;
    for (int i = 0; i < 36; ++i) stats->skillLevels[i] = record.perks[i];
    stats->experience = record.experience;
    stats->pendingExperience = record.pendingExperience;
    stats->spentExperience = record.spentExperience;
    record.bought = false;
    const float newMaximum = static_cast<float>(stats->HitPointCurve());
    // Only when both maxima are above 1: the slots are healed by what keeps the hit points in the
    // old proportion to the new maximum.
    EI_NAN_AUDIT(oldMaximum, newMaximum);
    if (oldMaximum <= 1.0f || newMaximum <= 1.0f) return;
    const double hitPoints = stats->Reduce(newMaximum);
    stats->HealSlots(newMaximum, static_cast<float>(oldHitPoints / oldMaximum * newMaximum - hitPoints));
}

// --- Settling and choosing the hero ------------------------------------------------------------------

void CTradePanel::Settle(bool keep, bool all) {
    if (all) {
        int saved = recordIndex_;
        for (int i = 0; i < std::ssize(records_); ++i) {
            if (!records_[i].bought) continue;
            recordIndex_ = i;
            if (keep)
                SnapshotRecord();
            else
                RestoreRecord();
        }
        recordIndex_ = saved;
        return;
    }
    if (!records_[recordIndex_].bought) return;
    if (keep)
        SnapshotRecord();
    else
        RestoreRecord();
    BuildLists();
}

void CTradePanel::SelectHero(int index) {
    recordIndex_ = index;
    perkTop_ = 0;
    learnableTop_ = 0;
    BuildLists();
}

// --- A price as text ----------------------------------------------------------------------------------

std::string CTradePanel::FormatPrice(int price) {
    constexpr float kThousandth = 0.001f;
    constexpr float kMillionth = 1e-6f;
    constexpr float kBillionth = 1e-9f;
    if (price < 0) price = 0x7FFFFFFF;   // shown in billions
    else if (price < 1000) return std::format("{}", price);
    else if (price < 10000) return str::Format("%.1fk", static_cast<double>(price) * kThousandth);
    else if (price < 1000000) return std::format("{}k", price / 1000);
    else if (price < 10000000) return str::Format("%.1fm", static_cast<double>(price) * kMillionth);
    else if (price < 1000000000) return std::format("{}m", price / 1000000);
    return str::Format("%.1fg", static_cast<double>(price) * kBillionth);
}
