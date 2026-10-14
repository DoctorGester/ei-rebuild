// The battle HUD's unit info texts (00647040, thiscall on the info panel, RET 4): the lines drawn
// for the unit under the cursor or selected (called by 006462A0). Each line is a label from the
// panel's label table followed by values, drawn with the panel surface's text routine and moved
// down 15 pixels. See docs/notes/ui.md ("Unit info texts").
#include "console.h"
#include "ui.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

const char* const kValuePair = Text(0x00792F08);     // "%s %d/%d"
const char* const kMoveKey = Text(0x00794610);       // "move_%d"
const char* const kSpeedLine = Text(0x00794604);     // "%s %.1f %s"
const char* const kValueUnit = Text(0x00792F48);     // "%s %d%s"
const char* const kValue = Text(0x00792F14);         // "%s %d"
const char* const kDamageKey = Text(0x00792F70);     // "dmg_%d"
const char* const kDamageLine = Text(0x00792EF0);    // "%s %d-%d %s"
const char* const kSpace = Text(0x0078DA40);         // " "
const char* const kStronger = Text(0x007945F4);      // "consider_more%d"
const char* const kWeaker = Text(0x007945E4);        // "consider_less%d"
const char* const kTwoTexts = Text(0x00792F34);      // "%s %s"
const char* const kCampExp = Text(0x00793858);       // "camp_current_exp"

constexpr std::uint32_t kTextColour = 0xA7D7E4;
constexpr int kLineHeight = 15;
constexpr int kLevelDifferenceLimit = 25;

// The panel surface's text draw [005FDE20, Rebirth-patched: text, rectangle, 0, colour, 0, 1].
// The text manager (007AF788): the text of a key [004E0020: key, out].
// CUnit [thiscall]: the move speed of a mode [0052FAF0] or the current one [0052FA20] (in ST0),
// the prototype [00530930], the level [00532D40].
__declspec(naked) double __fastcall MoveSpeedFor(void*, void*, int) EI_ORIGINAL(0x0052FAF0)
__declspec(naked) double __fastcall MoveSpeed(void*) EI_ORIGINAL(0x0052FA20)
// The world (007AF5B8): the time of day [0055FC50, ST0].
double __fastcall WorldDayTime(void* a0) { return WorldDayTimeOriginal(a0); }
// The unit's parameters (+0x330) [thiscall]: the vision range at a time of day [005271E0, ST0;
// UnitParamsVisionRangeOriginal, originals_units.h]; the description texts [005270E0: unit +0x17C, out, name is a text key].
// 004613A0: whether an id is a text key [__fastcall].
// 005F13A0 [__fastcall, Rebirth-patched]: the description as lines [description, out, 1].
__declspec(naked) void __fastcall DescriptionLines(void*, console::CStringArray*, int) EI_ORIGINAL(0x005F13A0)
// The session (007AFE20): a multiplayer client [0057F440, AL].

// What the unit's controller (+0x240, slot 50) reports for the info panel (0x20 bytes).
struct CombatInfo {
    int unknown_00;
    std::uint32_t damageType;       // +0x04 below 7: "dmg_<n>" names it
    int unknown_08;
    float damageMin;                // +0x0C
    float damageMax;                // +0x10
    int unknown_14;
    int attack;                     // +0x18
    int defence;                    // +0x1C
};
static_assert(sizeof(CombatInfo) == 0x20);
constexpr int kCombatInfoSlot = 50;

// The speed shown: (00742408 * speed) * 00742AEC on the FPU, the speed from 0052FAF0 (mode 3) for
// a unit outside the group, else from 0052FA20 (`unit` in ECX, the flag in EDX).
__declspec(naked) double __fastcall ShownSpeed(void* /*unit*/, int /*outsideGroup*/) {
    __asm {
        test edx, edx
        je in_group
        push 3
        call MoveSpeedFor
        jmp scale
    in_group:
        call MoveSpeed
    scale:
        fld dword ptr ds:[0x00742408]
        fmul st, st(1)
        fmul dword ptr ds:[0x00742AEC]
        fstp st(1)
        ret
    }
}

struct Panel {
    void* surface;
    const mfc::CString* labels;
    RECT rect;
    mfc::CString text;

    // Draws `text` and moves down a line.
    void DrawLine() { DrawLine(text); }
    void DrawLine(const char* line) {
        SurfaceDrawText(surface, nullptr, line, &rect, 0, kTextColour, 0, 1);
        gameapi::OffsetRect(&rect, 0, kLineHeight);
    }
    // "<label> <line>" when the line is not empty.
    void DrawLabelled(int label, const mfc::CString& line) {
        text = labels[label] + kSpace + line;
        DrawLine();
    }
};

// Whether `unit` is not in the group (+0x148: +0x16C units, +0x170 count).
bool OutsideGroup(CHudUnitPanel* self, void* unit) {
    char* group = reinterpret_cast<char*>(self->owner_);
    int count = At<int>(group, 0x170);
    void** units = At<void**>(group, 0x16C);
    for (int i = 0; i < count; ++i) {
        if (units[i] == unit) return false;
    }
    return true;
}

bool IsEmpty(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2] == 0; }

} // namespace

// 00647040. Draws the info lines of `unit`.
void __fastcall DrawUnitInfo(char* self, void*, char* unit) {
    void* texts = *reinterpret_cast<void**>(0x007AF788);
    auto* hud = reinterpret_cast<CHudUnitPanel*>(self);
    auto* unitObject = reinterpret_cast<CUnit*>(unit);
    UnitParams* params = unitObject->Params();
    char* stats = unit + 0x330;
    mfc::CString suffix;
    Panel panel;
    panel.surface = *hud->surfaces_.data;
    panel.labels = At<const mfc::CString*>(self, 0x1A0);
    panel.rect = {5, 0x23, 0x9B, 0x32};

    std::int16_t health = unitObject->hitPoints;
    panel.text.Format(kValuePair, static_cast<const char*>(panel.labels[0]), health > 0 ? static_cast<int>(health) : 0,
                      static_cast<int>(unitObject->maxHitPoints));
    panel.DrawLine();
    bool outside = OutsideGroup(hud, unit);
    panel.text.Format(kValuePair, static_cast<const char*>(panel.labels[1]), static_cast<int>(unitObject->mana),
                      static_cast<int>(unitObject->maxMana));
    panel.DrawLine();

    std::uint8_t moveBonus = params->movementKind;
    if (moveBonus > 1) {
        panel.text.Format(kMoveKey, moveBonus - 1);
        TextsGetString(texts, nullptr, panel.text, &suffix);
    } else {
        suffix.Empty();
    }
    double speed = ShownSpeed(unit, outside);
    panel.text.Format(kSpeedLine, static_cast<const char*>(panel.labels[2]), speed, static_cast<const char*>(suffix));
    panel.DrawLine();

    float dayTime = static_cast<float>(WorldDayTime(reinterpret_cast<void*>(0x007AF5B8)));
    int vision = x87::fistp(static_cast<float>(UnitParamsVisionRangeOriginal(stats, nullptr, dayTime)));
    panel.text.Format(kValueUnit, static_cast<const char*>(panel.labels[6]), vision, static_cast<const char*>(panel.labels[13]));
    panel.DrawLine();

    CombatInfo combat;
    void* controller = unitObject->controller;
    reinterpret_cast<void(__fastcall*)(void*, void*, CombatInfo*)>((*static_cast<void***>(controller))[kCombatInfoSlot])(
        controller, nullptr, &combat);
    panel.text.Format(kValue, static_cast<const char*>(panel.labels[3]), combat.attack);
    panel.DrawLine();
    panel.text.Format(kValue, static_cast<const char*>(panel.labels[4]), combat.defence);
    panel.DrawLine();
    if (combat.damageType < 7) {
        panel.text.Format(kDamageKey, combat.damageType);
        TextsGetString(texts, nullptr, panel.text, &suffix);   // else the move text stays
    }
    int damageMax = static_cast<int>(combat.damageMax);
    int damageMin = static_cast<int>(combat.damageMin);
    panel.text.Format(kDamageLine, static_cast<const char*>(panel.labels[9]), damageMin, damageMax,
                      static_cast<const char*>(suffix));
    panel.DrawLine();
    panel.text.Format(kValue, static_cast<const char*>(panel.labels[15]), static_cast<int>(params->rate));
    panel.DrawLine();

    std::uint8_t description[0x1C];
    UnitParameterBlock(stats, nullptr, unit + 0x17C, description, IsTextKeyOriginal(unitObject->id_));
    console::CStringArray lines;
    DescriptionLines(description, &lines, 1);
    panel.DrawLabelled(10, lines[0]);
    if (outside) {
        if (!IsEmpty(lines[1])) panel.DrawLabelled(11, lines[1]);
        auto* prototype = static_cast<const UnitPrototypeRecord*>(::UnitPrototypeOf(unit));
        if (prototype->spellNames.end != prototype->spellNames.begin) panel.DrawLine(panel.labels[5]);
        char* group = reinterpret_cast<char*>(hud->owner_);
        if (At<int>(group, 0x170) > 0) {
            void* leader = At<void**>(group, 0x16C)[0];
            int difference = UnitLevel(reinterpret_cast<CUnit*>(unit)) - UnitLevel(static_cast<CUnit*>(leader));
            if (difference > kLevelDifferenceLimit) {
                suffix.Format(kStronger, kLevelDifferenceLimit);
            } else if (difference < -kLevelDifferenceLimit) {
                suffix.Format(kWeaker, kLevelDifferenceLimit);
            } else if (difference >= 0) {
                suffix.Format(kStronger, difference);
            } else {
                suffix.Format(kWeaker, -difference);
            }
            TextsGetString(texts, nullptr, suffix, &suffix);
            panel.text.Format(kTwoTexts, static_cast<const char*>(panel.labels[16]), static_cast<const char*>(suffix));
            panel.DrawLine();
        }
    } else if (!IsEmpty(lines[2])) {
        panel.DrawLabelled(12, lines[2]);
    }

    if (IsNetworkClientOriginal() && !outside) {
        TextsGetString(texts, nullptr, kCampExp, &suffix);
        panel.text.Format(kValue, static_cast<const char*>(suffix), params->unspentExperience);
        panel.DrawLine();
    }
}
EI_REPLACE(0x00647040, DrawUnitInfo);
