#pragma once

// The game's logic settings (a global): the defaults below, then what LogicSettings::Load reads from a registry file
// at startup. Field names are the registry keys.

#include <cstddef>
#include <cstdint>
#include <string>

struct LogicSettings {
    // Reads the keys from the registry at `path` unless `loaded` is set; a key that is missing keeps the
    // current value. The difficulty arrays start at 1.0 and take up to three values each.
    void Load(const std::string& path);

    // The defaults Load keeps for a missing key.
    bool loaded = false;                // never set by Load
    // [Logic]
    float guardStayTime = 10.0f;
    float sentryStayTime = 10.0f;
    float followMinDist = 2.0f;
    float followMaxDist = 4.0f;
    float guardRadius = 10.0f;
    float callForHelpRadius = 7.0f;
    float callForHelpRadiusDefault = 7.0f;
    float followLimit = 15.0f;
    float playerCallForHelp = 16.0f;
    float rangedRunDist = 4.0f;
    float rangedAttackDist = 7.0f;
    float swarmRadius = 10.0f;
    // [RPG]
    float humanCrossAnimationSpeed = 1.0f;
    float backstabAdd = 3.0f;
    float backstabMult = 0.0f;
    float rotateSpeedMult = 100.0f;
    float damageLevel1 = 0.5f;
    float damageLevel1Value = 0.5f;
    float damageLevel2 = 0.0f;
    float damageLevel2Value = 0.33f;
    float damageLevelRunLimit = 0.5f;
    float int1ExpBonus = 0.2f;
    float hpVal[5] = {25.0f, 30.0f, 50.0f, 1.1f, 1.61f};   // "HP Val 1..5"
    float mpVal[5] = {25.0f, 30.0f, 50.0f, 1.1f, 1.61f};   // "MP Val 1..5"
    float skillVal[2] = {50.0f, 5.0f};                     // "Skill Val 1..2"
    float perkPowerBase = 2.0f;
    float considerModifier = 200.0f;
    float considerLogBase = 1.61f;
    // [LMP]
    float lostMoney = 0.1f;
    float lostXp = 0.1f;
    int questsToRespawn = 3;                // [Scenario]
    float itemDurabilityCritical = 0.3f;    // [RPG]
    // [DifficultyLevels], per difficulty
    float absorption[3] = {1.0f, 1.0f, 1.0f};
    float attack[3] = {1.0f, 1.0f, 1.0f};
    float defence[3] = {1.0f, 1.0f, 1.0f};
};
static_assert(offsetof(LogicSettings, swarmRadius) == 0x30);
static_assert(offsetof(LogicSettings, considerLogBase) == 0x94);
static_assert(offsetof(LogicSettings, itemDurabilityCritical) == 0xA4);
static_assert(sizeof(LogicSettings) == 0xCC);

inline constinit LogicSettings g_logicSettings{};
