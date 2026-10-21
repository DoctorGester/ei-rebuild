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

// The texture manager's FatalError (00691D80).
inline const char* CantFindTextureFormatText() { return "Can't find texture format %d.\n"; }

// The window surfaces' FatalDirectX (005FCC00 / 005FD120).
inline const char* CantGetSurfaceDescriptionText() { return "Can't get surface description.\n"; }
// 005FCF80's picture file name.
inline const char* MmpExtensionText() { return ".mmp"; }

// The battle screen's (ui_hud.cpp): the sound of a portrait's unit clicked, the console line of a
// click on the ground ("Terrain Coords", space), a zone exit's player variable prefix.
inline const char* SoundOnOffText() { return "buttons\\battle\\on_off.wav"; }
inline const char* TerrainCoordsText() { return "Terrain Coords "; }
inline const char* SpaceText() { return " "; }
inline const char* ZoneVariablePrefixText() { return "z."; }

// A prototype's weapon name that gives no weapon.
inline const char* NoneText() { return "none"; }

// The bow shot's figure (CEffectArrowFigure): model and texture.
inline const char* ArrowFigureModelText() { return "efar1"; }
inline const char* ArrowFigureTextureText() { return "arrow01"; }

// The bird circling the followed hero (CEffectMoshka): model and texture.
inline const char* MoshkaModelText() { return "unmobi"; }
inline const char* MoshkaTextureText() { return "bird00"; }

// The tornado's looping sound (CEffectTornado).
inline const char* TornadoSoundText() { return "nature\\tornado\\circle.wav"; }

// A player's game-time variable (CWorldServer::UpdatePlayerTimes).
inline const char* GTimeText() { return "gtime"; }
// LoadSave's FatalError.
inline const char* CantLoadSaveMapText() { return "CWorldServer::LoadSave()\n Can't load map.\n"; }

// The trader stock's prototype names (item_stock.cpp); "wand" is skipped, "scroll" has no instruction.
inline const char* WandText() { return "wand"; }
inline const char* MaterialText() { return "material"; }
inline const char* InstructionWeaponText() { return "instruction weapon"; }
inline const char* InstructionArmorText() { return "instruction armor"; }
inline const char* InstructionQuickItemText() { return "instruction quick item"; }
inline const char* ScrollText() { return "scroll"; }
inline const char* PrototypeText() { return "prototype"; }
inline const char* RuneText() { return "rune"; }
inline const char* SpellContainerText() { return "spell container"; }

// The interface's help topics file (CInterface3D, Create 005F4990 / Shutdown 005F4F90): the name
// (after the data path) and the line format (a topic and its count).
inline const char* TutorialIniText() { return "config\\tutorial.ini"; }
inline const char* TutorialLineFormat() { return "%s %d\n"; }

// The flying hit points' draw (005E18E0): the amount's format and its FatalError.
inline const char* DecimalFormat() { return "%d"; }
inline const char* FlyingHitPointsTextureText() { return "CFlyingHitPoints::Draw.\n SetTexture failed.\n"; }

// The figure manager's file names (figure_manager.cpp), its FatalError and a bone's animation extension.
inline const char* FigFileFormat() { return "%s.fig"; }
inline const char* FigPathFormat() { return "%s%s.fig"; }
inline const char* CantOpenFigText() { return "Can't open file \"%s%s.fig\"\n"; }
inline const char* AstExtensionText() { return ".ast"; }

// The server's and the client's lists show it for a name that is missing.
inline const char* QuestionMarkText() { return "?"; }

// The suffixes of the zone messages and the quest registry keys: enter, leave, finish.
inline const char* ZoneSuffix1Text() { return "_1"; }
inline const char* ZoneSuffix2Text() { return "_2"; }
inline const char* ZoneSuffix3Text() { return "_3"; }

// The interface sounds (RotateSoundText: the hero screen's rotate arrows).
inline const char* CancelSoundText() { return "buttons\\messbox\\cancel.wav"; }
inline const char* OkSoundText() { return "buttons\\messbox\\ok.wav"; }
inline const char* SelectSoundText() { return "buttons\\save\\select.wav"; }
inline const char* PutOnSoundText() { return "buttons\\camp\\put_on.wav"; }
inline const char* PutOffSoundText() { return "buttons\\camp\\put_off.wav"; }
inline const char* RotateSoundText() { return "buttons\\camp\\move.wav"; }

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
