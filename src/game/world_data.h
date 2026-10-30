#pragma once

// Constants the world server's code reads from memory (strings, float constants as the original does, rewrite
// guide rule 9, vtables and globals).

#include <cstdint>
#include <unordered_map>
#include "options.h"

#include "engine_object.h"
#include "lib/vecmath.h"
#include "game_singletons.h"

class CLight;
struct QuestBriefingRecord;

namespace worlddata {

inline const char* StartZoneText() { return "zone1"; }
inline const char* ScriptBeginText() { return "WorldScript("; }
inline const char* AddToHeroesFormat() { return "AddObject( Heroes, GetObjectByID( \"%i\" ) )"; }
inline const char* HeroArrivalText() { return "PlayAnimation(GetObjectByName( \"hero\" ),\"uspecial25\")"; }
inline const char* ScriptEndText() { return ")"; }
inline const char* HairFormat() { return "hr.%.2d"; }
inline const char* HairPartText() { return "hr"; }

// The figure parts of a hero ("hd", "bd", "hp", "hr", "lh1" ... "rl3"), in the game's order.
constexpr int kHeroPartCount = 16;
inline const char* HeroPartText(int index) {
    static constexpr const char* kParts[kHeroPartCount] = {"hd",  "bd",  "hp",  "hr",  "lh1", "lh2", "lh3", "rh1",
                                                           "rh2", "rh3", "ll1", "ll2", "ll3", "rl1", "rl2", "rl3"};
    return kParts[index];
}

inline constexpr float kHeroStartX = 54.0f;        // the hero's start on zone1
inline constexpr float kHeroStartY = 138.0f;
inline constexpr float kHeroStartRadius2 = 300.0f;
inline constexpr float kAxisEpsilon = 1e-8f;
inline constexpr float kLineAngleOffset = 3.1415927f;
inline constexpr float kLineSpacing = 1.2f;

inline const char* QuickEffectPrototype() { return "st.tr.130"; }
inline const char* QuickEffectModel() { return "unhuma"; }

inline const char* CampfireText() { return "campfire"; }
inline const char* NoWaveNameText() { return "Adding sound without wave name"; }
inline constexpr float kCampfireHeight = 0.7f;
inline constexpr float kTorchLightScale = 10.0f;  // the flame's radius factor (10)

// The textures' folder under the text folder (TheTextFolder) and the lights table.
inline const char* TexturesFolderText() { return "textures\\"; }
inline const char* LightsTableText() { return "LightsGipat.ini"; }
inline const char* GipathText() { return "gipath"; }
inline const char* TextsFileText() { return "texts.res"; }
inline const char* TextsLmpFileText() { return "textsLmp.res"; }
// The shadow setting (0 off, 2 blurred; map_object_shadow.cpp's ShadowMode): above 0 the client
// sets its flag 0x200.
inline int ShadowSetting() { return static_cast<const volatile int&>(options::g_options.shadowQuality); }

inline const char* DefaultAllodText() { return "gipat"; }
inline const char* CantLoadTerrainFormat() { return "Can't load terrain or map file \"%s\".\n"; }
inline const char* CaveSkyText() { return "Sky01"; }
inline const char* OpenSkyText() { return "Sky00"; }
inline const char* LightsPrefixText() { return "Lights"; }
inline const char* CaveText() { return "Cave"; }
inline const char* IniSuffixText() { return ".ini"; }
inline const char* FinalZoneText() { return "gz20g"; }
inline const char* FinalMusicText() { return "final"; }
// Options (options.cpp): Audio, EnableWaterWaves, EnableSelfShadowing and DefaultWindForce.
// AudioStreaming / AudioSFX (the sound manager takes them on the client's tenth tick) are read from the options.
inline int AudioOption() { return static_cast<const volatile int&>(options::g_options.audio); }
inline int WaterWavesOption() { return static_cast<const volatile int&>(options::g_options.enableWaterWaves); }
inline int SelfShadowingOption() { return static_cast<const volatile int&>(options::g_options.enableSelfShadowing); }
inline float DefaultWindForceOption() { return static_cast<const volatile float&>(options::g_options.defaultWindForce); }
// The hour the terrain's sun was last set for (world_client_lights.cpp); -1 makes the next UpdateLighting set it.
inline float g_lastLitHour = /*0078FF38*/ -1.0f;

// The configured paths the game's folders (TheDataFolder, TheTextFolder, TheDiscFolder the base folder;
// game_singletons.h) are set from (their text pointers).
inline const char* ConfiguredDataPath() { return options::g_options.workPath.c_str(); }
inline const char* ConfiguredTextPath() { return options::g_options.resourcesPath.c_str(); }
inline const char* ConfiguredBasePath() { return options::g_options.cdRomPath.c_str(); }

inline const char* AutosaveEventText() { return "autosave"; }
inline const char* SegmentDisconnectedText() { return "CWorldClient::Segment().\nDisconnecting client due to packet parsing error.\n"; }
// The milliseconds of real time one world update takes (55; the speed-up key sets 27, the main loop reads it).
inline int g_updatePeriod = /*0078FF34*/ 55;
// The point the light effects' grey levels start from (its distance to the sun), the origin; the picking returns it without a view matrix.
// The scene's camera while the world's draw runs (the scene stack's top it pushes first: projection x
// view; world_client_draw.cpp), else null. The figure parts lit on the GPU take their model matrix from it.
const Matrix4* SceneCamera();
inline constexpr Vector3 kOrigin = /*007AFDB0*/ {0.0f, 0.0f, 0.0f};
inline constexpr float kColourScale = 255.0f;

// ---- The world server's quests (world_quests.cpp) -----------------------------------------------

// The zones where the ten mercenaries are briefed.
inline const char* MercZoneName(int slot) {
    static constexpr const char* kZones[10] = /*007907AC*/ {"bz1g", "bz1g", "bz2g", "bz6g", "bz8k", "bz8k", "bz14h",
                                                            "bz14h", "bz9k", "bz14h"};
    return kZones[slot];
}
inline const char* MercNameFormat() { return "merc%i"; }
// The briefing variables of a mercenary slot: "b.%s.n<k>_%i" and "amerc_%i".
inline const char* MercVariableN8Format() { return "b.%s.n8_%i"; }
inline const char* MercVariableN1Format() { return "b.%s.n1_%i"; }
inline const char* MercVariableN2Format() { return "b.%s.n2_%i"; }
inline const char* MercVariableN3Format() { return "b.%s.n3_%i"; }
inline const char* MercVariableN9Format() { return "b.%s.n9_%i"; }
inline const char* MercVariableN10Format() { return "b.%s.n10_%i"; }
inline const char* MercVariableN11Format() { return "b.%s.n11_%i"; }
inline const char* MercHiredFormat() { return "amerc_%i"; }
inline const char* BriefingCompleteScript() { return "WorldScript( GSSetVar( %i,\"%s\",2 ) #OnBriefingComplete( %i, \"%s\" ) )"; }
inline const char* HumanHeroText() { return "Human Hero"; }
// The quest database's briefings by name (QuestBriefingTable::Index, quest_records.h; operator[] files an
// unknown name with null).
std::unordered_map<std::string, QuestBriefingRecord*>& QuestBriefings();
inline const char* DisconnectingPlayerText() { return "Disconnecting player "; }
inline const char* HeroesGroupText() { return "Heroes"; }
inline constexpr float kHearingScale = 2.0f;   // the units' perception scale (SyncPlayer too)
inline constexpr float kWakeMargin = 20.0f;     // added to it when units wake others

// ---- Effect constants (spell_effect_*.cpp, world_effect_*.cpp) -----------------------------------
// References, so the effects read them through a volatile (ConstAt) or pass their address (ConstPtr),
// one object per value (no Rebirth patch writes these cells).
template <float V>
inline constexpr float kFloatConstant = V;
inline const float& OneRef() { return kFloatConstant<1.0f>; }
inline const float& ZeroRef() { return kFloatConstant<0.0f>; }
inline const float& HalfRef() { return kFloatConstant<0.5f>; }
inline const float& AcidRayScale() { return kFloatConstant<0.4f>; }
inline const float& FireballScale() { return kFloatConstant<0.3f>; }
inline const float& FireballNear() { return kFloatConstant<1.25f>; }
inline const float& StrikeDepth() { return kFloatConstant<3.0f>; }
inline const float& StrikeHeight() { return kFloatConstant<30.0f>; }
inline const float& StrikeWidth() { return kFloatConstant<-7.0f>; }
inline const float& LightAbove() { return kFloatConstant<5.0f>; }
inline const float& BoltWidthScale() { return kFloatConstant<5.0f>; }   // 5.0 (the same constant)
inline const float& ImpactWakeMargin() { return kFloatConstant<5.0f>; }   // 5.0 (the same constant)
inline const float& TeleportDepth() { return kFloatConstant<0.2f>; }
inline const float& BuffScale() { return kFloatConstant<16.0f>; }
inline const float& TargetHeight() { return kFloatConstant<1.2f>; }
inline const float& RangeEpsilon() { return kFloatConstant<0.001f>; }
inline const float& ScorchAlpha() { return kFloatConstant<0.6f>; }
inline const float& ScorchMargin() { return kFloatConstant<0.3f>; }   // 0.3 (the same constant as FireballScale)
inline const float& DefaultRadius() { return kFloatConstant<100.0f>; }
inline const float& SplashScale() { return kFloatConstant<0.7f>; }   // 0.7: the impact particle's size per power
inline const float& RiseScale() { return kFloatConstant<1.5f>; }   // 1.5: impact lights rise faster and reach further
inline const float& NoiseKind() { return kFloatConstant<4.0f>; }   // 4.0, truncated by _ftol
inline const float& PossessWake() { return kFloatConstant<2.0f>; }
inline const float& PossessWakeBase() { return kFloatConstant<20.0f>; }

// ---- AI, interface mesh and server constants ----------------------------------------------------
inline constexpr float kFixedPoint = 65536.0f;
inline constexpr float kOutsideDistance = 100.0f;
inline constexpr float kTexel = 0.00390625f;   // 1 / 256
inline constexpr float kHalfTexel = 0.001953125f;   // 1 / 512
inline constexpr float kLayoutX = 0.00125f;   // 2 / 1600
inline constexpr float kLayoutY = 0.0025f;   // 1 / 400
inline constexpr float kThreeQuarters = 0.75f;
inline constexpr float kInterfaceView = 0.48157462f;   // 0.48157 (the interface camera's half view)
// By reference, where the game copies the bits or passes the address on.
inline const float& AnnouncePeriodRef() { return kFloatConstant<20.0f>; }   // seconds between announcements to the master
inline const float& QueueWaitLimitRef() { return kFloatConstant<60.0f>; }   // squared distance a blocking ally may be away
// Tables of the AI (ai_refresh.cpp; the height of each of 8 levels is ai_map.h's kLevelHeights): per occluder type
// the cost added by full cover and the damping exponent; 5 rows x 16 dwords of cell marks.
inline constexpr int kOccluderCosts[3] = /*007912A4*/ {290, 20};
inline constexpr int kOccluderDamping[3] = /*007912B0*/ {7, 2};
inline constexpr std::uint32_t kMarkTable[80] = /*00791164*/ {
    0, 0, 1, 1, 1, 1, 4, 7, 0xB, 0xC, 0xE, 0xE, 0xF, 0xF, 0xF, 0xF, 0, 0, 1, 1, 1, 1, 4, 7, 0xB, 0xC, 0xD, 0xD,
    0xD, 0xD, 0xD, 0xD, 0, 0, 1, 1, 1, 1, 4, 7, 8, 9, 0xA, 0xA, 0xA, 0xA, 0xA, 0xA, 0, 0, 1, 1, 1, 1, 4, 5, 6, 6,
    6, 6, 6, 6, 6, 6, 0, 0, 1, 1, 1, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3};

} // namespace worlddata
