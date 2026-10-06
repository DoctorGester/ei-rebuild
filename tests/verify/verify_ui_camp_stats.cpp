// Checks for the camp's statistics column and trade panel (ui_camp_stats.cpp). The hovers run
// side by side: the
// original into a copy of the caller's record, ours into the record; every field must agree (the
// fields neither writes keep the caller's values in both). Reached every frame the camp is open
// with nothing under the mouse (its Draw 0060F050: the trade panel's on page 1, the statistics
// column's on the other pages; ui_camp, ui_describe) and by the hero screen's draw (006159E0).
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/lib/mfc.h"

#include <cstring>

namespace {

void CopyInfo(ItemHoverInfo* to, const ItemHoverInfo& from) {
    to->kind = from.kind;
    to->index = from.index;
    to->part = from.part;
    to->weapon = from.weapon;
    to->stats = from.stats;
    to->text = from.text;
}

void Compare(const char* name, const ItemHoverInfo& e, const ItemHoverInfo& a, int x, int y, int region) {
    const char* et = e.text;
    const char* at = a.text;
    bool same = e.kind == a.kind && e.index == a.index && e.part == a.part && e.weapon == a.weapon &&
                e.stats == a.stats && std::strcmp(et, at) == 0;
    ei::verify::Check(name, true, same, "(%d,%d) region %d: kind %d/%d index %d/%d part %d/%d weapon %p/%p \"%.40s\" / \"%.40s\"",
                      x, y, region, e.kind, a.kind, e.index, a.index, e.part, a.part, e.weapon, a.weapon, et, at);
}

} // namespace

// CTradePanel::HoverInfo (00625690). PUSH EBX; PUSH ESI; MOV ESI,[ESP+0x14] (6).
static void(__fastcall* s_tradeHover)(CTradePanel*, void*, int, int, ItemHoverInfo*);
static void __fastcall TradeHoverVerify(CTradePanel* self, void*, int x, int y, ItemHoverInfo* info) {
    ItemHoverInfo expected;
    CopyInfo(&expected, *info);
    s_tradeHover(self, nullptr, x, y, &expected);
    self->CTradePanel::HoverInfo(x, y, info);
    Compare("CTradePanel::HoverInfo", expected, *info, x, y, self->RegionAt(x, y));
}
EI_VERIFY(0x00625690, TradeHoverVerify, 6, s_tradeHover);

// CHeroStatsPanel::HoverInfo (00624CB0). PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,ECX (6).
static void(__fastcall* s_statsHover)(CHeroStatsPanel*, void*, int, int, ItemHoverInfo*);
static void __fastcall StatsHoverVerify(CHeroStatsPanel* self, void*, int x, int y, ItemHoverInfo* info) {
    ItemHoverInfo expected;
    CopyInfo(&expected, *info);
    s_statsHover(self, nullptr, x, y, &expected);
    self->CHeroStatsPanel::HoverInfo(x, y, info);
    Compare("CHeroStatsPanel::HoverInfo", expected, *info, x, y, self->RegionAt(x, y));
}
EI_VERIFY(0x00624CB0, StatsHoverVerify, 6, s_statsHover);

namespace {

// What SetHero writes: the arrows' visibility, whether their regions have a tooltip, the hero,
// the title and +0x170.
struct HeroState {
    int arrow0, arrow1;
    int tip0, tip1;
    int hero;
    int points;
};
HeroState Snapshot(CHeroStatsPanel* self) {
    return HeroState{reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[0])->visible_,
                     reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[1])->visible_,
                     self->regions_.data[0]->tip_ ? 1 : 0, self->regions_.data[1]->tip_ ? 1 : 0,
                     self->heroIndex_, self->points_};
}
mfc::CString& Title(CHeroStatsPanel* self) { return *reinterpret_cast<mfc::CString*>(&self->title_); }

} // namespace

// CHeroStatsPanel::SetHero (006232D0): the original first (its tooltips replaced again by ours,
// its drawing redone by ours' DrawStats), the written fields recorded and put back, then ours.
// Reached by the camp's Create and hero arrows (ShowHero 0060F830; ui_camp regions 0 / 1) and the
// hero screen. PUSH ESI; MOV ESI,ECX; MOV ECX,imm32 (8).
static void(__fastcall* s_setHero)(CHeroStatsPanel*, void*, int, const char*);
static void __fastcall SetHeroVerify(CHeroStatsPanel* self, void*, int hero, const char* title) {
    HeroState before = Snapshot(self);
    mfc::CString beforeTitle = Title(self);
    s_setHero(self, nullptr, hero, title);
    HeroState expected = Snapshot(self);
    mfc::CString expectedTitle = Title(self);
    self->heroIndex_ = before.hero;
    self->points_ = before.points;
    Title(self) = beforeTitle;
    self->CHeroStatsPanel::SetHero(hero, title);
    HeroState actual = Snapshot(self);
    const char* et = expectedTitle;
    const char* at = Title(self);
    bool same = std::memcmp(&expected, &actual, sizeof(HeroState)) == 0 && std::strcmp(et, at) == 0;
    ei::verify::Check("CHeroStatsPanel::SetHero", true, same, "hero %d arrows %d,%d/%d,%d tips %d,%d/%d,%d points %d/%d \"%.30s\" / \"%.30s\"",
                      hero, expected.arrow0, expected.arrow1, actual.arrow0, actual.arrow1, expected.tip0, expected.tip1,
                      actual.tip0, actual.tip1, expected.points, actual.points, et, at);
}
EI_VERIFY(0x006232D0, SetHeroVerify, 8, s_setHero);
