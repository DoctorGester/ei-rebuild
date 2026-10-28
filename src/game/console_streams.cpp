// The console's static objects: its two command maps, the eight streambufs (one per message filter) and the
// eight ostreams on them. Each is built by its static initialiser at the list's place and destroyed by the
// exit function that initialiser registers with the game's atexit (docs/notes/standalone.md, "The static
// initialisers' objects"). See docs/notes/console.md.
#include "console.h"
#include "library_trees.h"

#include <cstdlib>

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
    std::atexit(exit);
}
void BuildStreambuf(StreambufObject& buffer, unsigned filter, ExitFunction exit) {
    buffer.Construct(filter);
    std::atexit(exit);
}
// An ostream on its streambuf (ostream(streambuf*, 1)); destroyed through its ios, ostream's part
// and then ios's.
void BuildStream(OStreamObject& stream, StreambufObject& buffer, ExitFunction exit) {
    stream.Construct(&buffer.Get(), 1);
    std::atexit(exit);
}
void DestroyStream(OStreamObject& stream) {
    stream.ios.DestructOstream();
    stream.ios.Destruct();
}

void DestroyCheatCommands() { lib_trees::Commands::Destroy(&console::g_cheatCommands.Get()); }
void DestroyPublicCommands() { lib_trees::Commands::Destroy(&console::g_publicCommands.Get()); }
void DestroyAiLogBuf() { console::g_aiLogBuf.Destroy(); }
void DestroyEventLogBuf() { console::g_eventLogBuf.Destroy(); }
void DestroyGraphicsLogBuf() { console::g_graphicsLogBuf.Destroy(); }
void DestroyRpgLogBuf() { console::g_rpgLogBuf.Destroy(); }
void DestroyUnitAcksLogBuf() { console::g_unitAcksLogBuf.Destroy(); }
void DestroyNetLogBuf() { console::g_netLogBuf.Destroy(); }
void DestroyAllLogBuf() { console::g_allLogBuf.Destroy(); }
void DestroyConsoleBuf() { console::g_consoleBuf.Destroy(); }
void DestroyAiLogOut() { DestroyStream(console::g_aiLogOut); }
void DestroyEventLogOut() { DestroyStream(console::g_eventLogOut); }
void DestroyGraphicsLogOut() { DestroyStream(console::g_graphicsLogOut); }
void DestroyRpgLogOut() { DestroyStream(console::g_rpgLogOut); }
void DestroyNetLogOut() { DestroyStream(console::g_netLogOut); }
void DestroyUnitAcksLogOut() { DestroyStream(console::g_unitAcksLogOut); }
void DestroyAllLogOut() { DestroyStream(console::g_allLogOut); }
void DestroyConsoleOut() { DestroyStream(console::g_consoleOut); }
} // namespace

void ConstructCheatCommands() { BuildCommands(console::g_cheatCommands, DestroyCheatCommands); }
void ConstructPublicCommands() { BuildCommands(console::g_publicCommands, DestroyPublicCommands); }

void ConstructAiLogBuf() { BuildStreambuf(console::g_aiLogBuf, 1, DestroyAiLogBuf); }
void ConstructEventLogBuf() { BuildStreambuf(console::g_eventLogBuf, 2, DestroyEventLogBuf); }
void ConstructGraphicsLogBuf() { BuildStreambuf(console::g_graphicsLogBuf, 4, DestroyGraphicsLogBuf); }
void ConstructRpgLogBuf() { BuildStreambuf(console::g_rpgLogBuf, 8, DestroyRpgLogBuf); }
void ConstructUnitAcksLogBuf() { BuildStreambuf(console::g_unitAcksLogBuf, 0x10, DestroyUnitAcksLogBuf); }
void ConstructNetLogBuf() { BuildStreambuf(console::g_netLogBuf, 0x20, DestroyNetLogBuf); }
void ConstructAllLogBuf() { BuildStreambuf(console::g_allLogBuf, 0xFFFFFFFF, DestroyAllLogBuf); }
void ConstructConsoleBuf() { BuildStreambuf(console::g_consoleBuf, 0, DestroyConsoleBuf); }

void ConstructAiLogOut() { BuildStream(console::g_aiLogOut, console::g_aiLogBuf, DestroyAiLogOut); }
void ConstructEventLogOut() { BuildStream(console::g_eventLogOut, console::g_eventLogBuf, DestroyEventLogOut); }
void ConstructGraphicsLogOut() {
    BuildStream(console::g_graphicsLogOut, console::g_graphicsLogBuf, DestroyGraphicsLogOut);
}
void ConstructRpgLogOut() { BuildStream(console::g_rpgLogOut, console::g_rpgLogBuf, DestroyRpgLogOut); }
void ConstructNetLogOut() { BuildStream(console::g_netLogOut, console::g_netLogBuf, DestroyNetLogOut); }
void ConstructUnitAcksLogOut() {
    BuildStream(console::g_unitAcksLogOut, console::g_unitAcksLogBuf, DestroyUnitAcksLogOut);
}
void ConstructAllLogOut() { BuildStream(console::g_allLogOut, console::g_allLogBuf, DestroyAllLogOut); }
void ConstructConsoleOut() { BuildStream(console::g_consoleOut, console::g_consoleBuf, DestroyConsoleOut); }
