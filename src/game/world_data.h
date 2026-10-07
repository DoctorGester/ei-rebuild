#pragma once

// Data of the exe that the world server's rewritten code reads by address (strings, float
// constants read from memory as the original does (rewrite guide rule 9), vtables and globals).
// world_units.cpp; names are provisional.

#include <cstdint>

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

} // namespace worlddata
