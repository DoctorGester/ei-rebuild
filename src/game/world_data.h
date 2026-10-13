#pragma once

// Data of the exe that the world server's rewritten code reads by address (strings, float
// constants read from memory as the original does (rewrite guide rule 9), vtables and globals).
// world_units.cpp; names are provisional.

#include <cstdint>

#include "engine_object.h"
#include "lib/stlp.h"

class CLight;   // spell.h

namespace worlddata {

inline float ExeFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// ---- CreateUnits (0058EF20) --------------------------------------------------------------------

inline const char* StartZoneText() { return reinterpret_cast<const char*>(0x00790950); }      // "zone1"
inline const char* ScriptBeginText() { return reinterpret_cast<const char*>(0x00790940); }    // "WorldScript("
// AddObject( Heroes, GetObjectByID( "%i" ) )
inline const char* AddToHeroesFormat() { return reinterpret_cast<const char*>(0x00790914); }
// PlayAnimation(GetObjectByName( "hero" ),"uspecial25")
inline const char* HeroArrivalText() { return reinterpret_cast<const char*>(0x007908DC); }
inline const char* ScriptEndText() { return reinterpret_cast<const char*>(0x0078D71C); }      // ")"
inline const char* HairFormat() { return reinterpret_cast<const char*>(0x0078FB0C); }         // "hr.%.2d"
inline const char* HairPartText() { return reinterpret_cast<const char*>(0x0078FB38); }       // "hr"

// The figure parts of a hero ("hd", "bd", "hp", "hr", "lh1" ... "rl3"), in the original's order.
constexpr int kHeroPartCount = 16;
inline const char* HeroPartText(int index) {
    static constexpr std::uintptr_t kParts[kHeroPartCount] = {
        std::uintptr_t(0x0078AD74), std::uintptr_t(0x0078AD38), std::uintptr_t(0x007900E4), std::uintptr_t(0x0078FB38),
        std::uintptr_t(0x007900E0), std::uintptr_t(0x0078AD70), std::uintptr_t(0x007900DC), std::uintptr_t(0x007900D8),
        std::uintptr_t(0x0078AD6C), std::uintptr_t(0x0078F95C), std::uintptr_t(0x007900D4), std::uintptr_t(0x0078AD68),
        std::uintptr_t(0x0078B008), std::uintptr_t(0x007900D0), std::uintptr_t(0x0078AD64), std::uintptr_t(0x0078B004)};
    return reinterpret_cast<const char*>(kParts[index]);
}

// Floats.
inline float HeroStartX() { return ExeFloat(std::uintptr_t(0x0073FA9C)); }        // the hero's start on zone1
inline float HeroStartY() { return ExeFloat(std::uintptr_t(0x0073FAA0)); }
inline float HeroStartRadius2() { return ExeFloat(std::uintptr_t(0x0073FA98)); }
inline float AxisEpsilon() { return ExeFloat(std::uintptr_t(0x0073BAE0)); }
inline float One() { return ExeFloat(std::uintptr_t(0x0073B84C)); }
inline float Zero() { return ExeFloat(std::uintptr_t(0x0073B858)); }
inline float LineAngleOffset() { return ExeFloat(std::uintptr_t(0x0073F8EC)); }
inline float LineSpacing() { return ExeFloat(std::uintptr_t(0x0073CDF4)); }

// The vtable of the counted record an item built from a party record points at (ItemSourceRef,
// item.h), stored after the reference value constructor 00419720.
constexpr std::uintptr_t kItemSourceRefVtable = std::uintptr_t(0x0073FA88);
// The unit record's (mapspec::UnitSpec) vtable and its behaviour records' destructor body.
constexpr std::uintptr_t kUnitSpecVtable = std::uintptr_t(0x0073C6E0);
constexpr std::uintptr_t kBehaviourDestructor = std::uintptr_t(0x0046C290);

// The unit prototypes' name map (an STLport hash_map<string, UnitPrototypeRecord*>, unit_db.h).
inline void* PrototypeNameMap() { return reinterpret_cast<void*>(0x007AF364); }

// ---- UseQuickItem (0058ECE0) -------------------------------------------------------------------

inline const char* QuickEffectPrototype() { return reinterpret_cast<const char*>(0x007908D0); }   // "st.tr.130"
inline const char* QuickEffectModel() { return reinterpret_cast<const char*>(0x007908C8); }       // "unhuma"
// The vtable of the object record (mapspec::ObjectSpec) with no members of its own.
constexpr std::uintptr_t kQuickEffectSpecVtable = std::uintptr_t(0x0073FA58);

// ---- CreateObject (005927C0) -------------------------------------------------------------------

inline const char* CampfireText() { return reinterpret_cast<const char*>(0x00790A54); }
inline const char* NoWaveNameText() { return reinterpret_cast<const char*>(0x00790A34); }
inline float CampfireHeight() { return ExeFloat(std::uintptr_t(0x0073C538)); }   // 0.7
inline float TorchLightScale() { return ExeFloat(std::uintptr_t(0x0073BAAC)); }  // the flame's radius factor (10)

// ---- The client world's Setup (00561E50) --------------------------------------------------------

// The game's folders (CStrings): the data folder and the text resources' folder.
inline const void* DataPathString() { return reinterpret_cast<const void*>(std::uintptr_t(0x007AF460)); }
inline const void* TextPathString() { return reinterpret_cast<const void*>(std::uintptr_t(0x007AF45C)); }
inline const char* TexturesFolderText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078FFE4)); }   // "textures\"
inline const char* LightsTableText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078FFD4)); }      // "LightsGipat.ini"
inline const char* GipathText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078FFCC)); }           // "gipath"
inline const char* TextsFileText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078FFC0)); }        // "texts.res"
inline const char* TextsLmpFileText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078FFB0)); }     // "textsLmp.res"
inline const char* CursorFileText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078FF9C)); }       // "wincurs\Df0000.cur"
// The shadow setting (0 off, 2 blurred; map_object_shadow.cpp's ShadowMode): above 0 the client
// sets its flag 0x200.
inline int ShadowSetting() { return *reinterpret_cast<const volatile int*>(std::uintptr_t(0x0079BD0C)); }

// Vtables Setup stores while it inlines the constructors.
constexpr std::uintptr_t kClientTextsVtable = std::uintptr_t(0x0073F6F0);
constexpr std::uintptr_t kTerrainVtable = std::uintptr_t(0x0073F6A8);
constexpr std::uintptr_t kParticleManagerVtable = std::uintptr_t(0x0073F644);
constexpr std::uintptr_t kClientHandleVtable = std::uintptr_t(0x0073F628);
constexpr std::uintptr_t kClientListVtable = std::uintptr_t(0x0073F61C);   // the list manager's CList
constexpr std::uintptr_t kClientListManagerVtable = std::uintptr_t(0x0073F5B8);
constexpr std::uintptr_t kClientArrayManagerVtable = std::uintptr_t(0x0073F554);
constexpr std::uintptr_t kPlatformListVtable = std::uintptr_t(0x0073F548);       // the platform set's CList
constexpr std::uintptr_t kClientArrayListVtable = std::uintptr_t(0x0073F73C);   // the array manager's CList

// ---- WorldCreateManagers (0055EE40) -----------------------------------------------------------

// The game's folders (CStrings 007AF460 data, 007AF45C text resources, 007AF458 the base folder)
// and the configured paths they are set from (the CStrings 0079BC90 / 0079BC94 / 0079BC98; their
// text pointers).
inline void* DataFolderString() { return reinterpret_cast<void*>(std::uintptr_t(0x007AF460)); }
inline void* TextFolderString() { return reinterpret_cast<void*>(std::uintptr_t(0x007AF45C)); }
inline void* BaseFolderString() { return reinterpret_cast<void*>(std::uintptr_t(0x007AF458)); }
inline const char* ConfiguredDataPath() { return *reinterpret_cast<const char* const*>(std::uintptr_t(0x0079BC90)); }
inline const char* ConfiguredTextPath() { return *reinterpret_cast<const char* const*>(std::uintptr_t(0x0079BC94)); }
inline const char* ConfiguredBasePath() { return *reinterpret_cast<const char* const*>(std::uintptr_t(0x0079BC98)); }
// The vtables WorldCreateManagers stores after the base manager's constructor: the player list
// (+0x38) and the timed objects (+0x48); and the counted helper's (+0x5C).
constexpr std::uintptr_t kPlayerListVtable = std::uintptr_t(0x0073F328);
constexpr std::uintptr_t kTimedObjectsVtable = std::uintptr_t(0x0073F2C4);
constexpr std::uintptr_t kSharedHelperVtable = std::uintptr_t(0x0073F240);

// ---- The client's Segment (005641F0) ------------------------------------------------------------

// The interface manager (007C7D50; script string events go to it).
inline void* InterfaceManager() { return reinterpret_cast<void*>(std::uintptr_t(0x007C7D50)); }
inline const char* AutosaveEventText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00787704)); }   // "autosave"
// "CWorldClient::Segment(): ..." (the lost connection's FatalError).
inline const char* SegmentDisconnectedText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00790084)); }
// The volume settings the sound manager takes on the client's tenth tick (006B7740 / 006B78A0 /
// 006B7A00; the last is the movies' volume, bink_video.cpp).
inline int VolumeSettingA() { return *reinterpret_cast<const int*>(std::uintptr_t(0x0079BCC8)); }
inline int VolumeSettingB() { return *reinterpret_cast<const int*>(std::uintptr_t(0x0079BCCC)); }
inline int MovieVolumeSetting() { return *reinterpret_cast<const int*>(std::uintptr_t(0x0079BCC4)); }
// 007AFC9C: 2 while the party fights (sound_music.cpp).
inline int FightState() { return *reinterpret_cast<const volatile int*>(std::uintptr_t(0x007AFC9C)); }
// The interface's list of the multiplayer peers (an STLport vector of 4-byte entries at 007C7D84).
inline const stlp::vector<int>& PeerList() { return *reinterpret_cast<const stlp::vector<int>*>(std::uintptr_t(0x007C7D84)); }
// The light effects' grey levels: on when 0079BD58 is set; the countdown takes the world update
// period (0078FF34) per tick and restarts at 0079BD24.
inline int LightLevelsSetting() { return *reinterpret_cast<const int*>(std::uintptr_t(0x0079BD58)); }
inline int WorldUpdatePeriod() { return *reinterpret_cast<const int*>(std::uintptr_t(0x0078FF34)); }
inline int LightLevelsPeriod() { return *reinterpret_cast<const int*>(std::uintptr_t(0x0079BD24)); }
// The scene's sun light (007AF07C; figure_part_draw.cpp) and the point 007AFDB0 the grey levels
// start from (its distance to the sun; meaning unknown).
inline CLight* SceneSun() { return *reinterpret_cast<CLight* const*>(std::uintptr_t(0x007AF07C)); }
inline const void* LightReferencePoint() { return reinterpret_cast<const void*>(std::uintptr_t(0x007AFDB0)); }
inline float ColourScale() { return ExeFloat(std::uintptr_t(0x0073C370)); }   // 255

// ---- The world server's quests (world_quests.cpp) -----------------------------------------------

// The zones where the ten mercenaries are briefed (pointers at 007907AC).
inline const char* MercZoneName(int slot) { return reinterpret_cast<const char* const*>(std::uintptr_t(0x007907AC))[slot]; }
inline const char* MercNameFormat() { return reinterpret_cast<const char*>(std::uintptr_t(0x007909E4)); }   // "merc%i"
// The briefing variables of a mercenary slot: "b.%s.n<k>_%i" and "amerc_%i".
inline const char* MercVariableN8Format() { return reinterpret_cast<const char*>(std::uintptr_t(0x007909D8)); }
inline const char* MercVariableN1Format() { return reinterpret_cast<const char*>(std::uintptr_t(0x007909CC)); }
inline const char* MercVariableN2Format() { return reinterpret_cast<const char*>(std::uintptr_t(0x007909C0)); }
inline const char* MercVariableN3Format() { return reinterpret_cast<const char*>(std::uintptr_t(0x007909B4)); }
inline const char* MercVariableN9Format() { return reinterpret_cast<const char*>(std::uintptr_t(0x007909A8)); }
inline const char* MercVariableN10Format() { return reinterpret_cast<const char*>(std::uintptr_t(0x0079099C)); }
inline const char* MercVariableN11Format() { return reinterpret_cast<const char*>(std::uintptr_t(0x00790990)); }
inline const char* MercHiredFormat() { return reinterpret_cast<const char*>(std::uintptr_t(0x00790984)); }
// "WorldScript( GSSetVar( %i,"%s",2 ) #OnBriefingComplete( %i, "%s" ) )"
inline const char* BriefingCompleteScript() { return reinterpret_cast<const char*>(std::uintptr_t(0x007909EC)); }
inline const char* HumanHeroText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078B0FC)); }   // "Human Hero"
// The quest database's briefings by name (an STLport hash_map<string, BriefingRecord*>, ui_briefing.h).
inline void* QuestBriefings() { return reinterpret_cast<void*>(std::uintptr_t(0x007C87D4)); }
// The vtables of the lists and arrays the world server builds on the stack: CPtrList of items
// (0073BE80), CList of experience shares (0073FAB0), CArray of unit record pointers (0073FAA4).
constexpr std::uintptr_t kItemListVtable = std::uintptr_t(0x0073BE80);
constexpr std::uintptr_t kShareListVtable = std::uintptr_t(0x0073FAB0);
constexpr std::uintptr_t kRecordArrayVtable = std::uintptr_t(0x0073FAA4);

// ---- Update (world_update.cpp) --------------------------------------------------------------
inline const char* DisconnectingPlayerText() { return reinterpret_cast<const char*>(std::uintptr_t(0x00790894)); }   // "Disconnecting player "
inline const char* HeroesGroupText() { return reinterpret_cast<const char*>(std::uintptr_t(0x007908C0)); }   // "Heroes"
inline float HearingScale() { return ExeFloat(std::uintptr_t(0x0073F910)); }   // the units' perception scale (SyncPlayer too)
inline float WakeMargin() { return ExeFloat(std::uintptr_t(0x0073CCF8)); }     // added to it when units wake others
// 00435000: the ostream flush manipulator (ostream& flush(ostream&)).
inline void* FlushManipulator() { return reinterpret_cast<void*>(std::uintptr_t(0x00435000)); }

} // namespace worlddata
