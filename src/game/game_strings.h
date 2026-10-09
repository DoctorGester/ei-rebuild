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

// 0073F920: "gtime" (a player's game-time variable; CWorldServer::UpdatePlayerTimes).
inline const char* GTimeText() { return reinterpret_cast<const char*>(0x0073F920); }
// 00790BB8: "CWorldServer::LoadSave(): Can't load map..." (LoadSave's FatalError).
inline const char* CantLoadSaveMapText() { return reinterpret_cast<const char*>(0x00790BB8); }

// The trader stock's prototype names (item_stock.cpp): 0079391C "wand" (skipped), 007938C4
// "material", 00796370 "instruction weapon", 0079635C "instruction armor", 0079638C "instruction
// quick item", 00796384 "scroll" (no instruction), 00793924 "prototype", 007963A4 "rune", 007938A0
// "spell container".
inline const char* WandText() { return reinterpret_cast<const char*>(0x0079391C); }
inline const char* MaterialText() { return reinterpret_cast<const char*>(0x007938C4); }
inline const char* InstructionWeaponText() { return reinterpret_cast<const char*>(0x00796370); }
inline const char* InstructionArmorText() { return reinterpret_cast<const char*>(0x0079635C); }
inline const char* InstructionQuickItemText() { return reinterpret_cast<const char*>(0x0079638C); }
inline const char* ScrollText() { return reinterpret_cast<const char*>(0x00796384); }
inline const char* PrototypeText() { return reinterpret_cast<const char*>(0x00793924); }
inline const char* RuneText() { return reinterpret_cast<const char*>(0x007963A4); }
inline const char* SpellContainerText() { return reinterpret_cast<const char*>(0x007938A0); }

// The interface's help topics file (CInterface3D, Create 005F4990 / Shutdown 005F4F90): 00792FF0
// "config\tutorial.ini" (after the data path), 00793044 "%s %d\n" (a topic and its count).
inline const char* TutorialIniText() { return reinterpret_cast<const char*>(0x00792FF0); }
inline const char* TutorialLineFormat() { return reinterpret_cast<const char*>(0x00793044); }

// The flying hit points' draw (005E18E0): 0078EEEC "%d" (the amount), 00792D84
// "CFlyingHitPoints::Draw.\n SetTexture failed.\n" (its FatalError).
inline const char* DecimalFormat() { return reinterpret_cast<const char*>(0x0078EEEC); }
inline const char* FlyingHitPointsTextureText() { return reinterpret_cast<const char*>(0x00792D84); }

} // namespace game
