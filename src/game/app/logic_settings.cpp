// LogicSettings::Load: the logic settings from their registry file. See logic_settings.h.
#include "game/app/logic_settings.h"

#include "engine/data/registry.h"

#include <algorithm>
#include <vector>

namespace {
// Up to three values of a [DifficultyLevels] array into `out`.
void ReadDifficulties(FileRegistry& reg, const char* key, float* out) {
    std::vector<float> values;
    reg.GetFloatArray("DifficultyLevels", key, &values);
    std::copy_n(values.begin(), std::min<std::size_t>(values.size(), 3), out);
}
} // namespace

void LogicSettings::Load(const std::string& path) {
    *this = LogicSettings{};
    FileRegistry reg;
    reg.Open(path);
    auto read = [&reg](const char* section, const char* key, float& value) {
        value = static_cast<float>(reg.GetFloat(section, key, value));
    };
    read("Logic", "GuardStayTime", guardStayTime);
    read("Logic", "SentryStayTime", sentryStayTime);
    read("Logic", "FollowMinDist", followMinDist);
    read("Logic", "FollowMaxDist", followMaxDist);
    read("Logic", "GuardRadius", guardRadius);
    read("Logic", "CallForHelpRadius", callForHelpRadius);
    read("Logic", "CallForHelpRadiusDefault", callForHelpRadiusDefault);
    read("Logic", "FollowLimit", followLimit);
    read("Logic", "PlayerCallForHelp", playerCallForHelp);
    read("Logic", "RangedRunDist", rangedRunDist);
    read("Logic", "RangedAttackDist", rangedAttackDist);
    read("Logic", "SwarmRadius", swarmRadius);
    read("RPG", "HumanCrossAnimationSpeed", humanCrossAnimationSpeed);
    read("RPG", "BackstabAdd", backstabAdd);
    read("RPG", "BackstabMult", backstabMult);
    read("RPG", "RotateSpeedMult", rotateSpeedMult);
    read("RPG", "DamageLevel1", damageLevel1);
    read("RPG", "DamageLevel1Value", damageLevel1Value);
    read("RPG", "DamageLevel2", damageLevel2);
    read("RPG", "DamageLevel2Value", damageLevel2Value);
    read("RPG", "DamageLevelRunLimit", damageLevelRunLimit);
    read("RPG", "Int1ExpBonus", int1ExpBonus);
    read("RPG", "HP Val 1", hpVal[0]);
    read("RPG", "HP Val 2", hpVal[1]);
    read("RPG", "HP Val 3", hpVal[2]);
    read("RPG", "HP Val 4", hpVal[3]);
    read("RPG", "HP Val 5", hpVal[4]);
    read("RPG", "MP Val 1", mpVal[0]);
    read("RPG", "MP Val 2", mpVal[1]);
    read("RPG", "MP Val 3", mpVal[2]);
    read("RPG", "MP Val 4", mpVal[3]);
    read("RPG", "MP Val 5", mpVal[4]);
    read("RPG", "Skill Val 1", skillVal[0]);
    read("RPG", "Skill Val 2", skillVal[1]);
    read("RPG", "Perk Power Base", perkPowerBase);
    read("RPG", "Consider Modifier", considerModifier);
    read("RPG", "Consider Log Base", considerLogBase);
    read("RPG", "Item Durability Critical", itemDurabilityCritical);
    questsToRespawn = reg.GetInt("Scenario", "QuestsToRespawn", questsToRespawn);
    read("LMP", "Lost Money", lostMoney);
    read("LMP", "Lost XP", lostXp);
    ReadDifficulties(reg, "Absorption", absorption);
    ReadDifficulties(reg, "Attack", attack);
    ReadDifficulties(reg, "Defence", defence);
}
