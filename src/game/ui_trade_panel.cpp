// The camp's trade panel (the level-up page, vtable 007417A8): its layout (00624F10), its lists of
// schools and perks (00625880) and their drawing (006261A0). See docs/notes/ui.md ("Ninth pass",
// "Tenth pass").
#include "ui_camp.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include "originals_ui.h"

namespace {

const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
mfc::CString* Str(char*& field) { return reinterpret_cast<mfc::CString*>(&field); }
const mfc::CString& StrAt(const std::uint8_t* address) { return *reinterpret_cast<const mfc::CString*>(address); }

// The hero's statistics (UnitStats at the hero record + 0x20): 00524CD0 resets them to the base
// values (thiscall, RET 4), 005260A0 whether a skill record can be learned (RET 8), 00528050 a
// skill record's price (RET 4); 00527FF0 (__fastcall) the price of a school from one level to the next.
__declspec(naked) void __fastcall ResetStats(std::uint8_t* /*stats*/, void*, int) EI_ORIGINAL(0x00524CD0)
__declspec(naked) int __fastcall CanLearn(std::uint8_t* /*stats*/, void*, void* /*database*/, std::uint8_t* /*record*/)
    EI_ORIGINAL(0x005260A0)
__declspec(naked) int __fastcall SkillPrice(std::uint8_t* /*stats*/, void*, std::uint8_t* /*record*/) EI_ORIGINAL(0x00528050)
__declspec(naked) int __fastcall SchoolPrice(int /*from*/, int /*to*/) EI_ORIGINAL(0x00527FF0)

// The text table (00578E20 on 007AFEA4 returns [007AF788]): 004DEFC0 whether it has a key (RET 4),
// 004DF8D0 the found text into a CString (RET 8), 004E0020 the text of a key (RET 8).
struct TextGlue {
    void* Table();   // 00578E20
};
__declspec(naked) void* TextGlue::Table() EI_ORIGINAL(0x00578E20)
__declspec(naked) int __fastcall TextHasKey(void*, void*, const char*) EI_ORIGINAL(0x004DEFC0)
__declspec(naked) int __fastcall TextReadLine(void*, void*, mfc::CString*, int) EI_ORIGINAL(0x004DF8D0)
__declspec(naked) int __fastcall TextGetString(void*, void*, const char*, mfc::CString*) EI_ORIGINAL(0x004E0020)

// vector<TradeRow> pieces (library): 0062A310 copy-constructs a row (thiscall, RET 4), 0062A7C0
// destroys one (and frees it with flag 1; RET 4), 00629E60 the insert that reallocates (RET 0xC).
__declspec(naked) void __fastcall RowCopy(TradeRow*, void*, const TradeRow*) EI_ORIGINAL(0x0062A310)
__declspec(naked) void __fastcall RowDestroy(TradeRow*, void*, int) EI_ORIGINAL(0x0062A7C0)
__declspec(naked) void __fastcall RowsInsertOverflow(TradeRows*, void*, TradeRow* /*pos*/, const TradeRow*, int /*n*/)
    EI_ORIGINAL(0x00629E60)

// 00626D70 (__fastcall): clamps the two scroll positions, shades the four arrows and draws.
// 006261A0 through its address (the rewrite below when hooked).
__declspec(naked) void __fastcall ClampScroll(CTradePanel*) EI_ORIGINAL(0x00626D70)
__declspec(naked) void __fastcall DrawListsAt(CTradePanel*) EI_ORIGINAL(0x006261A0)
// 00626090 (thiscall, RET 8): a price as text ("%d", "%.1fk", "%dk", ... ; negative as 0x7FFFFFFF).
__declspec(naked) void __fastcall FormatPrice(CTradePanel*, void*, int, mfc::CString*) EI_ORIGINAL(0x00626090)

struct SurfaceDraw {
    void* Begin() { return SurfaceGetDC(this); }                                                                   // 005FDC00
    void Clear();                                                                   // 005FE2F0
    int Text(const char* text, RECT* rect, int a, std::uint32_t colour, int format, int b);   // 005FDE20 (Rebirth)
    void End() { SurfaceReleaseDC(this); }                                                                     // 005FDC60
};
__declspec(naked) void SurfaceDraw::Clear() EI_ORIGINAL(0x005FE2F0)
int SurfaceDraw::Text(const char* text, RECT* rect, int format, std::uint32_t colour, int a, int b) { return SurfaceDrawText(this, nullptr, text, rect, format, colour, a, b); }

constexpr std::uintptr_t kSkillDatabase = 0x007AF268;   // its records: vector at +0xC (0x44 bytes each)
constexpr int kSkillRecordSize = 0x44;
std::uint8_t* SkillRecords() { return *reinterpret_cast<std::uint8_t* const*>(kSkillDatabase + 0xC); }
int SkillCount() {
    std::uint8_t* end = *reinterpret_cast<std::uint8_t* const*>(kSkillDatabase + 0x10);
    return static_cast<int>(end - SkillRecords()) / kSkillRecordSize;
}

// The rows are pushed as copies of an empty row. The original's row has uninitialised price,
// level and flag (stack bytes, all overwritten except the flag of the non-learnable rows); ours
// are 0.
TradeRow BlankRow() {
    char* empty = *reinterpret_cast<char* const*>(0x00796E48);
    TradeRow row{};
    row.name = row.key = row.nextKey = empty;
    return row;
}

TradeRow* PushBack(TradeRows* rows, const TradeRow& value) {
    if (rows->end != rows->capacity) {
        if (rows->end) RowCopy(rows->end, nullptr, &value);
        ++rows->end;
    } else {
        RowsInsertOverflow(rows, nullptr, rows->end, &value, 1);
    }
    return rows->end - 1;
}

// erase(begin, end): the copy over the empty tail does nothing; the rows are destroyed.
void Clear(TradeRows* rows) {
    for (TradeRow* row = rows->begin; row != rows->end; ++row) RowDestroy(row, nullptr, 0);
    rows->end = rows->begin;
}

// Whether the text table has "perk <name>" (the key's temporaries released before returning).
int HasPerkText(void* table, const mfc::CString& name) {
    mfc::CString prefix(GameText(0x00792FC8));   // "perk "
    mfc::CString key = prefix + name;
    return TextHasKey(table, nullptr, key);
}
int HasPerkText(void* table, const char* name) {
    mfc::CString prefix(GameText(0x00792FC8));
    mfc::CString key = prefix + name;
    return TextHasKey(table, nullptr, key);
}

// A perk record's text key: its name (+4) without trailing level digits, plus "0".
void PerkBaseName(mfc::CString* text, const std::uint8_t* record) {
    *text = StrAt(record + 4);
    text->TrimRight(GameText(0x00793E60));   // "123"
    *text += GameText(0x007856B8);           // "0"
}

void Down(RECT* rect) { gameapi::OffsetRect(rect, 0, 15); }

constexpr std::uint32_t kBright = 0xA7D7E4;
constexpr std::uint32_t kGrey = 0x828282;
std::uint32_t PriceColour(int price, int experience) { return price > experience ? kGrey : kBright; }

} // namespace

// --- 00625880 -----------------------------------------------------------------------------------

void CTradePanel::BuildLists() {
    using HeroFn = std::uint8_t*(__thiscall*)(CUiWindow*);
    using RefreshFn = void(__thiscall*)(CUiWindow*);
    std::uint8_t* stats = reinterpret_cast<HeroFn>(owner_->vtable_[51])(owner_) + 0x20;
    ResetStats(stats, nullptr, 0);
    reinterpret_cast<RefreshFn>(owner_->vtable_[47])(owner_);
    void* table = reinterpret_cast<TextGlue*>(0x007AFEA4)->Table();
    Clear(&perks_);
    Clear(&learnable_);
    Clear(&schools_);
    Clear(&science_);

    const TradeRow blank = BlankRow();
    // The schools: the names are read through the pointer table 00793BD4 (six, then science).
    const char* const* names = reinterpret_cast<const char* const*>(0x00793BD4);
    for (int i = 0; i < 6; ++i) {
        TradeRow* row = PushBack(&schools_, blank);
        if (HasPerkText(table, names[i]))
            TextReadLine(table, nullptr, Str(row->name), 1);
        else
            *Str(row->name) = names[i];
        row->level = stats[0x6CB + i];
        row->price = SchoolPrice(row->level, row->level + 1);
    }
    {
        TradeRow* row = PushBack(&science_, blank);
        if (HasPerkText(table, names[6]))
            TextReadLine(table, nullptr, Str(row->name), 1);
        else
            *Str(row->name) = names[6];
        row->level = stats[0x6C8];
        row->price = SchoolPrice(row->level, row->level + 1);
    }

    mfc::CString text;
    mfc::CString perkLevel;
    TextGetString(table, nullptr, GameText(0x00793E70), &perkLevel);   // "perk_level"

    // The perks held: the skill records come in groups of three levels; the group's level byte.
    for (unsigned group = 0; group < 0x6C; group += 3) {
        int level = stats[0x6D3 + group / 3];
        if (level <= 0) continue;
        unsigned index = group + level - 1;
        if (index >= static_cast<unsigned>(SkillCount())) continue;
        std::uint8_t* record = SkillRecords() + index * kSkillRecordSize;
        if (!record) continue;
        TradeRow* row = PushBack(&perks_, blank);
        PerkBaseName(&text, record);
        if (HasPerkText(table, text))
            TextReadLine(table, nullptr, &text, 1);
        else
            text = StrAt(record);
        if (level > 1)
            Str(row->name)->Format(GameText(0x00793E64), static_cast<const char*>(text),   // "%s (%s%d)"
                                   static_cast<const char*>(perkLevel), level);
        else
            *Str(row->name) = text;
        if (level < 3) {
            // The next level's record (null past the end: the original reads its name through
            // null then, as here).
            unsigned next = index + 1;
            std::uint8_t* nextRecord =
                next < static_cast<unsigned>(SkillCount()) ? SkillRecords() + next * kSkillRecordSize : nullptr;
            *Str(row->nextKey) = StrAt(nextRecord + 4);
            row->price = SkillPrice(stats, nullptr, nextRecord);
        } else {
            row->price = 0;
        }
        *Str(row->key) = StrAt(record + 4);
        row->level = level;
    }

    // The first levels the hero may learn.
    int count = SkillCount();
    for (int index = 0; index < count; index += 3) {
        if (static_cast<unsigned>(index) >= static_cast<unsigned>(SkillCount())) continue;
        std::uint8_t* record = SkillRecords() + index * kSkillRecordSize;
        if (!record) continue;
        if (!CanLearn(stats, nullptr, reinterpret_cast<void*>(kSkillDatabase), record)) continue;
        TradeRow* row = PushBack(&learnable_, blank);
        PerkBaseName(&text, record);
        if (HasPerkText(table, text))
            TextReadLine(table, nullptr, Str(row->name), 1);
        else
            *Str(row->name) = StrAt(record);
        row->price = SkillPrice(stats, nullptr, record);
        *Str(row->key) = StrAt(record + 4);
        row->learnable = true;
        row->level = 0;
    }

    // Experience left: earned (+0x700) plus +4 minus +8, truncated (the game's _ftol).
    float earned, plus, minus;
    std::memcpy(&earned, stats + 0x700, 4);
    std::memcpy(&plus, stats + 4, 4);
    std::memcpy(&minus, stats + 8, 4);
    experience_ = static_cast<int>(static_cast<double>(earned) + plus - minus);
    ClampScroll(this);
    DrawListsAt(this);
}
namespace {
void __fastcall TradeBuildLists(CTradePanel* self) { self->BuildLists(); }
} // namespace
EI_REPLACE(0x00625880, TradeBuildLists);

// --- 006261A0 -----------------------------------------------------------------------------------

namespace {

// The coin sprite after a price: texels (0x7C, 0x8A)-(0xA4, 0x99) between x `left` and `right`,
// the row's height lower by 100 (the panel's layout offset).
void PriceSprite(CTradePanel* panel, const RECT& row, float left, float right) {
    CUiMesh* sprite = panel->AddSprite(0);
    sprite->SetRectangle(6.0f, 100.0f, left, static_cast<float>(row.top + 100), right,
                         static_cast<float>(row.bottom + 100), 0x7C, 0x8A, 0xA4, 0x99);
}

// A school row: name, level 40 pixels in from the right, and below level 100 the next level's price.
void DrawSchool(CTradePanel* panel, SurfaceDraw* draw, RECT* row, TradeRow* school, mfc::CString* value) {
    draw->Text(school->name, row, 0, kBright, 0, 1);
    value->Format(GameText(0x0078EEEC), school->level);   // "%d"
    RECT shifted = *row;
    gameapi::OffsetRect(&shifted, -40, 0);
    RECT levelRect = shifted;
    draw->Text(*value, &levelRect, 0, kBright, 2, 1);
    if (school->level < 100) {
        FormatPrice(panel, nullptr, school->price, value);
        draw->Text(*value, row, 0, PriceColour(school->price, panel->experience_), 2, 1);
        PriceSprite(panel, *row, 350.0f, 390.0f);
    }
}

} // namespace

void CTradePanel::DrawLists() {
    SurfaceDraw* draw = reinterpret_cast<SurfaceDraw*>(surfaces_.data_[0]);
    draw->Begin();
    draw->Clear();
    mfc::CString value;
    while (sceneObjects_.size_ > 4) RemoveSceneObjectAt(sceneObjects_.size_ - 1);

    RECT title = {0, 10, 200, 25};
    draw->Text(texts_[0], &title, 0, 0xFFFFFF, 1, 1);
    title = {0, 205, 200, 220};
    draw->Text(texts_[1], &title, 0, 0xFFFFFF, 1, 1);
    title = {200, 10, 400, 25};
    draw->Text(texts_[2], &title, 0, 0xFFFFFF, 1, 1);

    // The schools; the third one's row stays empty (science is drawn there).
    RECT row = {15, 35, 180, 50};
    for (unsigned i = 0; i < 6; ++i) {
        if (i == 2) {
            Down(&row);
            Down(&row);
            continue;
        }
        DrawSchool(this, draw, &row, &schools_.begin[i], &value);
        Down(&row);
    }
    row = {15, 65, 180, 80};
    DrawSchool(this, draw, &row, science_.begin, &value);

    // The perks held, ten rows from perkTop_; the price only where there is a next level.
    row = {15, 230, 180, 245};
    unsigned index = static_cast<unsigned>(perkTop_);
    for (int shown = 0; shown < 10; ++shown, ++index) {
        if (index >= static_cast<unsigned>(perks_.Count())) break;
        TradeRow* perk = &perks_.begin[index];
        draw->Text(perk->name, &row, 0, kBright, 0, 1);
        if (Str(perk->nextKey)->GetLength() != 0) {
            std::uint32_t colour = PriceColour(perk->price, experience_);
            FormatPrice(this, nullptr, perk->price, &value);
            draw->Text(value, &row, 0, colour, 2, 1);
            PriceSprite(this, row, 350.0f, 390.0f);
        }
        Down(&row);
    }

    // The perks to learn, 23 rows from learnableTop_ (grey when not learnable).
    row = {215, 35, 380, 50};
    index = static_cast<unsigned>(learnableTop_);
    for (int shown = 0; shown < 23; ++shown, ++index) {
        if (index >= static_cast<unsigned>(learnable_.Count())) break;
        TradeRow* perk = &learnable_.begin[index];
        std::uint32_t textColour = kGrey;
        std::uint32_t priceColour = kGrey;
        if (perk->learnable) {
            textColour = kBright;
            priceColour = PriceColour(perk->price, experience_);
        }
        draw->Text(perk->name, &row, 0, textColour, 0, 1);
        FormatPrice(this, nullptr, perk->price, &value);
        draw->Text(value, &row, 0, priceColour, 2, 1);
        PriceSprite(this, row, 550.0f, 590.0f);
        Down(&row);
    }
    draw->End();
}
namespace {
void __fastcall TradeDrawLists(CTradePanel* self) { self->DrawLists(); }
} // namespace
EI_REPLACE(0x006261A0, TradeDrawLists);

// --- 00624F10 -----------------------------------------------------------------------------------

namespace {
// 0062A0B0 (thiscall, RET 0xC): vector<TradeRecord>'s insert that reallocates; 00626F10
// (__fastcall): fills record +0x150 from the local player's unit records; 00578BA0 (on 007AFEA5):
// the local player's data (+0x34 the number of unit records).
__declspec(naked) void __fastcall RecordsInsertOverflow(TradeRecords*, void*, TradeRecord* /*pos*/, const TradeRecord*,
                                                        int /*n*/) EI_ORIGINAL(0x0062A0B0)
__declspec(naked) void __fastcall FillRecord(CTradePanel*) EI_ORIGINAL(0x00626F10)
__declspec(naked) std::uint8_t* __fastcall TradePlayerData(void*) EI_ORIGINAL(0x00578BA0)
// CStringArray::SetAtGrow (thiscall, RET 8).
__declspec(naked) void __fastcall TitlesSetAtGrow(void*, void*, int, const mfc::CString*) EI_ORIGINAL(0x006FEBFA)

int PartyRecordCount() { return *reinterpret_cast<int*>(TradePlayerData(reinterpret_cast<void*>(0x007AFEA5)) + 0x34); }

CUiRegion* AddRect(CUiWindow* window, int left, int top, int right, int bottom) {
    CUiRegion* region = window->AddRegion();
    region->CallSetRect(left, top, right, bottom);
    return region;
}
CUiRegion* AddRect(CUiWindow* window, const RECT& rect) {
    RECT copy = rect;
    return AddRect(window, copy.left, copy.top, copy.right, copy.bottom);
}
} // namespace

void CTradePanel::Build() {
    rect_ = RECT{200, 100, 600, 500};
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 200, 100, 600, 500, 5, 1) : nullptr);
    recordIndex_ = 0;
    perkTop_ = 0;
    learnableTop_ = 0;

    // Regions 0-3: the scroll arrows (held perks up / down, learnable up / down).
    AddRect(this, 360, 320, 380, 330);
    AddRect(this, 360, 480, 380, 490);
    AddRect(this, 560, 125, 580, 135);
    AddRect(this, 560, 480, 580, 490);
    // The school rows, an empty region, three more rows (two rows skipped between them), science.
    RECT row = {215, 135, 385, 150};
    for (int i = 0; i < 2; ++i) {
        AddRect(this, row);
        Down(&row);
    }
    Down(&row);
    Down(&row);
    AddRect(this, 0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        AddRect(this, row);
        Down(&row);
    }
    Down(&row);
    AddRect(this, 215, 165, 385, 180);
    // The learnable perks (23 rows) and the held perks (10 rows).
    for (int y = 135; y < 480; y += 15) AddRect(this, 415, y, 585, y + 15);
    for (int y = 330; y < 480; y += 15) AddRect(this, 215, y, 385, y + 15);

    // The four arrows' faces.
    AddSprite(0)->SetRectangle(6.0f, 100.0f, 360.0f, 320.0f, 380.0f, 330.0f, 0x82, 3, 0x96, 0xD);
    AddSprite(0)->SetRectangle(6.0f, 100.0f, 360.0f, 480.0f, 380.0f, 490.0f, 0x82, 0xD, 0x96, 3);
    AddSprite(0)->SetRectangle(6.0f, 100.0f, 560.0f, 125.0f, 580.0f, 135.0f, 0x82, 3, 0x96, 0xD);
    AddSprite(0)->SetRectangle(6.0f, 100.0f, 560.0f, 480.0f, 580.0f, 490.0f, 0x82, 0xD, 0x96, 3);

    // The titles.
    void* table = reinterpret_cast<TextGlue*>(0x007AFEA4)->Table();
    {
        mfc::CString text;
        void* titles = &textsVtable_;
        auto* size = reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + 0x1AC);
        TextGetString(table, nullptr, GameText(0x00793E58), &text);   // "skills"
        TitlesSetAtGrow(titles, nullptr, *size, &text);
        TextGetString(table, nullptr, GameText(0x00793E50), &text);   // "perks"
        TitlesSetAtGrow(titles, nullptr, *size, &text);
        TextGetString(table, nullptr, GameText(0x00793E44), &text);   // "perks_av"
        TitlesSetAtGrow(titles, nullptr, *size, &text);

        // One statistics record per party member (the vector emptied first; the records hold no
        // resources). The original pushes uninitialised stack bytes; 00626F10 writes all but
        // +0x2B and +0x39..+0x3B. Ours pushes zeros.
        records_.end = records_.begin;
        const TradeRecord blank{};
        for (int i = 0; i < PartyRecordCount(); ++i) {
            if (records_.end != records_.capacity) {
                if (records_.end) *records_.end = blank;
                ++records_.end;
            } else {
                RecordsInsertOverflow(&records_, nullptr, records_.end, &blank, 1);
            }
            recordIndex_ = i;
            FillRecord(this);
        }
        unknown_19C = -1;
        unknown_1A0 = 0;
    }
}
namespace {
void __fastcall TradeBuild(CTradePanel* self) { self->Build(); }
} // namespace
EI_REPLACE(0x00624F10, TradeBuild);
