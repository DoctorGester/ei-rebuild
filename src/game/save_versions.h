#pragma once

// The save versions the loads test (CStream::version_ of the stream being read): each names what that version
// added, and a load reads the field from that version on unless the comment says otherwise. Saves are written at
// kSaveVersion; the oldest the game reads is kSaveOldestVersion (world.h).

inline constexpr int kVersionMissileCaster = 0x43;       // a missile's caster id and from-caster flag
inline constexpr int kVersionEffectUnitId = 0x43;        // a visual effect's unit id
inline constexpr int kVersionLookStopFlag = 0x44;        // an AI look's stop flag
inline constexpr int kVersionActingCheckTime = 0x44;     // CUnitServer::actingCheckTime
inline constexpr int kVersionStatsPastFlags = 0x45;      // below: the unit statistics up to the flags only
inline constexpr int kVersionPlayerGroup = 0x46;         // before: a script group that is read and dropped
inline constexpr int kVersionScenarioInput = 0x47;       // scenario.sav holds the input state and the scripts
                                                         // (before: mission.sav)
inline constexpr int kVersionFastGame = 0x47;            // the interface's fast game flag
inline constexpr int kVersionBattleText = 0x47;          // the battle screen's saved text
inline constexpr int kVersionPlanDirection = 0x48;       // a plan's direction and an order's use-spot flag
inline constexpr int kVersionUnusedDword = 0x48;         // a dword of the unit controller nothing reads
inline constexpr int kVersionFightAtWill = 0x49;
inline constexpr int kVersionPlayerByte = 0x49;          // before: a byte that is read and dropped
inline constexpr int kVersionSearchingTarget = 0x4A;
inline constexpr int kVersionAttackOptions = 0x4B;       // the AI's weapon options
inline constexpr int kVersionTimeSpeedFloat = 0x4C;      // the time speed is a float (an int before)
inline constexpr int kVersionProtection = 0x4C;          // the unit controller's protection words
inline constexpr int kVersionPrototypeNames = 0x4D;
inline constexpr int kVersionBaseScale = 0x4E;           // a map object's base scale
inline constexpr int kVersionCountdowns = 0x4E;
inline constexpr int kVersionFixedMaxHitPoints = 0x50;   // below: the statistics stop before fixedMaxHitPoints,
                                                         // which comes from the server object
inline constexpr int kVersionRecordFlags = 0x51;
inline constexpr int kVersionScenarioGlobals = 0x52;     // scenario.sav holds the globals (before: mission.sav)
inline constexpr int kVersionFearFlag = 0x53;            // the fear mode's flag
inline constexpr int kVersionUnitAlarm = 0x53;           // a unit's alarm in the map spec
inline constexpr int kVersionBoredTicks = 0x54;
inline constexpr int kVersionPlayerLists = 0x55;
inline constexpr int kVersionInterfaceList = 0x56;       // an obsolete list of names and values in the interface
inline constexpr int kVersionScriptAreas = 0x57;
inline constexpr int kVersionEffectAlpha = 0x58;         // a visual effect's alpha and type parameter
inline constexpr int kVersionMapObjectName = 0x59;
inline constexpr int kVersionSpellOptions = 0x5A;        // the AI's spell options and heal weight; the options'
                                                         // full layout (before: part, a dword, damage)
inline constexpr int kVersionSoundLooping = 0x5C;
inline constexpr int kVersionMessageFlag = 0x5D;         // the interface's message flag
inline constexpr int kVersionPlayerModeGroupDropped = 0x5E;  // below (from kVersionPlayerGroup): a Player mode's
                                                             // script group, read and dropped
inline constexpr int kVersionSavedText = 0x5F;           // the interface's saved text
inline constexpr int kVersionMobFiles = 0x60;
inline constexpr int kVersionPendingExit = 0x62;         // the battle screen's pending exit
inline constexpr int kVersionPendingExperience = 0x63;   // below: the statistics stop before pendingExperience
inline constexpr int kVersionGuardFailures = 0x66;       // the guard mode's failure count
inline constexpr int kVersionLightFlags = 0x67;          // a point light effect's flags
inline constexpr int kVersionExitExtras = 0x68;          // an exit area's extra fields and angle
inline constexpr int kVersionPendingHit = 0x69;
inline constexpr int kVersionSecondName = 0x6A;
inline constexpr int kVersionPartyKey = 0x6B;            // above: the party's key
inline constexpr int kVersionChecksum = 0x6B;            // above: a checksum of the uncompressed data
inline constexpr int kVersionSoundNoFade = 0x6D;
inline constexpr int kVersionSharedObjects = 0x6E;
inline constexpr int kVersionCameraViews = 0x6F;         // below: the camera's 0xC0 bytes of views (since: the last 0x60)
inline constexpr int kVersionFleePoint = 0x70;           // the fear mode's flee point
inline constexpr int kVersionAlarmSlot = 0x71;           // the brain's alarm slot
inline constexpr int kVersionCompressedFlag = 0x71;      // above: a byte saying whether the data is compressed
inline constexpr int kVersionWaitFrame = 0x73;           // the unit controller's wait frame (written, not read)
inline constexpr int kVersionRainBoltTick = 0x74;
