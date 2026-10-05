// CHudSpellColumn::SetUnit (0063E820, ui_spell_column.cpp) runs side by side: it drops every
// sprite and region first, so the original runs, its result is recorded (fields, each sprite's
// position, orientation, texture corner and colour, each region's rectangle and its tooltip's
// lines, size and arguments), then ours runs over it and both must agree. Reached when the
// selection changes (00637DC0) and from the battle update (00638280): every scenario with a game.
#include "verify.h"

#include "../../src/game/ui.h"

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

std::vector<std::uint8_t> SpellColumnState(CHudSpellColumn* panel) {
    std::vector<std::uint8_t> out;
    Append(out, &panel->count_, 4);
    Append(out, &panel->selected_, 4);
    Append(out, &panel->unit_, 4);
    Append(out, &panel->sceneObjects_.size_, 4);
    for (int i = 0; i < panel->sceneObjects_.size_; ++i) {
        auto* sprite = reinterpret_cast<const std::uint8_t*>(panel->sceneObjects_.data_[i]);
        if (!sprite) continue;
        Append(out, sprite + 0x1C, 12);    // local position
        Append(out, sprite + 0x34, 16);    // local rotation
        Append(out, sprite + 0xCC, 16);    // texture corner, -1, colour
    }
    Append(out, &panel->regions_.size_, 4);
    for (int i = 0; i < panel->regions_.size_; ++i) {
        const CUiRegion* region = panel->regions_.data_[i];
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
        Append(out, tip + 0x164, 8);       // the two arguments
        Append(out, tip + 0x174, 8);       // width, height
    }
    return out;
}

} // namespace

// 0063E820: PUSH -1; PUSH imm32 (7).
static void(__fastcall* s_spellsSetUnit)(CHudSpellColumn*, void*, CUnit*);
static void __fastcall SpellColumnSetUnitVerify(CHudSpellColumn* self, void*, CUnit* unit) {
    s_spellsSetUnit(self, nullptr, unit);
    std::vector<std::uint8_t> expected = SpellColumnState(self);
    self->SetUnit(unit);
    std::vector<std::uint8_t> actual = SpellColumnState(self);
    int at = -1;
    if (expected.size() != actual.size()) {
        at = -2;
    } else {
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (expected[i] != actual[i]) {
                at = static_cast<int>(i);
                break;
            }
    }
    ei::verify::Check("CHudSpellColumn::SetUnit", -1, at, "state byte %d differs (sizes %d/%d, sprites %d)", at,
                      static_cast<int>(expected.size()), static_cast<int>(actual.size()), self->count_);
}
EI_VERIFY(0x0063E820, SpellColumnSetUnitVerify, 7, s_spellsSetUnit);
