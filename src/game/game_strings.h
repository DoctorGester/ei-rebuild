#pragma once

// Strings of the exe's data that the rewritten code uses by address.

#include <cstdint>

namespace game {

// 0079B298: the exe's empty string (""). The game's text readers and CString constructors fall back
// to it, and some loops compare against this address (not only against the contents).
constexpr std::uintptr_t kEmptyTextAddress = 0x0079B298;
inline const char* EmptyText() { return reinterpret_cast<const char*>(kEmptyTextAddress); }

// 00795D74: "Can't find texture format %d.\n" (the texture manager's FatalError, 00691D80).
inline const char* CantFindTextureFormatText() { return reinterpret_cast<const char*>(0x00795D74); }

// 0078E6C4: "Can't get surface description" (the window surfaces' FatalDirectX, 005FCC00 / 005FD120).
inline const char* CantGetSurfaceDescriptionText() { return reinterpret_cast<const char*>(0x0078E6C4); }
// 0078FB4C: ".mmp" (005FCF80's picture file name).
inline const char* MmpExtensionText() { return reinterpret_cast<const char*>(0x0078FB4C); }

// The battle screen's (ui_hud.cpp): 007935BC "buttons\battle\on_off.wav" (a portrait's unit
// clicked), 00794418 "Terrain Coords " and 0078DA40 " " (the console line of a click on the
// ground), 007900E8 "z." (a zone exit's player variable prefix).
inline const char* SoundOnOffText() { return reinterpret_cast<const char*>(0x007935BC); }
inline const char* TerrainCoordsText() { return reinterpret_cast<const char*>(0x00794418); }
inline const char* SpaceText() { return reinterpret_cast<const char*>(0x0078DA40); }
inline const char* ZoneVariablePrefixText() { return reinterpret_cast<const char*>(0x007900E8); }

// 007846F0: "none" (a prototype's weapon name that gives no weapon).
inline const char* NoneText() { return reinterpret_cast<const char*>(0x007846F0); }

// The bow shot's figure (CEffectArrowFigure): model 0079543C "efar1", texture 00795434 "arrow01",
// and the class name 00795420 "CEffectArrowFigure".
inline const char* ArrowFigureModelText() { return reinterpret_cast<const char*>(0x0079543C); }
inline const char* ArrowFigureTextureText() { return reinterpret_cast<const char*>(0x00795434); }

// The bird circling the followed hero (CEffectMoshka): model 00795474 "unmobi", texture 0079546C
// "bird00".
inline const char* MoshkaModelText() { return reinterpret_cast<const char*>(0x00795474); }
inline const char* MoshkaTextureText() { return reinterpret_cast<const char*>(0x0079546C); }

// 0079548C: the tornado's looping sound "nature\tornado\circle.wav" (CEffectTornado).
inline const char* TornadoSoundText() { return reinterpret_cast<const char*>(0x0079548C); }

} // namespace game
