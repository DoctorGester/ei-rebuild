#pragma once

// The game's logic settings (the global at 007C53F8), read from a registry file at startup by
// LogicSettings::Load (005CE0F0, called by 00456440). Field names are the registry keys.

#include "game_data.h"

#include <cstddef>
#include <cstdint>

struct LogicSettings {
    // 005CE0F0 (RET 4). Reads the keys from the registry at `path` unless `loaded` is set; a key
    // that is missing keeps the current value. The difficulty arrays start at 1.0 and take up to
    // three values each.
    void Load(const char* path);
    // 005CDFC0 (the static initialiser 005CDFB0; Ghidra has no function there). The defaults Load
    // keeps for a missing key, not loaded. Returns this.
    LogicSettings* Construct();

    bool loaded;                       // +0x00 never set by Load
    std::uint8_t unused_01[3];
    // [Logic]
    float guardStayTime;                // +0x04
    float sentryStayTime;               // +0x08
    float followMinDist;                // +0x0C
    float followMaxDist;                // +0x10
    float guardRadius;                  // +0x14
    float callForHelpRadius;            // +0x18
    float callForHelpRadiusDefault;     // +0x1C
    float followLimit;                  // +0x20
    float playerCallForHelp;            // +0x24
    float rangedRunDist;                // +0x28
    float rangedAttackDist;             // +0x2C
    float swarmRadius;                  // +0x30
    // [RPG]
    float humanCrossAnimationSpeed;     // +0x34
    float backstabAdd;                  // +0x38
    float backstabMult;                 // +0x3C
    float rotateSpeedMult;              // +0x40
    float damageLevel1;                 // +0x44
    float damageLevel1Value;            // +0x48
    float damageLevel2;                 // +0x4C
    float damageLevel2Value;            // +0x50
    float damageLevelRunLimit;          // +0x54
    float int1ExpBonus;                 // +0x58
    float hpVal[5];                     // +0x5C "HP Val 1..5"
    float mpVal[5];                     // +0x70 "MP Val 1..5"
    float skillVal[2];                  // +0x84 "Skill Val 1..2"
    float perkPowerBase;                // +0x8C
    float considerModifier;             // +0x90
    float considerLogBase;              // +0x94
    // [LMP]
    float lostMoney;                    // +0x98
    float lostXp;                       // +0x9C
    int questsToRespawn;                // +0xA0 [Scenario]
    float itemDurabilityCritical;       // +0xA4 [RPG]
    // [DifficultyLevels], per difficulty
    float absorption[3];                // +0xA8
    float attack[3];                    // +0xB4
    float defence[3];                   // +0xC0
};
static_assert(offsetof(LogicSettings, swarmRadius) == 0x30);
static_assert(offsetof(LogicSettings, considerLogBase) == 0x94);
static_assert(offsetof(LogicSettings, itemDurabilityCritical) == 0xA4);
static_assert(sizeof(LogicSettings) == 0xCC);

inline LogicSettings& GameLogicSettings() { return ei::GameData<LogicSettings>(0x007C53F8); }
