// The battle HUD's unit info texts: the lines drawn
// for the unit under the cursor or selected. Each line is a label from the
// panel's label table followed by values, drawn with the panel surface's text routine and moved
// down 15 pixels. See docs/notes/ui.md ("Battle HUD panels").
#include "combat.h"
#include "console.h"
#include "game_strings.h"
#include "object_tables.h"
#include "ui.h"
#include "ui_item_info.h"
#include "unit.h"
#include "unit_db.h"
#include "item_db_text.h"

#include "ei/x87.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstdint>
#include <string>

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

// The world's night level, unrounded.
double WorldDayTime(CWorld* a0) { return a0->NightLevel(); }


// The speed shown: (a constant * speed) * a constant unrounded, the speed of the mode (mode 3) for a
// unit outside the group, else from the current one.
double ShownSpeed(CUnit* unit, int outsideGroup) {
    const double speed = outsideGroup ? unit->MoveSpeedFor(3) : unit->MoveSpeed2();
    return static_cast<double>(1.0f /*00742408*/) * speed * 7.5f /*00742AEC*/;
}

struct Panel {
    CUiSurface* surface;
    const std::string* labels;
    RECT rect;
    mfc::CString text;

    // Draws `text` and moves down a line.
    void DrawLine() { DrawLine(text); }
    void DrawLine(std::string_view line) {
        surface->DrawTextIn(line, &rect, 0, kTextColour, 0, 1);
        gameapi::OffsetRect(&rect, 0, kLineHeight);
    }
    // "<label> <line>" when the line is not empty.
    void DrawLabelled(int label, const std::string& line) {
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


} // namespace

// Draws the info lines of `unit`.
void CHudUnitPanel::DrawUnitInfo(CUnit* unitIn) {
    CTextManager* texts = TheTextManager();
    UnitParams* params = unitIn->Params();
    std::string suffix;
    Panel panel;
    panel.surface = *surfaces_.data;
    panel.labels = texts_.data();
    panel.rect = {5, 0x23, 0x9B, 0x32};

    std::int16_t health = unitIn->packedVitals.hitPoints;
    panel.text.Format(kValuePair, panel.labels[0].c_str(), health > 0 ? static_cast<int>(health) : 0,
                      static_cast<int>(unitIn->packedVitals.maxHitPoints));
    panel.DrawLine();
    bool outside = OutsideGroup(this, unitIn);
    panel.text.Format(kValuePair, panel.labels[1].c_str(), static_cast<int>(unitIn->packedVitals.mana),
                      static_cast<int>(unitIn->packedVitals.maxMana));
    panel.DrawLine();

    std::uint8_t moveBonus = params->movementKind;
    if (moveBonus > 1) {
        panel.text.Format(kMoveKey, moveBonus - 1);
        texts->CTextManager::GetString(panel.text, &suffix);
    } else {
        suffix.clear();
    }
    double speed = ShownSpeed(unitIn, outside);
    panel.text.Format(kSpeedLine, panel.labels[2].c_str(), speed, suffix.c_str());
    panel.DrawLine();

    float dayTime = static_cast<float>(WorldDayTime(TheClientWorld()));
    int vision = x87::fistp(static_cast<float>(params->VisionRange(dayTime)));
    panel.text.Format(kValueUnit, panel.labels[6].c_str(), vision, panel.labels[13].c_str());
    panel.DrawLine();

    // The controller's attack parameters: attack, defence, the damage type (below 7 unsigned: "dmg_<n>" names
    // it) and range.
    AttackParams combat;
    unitIn->Controller()->GetAttackParams(&combat);
    panel.text.Format(kValue, panel.labels[3].c_str(), combat.attack);
    panel.DrawLine();
    panel.text.Format(kValue, panel.labels[4].c_str(), combat.defence);
    panel.DrawLine();
    if (static_cast<std::uint32_t>(combat.damageType) < 7) {
        panel.text.Format(kDamageKey, combat.damageType);
        texts->CTextManager::GetString(panel.text, &suffix);   // else the move text stays
    }
    int damageMax = static_cast<int>(combat.damageMax);
    int damageMin = static_cast<int>(combat.damageMin);
    panel.text.Format(kDamageLine, panel.labels[9].c_str(), damageMin, damageMax,
                      suffix.c_str());
    panel.DrawLine();
    panel.text.Format(kValue, panel.labels[15].c_str(), static_cast<int>(params->rate));
    panel.DrawLine();

    float description[7];
    params->ProtectionBlock(&unitIn->packedVitals, description, static_cast<unsigned char>(IsTextKey(unitIn->id_)));
    std::vector<std::string> lines;
    ItemDamageLists(description, &lines, static_cast<char>(1));
    panel.DrawLabelled(10, lines[0]);
    if (outside) {
        if (lines[1].size() != 0) panel.DrawLabelled(11, lines[1]);
        auto* prototype = unitIn->GetPrototype();
        if (prototype->spellNames.end() != prototype->spellNames.begin()) panel.DrawLine(panel.labels[5]);
        CBattleScreen* group = owner_;
        if (group->party_.size > 0) {
            CUnit* leader = group->party_[0];
            int difference = unitIn->Level() - leader->Level();
            if (difference > kLevelDifferenceLimit) {
                suffix = str::Format(kStronger, kLevelDifferenceLimit);
            } else if (difference < -kLevelDifferenceLimit) {
                suffix = str::Format(kWeaker, kLevelDifferenceLimit);
            } else if (difference >= 0) {
                suffix = str::Format(kStronger, difference);
            } else {
                suffix = str::Format(kWeaker, -difference);
            }
            texts->CTextManager::GetString(suffix.c_str(), &suffix);
            panel.text.Format(kTwoTexts, panel.labels[16].c_str(), suffix.c_str());
            panel.DrawLine();
        }
    } else if (lines[2].size() != 0) {
        panel.DrawLabelled(12, lines[2]);
    }

    if (IsMultiplayerClient() && !outside) {
        texts->CTextManager::GetString(kCampExp, &suffix);
        panel.text.Format(kValue, suffix.c_str(), params->unspentExperience);
        panel.DrawLine();
    }
}
