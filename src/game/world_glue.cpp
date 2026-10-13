// The game glue's small functions (game.exe 0x00578000-0x00578E30). See world_glue.h and
// docs/notes/world.md.
#include "world_glue.h"

#include "net_classes.h"
#include "world_data.h"

#include "ei/hook.h"
#include "lib/stlp.h"

const mfc::CString* GameDataPathString() {
    return static_cast<const mfc::CString*>(worlddata::DataFolderString());
}
EI_REPLACE(0x00578DB0, GameDataPathString);

const mfc::CString* GameBasePathString() {
    return static_cast<const mfc::CString*>(worlddata::BaseFolderString());
}
EI_REPLACE(0x00578DC0, GameBasePathString);

int __stdcall FinishQuestMap(const char* name) {
    net::ServerSession* server = net::GameServer();
    if (!server || (server->flags7_ & CRefCounted::kDetached)) return 0;
    stlp::string text(name);
    return server->FinishMap(reinterpret_cast<const net::StringRep*>(&text)) ? 1 : 0;
}
EI_REPLACE(0x0057FB90, FinishQuestMap);
