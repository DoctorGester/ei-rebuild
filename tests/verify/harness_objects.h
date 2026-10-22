#pragma once

// The global objects C18k5 named (the cursor, the interface manager, the game flow, the options), as the harness reads
// them. In the image they are our named globals; in --no-hooks runs the harness runs inside game.exe's own code, which
// never touches ours, so it reads game.exe's copy at the original's address (ei::OriginalGameData; docs/notes/
// standalone.md, "Named data (C18)"). Include the objects' headers (ui_cursor.h, ui.h, world.h, options.h) first.

#include "../../src/game/game_data.h"
#include "../../src/game/game_singletons.h"
#include "../../src/game/options.h"
#include "../../src/game/world.h"   // CWorldServer::sessionKind_
#include "../../src/game/app.h"
#include "ei/hook.h"

// The application's globals (app.h).
inline const char* HarnessWindowTitle() {
    return ei::Standalone() ? app::g_windowTitle : &ei::OriginalGameData<const char>(0x0079BB24);
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

// The campaign mode: the server world's session kind (007C2CC4), which GameFlow::StartZone sets. The server world
// is not named, so its game data address serves both runs.
inline int HarnessCampaignMode() {
    return TheServerWorld()->sessionKind_;
}
