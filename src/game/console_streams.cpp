// The console's static objects: its two command maps, the eight streambufs (one per message filter) and the
// eight ostreams on them. Each is built by its static initialiser at the list's place and destroyed by the
// exit function that initialiser registers with the game's atexit (docs/notes/standalone.md, "The static
// initialisers' objects"). See docs/notes/console.md.
#include "console.h"
#include "library_trees.h"

#include "ei/hook.h"

namespace console {

constinit lib::StaticObject<CommandMap> g_publicCommands = /*007C7B28*/ {};
constinit lib::StaticObject<CommandMap> g_cheatCommands = /*007C7B38*/ {};

constinit OStreamObject g_consoleOut = /*007C55A8*/ {};
constinit OStreamObject g_allLogOut = /*007C5600*/ {};
constinit OStreamObject g_unitAcksLogOut = /*007C5658*/ {};
constinit OStreamObject g_netLogOut = /*007C56B0*/ {};
constinit OStreamObject g_rpgLogOut = /*007C5708*/ {};
constinit OStreamObject g_graphicsLogOut = /*007C5760*/ {};
constinit OStreamObject g_eventLogOut = /*007C57B8*/ {};
constinit OStreamObject g_aiLogOut = /*007C5810*/ {};

constinit lib::StaticObject<ConsoleStreambuf> g_consoleBuf = /*007C5868*/ {};
constinit lib::StaticObject<ConsoleStreambuf> g_allLogBuf = /*007C5CC0*/ {};
constinit lib::StaticObject<ConsoleStreambuf> g_netLogBuf = /*007C6118*/ {};
constinit lib::StaticObject<ConsoleStreambuf> g_unitAcksLogBuf = /*007C6570*/ {};
constinit lib::StaticObject<ConsoleStreambuf> g_rpgLogBuf = /*007C69C8*/ {};
constinit lib::StaticObject<ConsoleStreambuf> g_graphicsLogBuf = /*007C6E20*/ {};
constinit lib::StaticObject<ConsoleStreambuf> g_eventLogBuf = /*007C7278*/ {};
constinit lib::StaticObject<ConsoleStreambuf> g_aiLogBuf = /*007C76D0*/ {};

} // namespace console

namespace {

using console::ConsoleStreambuf;
using console::OStreamObject;
using StreambufObject = lib::StaticObject<ConsoleStreambuf>;
using ExitFunction = void(__cdecl*)();

void BuildCommands(lib::StaticObject<console::CommandMap>& map, ExitFunction exit) {
    lib_trees::Commands::Construct(&map.Get());
    libcrt::AtExit(exit);
}
void BuildStreambuf(StreambufObject& buffer, unsigned filter, ExitFunction exit) {
    buffer.Construct(filter);
    libcrt::AtExit(exit);
}
// An ostream on its streambuf (ostream(streambuf*, 1)); destroyed through its ios, ostream's part
// and then ios's.
void BuildStream(OStreamObject& stream, StreambufObject& buffer, ExitFunction exit) {
    stream.Construct(&buffer.Get(), 1);
    libcrt::AtExit(exit);
}
void DestroyStream(OStreamObject& stream) {
    stream.ios.DestructOstream();
    stream.ios.Destruct();
}

void __cdecl DestroyCheatCommands() { lib_trees::Commands::Destroy(&console::g_cheatCommands.Get()); }
void __cdecl DestroyPublicCommands() { lib_trees::Commands::Destroy(&console::g_publicCommands.Get()); }
void __cdecl DestroyAiLogBuf() { console::g_aiLogBuf.Destroy(); }
void __cdecl DestroyEventLogBuf() { console::g_eventLogBuf.Destroy(); }
void __cdecl DestroyGraphicsLogBuf() { console::g_graphicsLogBuf.Destroy(); }
void __cdecl DestroyRpgLogBuf() { console::g_rpgLogBuf.Destroy(); }
void __cdecl DestroyUnitAcksLogBuf() { console::g_unitAcksLogBuf.Destroy(); }
void __cdecl DestroyNetLogBuf() { console::g_netLogBuf.Destroy(); }
void __cdecl DestroyAllLogBuf() { console::g_allLogBuf.Destroy(); }
void __cdecl DestroyConsoleBuf() { console::g_consoleBuf.Destroy(); }
void __cdecl DestroyAiLogOut() { DestroyStream(console::g_aiLogOut); }
void __cdecl DestroyEventLogOut() { DestroyStream(console::g_eventLogOut); }
void __cdecl DestroyGraphicsLogOut() { DestroyStream(console::g_graphicsLogOut); }
void __cdecl DestroyRpgLogOut() { DestroyStream(console::g_rpgLogOut); }
void __cdecl DestroyNetLogOut() { DestroyStream(console::g_netLogOut); }
void __cdecl DestroyUnitAcksLogOut() { DestroyStream(console::g_unitAcksLogOut); }
void __cdecl DestroyAllLogOut() { DestroyStream(console::g_allLogOut); }
void __cdecl DestroyConsoleOut() { DestroyStream(console::g_consoleOut); }
} // namespace

void __cdecl ConstructCheatCommands() { BuildCommands(console::g_cheatCommands, DestroyCheatCommands); }
void __cdecl ConstructPublicCommands() { BuildCommands(console::g_publicCommands, DestroyPublicCommands); }

void __cdecl ConstructAiLogBuf() { BuildStreambuf(console::g_aiLogBuf, 1, DestroyAiLogBuf); }
void __cdecl ConstructEventLogBuf() { BuildStreambuf(console::g_eventLogBuf, 2, DestroyEventLogBuf); }
void __cdecl ConstructGraphicsLogBuf() { BuildStreambuf(console::g_graphicsLogBuf, 4, DestroyGraphicsLogBuf); }
void __cdecl ConstructRpgLogBuf() { BuildStreambuf(console::g_rpgLogBuf, 8, DestroyRpgLogBuf); }
void __cdecl ConstructUnitAcksLogBuf() { BuildStreambuf(console::g_unitAcksLogBuf, 0x10, DestroyUnitAcksLogBuf); }
void __cdecl ConstructNetLogBuf() { BuildStreambuf(console::g_netLogBuf, 0x20, DestroyNetLogBuf); }
void __cdecl ConstructAllLogBuf() { BuildStreambuf(console::g_allLogBuf, 0xFFFFFFFF, DestroyAllLogBuf); }
void __cdecl ConstructConsoleBuf() { BuildStreambuf(console::g_consoleBuf, 0, DestroyConsoleBuf); }

void __cdecl ConstructAiLogOut() { BuildStream(console::g_aiLogOut, console::g_aiLogBuf, DestroyAiLogOut); }
void __cdecl ConstructEventLogOut() { BuildStream(console::g_eventLogOut, console::g_eventLogBuf, DestroyEventLogOut); }
void __cdecl ConstructGraphicsLogOut() {
    BuildStream(console::g_graphicsLogOut, console::g_graphicsLogBuf, DestroyGraphicsLogOut);
}
void __cdecl ConstructRpgLogOut() { BuildStream(console::g_rpgLogOut, console::g_rpgLogBuf, DestroyRpgLogOut); }
void __cdecl ConstructNetLogOut() { BuildStream(console::g_netLogOut, console::g_netLogBuf, DestroyNetLogOut); }
void __cdecl ConstructUnitAcksLogOut() {
    BuildStream(console::g_unitAcksLogOut, console::g_unitAcksLogBuf, DestroyUnitAcksLogOut);
}
void __cdecl ConstructAllLogOut() { BuildStream(console::g_allLogOut, console::g_allLogBuf, DestroyAllLogOut); }
void __cdecl ConstructConsoleOut() { BuildStream(console::g_consoleOut, console::g_consoleBuf, DestroyConsoleOut); }

EI_REWRITTEN(0x005DC050, ConstructCheatCommands);
EI_REWRITTEN(0x005DC070, DestroyCheatCommands);
EI_REWRITTEN(0x005DC120, ConstructPublicCommands);
EI_REWRITTEN(0x005DC140, DestroyPublicCommands);
EI_REWRITTEN(0x005DC1E0, ConstructAiLogBuf);
EI_REWRITTEN(0x005DC2C0, DestroyAiLogBuf);
EI_REWRITTEN(0x005DC2D0, ConstructEventLogBuf);
EI_REWRITTEN(0x005DC2F0, DestroyEventLogBuf);
EI_REWRITTEN(0x005DC300, ConstructGraphicsLogBuf);
EI_REWRITTEN(0x005DC320, DestroyGraphicsLogBuf);
EI_REWRITTEN(0x005DC330, ConstructRpgLogBuf);
EI_REWRITTEN(0x005DC350, DestroyRpgLogBuf);
EI_REWRITTEN(0x005DC360, ConstructUnitAcksLogBuf);
EI_REWRITTEN(0x005DC380, DestroyUnitAcksLogBuf);
EI_REWRITTEN(0x005DC390, ConstructNetLogBuf);
EI_REWRITTEN(0x005DC3B0, DestroyNetLogBuf);
EI_REWRITTEN(0x005DC3C0, ConstructAllLogBuf);
EI_REWRITTEN(0x005DC3E0, DestroyAllLogBuf);
EI_REWRITTEN(0x005DC3F0, ConstructConsoleBuf);
EI_REWRITTEN(0x005DC410, DestroyConsoleBuf);
EI_REWRITTEN(0x005DC420, ConstructAiLogOut);
EI_REWRITTEN(0x005DC440, DestroyAiLogOut);
EI_REWRITTEN(0x005DC460, ConstructEventLogOut);
EI_REWRITTEN(0x005DC480, DestroyEventLogOut);
EI_REWRITTEN(0x005DC4A0, ConstructGraphicsLogOut);
EI_REWRITTEN(0x005DC4C0, DestroyGraphicsLogOut);
EI_REWRITTEN(0x005DC4E0, ConstructRpgLogOut);
EI_REWRITTEN(0x005DC500, DestroyRpgLogOut);
EI_REWRITTEN(0x005DC520, ConstructNetLogOut);
EI_REWRITTEN(0x005DC540, DestroyNetLogOut);
EI_REWRITTEN(0x005DC560, ConstructUnitAcksLogOut);
EI_REWRITTEN(0x005DC580, DestroyUnitAcksLogOut);
EI_REWRITTEN(0x005DC5A0, ConstructAllLogOut);
EI_REWRITTEN(0x005DC5C0, DestroyAllLogOut);
EI_REWRITTEN(0x005DC5E0, ConstructConsoleOut);
EI_REWRITTEN(0x005DC600, DestroyConsoleOut);
