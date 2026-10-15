// LogicSettings::Load (005CE0F0): the logic settings from their registry file. See logic_settings.h.
#include "logic_settings.h"

#include "registry.h"

#include "ei/hook.h"

#include <cstring>

namespace {
// Up to three values of a [DifficultyLevels] array into `out` (copied as dwords).
void ReadDifficulties(CFileRegistry& reg, const char* key, float* out) {
    DwordArray values{};
    reg.GetFloatArray("DifficultyLevels", key, &values);
    int n = values.size < 3 ? values.size : 3;
    if (n > 0) std::memcpy(out, values.data, static_cast<std::size_t>(n) * sizeof(float));
    values.SetSize(0);
}
} // namespace

void LogicSettings::Load(const char* path) {
    if (loaded) return;
    CFileRegistry reg;
    reg.Construct();
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
    questsToRespawn = reg.GetInt2("Scenario", "QuestsToRespawn", questsToRespawn);
    read("LMP", "Lost Money", lostMoney);
    read("LMP", "Lost XP", lostXp);
    for (int i = 0; i < 3; ++i) {
        absorption[i] = 1.0f;
        attack[i] = 1.0f;
        defence[i] = 1.0f;
    }
    ReadDifficulties(reg, "Absorption", absorption);
    ReadDifficulties(reg, "Attack", attack);
    ReadDifficulties(reg, "Defence", defence);
    reg.Destroy();
}

EI_REPLACE_METHOD(0x005CE0F0, LogicSettings::Load);

LogicSettings* LogicSettings::Construct() {
    // [Logic]
    guardStayTime = 10.0f;
    sentryStayTime = 10.0f;
    followMinDist = 2.0f;
    followMaxDist = 4.0f;
    guardRadius = 10.0f;
    callForHelpRadius = 7.0f;
    callForHelpRadiusDefault = 7.0f;
    followLimit = 15.0f;
    playerCallForHelp = 16.0f;
    rangedRunDist = 4.0f;
    rangedAttackDist = 7.0f;
    swarmRadius = 10.0f;
    // [RPG]
    humanCrossAnimationSpeed = 1.0f;
    backstabAdd = 3.0f;
    backstabMult = 0.0f;
    rotateSpeedMult = 100.0f;
    damageLevel1 = 0.5f;
    damageLevel1Value = 0.5f;
    damageLevel2 = 0.0f;
    damageLevel2Value = 0.33f;
    damageLevelRunLimit = 0.5f;
    int1ExpBonus = 0.2f;
    const float hpMp[5] = {25.0f, 30.0f, 50.0f, 1.1f, 1.61f};
    for (int i = 0; i < 5; ++i) {
        hpVal[i] = hpMp[i];
        mpVal[i] = hpMp[i];
    }
    skillVal[0] = 50.0f;
    skillVal[1] = 5.0f;
    perkPowerBase = 2.0f;
    considerModifier = 200.0f;
    considerLogBase = 1.61f;
    // [LMP], [Scenario]
    lostMoney = 0.1f;
    lostXp = 0.1f;
    questsToRespawn = 3;
    itemDurabilityCritical = 0.3f;
    for (int i = 0; i < 3; ++i) {
        absorption[i] = 1.0f;
        attack[i] = 1.0f;
        defence[i] = 1.0f;
    }
    loaded = false;
    return this;
}
EI_REPLACE_METHOD(0x005CDFC0, LogicSettings::Construct);
