#pragma once

// Constants the world server's code reads from memory (strings, float constants as the original does, rewrite
// guide rule 9, vtables and globals).

#include <cstdint>
#include "options.h"

#include "engine_object.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"
#include "lib/stlp_hash.h"
#include "game_data.h"
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

inline float HeroStartX() { return 54.0f /*0073FA9C*/; }        // the hero's start on zone1
inline float HeroStartY() { return 138.0f /*0073FAA0*/; }
inline float HeroStartRadius2() { return 300.0f /*0073FA98*/; }
inline float AxisEpsilon() { return 1e-8f /*0073BAE0*/; }
inline float One() { return 1.0f /*0073B84C*/; }
inline float Zero() { return 0.0f /*0073B858*/; }
inline float Half() { return 0.5f /*0073B86C*/; }
inline float LineAngleOffset() { return 3.1415927f /*0073F8EC*/; }
inline float LineSpacing() { return 1.2f /*0073CDF4*/; }

inline const char* QuickEffectPrototype() { return "st.tr.130"; }
inline const char* QuickEffectModel() { return "unhuma"; }

inline const char* CampfireText() { return "campfire"; }
inline const char* NoWaveNameText() { return "Adding sound without wave name"; }
inline float CampfireHeight() { return 0.7f /*0073C538*/; }
inline float TorchLightScale() { return 10.0f /*0073BAAC*/; }  // the flame's radius factor (10)

// The textures' folder under the text folder (TheTextFolder) and the lights table.
inline const char* TexturesFolderText() { return "textures\\"; }
inline const char* LightsTableText() { return "LightsGipat.ini"; }
inline const char* GipathText() { return "gipath"; }
inline const char* TextsFileText() { return "texts.res"; }
inline const char* TextsLmpFileText() { return "textsLmp.res"; }
inline const char* CursorFileText() { return "wincurs\\Df0000.cur"; }
// The shadow setting (0 off, 2 blurred; map_object_shadow.cpp's ShadowMode): above 0 the client
// sets its flag 0x200.
inline int ShadowSetting() { return static_cast<const volatile int&>(options::TheOptions()->shadowQuality_); }

inline const char* DefaultAllodText() { return "gipat"; }
inline const char* CantLoadTerrainFormat() { return "Can't load terrain or map file \"%s\".\n"; }
inline const char* CaveSkyText() { return "Sky01"; }
inline const char* OpenSkyText() { return "Sky00"; }
inline const char* LightsPrefixText() { return "Lights"; }
inline const char* CaveText() { return "Cave"; }
inline const char* IniSuffixText() { return ".ini"; }
inline const char* FinalZoneText() { return "gz20g"; }
inline const char* FinalMusicText() { return "final"; }
// Options (options.cpp): Audio, EnableWaterWaves, EnableSelfShadowing,
// LightingFreq (milliseconds; the zone load turns it into frames at 66 ms each) and
// DefaultWindForce. AudioStreaming / AudioSFX are MovieVolumeSetting / VolumeSettingA below.
inline int AudioOption() { return static_cast<const volatile int&>(options::TheOptions()->audio_); }
inline int WaterWavesOption() { return static_cast<const volatile int&>(options::TheOptions()->enableWaterWaves_); }
inline int SelfShadowingOption() { return static_cast<const volatile int&>(options::TheOptions()->enableSelfShadowing_); }
inline int LightingFrequencyOption() { return static_cast<const volatile int&>(options::TheOptions()->lightingFreq_); }
inline float DefaultWindForceOption() { return static_cast<const volatile float&>(options::TheOptions()->defaultWindForce_); }
// The hour the terrain's sun was last set for (world_client_lights.cpp); -1 makes the next UpdateLighting set it.
inline float g_lastLitHour = /*0078FF38*/ -1.0f;

// The configured paths the game's folders (TheDataFolder, TheTextFolder, TheDiscFolder the base folder;
// game_singletons.h) are set from (their text pointers).
inline const char* ConfiguredDataPath() { return options::TheOptions()->workPath_.GetString(); }
inline const char* ConfiguredTextPath() { return options::TheOptions()->resourcesPath_.GetString(); }
inline const char* ConfiguredBasePath() { return options::TheOptions()->cdRomPath_.GetString(); }

inline const char* AutosaveEventText() { return "autosave"; }
inline const char* SegmentDisconnectedText() { return "CWorldClient::Segment().\nDisconnecting client due to packet parsing error.\n"; }
// The volume settings the sound manager takes on the client's tenth tick; the movies' is bink_video.cpp's.
inline int VolumeSettingA() { return options::TheOptions()->audioSfx_; }
inline int VolumeSettingB() { return options::TheOptions()->audioSpeech_; }
inline int MovieVolumeSetting() { return options::TheOptions()->audioStreaming_; }
// The party's fight state the client world keeps (2: fighting; sound_music.cpp, world_client.cpp).
int FightState();
// The light effects' grey levels: on when its setting is set; the countdown takes the world update
// period per tick and restarts at its start value.
inline int LightLevelsSetting() { return options::TheOptions()->enableBloodprintsRelight_; }
// The milliseconds of real time one world update takes (55; the speed-up key sets 27, the main loop reads it).
inline int g_updatePeriod = /*0078FF34*/ 55;
inline int WorldUpdatePeriod() { return g_updatePeriod; }
inline int LightLevelsPeriod() { return options::TheOptions()->shadowLightingFreq_; }
// The scene renderer's sun light (figure_part_draw.cpp; world_singletons.cpp) and the point the grey levels start
// from (its distance to the sun), the origin; the picking returns it without a view matrix.
CLight* SceneSun();
// The scene's camera while the world's draw runs (the scene stack's top it pushes first: projection x
// view; world_client_draw.cpp), else null. The figure parts lit on the GPU take their model matrix from it.
const Matrix4* SceneCamera();
inline constexpr Vector3 kOrigin = /*007AFDB0*/ {0.0f, 0.0f, 0.0f};
inline float ColourScale() { return 255.0f /*0073C370*/; }

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
// The quest database's briefings by name (QuestBriefingTable::Index, quest_records.h; operator[]
// Stlp::NamedSlot).
stlp::hash_map<stlp::string, QuestBriefingRecord*, stlp::StringHash>& QuestBriefings();
inline const char* DisconnectingPlayerText() { return "Disconnecting player "; }
inline const char* HeroesGroupText() { return "Heroes"; }
inline float HearingScale() { return 2.0f /*0073F910*/; }   // the units' perception scale (SyncPlayer too)
inline float WakeMargin() { return 20.0f /*0073CCF8*/; }     // added to it when units wake others

// ---- Effect constants (spell_effect_*.cpp, world_effect_*.cpp) -----------------------------------
// References, so the effects read them through a volatile (ConstAt) or pass their address (ConstPtr),
// one object per value (no Rebirth patch writes these cells).
template <float V>
inline constexpr float kFloatConstant = V;
inline const float& OneRef() { return kFloatConstant<1.0f /*0073B84C*/>; }
inline const float& ZeroRef() { return kFloatConstant<0.0f /*0073B858*/>; }
inline const float& HalfRef() { return kFloatConstant<0.5f /*0073B86C*/>; }
inline const float& AcidRayScale() { return kFloatConstant<0.4f /*0073B868*/>; }
inline const float& FireballScale() { return kFloatConstant<0.3f /*0073C54C*/>; }
inline const float& FireballNear() { return kFloatConstant<1.25f /*0073CF50*/>; }
inline const float& StrikeDepth() { return kFloatConstant<3.0f /*0073BAA4*/>; }
inline const float& StrikeHeight() { return kFloatConstant<30.0f /*0073BAF0*/>; }
inline const float& StrikeWidth() { return kFloatConstant<-7.0f /*007439B0*/>; }
inline const float& LightAbove() { return kFloatConstant<5.0f /*0073BF34*/>; }
inline const float& BoltWidthScale() { return kFloatConstant<5.0f /*0073BF34*/>; }   // 5.0 (the same constant)
inline const float& ImpactWakeMargin() { return kFloatConstant<5.0f /*0073BF34*/>; }   // 5.0 (the same constant)
inline const float& TeleportDepth() { return kFloatConstant<0.2f /*0073BAD4*/>; }
inline const float& BuffScale() { return kFloatConstant<16.0f /*0073CA98*/>; }
inline const float& TargetHeight() { return kFloatConstant<1.2f /*0073CDF4*/>; }
inline const float& RangeEpsilon() { return kFloatConstant<0.001f /*0073CCF4*/>; }
inline const float& ScorchAlpha() { return kFloatConstant<0.6f /*0073CDE0*/>; }
inline const float& ScorchMargin() { return kFloatConstant<0.3f /*0073C54C*/>; }   // 0.3 (the same constant as FireballScale)
inline const float& DefaultRadius() { return kFloatConstant<100.0f /*007433AC*/>; }
inline const float& SplashScale() { return kFloatConstant<0.7f /*0073C538*/>; }   // 0.7: the impact particle's size per power
inline const float& RiseScale() { return kFloatConstant<1.5f /*00743998*/>; }   // 1.5: impact lights rise faster and reach further
inline const float& NoiseKind() { return kFloatConstant<4.0f /*0074396C*/>; }   // 4.0, truncated by _ftol
inline const float& PossessWake() { return kFloatConstant<2.0f /*00743970*/>; }
inline const float& PossessWakeBase() { return kFloatConstant<20.0f /*0073CCF8*/>; }

// ---- AI, interface mesh and server constants ----------------------------------------------------
inline float CellSize() { return 1.0f /*0073FD18*/; }   // 1.0: the AI grid cell
inline float FixedPoint() { return 65536.0f /*0073FDC0*/; }
inline float OutsideDistance() { return 100.0f /*0073BAB0*/; }
inline float Texel() { return 0.00390625f /*0073EEB8*/; }   // 1 / 256
inline float HalfTexel() { return 0.001953125f /*0073CC90*/; }   // 1 / 512
inline float LayoutX() { return 0.00125f /*00740164*/; }   // 2 / 1600
inline float LayoutY() { return 0.0025f /*0074015C*/; }   // 1 / 400
inline float ThreeQuarters() { return 0.75f /*0073C524*/; }
inline float InterfaceView() { return 0.48157462f /*00740158*/; }   // 0.48157 (the interface camera's half view)
// By reference, where the game copies the bits or passes the address on.
inline const float& AnnouncePeriodRef() { return kFloatConstant<20.0f /*0073BB20*/>; }   // seconds between announcements to the master
inline const float& QueueWaitLimitRef() { return kFloatConstant<60.0f /*0073F090*/>; }   // squared distance a blocking ally may be away
// Tables of the AI (ai_refresh.cpp; the height of each of 8 levels is ai_map.h's kLevelHeights): per occluder type
// the cost added by full cover and the damping exponent; 5 rows x 16 dwords of cell marks.
inline constexpr int kOccluderCosts[3] = /*007912A4*/ {290, 20};
inline constexpr int kOccluderDamping[3] = /*007912B0*/ {7, 2};
inline constexpr std::uint32_t kMarkTable[80] = /*00791164*/ {
    0, 0, 1, 1, 1, 1, 4, 7, 0xB, 0xC, 0xE, 0xE, 0xF, 0xF, 0xF, 0xF, 0, 0, 1, 1, 1, 1, 4, 7, 0xB, 0xC, 0xD, 0xD,
    0xD, 0xD, 0xD, 0xD, 0, 0, 1, 1, 1, 1, 4, 7, 8, 9, 0xA, 0xA, 0xA, 0xA, 0xA, 0xA, 0, 0, 1, 1, 1, 1, 4, 5, 6, 6,
    6, 6, 6, 6, 6, 6, 0, 0, 1, 1, 1, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3};

} // namespace worlddata
