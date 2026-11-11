// The camp's trade panel (the level-up page): its layout, its lists of schools and perks and their drawing.
// See docs/notes/ui.md ("Camp, global map, hero screens").
#include "game/ui/ui_camp.h"
#include "engine/core/core_maths.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/players/party.h"
#include "engine/data/text_manager.h"
#include "engine/scene/scene_renderer.h"
#include "game/ui/ui.h"
#include "game/ui/ui_item_info.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_screens.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "render/render.h"
#include "game/ui/screen_layout.h"

#include <cstdint>
#include <format>
#include <string>

namespace {
// Opens text "perk <name>" (its first line is the row's shown name).
bool OpenPerkText(TextManager* texts, std::string_view name) {
    std::string key("perk ");
    key += name;
    return texts->TextManager::Open(key);
}
// The open perk text's line; the cursor moves to the next line.
std::string ReadPerkLine(TextManager* texts) {
    std::string line = texts->ReadLine(true);
    return line;
}

// A perk record's text key: its name without trailing level digits, plus "0".
std::string PerkBaseName(const SkillLevelRecord* record) {
    std::string text = record->name;
    str::TrimRight(text, "123");
    text += "0";
    return text;
}

void Down(Rect* rect) { rect->Offset(0, 15); }

constexpr std::uint32_t kGrey = 0x828282;
std::uint32_t PriceColour(int price, int experience) { return price > experience ? kGrey : kUiTextColour; }

// The coin sprite after a price: texels (0x7C, 0x8A)-(0xA4, 0x99) between x `left` and `right`,
// the row's height lower by 100 (the panel's layout offset).
void PriceSprite(TradePanel* panel, const Rect& row, float left, float right) {
    UiMesh* sprite = panel->AddBlankSprite();
    panel->priceSprites.push_back(sprite);
    sprite->SetRectangle(6.0f, kSpriteScale, left, static_cast<float>(row.top + 100), right,
                         static_cast<float>(row.bottom + 100), 0x7C, 0x8A, 0xA4, 0x99);
}

void DrawSchool(TradePanel* panel, Rect* row, const TradeRow& school) {
    panel->labels.push_back({school.name, *row, {0, kUiTextColour, render::text::Format::Left}});
    Rect levelRect = *row;
    levelRect.Offset(-40, 0);
    panel->labels.push_back({std::format("{}", school.level), levelRect, {0, kUiTextColour, render::text::Format::Right}});
    if (school.level < 100) {
        panel->labels.push_back({TradePanel::FormatPrice(school.price), *row, {0, PriceColour(school.price, panel->experience), render::text::Format::Right}});
        PriceSprite(panel, *row, 350.0f, 390.0f);
    }
}

UnitStats* PartyMemberStats(int index) { return &LocalPlayerData()->members[index].stats; }

const char* const kPerkSound = "buttons\\camp\\perk.wav";

constexpr int kMaxLevel = 100;
constexpr int kFirstHoldTicks = -500;    // milliseconds: the first repeat waits 750 ms
constexpr int kRepeatTicks = 250;        // then a level every frame

// One list's items: cell i at column i % 4, row i / 4; `yOffset` and the sizes differ between the
// bought (upper) and sold (lower) lists, as do the text rows.
void DrawItems(TradeGrid* grid, SceneRenderer* pass, const std::vector<Item*>& list, std::vector<UiLabel>& labels,
               float yOffset, float bigSize, float smallSize, int countTop, int priceTop, PriceKind kind) {
    for (int i = 0; i < std::ssize(list); ++i) {
        int x = (i % 4) * 100;
        int y = (i / 4) * 100;
        ItemCell cell{list[i], x + 250.0f, y + yOffset,
                      i <= 3 ? bigSize : smallSize, 100.0f, grid->mode};
        grid->owner->DrawItem(pass, cell);
        int pieces = static_cast<int>(cell.item->count);
        if (pieces > 1) {
            labels.push_back({std::format("{}", pieces), Rect{x + 15, y + countTop, x + 0x55, y + countTop + 20}, {1, kUiTextColour, render::text::Format::Right}});
        }
        if (!grid->exchange) {
            labels.push_back({std::format("{}", grid->camp->Price(cell.item, kind)), Rect{x + 15, y + priceTop, x + 0x55, y + priceTop + 20}, {1, kUiTextColour, render::text::Format::Center}});
        }
    }
}

constexpr const char* kPerkLevelDigits = "123";
constexpr const char* kPerkFirstLevel = "0";
} // namespace

void TradePanel::BuildLists() {
    UnitStats* stats = &owner->HeroRecord()->stats;
    stats->ResetToBase(0.0f);
    owner->RefreshStats();
    TextManager* clientWorldTexts = g_clientWorld.texts.get();
    std::vector<SkillLevelRecord>& levels = g_skillDatabase.levels;
    perks.clear();
    learnable.clear();
    schools.clear();
    science.clear();
    // The rows are pushed empty. The original's row has uninitialised price, level and flag (stack bytes, all
    // overwritten except the flag of the non-learnable rows); ours are 0.
    const char* const* names = SkillNames();
    for (int i = 0; i < 6; ++i) {
        TradeRow& row = schools.emplace_back();
        row.name = OpenPerkText(clientWorldTexts, names[i]) ? ReadPerkLine(clientWorldTexts) : names[i];
        row.level = stats->skills[i];
        row.price = SchoolPrice(row.level, row.level + 1);
    }
    {
        TradeRow& row = science.emplace_back();
        row.name = OpenPerkText(clientWorldTexts, names[6]) ? ReadPerkLine(clientWorldTexts) : names[6];
        row.level = stats->attributes[0];
        row.price = SchoolPrice(row.level, row.level + 1);
    }

    std::string perkLevel = clientWorldTexts->TextManager::GetString("perk_level");

    for (unsigned group = 0; group < 0x6C; group += 3) {
        int level = stats->skillLevels[group / 3];
        if (level <= 0) continue;
        unsigned index = group + level - 1;
        if (index >= levels.size()) continue;
        SkillLevelRecord* record = &levels[index];
        TradeRow& row = perks.emplace_back();
        std::string text = PerkBaseName(record);
        if (OpenPerkText(clientWorldTexts, text))
            text = clientWorldTexts->ReadLine(true);
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
        TradeRow& row = learnable.emplace_back();
        row.name = OpenPerkText(clientWorldTexts, PerkBaseName(record)) ? ReadPerkLine(clientWorldTexts) : record->displayName;
        row.price = stats->SkillPrice(record);
        row.key = record->name;
        row.learnable = true;
        row.level = 0;
    }

    // Experience left: pending plus earned minus spent, truncated (the game's _ftol).
    experience = static_cast<int>(stats->pendingExperience + stats->experience -
                                   stats->spentExperience);

    ClampScroll();
    DrawLists();
}

void TradePanel::DrawLists() {
    labels.clear();
    // The last added first.
    while (!priceSprites.empty()) {
        RemoveSceneObject(priceSprites.back());
        priceSprites.pop_back();
    }

    labels.push_back({texts[0], Rect{0, 10, 200, 25}, {0, 0xFFFFFF, render::text::Format::Center}});
    labels.push_back({texts[1], Rect{0, 205, 200, 220}, {0, 0xFFFFFF, render::text::Format::Center}});
    labels.push_back({texts[2], Rect{200, 10, 400, 25}, {0, 0xFFFFFF, render::text::Format::Center}});

    Rect row = {15, 35, 180, 50};
    for (unsigned i = 0; i < 6; ++i) {
        if (i == 2) {
            Down(&row);
            Down(&row);
            continue;
        }
        DrawSchool(this, &row, schools[i]);
        Down(&row);
    }
    row = {15, 65, 180, 80};
    DrawSchool(this, &row, science[0]);

    row = {15, 230, 180, 245};
    unsigned index = static_cast<unsigned>(perkTop);
    for (int shown = 0; shown < 10 && index < perks.size(); ++shown, ++index) {
        const TradeRow& perk = perks[index];
        labels.push_back({perk.name, row, {0, kUiTextColour}});
        if (!perk.nextKey.empty()) {
            labels.push_back({FormatPrice(perk.price), row, {0, PriceColour(perk.price, experience), render::text::Format::Right}});
            PriceSprite(this, row, 350.0f, 390.0f);
        }
        Down(&row);
    }

    row = {215, 35, 380, 50};
    index = static_cast<unsigned>(learnableTop);
    for (int shown = 0; shown < 23 && index < learnable.size(); ++shown, ++index) {
        const TradeRow& perk = learnable[index];
        std::uint32_t textColour = kGrey;
        std::uint32_t priceColour = kGrey;
        if (perk.learnable) {
            textColour = kUiTextColour;
            priceColour = PriceColour(perk.price, experience);
        }
        labels.push_back({perk.name, row, {0, textColour}});
        labels.push_back({FormatPrice(perk.price), row, {0, priceColour, render::text::Format::Right}});
        PriceSprite(this, row, 550.0f, 590.0f);
        Down(&row);
    }
}

void TradePanel::Build() {
    rect = Rect{200, 100, 600, 500};
    AddSurface(std::make_unique<UiSurface>(200, 100, 600, 500, SurfaceBlend::Keyed, true))
        ->PaintWith([this](UiSurface& out) { PaintLabels(out, labels); });

    auto region = [this](const Rect& area) {
        UiRegion* added = AddRegion();
        added->SetRect(area);
        return added;
    };
    perkScroll[0] = region(Rect{360, 320, 380, 330});
    perkScroll[1] = region(Rect{360, 480, 380, 490});
    learnableScroll[0] = region(Rect{560, 125, 580, 135});
    learnableScroll[1] = region(Rect{560, 480, 580, 490});
    Rect row = {215, 135, 385, 150};
    for (int i = 0; i < 2; ++i) {
        schoolRows[i] = region(row);
        Down(&row);
    }
    Down(&row);
    Down(&row);
    schoolRows[2] = region(Rect{0, 0, 0, 0});
    for (int i = 3; i < kSchools; ++i) {
        schoolRows[i] = region(row);
        Down(&row);
    }
    Down(&row);
    scienceRow = region(Rect{215, 165, 385, 180});
    for (int i = 0; i < kLearnableRows; ++i) learnableRows[i] = region(Rect{415, 135 + 15 * i, 585, 150 + 15 * i});
    for (int i = 0; i < kHeldRows; ++i) perkRows[i] = region(Rect{215, 330 + 15 * i, 385, 345 + 15 * i});

    auto arrow = [this](float left, float top, float right, float bottom, int v0, int v1) {
        UiMesh* sprite = AddBlankSprite();
        sprite->SetRectangle(6.0f, kSpriteScale, left, top, right, bottom, 0x82, v0, 0x96, v1);
        return sprite;
    };
    perkScrollIcons[0] = arrow(360.0f, 320.0f, 380.0f, 330.0f, 3, 0xD);
    perkScrollIcons[1] = arrow(360.0f, 480.0f, 380.0f, 490.0f, 0xD, 3);
    learnableScrollIcons[0] = arrow(560.0f, 125.0f, 580.0f, 135.0f, 3, 0xD);
    learnableScrollIcons[1] = arrow(560.0f, 480.0f, 580.0f, 490.0f, 0xD, 3);

    TextManager* clientWorldTexts = g_clientWorld.texts.get();
    std::string text;
    for (const char* key : {"skills", "perks", "perks_av"}) {
        text = clientWorldTexts->TextManager::GetString(key);
        texts.push_back(text);
    }
    ResetRecords();
}

// --- The records again --------------------------------------------------------------------------------

void TradePanel::ResetRecords() {
    // The vector emptied first (the records hold no resources). The original pushes uninitialised stack bytes;
    // its record filler writes all but a few bytes. Ours pushes zeros.
    records.clear();
    for (int i = 0; i < std::ssize(LocalPlayerData()->members); ++i) {
        records.emplace_back();
        recordIndex = i;
        SnapshotRecord();
    }
}

// --- The record from the hero ------------------------------------------------------------------------

void TradePanel::SnapshotRecord() {
    if (recordIndex < 0) return;
    TradeStatsRecord& record = records[recordIndex];
    const UnitStats* stats = PartyMemberStats(recordIndex);
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

void TradePanel::RestoreRecord() {
    if (recordIndex < 0) return;
    TradeStatsRecord& record = records[recordIndex];
    UnitStats* stats = PartyMemberStats(recordIndex);
    // The hit point maximum and the hit points the slots allow, before (both stored as float).
    const float oldMaximum = stats->HitPointCurve();
    const float oldHitPoints = stats->Reduce(oldMaximum);
    for (int i = 0; i < 6; ++i) stats->skills[i] = record.schools[i];
    stats->attributes[0] = record.science;
    for (int i = 0; i < 36; ++i) stats->skillLevels[i] = record.perks[i];
    stats->experience = record.experience;
    stats->pendingExperience = record.pendingExperience;
    stats->spentExperience = record.spentExperience;
    record.bought = false;
    const float newMaximum = stats->HitPointCurve();
    // Only when both maxima are above 1: the slots are healed by what keeps the hit points in the
    // old proportion to the new maximum.
    if (oldMaximum <= 1.0f || newMaximum <= 1.0f) return;
    const float hitPoints = stats->Reduce(newMaximum);
    stats->HealSlots(newMaximum, oldHitPoints / oldMaximum * newMaximum - hitPoints);
}

// --- Settling and choosing the hero ------------------------------------------------------------------

void TradePanel::Settle(bool keep, bool all) {
    if (all) {
        int saved = recordIndex;
        for (int i = 0; i < std::ssize(records); ++i) {
            if (!records[i].bought) continue;
            recordIndex = i;
            if (keep)
                SnapshotRecord();
            else
                RestoreRecord();
        }
        recordIndex = saved;
        return;
    }
    if (!records[recordIndex].bought) return;
    if (keep)
        SnapshotRecord();
    else
        RestoreRecord();
    BuildLists();
}

void TradePanel::SelectHero(int index) {
    recordIndex = index;
    perkTop = 0;
    learnableTop = 0;
    BuildLists();
}

// --- A price as text ----------------------------------------------------------------------------------

std::string TradePanel::FormatPrice(int price) {
    constexpr float kThousandth = 0.001f;
    constexpr float kMillionth = 1e-6f;
    constexpr float kBillionth = 1e-9f;
    if (price < 0) price = 0x7FFFFFFF;   // shown in billions
    else if (price < 1000) return std::format("{}", price);
    else if (price < 10000) return str::Format("%.1fk", price * kThousandth);
    else if (price < 1000000) return std::format("{}k", price / 1000);
    else if (price < 10000000) return str::Format("%.1fm", price * kMillionth);
    else if (price < 1000000000) return std::format("{}m", price / 1000000);
    return str::Format("%.1fg", price * kBillionth);
}

// --- Input -------------------------------------------------------------------------------------------

// The trade panel's scroll clamp, its left button and its update:
// scrolling the perk lists and buying school levels, science and perks with experience, a school
// or science level again every frame from 750 ms of holding on while the button stays down (whole
// milliseconds). See docs/notes/ui.md, section 5.

// --- The scroll positions clamped ----------------------------------------------------------------------

void TradePanel::ClampScroll() {
    if (perkTop > static_cast<int>(perks.size()) - kHeldRows) perkTop = static_cast<int>(perks.size()) - kHeldRows;
    if (perkTop < 0) perkTop = 0;
    if (learnableTop > static_cast<int>(learnable.size()) - kLearnableRows) learnableTop = static_cast<int>(learnable.size()) - kLearnableRows;
    if (learnableTop < 0) learnableTop = 0;
    perkScrollIcons[0]->visible = perkTop > 0;
    perkScrollIcons[1]->visible = perkTop < static_cast<int>(perks.size()) - kHeldRows;
    learnableScrollIcons[0]->visible = learnableTop > 0;
    learnableScrollIcons[1]->visible = learnableTop < static_cast<int>(learnable.size()) - kLearnableRows;
    DrawLists();
}

// --- The left button (input click) ---------------------------------------------------------------------

void TradePanel::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    default: UiItemPanel::OnEvent(event); break;
    }
}

void TradePanel::LeftDown(UiEvent& event) {
    UiRegion* region = RegionUnder(event.at);
    if (region) {
        if (region == perkScroll[0]) { --perkTop; ClampScroll(); return; }
        if (region == perkScroll[1]) { ++perkTop; ClampScroll(); return; }
        if (region == learnableScroll[0]) { --learnableTop; ClampScroll(); return; }
        if (region == learnableScroll[1]) { ++learnableTop; ClampScroll(); return; }
        g_interface.PlayInterfaceSound(kPerkSound);
    }
    UnitStats* stats = &owner->HeroRecord()->stats;
    TradeStatsRecord& record = records[recordIndex];

    const TradeRow* row = nullptr;
    bool hold = false;
    const int school = RegionIndex(schoolRows, region);
    const int learnableRow = RegionIndex(learnableRows, region);
    if (school >= 0) {
        row = &schools[school];
        if (row->level >= kMaxLevel || row->price > experience) return;
        ++stats->skills[school];
        hold = true;
    } else if (region == scienceRow) {
        row = &science[0];
        if (row->level >= kMaxLevel || row->price > experience) return;
        ++stats->attributes[0];
        hold = true;
    } else if (learnableRow >= 0) {
        std::uint32_t index = static_cast<std::uint32_t>(learnableTop + learnableRow);
        if (index >= learnable.size()) return;
        row = &learnable[index];
        if (row->price > experience || !row->learnable) return;
        stats->LearnSkill(&g_skillDatabase, g_skillDatabase.FindLevel(row->key));
    } else {
        // A held perk's row, or no region (HeldPerkRow): the next level of a held perk.
        std::uint32_t index = static_cast<std::uint32_t>(perkTop + HeldPerkRow(region));
        if (index >= perks.size()) return;
        row = &perks[index];
        if (row->price > experience || row->nextKey.empty()) return;
        stats->LearnSkill(&g_skillDatabase, g_skillDatabase.FindLevel(row->nextKey));
    }
    stats->spentExperience = row->price + stats->spentExperience;
    record.bought = true;
    BuildLists();
    if (hold) {
        heldRow = region;
        holdTicks = kFirstHoldTicks;
        CaptureMouse();
    }
}

// --- The left button released (input release) -----------------------------------------------------------

void TradePanel::LeftUp(UiEvent&) {
    g_interface.mouseCapture = nullptr;
    heldRow = nullptr;
    holdTicks = 0;
}

// --- The held school / science button (update) ---------------------------------------------------------

void TradePanel::Update() {
    if (!heldRow) return;
    holdTicks += UiContainer::FrameTicks();
    if (holdTicks < kRepeatTicks) return;
    UnitStats* stats = &owner->HeroRecord()->stats;
    const TradeRow* row;
    std::uint8_t* level;
    if (const int school = RegionIndex(schoolRows, heldRow); school >= 0) {
        row = &schools[school];
        level = &stats->skills[school];
    } else if (heldRow == scienceRow) {
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
    g_interface.mouseCapture = nullptr;
    heldRow = nullptr;
}

// --- Ui trade grid draw ------------------------------------------------------------------------------

// The camp trade grid's draw (TradeGrid). The depth buffer is cleared to 0 over the screen and to 1 over the
// grid (200..600 x 100..500 in layout coordinates), so the cells' item models draw only there; then the
// window's sprites and models, and on its surface: every bought item and sold item in its cell of a 4 x 4
// layout (100 pixels apart; the item's model is drawn there), its count when more than one, and except on
// the exchange window its price (the owner's price virtual with the kind). Last the depth buffer is cleared
// to 1 again. The new renderer (src/render) gets the depth clears. See docs/notes/ui.md ("Camp") and
// docs/notes/renderer.md.

void TradeGrid::DrawSprites(SceneRenderer* pass) {
    Rect grid = screen_layout::ToScreen(Rect{200, 100, 600, 500});
    Rect screen{0, 0, g_interface.width, g_interface.height};
    ClearUiDepth(0.0f, &screen, 1);
    ClearUiDepth(1.0f, &grid, 1);
    DrawBase(pass);

    labels.clear();
    DrawItems(this, pass, *bought, labels, 150.0f, 48.0f, 24.0f, 0x0F, 0x41, buyKind);
    DrawItems(this, pass, *sold, labels, 350.0f, 12.0f, 6.0f, 0xD7, 0x109, sellKind);
    ClearUiDepth(1.0f, &screen, 1);
}

// --- Ui camp stats -----------------------------------------------------------------------------------

// The camp's statistics column (HeroStatsPanel) and trade panel (TradePanel): what the
// statistics under the mouse are, for the description panel (the camp's Draw
// and the hero screen's draw ask every frame), and the column's hero. See ui_camp.h and
// docs/notes/ui.md ("Camp, global map, hero screens").

// --- the trade panel -----------------------------------------------------------------------------

ItemHoverInfo TradePanel::HoverInfo(Point at) {
    ItemHoverInfo info;
    if (!enabled) return info;
    // No region falls through to the held perks (HeldPerkRow), past the list.
    const UiRegion* region = RegionUnder(at);
    const int school = RegionIndex(schoolRows, region);
    const int learnableRow = RegionIndex(learnableRows, region);
    if (school >= 0) {
        info.kind = HoverKind::Skill;
        info.index = school;
        info.text = SkillNames()[school];
    } else if (region == scienceRow) {
        info.kind = HoverKind::Science;
        info.index = 0;
        info.text = SkillNames()[6];
    } else if (RegionIndex(perkScroll, region) >= 0 || RegionIndex(learnableScroll, region) >= 0) {
        return info;
    } else {
        const TradeRow* row;
        if (learnableRow >= 0) {
            unsigned i = static_cast<unsigned>(learnableTop) + static_cast<unsigned>(learnableRow);
            if (i >= learnable.size()) return info;
            row = &learnable[i];
            info.index = 0;
        } else {
            unsigned i = static_cast<unsigned>(perkTop) + static_cast<unsigned>(HeldPerkRow(region));
            if (i >= perks.size()) return info;
            row = &perks[i];
            info.index = row->level;
        }
        info.kind = HoverKind::Perk;
        info.text = row->key;
        str::TrimRight(info.text, kPerkLevelDigits);
        info.text += kPerkFirstLevel;
    }
    info.stats = &owner->HeroRecord()->stats;
    return info;
}
