// Checks for unit_armour_layers.cpp (U9). All five compare against the original:
// - AddArmourLayer (005230C0) and RebuildArmourLayers (00523300) change only the statistics: the
//   original runs on a scratch copy of them, ours on the real ones, and all 0x704 bytes must match
//   (the sums are single float additions, exact). The original RebuildArmourLayers calls
//   005230C0, so on a scratch copy it goes through AddArmourLayer's wrapper too.
// - PackBodyProtection (00523020) writes only out[1..7]: the original into a copy of out, ours
//   into out; all eight shorts must match.
// - AttributeWithItems (00525E90) and SpellSchoolSkill (00527EB0) are pure: both run, results
//   within a few ULPs (CheckNear).
// Reached by: 005230C0 the camp's statistics panel (ui_camp.txt) and every RebuildArmourLayers;
// 00523300 CUnitServer slot 27 (armour taken off, 00530840); 00523020 CUnitServer slot 22, the
// stat refresh (00553500, every scenario with units); 00527EB0 spell orders (OrderCast /
// OrderCastAt), the camp's best party skill, the hero screen and item info (ui_camp.txt,
// ui_hero_screen.txt, ui_battle_spells.txt); 00525E90 skill use and taming only (probably no
// scenario).
#include "verify.h"

#include "../../src/game/unit.h"

#include <cstdint>
#include <cstring>

namespace {
// Reports the first differing byte of two statistics blocks.
void CompareStats(const char* name, const UnitStats* expected, const UnitStats* actual) {
    const auto* a = reinterpret_cast<const std::uint8_t*>(expected);
    const auto* b = reinterpret_cast<const std::uint8_t*>(actual);
    int first = -1;
    for (int i = 0; i < static_cast<int>(sizeof(UnitStats)); ++i) {
        if (a[i] != b[i]) {
            first = i;
            break;
        }
    }
    ei::verify::Check(name, -1, first, "first differing byte +0x%X: %02X vs %02X", first,
                      first >= 0 ? a[first] : 0, first >= 0 ? b[first] : 0);
}
} // namespace

// 005230C0: SUB ESP,0x20; PUSH EBX; MOV EBX,[ESP+0x28] (8 bytes).
static void(__fastcall* s_addArmourLayer)(UnitStats*, void*, CEngineObject*);
static void __fastcall AddArmourLayerVerify(UnitStats* self, void*, CEngineObject* armour) {
    static UnitStats scratch;
    std::memcpy(&scratch, self, sizeof(UnitStats));
    s_addArmourLayer(&scratch, nullptr, armour);
    self->AddArmourLayer(armour);
    CompareStats("UnitStats::AddArmourLayer", &scratch, self);
}
EI_VERIFY(0x005230C0, AddArmourLayerVerify, 8, s_addArmourLayer);

// 00523300: PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH EBX; MOV EBX,ECX (7 bytes).
static void(__fastcall* s_rebuildArmourLayers)(UnitStats*, void*, CUnit*);
static void __fastcall RebuildArmourLayersVerify(UnitStats* self, void*, CUnit* unit) {
    static UnitStats scratch;   // not shared with AddArmourLayerVerify, which the original calls
    std::memcpy(&scratch, self, sizeof(UnitStats));
    s_rebuildArmourLayers(&scratch, nullptr, unit);
    self->RebuildArmourLayers(unit);
    CompareStats("UnitStats::RebuildArmourLayers", &scratch, self);
}
EI_VERIFY(0x00523300, RebuildArmourLayersVerify, 7, s_rebuildArmourLayers);

// 00523020: PUSH EBP; MOV EBP,ESP; PUSH ECX; MOV EAX,[EBP+8] (7 bytes).
static void(__fastcall* s_packBodyProtection)(UnitStats*, void*, CUnit*, const UnitVitals*, std::int16_t*);
static void __fastcall PackBodyProtectionVerify(UnitStats* self, void*, CUnit* unit, const UnitVitals* vitals,
                                                std::int16_t* out) {
    std::int16_t expected[8];
    std::memcpy(expected, out, sizeof(expected));
    s_packBodyProtection(self, nullptr, unit, vitals, expected);
    self->PackBodyProtection(unit, vitals, out);
    for (int i = 0; i < 8; ++i) {
        ei::verify::Check("UnitStats::PackBodyProtection", static_cast<int>(expected[i]), static_cast<int>(out[i]),
                          "out[%d], protection %g", i, i > 0 ? vitals->protection[i - 1].current : 0.0f);
    }
}
EI_VERIFY(0x00523020, PackBodyProtectionVerify, 7, s_packBodyProtection);

// 00525E90: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC; PUSH ESI (7 bytes).
static double(__fastcall* s_attributeWithItems)(UnitStats*, void*, int, CUnit*);
static double __fastcall AttributeWithItemsVerify(UnitStats* self, void*, int index, CUnit* unit) {
    double original = s_attributeWithItems(self, nullptr, index, unit);
    double ours = self->AttributeWithItems(index, unit);
    ei::verify::CheckNear("UnitStats::AttributeWithItems", original, ours, "index %d: %g vs %g", index, original,
                          ours);
    return original;
}
EI_VERIFY(0x00525E90, AttributeWithItemsVerify, 7, s_attributeWithItems);

// 00527EB0: MOV EAX,[ESP+4]; SUB ESP,8 (7 bytes).
static double(__fastcall* s_spellSchoolSkill)(UnitStats*, void*, int);
static double __fastcall SpellSchoolSkillVerify(UnitStats* self, void*, int school) {
    double original = s_spellSchoolSkill(self, nullptr, school);
    double ours = self->SpellSchoolSkill(school);
    ei::verify::CheckNear("UnitStats::SpellSchoolSkill", original, ours, "school %d: %g vs %g", school, original,
                          ours);
    return original;
}
EI_VERIFY(0x00527EB0, SpellSchoolSkillVerify, 7, s_spellSchoolSkill);
