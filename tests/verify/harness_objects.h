#pragma once

// The global objects C18k5 and C18t named (the cursor, the interface manager, the game flow, the options, the worlds,
// the scene renderer and its lock, the map database), as the harness reads them. In the image they are our named globals; in --no-hooks runs the harness runs inside game.exe's own code, which
// never touches ours, so it reads game.exe's copy at the original's address (ei::OriginalGameData; docs/notes/
// standalone.md, "Named data (C18)"). Include the objects' headers (ui_cursor.h, ui.h, world.h, options.h) first.
// The same for the game's functions more than one harness file calls: ours in the image, game.exe's own in its runs
// (harness_original.h).

#include "../../src/game/game_data.h"
#include "../../src/game/game_singletons.h"
#include "../../src/game/options.h"
#include "../../src/game/ui.h"   // CInterface3D::GetActiveWindow
#include "../../src/game/world.h"   // CWorldServer::sessionKind_
#include "../../src/game/app.h"
#include "../../src/render/render.h"
#include "ei/hook.h"
#include "harness_original.h"

// The application's globals (app.h).
inline const char* HarnessWindowTitle() {
    return ei::Standalone() ? app::g_windowTitle : &ei::OriginalGameData<const char>(0x0079BB24);
}
inline HWND HarnessMainWindow() {
    return ei::Standalone() ? app::g_mainWindow : ei::OriginalGameData<HWND>(0x0079BC28);
}
inline std::uint8_t& HarnessInterfaceHasScreen() {
    return ei::Standalone() ? app::g_interfaceHasScreen : ei::OriginalGameData<std::uint8_t>(0x00784750);
}
inline std::uint8_t& HarnessRecordingFrames() {
    return ei::Standalone() ? app::g_recordingFrames : ei::OriginalGameData<std::uint8_t>(0x0079BC54);
}
inline bool HarnessActive() {
    return (ei::Standalone() ? app::g_active : ei::OriginalGameData<std::uint8_t>(0x0079BC5C)) != 0;
}

inline CCursor* HarnessCursor() {
    return ei::Standalone() ? TheCursor() : &ei::OriginalGameData<CCursor>(0x007C7BD8);
}

inline options::GameOptions* HarnessOptions() {
    return ei::Standalone() ? options::TheOptions() : &ei::OriginalGameData<options::GameOptions>(0x0079BC70);
}

inline GameFlow* HarnessFlow() {
    return ei::Standalone() ? TheGameFlow() : &ei::OriginalGameData<GameFlow>(0x007AFE20);
}

inline CInterface3D* HarnessInterface() {
    return ei::Standalone() ? TheInterface() : &ei::OriginalGameData<CInterface3D>(0x007C7D50);
}

// The singletons C18t named (game_singletons.h): the two worlds, the scene renderer and its device lock, the map
// database.
inline CWorldServer* HarnessServerWorld() {
    return ei::Standalone() ? TheServerWorld() : &ei::OriginalGameData<CWorldServer>(0x007AFF60);
}
inline CWorldClient* HarnessClientWorld() {
    return ei::Standalone() ? TheClientWorld() : &ei::OriginalGameData<CWorldClient>(0x007AF5B8);
}
inline CSceneRenderer* HarnessSceneRenderer() {
    return ei::Standalone() ? TheSceneRenderer() : &ei::OriginalGameData<CSceneRenderer>(0x007AF060);
}
inline _RTL_CRITICAL_SECTION* HarnessDeviceLock() {
    return ei::Standalone() ? TheDeviceLock() : &ei::OriginalGameData<_RTL_CRITICAL_SECTION>(0x007AEE78);
}
inline CMapDatabase* HarnessMapDatabase() {
    return ei::Standalone() ? TheMapDatabase() : &ei::OriginalGameData<CMapDatabase>(0x0079BF00);
}

// The campaign mode: the server world's session kind (007C2CC4), which GameFlow::StartZone sets.
inline int HarnessCampaignMode() {
    return HarnessServerWorld()->sessionKind_;
}

// The window on top (CInterface3D::GetActiveWindow 005F7590, thiscall).
inline CUiWindow* HarnessActiveWindow(CInterface3D* ui) {
    return ei::Standalone() ? ui->GetActiveWindow()
                            : ei::verify::OriginalCode<CUiWindow*(__thiscall*)(CInterface3D*)>(0x005F7590)(ui);
}

// Whether the image's renderer draws (render::Active): never in game.exe's runs, which draw with the original code.
inline bool HarnessRenderActive() {
    return ei::Standalone() && render::Active();
}
