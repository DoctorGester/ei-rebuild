#pragma once

// The global objects C18k5 named (the cursor, the interface manager, the game flow, the options), as the harness reads
// them. In the image they are our named globals; in --no-hooks runs the harness runs inside game.exe's own code, which
// never touches ours, so it reads game.exe's copy at the original's address (ei::OriginalGameData; docs/notes/
// standalone.md, "Named data (C18)"). Include the objects' headers (ui_cursor.h, ui.h, world.h, options.h) first.

#include "../../src/game/game_data.h"
#include "../../src/game/game_singletons.h"
#include "ei/hook.h"

inline CCursor* HarnessCursor() {
    return ei::Standalone() ? TheCursor() : &ei::OriginalGameData<CCursor>(0x007C7BD8);
}

inline CInterface3D* HarnessInterface() {
    return ei::Standalone() ? TheInterface() : &ei::OriginalGameData<CInterface3D>(0x007C7D50);
}
