// The battle screen panels' tooltips (ui_hud_tips.cpp), all side by side against the original:
//
// - CHudWeaponBar::SetUnit (0063C180) and CHudActionColumn::SetUnit (0063F800) drop every sprite
//   and reset their regions' tips first, so the original runs, its result is recorded (fields,
//   each sprite's position, orientation, texture corner and colour, each region's rectangle and
//   its tooltip's lines and, for a tip with text, its size and arguments; the action column's
//   action array), then ours runs
//   over it and both must agree. Reached when the selection changes (00637DC0) and from the
//   battle update (00638280): every scenario with a game (ui_hud, ui_screens, ...).
// - CHudQuickItems::ItemTip (0063CEE0): the original writes into a copy of the caller's CString,
//   ours into the caller's; the texts must match. Reached from the quick item panel's unit
//   (0063D2B0) and its every-frame tip refresh (0063D9D0) for a unit with quick items (the hero's
//   potions in every new game).
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

template <typename T>
T Get(const void* p, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return v;
}

void Append(std::vector<std::uint8_t>& out, const void* p, std::size_t n) {
    auto* b = static_cast<const std::uint8_t*>(p);
    out.insert(out.end(), b, b + n);
}
void AppendText(std::vector<std::uint8_t>& out, const char* s) {
    if (!s) s = "";
    Append(out, s, std::strlen(s) + 1);
}

// The panel's fields +0x14C..+0x157, its sprites and its regions with their tips.
std::vector<std::uint8_t> PanelState(CUiWindow* panel) {
    std::vector<std::uint8_t> out;
    Append(out, reinterpret_cast<const std::uint8_t*>(panel) + 0x14C, 12);
    Append(out, &panel->sceneObjects_.size, 4);
    for (int i = 0; i < panel->sceneObjects_.size; ++i) {
        auto* sprite = reinterpret_cast<const std::uint8_t*>(panel->sceneObjects_.data[i]);
        if (!sprite) continue;
        Append(out, sprite + 0x1C, 12);    // local position
        Append(out, sprite + 0x34, 16);    // local rotation
        Append(out, sprite + 0xCC, 16);    // texture corner, -1, colour
    }
    Append(out, &panel->regions_.size, 4);
    for (int i = 0; i < panel->regions_.size; ++i) {
        const CUiRegion* region = panel->regions_.data[i];
        if (!region) continue;
        Append(out, &region->bounds_, sizeof(region->bounds_));
        const std::uint8_t* tip = reinterpret_cast<const std::uint8_t*>(region->tip_);
        std::uint32_t hasTip = tip ? 1 : 0;
        Append(out, &hasTip, 4);
        if (!tip) continue;
        int lines = Get<int>(tip, 0x150);
        Append(out, &lines, 4);
        char** text = Get<char**>(tip, 0x14C);
        for (int l = 0; l < lines; ++l) AppendText(out, text[l]);
        // The tooltip's constructor (0065E320) writes the two arguments and the size only for a
        // non-empty text; a cleared tip (SetTip(0, 1, 0): a fresh 0x184-byte allocation with an
        // empty text and no lines) keeps leftover heap bytes there.
        if (lines == 0) continue;
        Append(out, tip + 0x164, 8);       // the two arguments
        Append(out, tip + 0x174, 8);       // width, height
    }
    return out;
}

// The first differing byte, -2 for different sizes, -1 when equal.
int FirstDifference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) return -2;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return static_cast<int>(i);
    return -1;
}

} // namespace

// 0063C180: PUSH -1; PUSH imm32 (7).
static void(__fastcall* s_weaponBarSetUnit)(CHudWeaponBar*, void*, CUnit*);
static void __fastcall WeaponBarSetUnitVerify(CHudWeaponBar* self, void*, CUnit* unit) {
    s_weaponBarSetUnit(self, nullptr, unit);
    std::vector<std::uint8_t> expected = PanelState(self);
    self->SetUnit(unit);
    std::vector<std::uint8_t> actual = PanelState(self);
    int at = FirstDifference(expected, actual);
    ei::verify::Check("CHudWeaponBar::SetUnit", -1, at, "state byte %d differs (sizes %d/%d, slots %d)", at,
                      static_cast<int>(expected.size()), static_cast<int>(actual.size()), self->count_);
}
EI_VERIFY(0x0063C180, WeaponBarSetUnitVerify, 7, s_weaponBarSetUnit);

// 0063F800: MOV EAX,FS:[0]; PUSH -1 (8).
static void(__fastcall* s_actionsSetUnit)(CHudActionColumn*, void*, CUnit*, int);
static std::vector<std::uint8_t> ActionState(CHudActionColumn* self) {
    std::vector<std::uint8_t> out = PanelState(self);
    const int* data = reinterpret_cast<const int*>(self->actions_.data);
    int size = self->actions_.size;
    Append(out, &size, 4);
    if (data && size > 0) Append(out, data, static_cast<std::size_t>(size) * 4);
    return out;
}
static void __fastcall ActionColumnSetUnitVerify(CHudActionColumn* self, void*, CUnit* unit, int extra) {
    s_actionsSetUnit(self, nullptr, unit, extra);
    std::vector<std::uint8_t> expected = ActionState(self);
    self->SetUnit(unit, extra);
    std::vector<std::uint8_t> actual = ActionState(self);
    int at = FirstDifference(expected, actual);
    ei::verify::Check("CHudActionColumn::SetUnit", -1, at, "state byte %d differs (sizes %d/%d, buttons %d, extra %d)",
                      at, static_cast<int>(expected.size()), static_cast<int>(actual.size()), self->count_,
                      extra & 0xFF);
}
EI_VERIFY(0x0063F800, ActionColumnSetUnitVerify, 8, s_actionsSetUnit);

// 0063CEE0: MOV EAX,FS:[0]; PUSH -1 (8).
static void(__fastcall* s_itemTip)(CHudQuickItems*, void*, unsigned, void*);
static void __fastcall ItemTipVerify(CHudQuickItems* self, void*, unsigned index, void* out) {
    auto* text = static_cast<mfc::CString*>(out);
    mfc::CString expected(*text);   // untouched when the slot is empty
    s_itemTip(self, nullptr, index, &expected);
    self->ItemTip(index, text);
    const char* a = expected;
    const char* b = *text;
    ei::verify::Check("CHudQuickItems::ItemTip", 0, std::strcmp(a, b), "slot %u: \"%s\" / \"%s\"", index, a, b);
}
EI_VERIFY(0x0063CEE0, ItemTipVerify, 8, s_itemTip);
