#pragma once

// The game's logic settings (a global), read from a registry file at startup by
// LogicSettings::Load. Field names are the registry keys.

#include <cstddef>
#include <cstdint>
#include <string>

struct LogicSettings {
    // Reads the keys from the registry at `path` unless `loaded` is set; a key that is missing keeps the
    // current value. The difficulty arrays start at 1.0 and take up to three values each.
    void Load(const std::string& path);
    // The static initialiser (Ghidra has no function there). The defaults Load
    // keeps for a missing key, not loaded.
    LogicSettings* Construct();

    bool loaded;                       // never set by Load
    // [Logic]
    float guardStayTime;
    float sentryStayTime;
    float followMinDist;
    float followMaxDist;
    float guardRadius;
    float callForHelpRadius;
    float callForHelpRadiusDefault;
    float followLimit;
    float playerCallForHelp;
    float rangedRunDist;
    float rangedAttackDist;
    float swarmRadius;
    // [RPG]
    float humanCrossAnimationSpeed;
    float backstabAdd;
    float backstabMult;
    float rotateSpeedMult;
    float damageLevel1;
    float damageLevel1Value;
    float damageLevel2;
    float damageLevel2Value;
    float damageLevelRunLimit;
    float int1ExpBonus;
    float hpVal[5];                     // "HP Val 1..5"
    float mpVal[5];                     // "MP Val 1..5"
    float skillVal[2];                  // "Skill Val 1..2"
    float perkPowerBase;
    float considerModifier;
    float considerLogBase;
    // [LMP]
    float lostMoney;
    float lostXp;
    int questsToRespawn;                // [Scenario]
    float itemDurabilityCritical;       // [RPG]
    // [DifficultyLevels], per difficulty
    float absorption[3];
    float attack[3];
    float defence[3];
};
static_assert(offsetof(LogicSettings, swarmRadius) == 0x30);
static_assert(offsetof(LogicSettings, considerLogBase) == 0x94);
static_assert(offsetof(LogicSettings, itemDurabilityCritical) == 0xA4);
static_assert(sizeof(LogicSettings) == 0xCC);

// Zero until the static initialiser (logic_settings.cpp) sets the defaults.
inline LogicSettings g_logicSettings = /*007C53F8*/ {};
