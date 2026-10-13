// The game glue's small functions (game.exe 0x00578000-0x00578E30). See world_glue.h and
// docs/notes/world.md.
#include "world_glue.h"

#include "net_classes.h"
#include "object_tables.h"
#include "script.h"
#include "world.h"
#include "world_data.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_script.h"

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

namespace {
CScript* CommandScript() { return reinterpret_cast<CScript*>(std::uintptr_t(0x007C29CC)); }
} // namespace

int __stdcall RunScriptText(mfc::CString* text, char run, ScriptErrorInfo* error) {
    CScript* script = CommandScript();
    int loaded = ScriptLoadLine(script, nullptr, text, 1);
    if (loaded && run) ScriptTick(script);
    if (error) {
        // (00456360, inlined here: a copy of the script's error record.)
        ScriptErrorInfo last{script->ErrorText(), script->ErrorCode(), script->ErrorLine()};
        error->text = last.text;
        error->code = last.code;
        error->line = last.line;
    }
    return loaded;
}
EI_REPLACE(0x005785D0, RunScriptText);

void __stdcall PlayerZeroVariables(const char* prefix, NamedFloatTable* out) {
    WorldPlayer* player;
    if (IsMultiplayerClient()) {
        player = reinterpret_cast<LocalViewData*>(WorldClient()->LocalView())->player;
    } else {
        player = WorldServer()->GetPlayer(0);
        if (!player) {
            FloatTableClearOriginal(out);
            return;
        }
    }
    reinterpret_cast<NamedFloatTable*>(player->variables)->CopyPrefixed(prefix, out);
}
EI_REPLACE(0x00578C40, PlayerZeroVariables);

namespace {
mfc::CString& StartFolder() { return *reinterpret_cast<mfc::CString*>(std::uintptr_t(0x007AF454)); }
constexpr unsigned kFolderBufferSize = 0x400;
} // namespace

void __cdecl StoreStartFolder() {
    char folder[kFolderBufferSize];
    gameapi::GetCurrentDirectoryA(kFolderBufferSize, folder);
    mfc::CString text(folder);
    StartFolder() = static_cast<const char*>(PlusChar(text, '\\'));
}
EI_REPLACE(0x0055EA10, StoreStartFolder);
