#pragma once

// Strings of the exe's data that the rewritten code uses by address.

#include "game_data.h"

#include <cstdint>

namespace game {

// A string constant of game.exe, passed by its original address. In ei_vectors the string is in the
// stand-in for the game's data (game_data.h), put there by a fixture. New code writes the literal instead:
// vanilla's and Rebirth's exe hold the same data (docs/notes/standalone.md, section 1).
inline const char* Text(std::uintptr_t address) { return &ei::GameData<const char>(address); }

// 0079B298: the exe's empty string (""). The game's text readers and CString constructors fall back
// to it, and some loops compare against this address (not only against the contents).
constexpr std::uintptr_t kEmptyTextAddress = 0x0079B298;
inline const char* EmptyText() { return &ei::GameData<const char>(kEmptyTextAddress); }

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

// The figure manager's file names (figure_manager.cpp): 00790C6C "%s.fig", 00790C90 "%s%s.fig",
// 00790C74 "Can't open file \"%s%s.fig\"\n" (its FatalError), 00784630 ".ast" (a bone's animation).
inline const char* FigFileFormat() { return reinterpret_cast<const char*>(0x00790C6C); }
inline const char* FigPathFormat() { return reinterpret_cast<const char*>(0x00790C90); }
inline const char* CantOpenFigText() { return reinterpret_cast<const char*>(0x00790C74); }
inline const char* AstExtensionText() { return reinterpret_cast<const char*>(0x00784630); }

// 00783D6C: "?" (the server's and the client's lists show it for a name that is missing).
inline const char* QuestionMarkText() { return reinterpret_cast<const char*>(0x00783D6C); }

// The suffixes of the zone messages and the quest registry keys: 00783D70 "_1" (enter), 00783D80 "_2"
// (leave), 00783D84 "_3" (finish).
inline const char* ZoneSuffix1Text() { return reinterpret_cast<const char*>(0x00783D70); }
inline const char* ZoneSuffix2Text() { return reinterpret_cast<const char*>(0x00783D80); }
inline const char* ZoneSuffix3Text() { return reinterpret_cast<const char*>(0x00783D84); }

// The interface sounds: 007937E4 "buttons\messbox\cancel.wav", 00793A88 "buttons\messbox\ok.wav",
// 00793A60 "buttons\save\select.wav", 007937B0 "buttons\camp\put_on.wav", 007937C8
// "buttons\camp\put_off.wav", 00793764 (the hero screen's rotate arrows).
inline const char* CancelSoundText() { return reinterpret_cast<const char*>(0x007937E4); }
inline const char* OkSoundText() { return reinterpret_cast<const char*>(0x00793A88); }
inline const char* SelectSoundText() { return reinterpret_cast<const char*>(0x00793A60); }
inline const char* PutOnSoundText() { return reinterpret_cast<const char*>(0x007937B0); }
inline const char* PutOffSoundText() { return reinterpret_cast<const char*>(0x007937C8); }
inline const char* RotateSoundText() { return reinterpret_cast<const char*>(0x00793764); }

// The map files' keywords (map_database.cpp: the words of a map's .ini lines).
namespace mapwords {
inline const char* Allod() { return "#allod"; }
inline const char* Zone() { return "#zone"; }
inline const char* Game() { return "game"; }
inline const char* Brief() { return "brief"; }
inline const char* Edge() { return "edge"; }
inline const char* Res() { return "#res"; }
inline const char* Camera() { return "#camera"; }
inline const char* Maps() { return "#maps"; }
inline const char* Figure() { return "#figure"; }
inline const char* Weather() { return "#weather"; }
inline const char* Snow() { return "snow"; }
inline const char* Tornado() { return "tornado"; }
inline const char* Cage() { return "#cage"; }
inline const char* Restrict() { return "#restrict"; }
inline const char* Sky() { return "#sky"; }
inline const char* Cave() { return "cave"; }
inline const char* Exit() { return "#exit"; }
inline const char* Deploy() { return "#deploy"; }
inline const char* Remove() { return "#remove"; }
inline const char* PassTime() { return "#passtime"; }
inline const char* View() { return "#view"; }
inline const char* Zero() { return "0"; }
inline const char* DeployAngle() { return "#deployangle"; }
}  // namespace mapwords

// The quest file's section and key names (quest_database.cpp).
namespace questwords {
inline const char* Name() { return "name"; }
inline const char* Quest() { return "quest"; }
inline const char* Exp() { return "exp"; }
inline const char* Money() { return "money"; }
inline const char* ZoneIndex() { return "zone index"; }
inline const char* Order() { return "order"; }
inline const char* QuestPlaces() { return "questplaces"; }
inline const char* BriefingReceive() { return "briefing receive"; }
inline const char* BriefingReject() { return "briefing reject"; }
inline const char* BriefingComplete() { return "briefing complete"; }
// The six lists of a briefing, by index: "give items", "remove items", "give quests", "complete quests", "available zones", "complete zones".
inline const char* ListKey(int index) {
    switch (index) {
    case 0: return "give items";
    case 1: return "remove items";
    case 2: return "give quests";
    case 3: return "complete quests";
    case 4: return "available zones";
    default: return "complete zones";
    }
}
}  // namespace questwords

// The save/load screens' file names and texts (ui_saveload.cpp).
namespace savewords {
inline const char* SavePattern() { return "%ssaves\\save*"; }
inline const char* InfoFile() { return "info.sav"; }
inline const char* InfoInFolder() { return "\\info.sav"; }
inline const char* ShotFile() { return "shot.sav"; }
inline const char* BrokenTitle() { return "Error Save"; }
inline const char* NewFolder() { return "save%d"; }
inline const char* GameTime() { return "gtime"; }
inline const char* NotifySaving() { return "notify_saving"; }
inline const char* NoSpace() { return "no_disc_space"; }
inline const char* NoSpaceText() { return "no_disc_space_msg"; }
inline const char* CantLockSurface() { return "Can't lock surface.\n"; }  // "Can't lock surface.\n"
inline const char* CantUnlockSurface() { return "Can't unlock surface.\n"; }  // "Can't unlock surface.\n"
}  // namespace savewords

// The briefing and quest registry texts (world_briefing.cpp).
namespace briefingwords {
inline const char* BriefingSection() { return "briefing "; }
inline const char* ShowTag() { return "#show"; }
inline const char* Hero() { return "Hero"; }
inline const char* ElderPrefix() { return "b.elder."; }
inline const char* VariablePrefix() { return "b."; }
inline const char* QuestRegistry() { return "\\quest.reg"; }
inline const char* MapList() { return "\\map.txt"; }
inline const char* QuestSection() { return "quest "; }
}  // namespace briefingwords

// World loading error texts (world_zone.cpp).
namespace zonetexts {
inline const char* WrongEntrance() { return "Wrong entrance number in zone exits data"; }  // "Wrong entrance number in zone exit..."
inline const char* CantLoadMob() { return "CWorldServer::LoadMap.\n Can't load .mob file \"%s\".\n"; }  // "CWorldServer::LoadMap.\n Can't load .mob file "%s".\n"
inline const char* CantReadObjects() { return "CWorldServer::LoadMapObjects\nUnable to read file \"%s\"\n"; }  // "CWorldServer::LoadMapObjects\nUnable to read file "%s"\n"
inline const char* CantOpenObjects() { return "CWorldServer::LoadMapObjects()\nUnable to read file \"%s\"\n"; }  // "CWorldServer::LoadMapObjects()\nUnable to read file "%s"\n"
}  // namespace zonetexts

// Unit equipment error texts (unit_equipment.cpp).
namespace equipmenttexts {
inline const char* NoPrototype() { return "Can't find prototype \"%s\".\n"; }  // Can't find prototype "%s".
inline const char* NoArmour() { return "Can't create armor \"%s\" for unit \"%s\".\n"; }  // Can't create armor "%s" for unit "%s".
inline const char* NoWeapon() { return "Can't create weapon \"%s\" for unit \"%s\".\n"; }  // Can't create weapon "%s" for unit "%s".
inline const char* NoSpell() { return "Can't create spell \"%s\" for unit \"%s\".\n"; }  // Can't create spell "%s" for unit "%s".
}  // namespace equipmenttexts

// The menu rectangles' resource and its error texts (menu_rects.cpp).
namespace menutexts {
inline const char* MenusReg() { return "menus.reg"; }
inline const char* CantOpen() { return "Can't open file \"menus.reg\" with the main/esc menu rects\n"; }  // "Can't open file "menus.reg" with the main/esc menu rects\n"
inline const char* CantParse() { return "Can't open \"menus.reg\" as .reg with the main/esc menu rects\n"; }  // "Can't open "menus.reg" as .reg with ...\n"
}  // namespace menutexts

// The network log's lines (net_session_tick.cpp, net_connection.cpp).
namespace nettexts {
inline const char* ZeroLength() { return "ZERO length packet received from "; }
inline const char* Timeout() { return "TIMEOUT client, drop "; }
inline const char* UnknownHost() { return "ERROR: Update from unknown host"; }
inline const char* BadClient() { return "ERROR: client "; }
inline const char* BadUpdate() { return " sent incorrect update"; }
inline const char* RemoveClient() { return "REMOVE client "; }
inline const char* AddClient() { return "ADD client "; }
inline const char* CantHandleGetInfo() { return "ERROR: Can`t handle get info"; }  // "ERROR: Can't handle get info"
inline const char* CantHandleLogin() { return "ERROR: Can`t handle login"; }  // "ERROR: Can't handle login"
inline const char* CantHandleInfo() { return "ERROR: Can`t handle info"; }  // "ERROR: Can't handle info"
inline const char* CantHandleAccepted() { return "ERROR: Can`t handle accepted"; }  // "ERROR: Can't handle accepted"
inline const char* CantHandleRejected() { return "ERROR: Can`t handle rejected"; }  // "ERROR: Can't handle rejected"
inline const char* CantHandleMaster() { return "ERROR: Can`t handle master packet"; }  // "ERROR: Can't handle master packet"
inline const char* SkipUpdate() { return "SKIP update, newer version of object is already here"; }  // "SKIP update, newer version of object"
inline const char* SkipRemove() { return "SKIP remove, newer version of object is already here"; }  // "SKIP remove, newer version of object"
inline const char* TooOld() { return "SKIP too old update, dif="; }  // "SKIP too old update, dif "
inline const char* Duplicate() { return "DUPLICATE update received from "; }
}  // namespace nettexts

// "\n" (ui_console_window.cpp, ui_text.cpp) and a path suffix (net_server_map.cpp).
inline const char* NewLineText() { return "\n"; }
inline const char* PathSuffixText() { return ".1"; }

// The save folders and the key-mapping message (ui_menu_keys.cpp).
namespace menukeytexts {
inline const char* QuickSave() { return "saveq"; }
inline const char* AutoSaveA() { return "savea"; }
inline const char* AutoSaveB() { return "saveb"; }
inline const char* KeyMapped() { return "key_already_mapped"; }
inline const char* KeyMappedText() { return "key_already_mapped_msg"; }
}  // namespace menukeytexts

// The console window's prompt and "more" mark (ui_console_window.cpp).
namespace consoletexts {
inline const char* Prompt() { return "> "; }
inline const char* More() { return "+"; }
// The cheat word, each character XOR 0x66 (console.cpp).
inline const char* CheatWord() { return "\022\016\017\010\001\007\013\007\004\t\004"; }
}  // namespace consoletexts

// The thunderstorm's script lines (world_weather.cpp).
namespace weathertexts {
// "WorldScript(CreateFX( %f,%f,%f,60,150,"nature\thunder\%i.wav" ) )"
inline const char* ThunderScript() { return "WorldScript(CreateFX( %f,%f,%f,60,150,\"nature\\thunder\\%i.wav\" ) )"; }
// "WorldScript(CreatePointLight(1,%f,%f,%f,...) CreateFX( ... "nature\lightning\%i.wav" ) CreateLightning(1, ...) ... )"
inline const char* LightningScript() { return "WorldScript(CreatePointLight(1,%f,%f,%f,80,255,255,255 ) CreateFX( %f,%f,%f,60,150,\"nature\\lightning\\%i.wav\" ) CreateLightning(1,%f,%f,%f,%f,%f,%f,-7)Sleep(1) DeleteLightning(1) Sleep(1)CreateLightning(2,%f,%f,%f,%f,%f,%f,-7)Sleep(1) DeleteLightning(2) DeletePointLight(1) )"; }
}  // namespace weathertexts

} // namespace game
