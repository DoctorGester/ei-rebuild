#pragma once

// Data of the exe that the world server's rewritten code reads by address (strings, float
// constants read from memory as the original does (rewrite guide rule 9), vtables and globals).
// world_units.cpp; names are provisional.

#include <cstdint>
#include "options.h"

#include "engine_object.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "game_data.h"
#include "game_singletons.h"

class CInterface3D;   // ui.h
class CLight;   // spell.h

namespace worlddata {

inline float ExeFloat(std::uintptr_t address) { return ei::GameData<const volatile float>(address); }

// ---- CreateUnits (0058EF20) --------------------------------------------------------------------

inline const char* StartZoneText() { return "zone1"; }
inline const char* ScriptBeginText() { return "WorldScript("; }
// AddObject( Heroes, GetObjectByID( "%i" ) )
inline const char* AddToHeroesFormat() { return "AddObject( Heroes, GetObjectByID( \"%i\" ) )"; }
// PlayAnimation(GetObjectByName( "hero" ),"uspecial25")
inline const char* HeroArrivalText() { return "PlayAnimation(GetObjectByName( \"hero\" ),\"uspecial25\")"; }
inline const char* ScriptEndText() { return ")"; }
inline const char* HairFormat() { return "hr.%.2d"; }
inline const char* HairPartText() { return "hr"; }

// The figure parts of a hero ("hd", "bd", "hp", "hr", "lh1" ... "rl3"), in the original's order.
constexpr int kHeroPartCount = 16;
inline const char* HeroPartText(int index) {
    static constexpr const char* kParts[kHeroPartCount] = {"hd",  "bd",  "hp",  "hr",  "lh1", "lh2", "lh3", "rh1",
                                                           "rh2", "rh3", "ll1", "ll2", "ll3", "rl1", "rl2", "rl3"};
    return kParts[index];
}

// Floats.
inline float HeroStartX() { return 54.0f /*0073FA9C*/; }        // the hero's start on zone1
inline float HeroStartY() { return 138.0f /*0073FAA0*/; }
inline float HeroStartRadius2() { return 300.0f /*0073FA98*/; }
inline float AxisEpsilon() { return 1e-8f /*0073BAE0*/; }
inline float One() { return 1.0f /*0073B84C*/; }
inline float Zero() { return 0.0f /*0073B858*/; }
inline float Half() { return 0.5f /*0073B86C*/; }
inline float LineAngleOffset() { return 3.1415927f /*0073F8EC*/; }
inline float LineSpacing() { return 1.2f /*0073CDF4*/; }

// The vtable of the counted record an item built from a party record points at (ItemSourceRef,
// item.h), stored after the reference value constructor 00419720.
constexpr std::uintptr_t kItemSourceRefVtable = std::uintptr_t(0x0073FA88);
// The unit record's (mapspec::UnitSpec) vtable and its behaviour records' destructor body.
constexpr std::uintptr_t kUnitSpecVtable = std::uintptr_t(0x0073C6E0);
constexpr std::uintptr_t kBehaviourDestructor = std::uintptr_t(0x0046C290);

// ---- UseQuickItem (0058ECE0) -------------------------------------------------------------------

inline const char* QuickEffectPrototype() { return "st.tr.130"; }
inline const char* QuickEffectModel() { return "unhuma"; }
// The vtable of the object record (mapspec::ObjectSpec) with no members of its own.
constexpr std::uintptr_t kQuickEffectSpecVtable = std::uintptr_t(0x0073FA58);

// ---- CreateObject (005927C0) -------------------------------------------------------------------

inline const char* CampfireText() { return "campfire"; }
inline const char* NoWaveNameText() { return "Adding sound without wave name"; }
inline float CampfireHeight() { return 0.7f /*0073C538*/; }   // 0.7
inline float TorchLightScale() { return 10.0f /*0073BAAC*/; }  // the flame's radius factor (10)

// ---- The client world's Setup (00561E50) --------------------------------------------------------

// The game's folders (CStrings): the data folder and the text resources' folder.
inline const void* TextPathString() { return &TheTextFolder(); }
inline const char* TexturesFolderText() { return "textures\\"; }
inline const char* LightsTableText() { return "LightsGipat.ini"; }
inline const char* GipathText() { return "gipath"; }
inline const char* TextsFileText() { return "texts.res"; }
inline const char* TextsLmpFileText() { return "textsLmp.res"; }
inline const char* CursorFileText() { return "wincurs\\Df0000.cur"; }
// The shadow setting (0 off, 2 blurred; map_object_shadow.cpp's ShadowMode): above 0 the client
// sets its flag 0x200.
inline int ShadowSetting() { return static_cast<const volatile int&>(options::TheOptions()->shadowQuality_); }

// ---- The client world's zone load (00567AB0) ----------------------------------------------------

inline const char* DefaultAllodText() { return "gipat"; }
// "Can't load terrain or map file "%s"." and a line end
inline const char* CantLoadTerrainFormat() { return "Can't load terrain or map file \"%s\".\n"; }
inline const char* CaveSkyText() { return "Sky01"; }
inline const char* OpenSkyText() { return "Sky00"; }
inline const char* LightsPrefixText() { return "Lights"; }
inline const char* CaveText() { return "Cave"; }
inline const char* IniSuffixText() { return ".ini"; }
inline const char* FinalZoneText() { return "gz20g"; }
inline const char* FinalMusicText() { return "final"; }
// Options (the object at 0079BC70; options.cpp): Audio, EnableWaterWaves, EnableSelfShadowing,
// LightingFreq (milliseconds; the zone load turns it into frames at 66 ms each) and
// DefaultWindForce. AudioStreaming / AudioSFX are MovieVolumeSetting / VolumeSettingA below.
inline int AudioOption() { return static_cast<const volatile int&>(options::TheOptions()->audio_); }
inline int WaterWavesOption() { return static_cast<const volatile int&>(options::TheOptions()->enableWaterWaves_); }
inline int SelfShadowingOption() { return static_cast<const volatile int&>(options::TheOptions()->enableSelfShadowing_); }
inline int LightingFrequencyOption() { return static_cast<const volatile int&>(options::TheOptions()->lightingFreq_); }
inline float DefaultWindForceOption() { return static_cast<const volatile float&>(options::TheOptions()->defaultWindForce_); }
// The hour the terrain's sun was last set for (world_client_lights.cpp); -1 makes the next UpdateLighting set it.
inline float g_lastLitHour = /*0078FF38*/ -1.0f;

// Vtables Setup stores while it inlines the constructors.
constexpr std::uintptr_t kClientTextsVtable = std::uintptr_t(0x0073F6F0);
constexpr std::uintptr_t kTerrainVtable = std::uintptr_t(0x0073F6A8);
constexpr std::uintptr_t kClientHandleVtable = std::uintptr_t(0x0073F628);
constexpr std::uintptr_t kClientListVtable = std::uintptr_t(0x0073F61C);   // the lightning manager's CList
constexpr std::uintptr_t kPlatformListVtable = std::uintptr_t(0x0073F548);       // the platform set's CList
constexpr std::uintptr_t kClientArrayListVtable = std::uintptr_t(0x0073F73C);   // the array manager's CList

// ---- WorldCreateManagers (0055EE40) -----------------------------------------------------------

// The game's folders (CStrings: TheDataFolder, TheTextFolder, TheDiscFolder the base folder; game_singletons.h)
// and the configured paths they are set from (the CStrings 0079BC90 / 0079BC94 / 0079BC98; their
// text pointers).
inline void* TextFolderString() { return &TheTextFolder(); }
inline void* BaseFolderString() { return &TheDiscFolder(); }
inline const char* ConfiguredDataPath() { return static_cast<const char*>(options::TheOptions()->workPath_); }
inline const char* ConfiguredTextPath() { return static_cast<const char*>(options::TheOptions()->resourcesPath_); }
inline const char* ConfiguredBasePath() { return static_cast<const char*>(options::TheOptions()->cdRomPath_); }
// The counted helper's vtable (+0x5C), which WorldCreateManagers stores.
constexpr std::uintptr_t kSharedHelperVtable = std::uintptr_t(0x0073F240);

// ---- The client's Segment (005641F0) ------------------------------------------------------------

inline const char* AutosaveEventText() { return "autosave"; }
// "CWorldClient::Segment(): ..." (the lost connection's FatalError).
inline const char* SegmentDisconnectedText() { return "CWorldClient::Segment().\nDisconnecting client due to packet parsing error.\n"; }
// The volume settings the sound manager takes on the client's tenth tick (006B7740 / 006B78A0 /
// 006B7A00; the last is the movies' volume, bink_video.cpp).
inline int VolumeSettingA() { return options::TheOptions()->audioSfx_; }
inline int VolumeSettingB() { return options::TheOptions()->audioSpeech_; }
inline int MovieVolumeSetting() { return options::TheOptions()->audioStreaming_; }
// 007AFC9C: 2 while the party fights (sound_music.cpp).
inline int FightState() { return ei::GameData<const volatile int>(0x007AFC9C); }
// The light effects' grey levels: on when 0079BD58 is set; the countdown takes the world update
// period (0078FF34) per tick and restarts at 0079BD24.
inline int LightLevelsSetting() { return options::TheOptions()->enableBloodprintsRelight_; }
inline int WorldUpdatePeriod() { return ei::GameData<const int>(0x0078FF34); }
inline int LightLevelsPeriod() { return options::TheOptions()->shadowLightingFreq_; }
// The scene's sun light (007AF07C; figure_part_draw.cpp) and the point 007AFDB0 the grey levels
// start from (its distance to the sun; meaning unknown).
inline CLight* SceneSun() { return ei::GameData<CLight* const>(0x007AF07C); }
inline const void* LightReferencePoint() { return ei::GamePointer<const void>(0x007AFDB0); }
inline float ColourScale() { return 255.0f /*0073C370*/; }   // 255

// ---- The world server's quests (world_quests.cpp) -----------------------------------------------

// The zones where the ten mercenaries are briefed.
inline const char* MercZoneName(int slot) {
    static constexpr const char* kZones[10] = {"bz1g", "bz1g", "bz2g", "bz6g", "bz8k", "bz8k", "bz14h", "bz14h", "bz9k", "bz14h"};
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
// "WorldScript( GSSetVar( %i,"%s",2 ) #OnBriefingComplete( %i, "%s" ) )"
inline const char* BriefingCompleteScript() { return "WorldScript( GSSetVar( %i,\"%s\",2 ) #OnBriefingComplete( %i, \"%s\" ) )"; }
inline const char* HumanHeroText() { return "Human Hero"; }
// The quest database's briefings by name (an STLport hash_map<string, BriefingRecord*>, ui_briefing.h;
// operator[] 004297A0: stlp::NamedSlot).
stlp::NameMap& QuestBriefings();   // database_globals.cpp
// The vtables of the lists and arrays the world server builds on the stack: CPtrList of items
// (0073BE80), CList of experience shares (0073FAB0), CArray of unit record pointers (0073FAA4).
constexpr std::uintptr_t kItemListVtable = std::uintptr_t(0x0073BE80);

// ---- Update (world_update.cpp) --------------------------------------------------------------
inline const char* DisconnectingPlayerText() { return "Disconnecting player "; }
inline const char* HeroesGroupText() { return "Heroes"; }
inline float HearingScale() { return 2.0f /*0073F910*/; }   // the units' perception scale (SyncPlayer too)
inline float WakeMargin() { return 20.0f /*0073CCF8*/; }     // added to it when units wake others

// ---- Effect constants (spell_effect_*.cpp, world_effect_*.cpp) -----------------------------------
// References, so the effects read them through a volatile (ConstAt) or pass their address (ConstPtr)
// as the original does (one object per value; no Rebirth patch writes these cells).
template <float V>
inline constexpr float kFloatConstant = V;
inline const float& OneRef() { return kFloatConstant<1.0f /*0073B84C*/>; }   // 1.0
inline const float& ZeroRef() { return kFloatConstant<0.0f /*0073B858*/>; }   // 0.0
inline const float& HalfRef() { return kFloatConstant<0.5f /*0073B86C*/>; }   // 0.5
inline const float& AcidRayScale() { return kFloatConstant<0.4f /*0073B868*/>; }   // 0.4
inline const float& FireballScale() { return kFloatConstant<0.3f /*0073C54C*/>; }   // 0.3
inline const float& FireballNear() { return kFloatConstant<1.25f /*0073CF50*/>; }   // 1.25
inline const float& StrikeDepth() { return kFloatConstant<3.0f /*0073BAA4*/>; }   // 3.0
inline const float& StrikeHeight() { return kFloatConstant<30.0f /*0073BAF0*/>; }   // 30.0
inline const float& StrikeWidth() { return kFloatConstant<-7.0f /*007439B0*/>; }   // -7.0
inline const float& LightAbove() { return kFloatConstant<5.0f /*0073BF34*/>; }   // 5.0
inline const float& BoltWidthScale() { return kFloatConstant<5.0f /*0073BF34*/>; }   // 5.0 (the same constant)
inline const float& ImpactWakeMargin() { return kFloatConstant<5.0f /*0073BF34*/>; }   // 5.0 (the same constant)
inline const float& TeleportDepth() { return kFloatConstant<0.2f /*0073BAD4*/>; }   // 0.2
inline const float& BuffScale() { return kFloatConstant<16.0f /*0073CA98*/>; }   // 16.0
inline const float& TargetHeight() { return kFloatConstant<1.2f /*0073CDF4*/>; }   // 1.2
inline const float& RangeEpsilon() { return kFloatConstant<0.001f /*0073CCF4*/>; }   // 0.001
inline const float& ScorchAlpha() { return kFloatConstant<0.6f /*0073CDE0*/>; }   // 0.6
inline const float& ScorchMargin() { return kFloatConstant<0.3f /*0073C54C*/>; }   // 0.3 (the same constant as FireballScale)
inline const float& DefaultRadius() { return kFloatConstant<100.0f /*007433AC*/>; }   // 100.0
inline const float& SplashScale() { return kFloatConstant<0.7f /*0073C538*/>; }   // 0.7: the impact particle's size per power
inline const float& RiseScale() { return kFloatConstant<1.5f /*00743998*/>; }   // 1.5: impact lights rise faster and reach further
inline const float& NoiseKind() { return kFloatConstant<4.0f /*0074396C*/>; }   // 4.0, truncated by _ftol
inline const float& PossessWake() { return kFloatConstant<2.0f /*00743970*/>; }   // 2.0
inline const float& PossessWakeBase() { return kFloatConstant<20.0f /*0073CCF8*/>; }   // 20.0

// ---- AI, interface mesh and server constants ----------------------------------------------------
inline float CellSize() { return 1.0f /*0073FD18*/; }   // 1.0: the AI grid cell
inline float FixedPoint() { return 65536.0f /*0073FDC0*/; }   // 65536.0
inline float OutsideDistance() { return 100.0f /*0073BAB0*/; }   // 100.0
inline float Texel() { return 0.00390625f /*0073EEB8*/; }   // 1 / 256
inline float HalfTexel() { return 0.001953125f /*0073CC90*/; }   // 1 / 512
inline float LayoutX() { return 0.00125f /*00740164*/; }   // 2 / 1600
inline float LayoutY() { return 0.0025f /*0074015C*/; }   // 1 / 400
inline float ThreeQuarters() { return 0.75f /*0073C524*/; }   // 0.75
inline float InterfaceView() { return 0.48157462f /*00740158*/; }   // 0.48157 (the interface camera's half view)
inline float DampScale() { return ExeFloat(std::uintptr_t(0x007C2F8C)); }   // the occluders' damping scale (ai_refresh.cpp)
// By reference, where the original copies the bits or passes the address on.
inline const float& AnnouncePeriodRef() { return kFloatConstant<20.0f /*0073BB20*/>; }   // seconds between announcements to the master
inline const float& QueueWaitLimitRef() { return kFloatConstant<60.0f /*0073F090*/>; }   // squared distance a blocking ally may be away
// Tables of the AI (ai_refresh.cpp): the height of each of 8 levels; per occluder type the cost added by full
// cover and the damping exponent; 5 rows x 16 dwords of cell marks.
inline const float* LevelHeights() { return ei::GamePointer<const float>(0x007912BC); }
inline constexpr int kOccluderCosts[3] = /*007912A4*/ {290, 20};
inline constexpr int kOccluderDamping[3] = /*007912B0*/ {7, 2};
inline constexpr std::uint32_t kMarkTable[80] = /*00791164*/ {
    0, 0, 1, 1, 1, 1, 4, 7, 0xB, 0xC, 0xE, 0xE, 0xF, 0xF, 0xF, 0xF, 0, 0, 1, 1, 1, 1, 4, 7, 0xB, 0xC, 0xD, 0xD,
    0xD, 0xD, 0xD, 0xD, 0, 0, 1, 1, 1, 1, 4, 7, 8, 9, 0xA, 0xA, 0xA, 0xA, 0xA, 0xA, 0, 0, 1, 1, 1, 1, 4, 5, 6, 6,
    6, 6, 6, 6, 6, 6, 0, 0, 1, 1, 1, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3};

} // namespace worlddata
