#pragma once

// Strings of the exe's data that the rewritten code uses by address.

#include "game_data.h"

#include <cstdint>

namespace game {

// A string constant of game.exe, passed by its original address (Rebirth may patch the data). In
// ei_vectors the string is in the stand-in for the game's data (game_data.h), put there by a fixture.
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
inline const char* Allod() { return Text(0x00785768); }  // "#allod"
inline const char* Zone() { return Text(0x00785760); }  // "#zone"
inline const char* Game() { return Text(0x00785758); }  // "game"
inline const char* Brief() { return Text(0x00785750); }  // "brief"
inline const char* Edge() { return Text(0x00785748); }  // "edge"
inline const char* Res() { return Text(0x00785740); }  // "#res"
inline const char* Camera() { return Text(0x00785738); }  // "#camera"
inline const char* Maps() { return Text(0x00785730); }  // "#maps"
inline const char* Figure() { return Text(0x00785728); }  // "#figure"
inline const char* Weather() { return Text(0x0078571C); }  // "#weather"
inline const char* Snow() { return Text(0x00785714); }  // "snow"
inline const char* Tornado() { return Text(0x0078570C); }  // "tornado"
inline const char* Cage() { return Text(0x00785704); }  // "#cage"
inline const char* Restrict() { return Text(0x007856F8); }  // "#restrict"
inline const char* Sky() { return Text(0x007856F0); }  // "#sky"
inline const char* Cave() { return Text(0x007856E8); }  // "cave"
inline const char* Exit() { return Text(0x007856E0); }  // "#exit"
inline const char* Deploy() { return Text(0x007856D8); }  // "#deploy"
inline const char* Remove() { return Text(0x007856D0); }  // "#remove"
inline const char* PassTime() { return Text(0x007856C4); }  // "#passtime"
inline const char* View() { return Text(0x007856BC); }  // "#view"
inline const char* Zero() { return Text(0x007856B8); }  // "0"
inline const char* DeployAngle() { return Text(0x007856A8); }  // "#deployangle"
}  // namespace mapwords

// The quest file's section and key names (quest_database.cpp).
namespace questwords {
inline const char* Name() { return Text(0x007967C4); }  // "name"
inline const char* Quest() { return Text(0x007967BC); }  // "quest"
inline const char* Exp() { return Text(0x0079275C); }  // "exp"
inline const char* Money() { return Text(0x00792760); }  // "money"
inline const char* ZoneIndex() { return Text(0x007967B0); }  // "zone index"
inline const char* Order() { return Text(0x007967A8); }  // "order"
inline const char* QuestPlaces() { return Text(0x0079679C); }  // "questplaces"
inline const char* BriefingReceive() { return Text(0x00796788); }  // "briefing receive"
inline const char* BriefingReject() { return Text(0x00796720); }  // "briefing reject"
inline const char* BriefingComplete() { return Text(0x0079670C); }  // "briefing complete"
// The six lists of a briefing, by index: "give items", "remove items", "give quests", "complete quests", "available zones", "complete zones".
inline const char* ListKey(int index) {
    switch (index) {
    case 0: return Text(0x0079677C);
    case 1: return Text(0x0079676C);
    case 2: return Text(0x00796760);
    case 3: return Text(0x00796750);
    case 4: return Text(0x00796740);
    default: return Text(0x00796730);
    }
}
}  // namespace questwords

// The save/load screens' file names and texts (ui_saveload.cpp).
namespace savewords {
inline const char* SavePattern() { return Text(0x00794A34); }  // "%ssaves\\save*"
inline const char* InfoFile() { return Text(0x007949B8); }  // "info.sav"
inline const char* InfoInFolder() { return Text(0x00794A28); }  // "\\info.sav"
inline const char* ShotFile() { return Text(0x007949AC); }  // "shot.sav"
inline const char* BrokenTitle() { return Text(0x00794A1C); }  // "Error Save"
inline const char* NewFolder() { return Text(0x00794A14); }  // "save%d"
inline const char* GameTime() { return Text(0x00742B88); }  // "gtime"
inline const char* NotifySaving() { return Text(0x007949C4); }  // "notify_saving"
inline const char* NoSpace() { return Text(0x007949D4); }  // "no_disc_space"
inline const char* NoSpaceText() { return Text(0x007949E4); }  // "no_disc_space_msg"
inline const char* CantLockSurface() { return Text(0x0078E30C); }  // "Can't lock surface.\n"
inline const char* CantUnlockSurface() { return Text(0x0078E2F4); }  // "Can't unlock surface.\n"
}  // namespace savewords

// The briefing and quest registry texts (world_briefing.cpp).
namespace briefingwords {
inline const char* BriefingSection() { return Text(0x007906E4); }  // "briefing "
inline const char* ShowTag() { return Text(0x007906DC); }  // "#show"
inline const char* Hero() { return Text(0x0078B0F4); }  // "Hero"
inline const char* ElderPrefix() { return Text(0x007906F0); }  // "b.elder."
inline const char* VariablePrefix() { return Text(0x007906FC); }  // "b."
inline const char* QuestRegistry() { return Text(0x00790784); }  // "\quest.reg"
inline const char* MapList() { return Text(0x00790778); }  // "\map.txt"
inline const char* QuestSection() { return Text(0x00790790); }  // "quest "
}  // namespace briefingwords

// World loading error texts (world_zone.cpp).
namespace zonetexts {
inline const char* WrongEntrance() { return Text(0x00790958); }  // "Wrong entrance number in zone exit..."
inline const char* CantLoadMob() { return Text(0x00790B08); }  // "CWorldServer::LoadMap.\n Can't load .mob file "%s".\n"
inline const char* CantReadObjects() { return Text(0x00790B3C); }  // "CWorldServer::LoadMapObjects\nUnable to read file "%s"\n"
inline const char* CantOpenObjects() { return Text(0x00790B74); }  // "CWorldServer::LoadMapObjects()\nUnable to read file "%s"\n"
}  // namespace zonetexts

// Unit equipment error texts (unit_equipment.cpp).
namespace equipmenttexts {
inline const char* NoPrototype() { return Text(0x0078F330); }  // Can't find prototype "%s".
inline const char* NoArmour() { return Text(0x0078F308); }  // Can't create armor "%s" for unit "%s".
inline const char* NoWeapon() { return Text(0x0078F2DC); }  // Can't create weapon "%s" for unit "%s".
inline const char* NoSpell() { return Text(0x0078F2B4); }  // Can't create spell "%s" for unit "%s".
}  // namespace equipmenttexts

// The menu rectangles' resource and its error texts (menu_rects.cpp).
namespace menutexts {
inline const char* MenusReg() { return Text(0x00791020); }  // "menus.reg"
inline const char* CantOpen() { return Text(0x00790FE4); }  // "Can't open file "menus.reg" with the main/esc menu rects\n"
inline const char* CantParse() { return Text(0x00790FA4); }  // "Can't open "menus.reg" as .reg with ...\n"
}  // namespace menutexts

// The network log's lines (net_session_tick.cpp, net_connection.cpp).
namespace nettexts {
inline const char* ZeroLength() { return Text(0x00784020); }  // "ZERO length packet received from "
inline const char* Timeout() { return Text(0x00784008); }  // "TIMEOUT client, drop "
inline const char* UnknownHost() { return Text(0x00783F2C); }  // "ERROR: Update from unknown host"
inline const char* BadClient() { return Text(0x00783F1C); }  // "ERROR: client "
inline const char* BadUpdate() { return Text(0x00783F04); }  // " sent incorrect update"
inline const char* RemoveClient() { return Text(0x00783EE8); }  // "REMOVE client "
inline const char* AddClient() { return Text(0x00783EF8); }  // "ADD client "
inline const char* CantHandleGetInfo() { return Text(0x00783F4C); }  // "ERROR: Can't handle get info"
inline const char* CantHandleLogin() { return Text(0x00783F6C); }  // "ERROR: Can't handle login"
inline const char* CantHandleInfo() { return Text(0x00783F88); }  // "ERROR: Can't handle info"
inline const char* CantHandleAccepted() { return Text(0x00783FA4); }  // "ERROR: Can't handle accepted"
inline const char* CantHandleRejected() { return Text(0x00783FC4); }  // "ERROR: Can't handle rejected"
inline const char* CantHandleMaster() { return Text(0x00783FE4); }  // "ERROR: Can't handle master packet"
inline const char* SkipUpdate() { return Text(0x00783EB0); }  // "SKIP update, newer version of object"
inline const char* SkipRemove() { return Text(0x00783E78); }  // "SKIP remove, newer version of object"
inline const char* TooOld() { return Text(0x00783E5C); }  // "SKIP too old update, dif "
inline const char* Duplicate() { return Text(0x00783E3C); }  // "DUPLICATE update received from "
}  // namespace nettexts

// "\n" (ui_console_window.cpp, ui_text.cpp) and a path suffix (net_server_map.cpp).
inline const char* NewLineText() { return Text(0x00784340); }
inline const char* PathSuffixText() { return Text(0x00783D74); }

// The save folders and the key-mapping message (ui_menu_keys.cpp).
namespace menukeytexts {
inline const char* QuickSave() { return Text(0x007933AC); }  // "saveq"
inline const char* AutoSaveA() { return Text(0x007942E8); }  // "savea"
inline const char* AutoSaveB() { return Text(0x007940F0); }  // "saveb"
inline const char* KeyMapped() { return Text(0x00794874); }  // "key_already_mapped"
inline const char* KeyMappedText() { return Text(0x00794888); }  // "key_already_mapped_msg"
}  // namespace menukeytexts

// The console window's prompt and "more" mark (ui_console_window.cpp).
namespace consoletexts {
inline const char* Prompt() { return Text(0x00792DE0); }
inline const char* More() { return Text(0x00787BD8); }
// The cheat word, each character XOR 0x66 (console.cpp).
inline const char* CheatWord() { return Text(0x00740028); }
}  // namespace consoletexts

// The thunderstorm's script lines (world_weather.cpp).
namespace weathertexts {
// "WorldScript(CreateFX( %f,%f,%f,60,150,"nature\thunder\%i.wav" ) )"
inline const char* ThunderScript() { return Text(0x007904A8); }
// "WorldScript(CreatePointLight(1,%f,%f,%f,...) CreateFX( ... "nature\lightning\%i.wav" ) CreateLightning(1, ...) ... )"
inline const char* LightningScript() { return Text(0x00790394); }
}  // namespace weathertexts

} // namespace game
