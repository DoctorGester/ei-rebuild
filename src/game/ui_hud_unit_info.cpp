// The battle HUD's unit info texts (00647040, thiscall on the info panel, RET 4): the lines drawn
// for the unit under the cursor or selected (called by 006462A0). Each line is a label from the
// panel's label table followed by values, drawn with the panel surface's text routine and moved
// down 15 pixels. See docs/notes/ui.md ("Battle HUD panels").
#include "console.h"
#include "game_strings.h"
#include "object_tables.h"
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
#include "world.h"
#include "game_data.h"

#include <cstdint>

namespace {


const char* const kValuePair = "%s %d/%d";
const char* const kMoveKey = "move_%d";
const char* const kSpeedLine = "%s %.1f %s";
const char* const kValueUnit = "%s %d%s";
const char* const kValue = "%s %d";
const char* const kDamageKey = "dmg_%d";
const char* const kDamageLine = "%s %d-%d %s";
const char* const kSpace = " ";
const char* const kStronger = "consider_more%d";
const char* const kWeaker = "consider_less%d";
const char* const kTwoTexts = "%s %s";
const char* const kCampExp = "camp_current_exp";

constexpr std::uint32_t kTextColour = 0xA7D7E4;
constexpr int kLineHeight = 15;
constexpr int kLevelDifferenceLimit = 25;

// The panel surface's text draw [005FDE20, Rebirth-patched: text, rectangle, 0, colour, 0, 1].
// The text manager (007AF788): the text of a key [004E0020: key, out].
// CUnit [thiscall]: the move speed of a mode [0052FAF0] or the current one [0052FA20] (in ST0),
// the prototype [00530930], the level [00532D40] (rewritten, called directly).
// The world (007AF5B8): the time of day [0055FC50, ST0].
double __fastcall WorldDayTime(void* a0) { return static_cast<CWorld*>(a0)->NightLevel(); }
// The unit's parameters (+0x330) [thiscall]: the vision range at a time of day [005271E0, ST0];
// the description texts [005270E0: unit +0x17C, out, name is a text key].
// 004613A0: whether an id is a text key [__fastcall].
// 005F13A0 [__fastcall, Rebirth-patched]: the description as lines [description, out, 1].
__declspec(naked) void __fastcall DescriptionLines(void*, console::CStringArray*, int) EI_ORIGINAL(0x005F13A0)
// The session (007AFE20): a multiplayer client [0057F440, AL].

// What the unit's controller (+0x240, slot 50) reports for the info panel (0x20 bytes).
struct CombatInfo {
    int unused_00;
    std::uint32_t damageType;       // +0x04 below 7: "dmg_<n>" names it
    int unused_08;
    float damageMin;                // +0x0C
    float damageMax;                // +0x10
    int unused_14;
    int attack;                     // +0x18
    int defence;                    // +0x1C
};
static_assert(sizeof(CombatInfo) == 0x20);
constexpr int kCombatInfoSlot = 50;

// The speed shown: (00742408 * speed) * 00742AEC unrounded, the speed from 0052FAF0 (mode 3) for a
// unit outside the group, else from 0052FA20.
double ShownSpeed(void* unit, int outsideGroup) {
    const double speed = outsideGroup ? static_cast<CUnit*>(unit)->MoveSpeedFor(3) : static_cast<CUnit*>(unit)->MoveSpeed2();
    return static_cast<double>(1.0f /*00742408*/) * speed * 7.5f /*00742AEC*/;
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

// Whether `unit` is not in the group (the battle screen's party).
bool OutsideGroup(CHudUnitPanel* self, CUnit* unit) {
    CBattleScreen* group = self->owner_;
    int count = group->party_.size;
    CUnit** units = group->party_.data;
    for (int i = 0; i < count; ++i) {
        if (units[i] == unit) return false;
    }
    return true;
}

bool IsEmpty(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2] == 0; }

} // namespace

// 00647040. Draws the info lines of `unit`.
void CHudUnitPanel::DrawUnitInfo(CUnit* unitIn) {
    void* texts = ei::GameData<void*>(0x007AF788);
    UnitParams* params = unitIn->Params();
    mfc::CString suffix;
    Panel panel;
    panel.surface = *surfaces_.data;
    panel.labels = texts_.data;
    panel.rect = {5, 0x23, 0x9B, 0x32};

    std::int16_t health = unitIn->packedVitals.hitPoints;
    panel.text.Format(kValuePair, static_cast<const char*>(panel.labels[0]), health > 0 ? static_cast<int>(health) : 0,
                      static_cast<int>(unitIn->packedVitals.maxHitPoints));
    panel.DrawLine();
    bool outside = OutsideGroup(this, unitIn);
    panel.text.Format(kValuePair, static_cast<const char*>(panel.labels[1]), static_cast<int>(unitIn->packedVitals.mana),
                      static_cast<int>(unitIn->packedVitals.maxMana));
    panel.DrawLine();

    std::uint8_t moveBonus = params->movementKind;
    if (moveBonus > 1) {
        panel.text.Format(kMoveKey, moveBonus - 1);
        TextsGetString(texts, nullptr, panel.text, &suffix);
    } else {
        suffix.Empty();
    }
    double speed = ShownSpeed(unitIn, outside);
    panel.text.Format(kSpeedLine, static_cast<const char*>(panel.labels[2]), speed, static_cast<const char*>(suffix));
    panel.DrawLine();

    float dayTime = static_cast<float>(WorldDayTime(ei::GamePointer<void>(0x007AF5B8)));
    int vision = x87::fistp(static_cast<float>(params->VisionRange(dayTime)));
    panel.text.Format(kValueUnit, static_cast<const char*>(panel.labels[6]), vision, static_cast<const char*>(panel.labels[13]));
    panel.DrawLine();

    CombatInfo combat;
    void* controller = unitIn->controller;
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
    UnitParameterBlock(params, nullptr, &unitIn->packedVitals, description, IsTextKey(unitIn->id_));
    console::CStringArray lines;
    DescriptionLines(description, &lines, 1);
    panel.DrawLabelled(10, lines[0]);
    if (outside) {
        if (!IsEmpty(lines[1])) panel.DrawLabelled(11, lines[1]);
        auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(static_cast<CUnit*>(unitIn)->GetPrototype());
        if (prototype->spellNames.end() != prototype->spellNames.begin()) panel.DrawLine(panel.labels[5]);
        CBattleScreen* group = owner_;
        if (group->party_.size > 0) {
            CUnit* leader = group->party_[0];
            int difference = unitIn->Level() - leader->Level();
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

    if (IsMultiplayerClient() && !outside) {
        TextsGetString(texts, nullptr, kCampExp, &suffix);
        panel.text.Format(kValue, static_cast<const char*>(suffix), params->unspentExperience);
        panel.DrawLine();
    }
}
EI_REPLACE_METHOD(0x00647040, CHudUnitPanel::DrawUnitInfo);
