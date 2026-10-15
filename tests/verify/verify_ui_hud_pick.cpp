// Checks for the HUD buttons that start or drop a pending action (ui_hud_pick.cpp): a spell
// button (0063EC90), an action button (0063FB50) and a quick item slot (0063DD70).
//
// They run side by side: the original first (through the trampoline), the state it leaves
// recorded and the state before the call put back, then ours, and the two states compared. Their
// only effects besides the click sound are that state: the battle screen's pending action
// (+0x17C mode, +0x180 slot, +0x184 item), each of the quick item, spell and action panels'
// highlighted button (+0x150), and the colour (+0xD8) and scale (+0x23C, +0x250, +0x264) of the
// button sprites the highlights change. The click sound plays twice in the verify build.
// Reached by ui_battle_spells (spell and action buttons), ui_quick_items (quick item slots and
// the spell button) and the key handler 0063A560.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/unit.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// A button sprite's colour and scale, as dword bits.
struct SpriteState {
    std::uint32_t colour;
    std::uint32_t scale[3];
};

struct PickState {
    int action;
    int slot;
    int item;
    int selected[3];                 // quick items, spell column, action column
    std::vector<SpriteState> sprites;
};

// Floats are copied as dwords (no FPU load on the way).
std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
void SetBits(float& f, std::uint32_t u) { std::memcpy(&f, &u, 4); }

// The sprites a highlight may change: the quick item panel's region sprites (as 0063CD20 finds
// them), then every spell and action button.
std::vector<CUiMesh*> Sprites(CBattleScreen* screen) {
    std::vector<CUiMesh*> out;
    CHudQuickItems* quick = screen->quickItems_;
    for (int slot = 0; slot < 8; ++slot) {
        int index = quick->regionItem_[slot];
        if (index >= 0 && index < quick->shownCount_)
            out.push_back(reinterpret_cast<CUiMesh*>(quick->sceneObjects_.data[index]));
    }
    for (int i = 0; i < screen->spellColumn_->count_; ++i)
        out.push_back(reinterpret_cast<CUiMesh*>(screen->spellColumn_->sceneObjects_.data[i]));
    for (int i = 0; i < screen->actionColumn_->count_; ++i)
        out.push_back(reinterpret_cast<CUiMesh*>(screen->actionColumn_->sceneObjects_.data[i]));
    return out;
}

PickState Snapshot(CBattleScreen* screen) {
    PickState s{screen->pendingAction_,
                screen->pendingSlot_,
                screen->pendingItem_,
                {screen->quickItems_->selected_, screen->spellColumn_->selected_, screen->actionColumn_->selected_},
                {}};
    for (CUiMesh* sprite : Sprites(screen))
        s.sprites.push_back(SpriteState{sprite->colour_, {Bits(sprite->ScaleX()), Bits(sprite->ScaleY()), Bits(sprite->ScaleZ())}});
    return s;
}

void Restore(CBattleScreen* screen, const PickState& s) {
    screen->pendingAction_ = s.action;
    screen->pendingSlot_ = s.slot;
    screen->pendingItem_ = s.item;
    screen->quickItems_->selected_ = s.selected[0];
    screen->spellColumn_->selected_ = s.selected[1];
    screen->actionColumn_->selected_ = s.selected[2];
    std::vector<CUiMesh*> sprites = Sprites(screen);
    for (std::size_t i = 0; i < sprites.size() && i < s.sprites.size(); ++i) {
        sprites[i]->colour_ = s.sprites[i].colour;
        SetBits(sprites[i]->ScaleX(), s.sprites[i].scale[0]);
        SetBits(sprites[i]->ScaleY(), s.sprites[i].scale[1]);
        SetBits(sprites[i]->ScaleZ(), s.sprites[i].scale[2]);
    }
}

// The first sprite whose state differs, -1 when all agree (-2: a different count).
int FirstSpriteDiffering(const PickState& a, const PickState& b) {
    if (a.sprites.size() != b.sprites.size()) return -2;
    for (std::size_t i = 0; i < a.sprites.size(); ++i)
        if (std::memcmp(&a.sprites[i], &b.sprites[i], sizeof(SpriteState)) != 0) return static_cast<int>(i);
    return -1;
}

// Runs the original, then ours from the same state, and compares what each left.
template <typename Original, typename Ours>
void SideBySide(const char* name, CBattleScreen* screen, int index, Original original, Ours ours) {
    PickState before = Snapshot(screen);
    original();
    PickState expected = Snapshot(screen);
    Restore(screen, before);
    ours();
    PickState actual = Snapshot(screen);
    int sprite = FirstSpriteDiffering(expected, actual);
    bool match = expected.action == actual.action && expected.slot == actual.slot && expected.item == actual.item &&
                 std::memcmp(expected.selected, actual.selected, sizeof(expected.selected)) == 0 && sprite == -1;
    ei::verify::Check(name, true, match,
                      "index %d: mode %d slot %d item %d -> %d/%d %d/%d %d/%d, highlights %d %d %d -> %d %d %d / %d %d %d, "
                      "sprite %d",
                      index, before.action, before.slot, before.item, expected.action, actual.action, expected.slot,
                      actual.slot, expected.item, actual.item, before.selected[0], before.selected[1],
                      before.selected[2], expected.selected[0], expected.selected[1], expected.selected[2],
                      actual.selected[0], actual.selected[1], actual.selected[2], sprite);
}

} // namespace

// 0063EC90: PUSH EBX; PUSH EBP; MOV EBP,[ESP+0xC]; PUSH ESI (7).
static void(__fastcall* s_spellClick)(CHudSpellColumn*, void*, int);
static void __fastcall SpellClickVerify(CHudSpellColumn* self, void*, int index) {
    SideBySide(
        "CHudSpellColumn::Click", self->owner_, index, [&] { s_spellClick(self, nullptr, index); },
        [&] { self->CHudSpellColumn::Click(index); });
}
EI_VERIFY(0x0063EC90, SpellClickVerify, 7, s_spellClick);

// 0063FB50: PUSH EBX; PUSH EBP; MOV EBP,[ESP+0xC]; PUSH ESI (7).
static void(__fastcall* s_actionSelect)(CHudActionColumn*, void*, int);
static void __fastcall ActionSelectVerify(CHudActionColumn* self, void*, int index) {
    SideBySide(
        "CHudActionColumn::Select", self->owner_, index, [&] { s_actionSelect(self, nullptr, index); },
        [&] { self->CHudActionColumn::Select(index); });
}
EI_VERIFY(0x0063FB50, ActionSelectVerify, 7, s_actionSelect);

// 0063DD70: PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,ECX; XOR EBP,EBP (7).
static void(__fastcall* s_quickPick)(CHudQuickItems*, void*, int);
static void __fastcall QuickPickVerify(CHudQuickItems* self, void*, int slot) {
    SideBySide(
        "CHudQuickItems::Pick", self->owner_, slot, [&] { s_quickPick(self, nullptr, slot); },
        [&] { self->CHudQuickItems::Pick(slot); });
}
EI_VERIFY(0x0063DD70, QuickPickVerify, 7, s_quickPick);

// 0063E020 / 0063EF60 use the item or spell at once, so they run ours only: an early return (no
// unit, no item, index out of range) leaves the state as it was; every other path ends with the
// pending action dropped (mode 0, slot and item -1) and no button highlighted. Reached with the
// interface's double-click flag +0x7F on a quick item release (0063DB20) or a spell button
// release, or Ctrl / Alt with a spell or quick item key (0063A560); no scenario holds those yet.
namespace {
void* s_usedAtOnce;
template <typename Ours>
void UsedAtOnce(const char* name, CBattleScreen* screen, int index, bool early, Ours ours) {
    PickState before = Snapshot(screen);
    ours();
    PickState after = Snapshot(screen);
    bool same = before.action == after.action && before.slot == after.slot && before.item == after.item &&
                std::memcmp(before.selected, after.selected, sizeof(before.selected)) == 0;
    bool dropped = after.action == 0 && after.slot == -1 && after.item == -1 && after.selected[0] == -1 &&
                   after.selected[1] == -1 && after.selected[2] == -1;
    ei::verify::Check(name, true, early ? same : dropped, "index %d early %d: mode %d slot %d item %d", index, early,
                      after.action, after.slot, after.item);
}
} // namespace

static void __fastcall QuickUseNowVerify(CHudQuickItems* self, void*, int slot) {
    bool early = !self->unit_ || !self->unit_->GetQuickItem(slot);
    UsedAtOnce("CHudQuickItems::UseNow", self->owner_, slot, early, [&] { self->CHudQuickItems::UseNow(slot); });
}
EI_VERIFY(0x0063E020, QuickUseNowVerify, 0, s_usedAtOnce);

static void __fastcall SpellClickAlternateVerify(CHudSpellColumn* self, void*, int index) {
    bool early = !self->unit_ || index < 0 || index >= self->count_;
    UsedAtOnce("CHudSpellColumn::ClickAlternate", self->owner_, index, early,
               [&] { self->CHudSpellColumn::ClickAlternate(index); });
}
EI_VERIFY(0x0063EF60, SpellClickAlternateVerify, 0, s_usedAtOnce);
