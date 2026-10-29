// The battle HUD's unit info texts: the lines drawn
// for the unit under the cursor or selected. Each line is a label from the
// panel's label table followed by values, drawn with the panel surface's text routine and moved
// down 15 pixels. See docs/notes/ui.md ("Battle HUD panels").
#include "combat.h"
#include "console.h"
#include "object_tables.h"
#include "ui.h"
#include "ui_item_info.h"
#include "unit.h"
#include "unit_db.h"
#include "text_manager.h"

#include "lib/gameapi.h"
#include <cmath>
#include <cstdint>
#include <string>
#include "world.h"
#include "game_singletons.h"
#include "lib/str.h"


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

// The speed shown: (a constant * speed) * a constant unrounded, the speed of the mode (mode 3) for a
// unit outside the group, else from the current one.
double ShownSpeed(CUnit* unit, bool outsideGroup) {
    const double speed = outsideGroup ? unit->MoveSpeedFor(3) : unit->MoveSpeed2();
    return speed * 7.5f;
}

struct Panel {
    UiSurface* surface;
    const std::string* labels;
    RECT rect;
    std::string text;

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
bool OutsideGroup(HudUnitPanel* self, CUnit* unit) {
    BattleScreen* group = self->owner;
    int count = std::ssize(group->party);
    CUnit** units = group->party.data();
    for (int i = 0; i < count; ++i) {
        if (units[i] == unit) return false;
    }
    return true;
}

} // namespace

void HudUnitPanel::DrawUnitInfo(CUnit* unit) {
    TextManager* clientWorldTexts = g_clientWorld.texts;
    UnitParams* params = &unit->params;
    std::string suffix;
    Panel panel;
    panel.surface = surfaces[0];
    panel.labels = texts.data();
    panel.rect = {5, 0x23, 0x9B, 0x32};

    std::int16_t health = unit->packedVitals.hitPoints;
    panel.text = str::Format(kValuePair, panel.labels[0], health > 0 ? static_cast<int>(health) : 0,
                      static_cast<int>(unit->packedVitals.maxHitPoints));
    panel.DrawLine();
    bool outside = OutsideGroup(this, unit);
    panel.text = str::Format(kValuePair, panel.labels[1], static_cast<int>(unit->packedVitals.mana),
                      static_cast<int>(unit->packedVitals.maxMana));
    panel.DrawLine();

    std::uint8_t moveBonus = params->movementKind;
    if (moveBonus > 1) {
        panel.text = str::Format(kMoveKey, moveBonus - 1);
        suffix = clientWorldTexts->TextManager::GetString(panel.text);
    } else {
        suffix.clear();
    }
    double speed = ShownSpeed(unit, outside);
    panel.text = str::Format(kSpeedLine, panel.labels[2], speed, suffix);
    panel.DrawLine();

    float dayTime = static_cast<float>(g_clientWorld.NightLevel());
    int vision = std::lround(params->VisionRange(dayTime));
    panel.text = str::Format(kValueUnit, panel.labels[6], vision, panel.labels[13]);
    panel.DrawLine();

    // The controller's attack parameters: attack, defence, the damage type (below 7 unsigned: "dmg_<n>" names
    // it) and range.
    AttackParams combat;
    unit->Controller()->GetAttackParams(&combat);
    panel.text = str::Format(kValue, panel.labels[3], combat.attack);
    panel.DrawLine();
    panel.text = str::Format(kValue, panel.labels[4], combat.defence);
    panel.DrawLine();
    if (static_cast<std::uint32_t>(combat.damageType) < 7) {
        panel.text = str::Format(kDamageKey, combat.damageType);
        suffix = clientWorldTexts->TextManager::GetString(panel.text);   // else the move text stays
    }
    int damageMax = static_cast<int>(combat.damageMax);
    int damageMin = static_cast<int>(combat.damageMin);
    panel.text = str::Format(kDamageLine, panel.labels[9], damageMin, damageMax,
                      suffix);
    panel.DrawLine();
    panel.text = str::Format(kValue, panel.labels[15], static_cast<int>(params->rate));
    panel.DrawLine();

    float description[7];
    params->ProtectionBlock(&unit->packedVitals, description, static_cast<unsigned char>(IsTextKey(unit->id)));
    const DamageLists lines = ItemDamageLists(description, true);
    panel.DrawLabelled(10, lines.threshold);
    if (outside) {
        if (!lines.above.empty()) panel.DrawLabelled(11, lines.above);
        auto* prototype = unit->GetPrototype();
        if (prototype->spellNames.end() != prototype->spellNames.begin()) panel.DrawLine(panel.labels[5]);
        BattleScreen* group = owner;
        if (!group->party.empty()) {
            CUnit* leader = group->party[0];
            int difference = unit->Level() - leader->Level();
            if (difference > kLevelDifferenceLimit) {
                suffix = str::Format(kStronger, kLevelDifferenceLimit);
            } else if (difference < -kLevelDifferenceLimit) {
                suffix = str::Format(kWeaker, kLevelDifferenceLimit);
            } else if (difference >= 0) {
                suffix = str::Format(kStronger, difference);
            } else {
                suffix = str::Format(kWeaker, -difference);
            }
            suffix = clientWorldTexts->TextManager::GetString(suffix);
            panel.text = str::Format(kTwoTexts, panel.labels[16], suffix);
            panel.DrawLine();
        }
    } else if (!lines.below.empty()) {
        panel.DrawLabelled(12, lines.below);
    }

    if (IsMultiplayerClient() && !outside) {
        suffix = clientWorldTexts->TextManager::GetString(kCampExp);
        panel.text = str::Format(kValue, suffix, params->unspentExperience);
        panel.DrawLine();
    }
}
