// The console's static objects: its two command maps (plain globals), the eight streambufs (one per message filter)
// and the eight streams on them. Each streambuf and stream is built by its static initialiser at the list's place; the
// streambufs are destroyed by the exit function that initialiser registers with the game's atexit (docs/notes/standalone.md, "The
// static initialisers' objects"); a stream only names its streambuf, nothing to destroy. See docs/notes/console.md.
#include "console.h"

#include <cstdlib>

namespace console {

CommandMap g_publicCommands;
CommandMap g_cheatCommands;

constinit Stream g_consoleOut = /*007C55A8*/ {};
constinit Stream g_allLogOut = /*007C5600*/ {};
constinit Stream g_unitAcksLogOut = /*007C5658*/ {};
constinit Stream g_netLogOut = /*007C56B0*/ {};
constinit Stream g_rpgLogOut = /*007C5708*/ {};
constinit Stream g_graphicsLogOut = /*007C5760*/ {};
constinit Stream g_eventLogOut = /*007C57B8*/ {};
constinit Stream g_aiLogOut = /*007C5810*/ {};

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
using ExitFunction = void(*)();

void BuildStreambuf(lib::StaticObject<ConsoleStreambuf>& buffer, unsigned filter, ExitFunction exit) {
    buffer.Construct(filter);
    std::atexit(exit);
}

void DestroyAiLogBuf() { console::g_aiLogBuf.Destroy(); }
void DestroyEventLogBuf() { console::g_eventLogBuf.Destroy(); }
void DestroyGraphicsLogBuf() { console::g_graphicsLogBuf.Destroy(); }
void DestroyRpgLogBuf() { console::g_rpgLogBuf.Destroy(); }
void DestroyUnitAcksLogBuf() { console::g_unitAcksLogBuf.Destroy(); }
void DestroyNetLogBuf() { console::g_netLogBuf.Destroy(); }
void DestroyAllLogBuf() { console::g_allLogBuf.Destroy(); }
void DestroyConsoleBuf() { console::g_consoleBuf.Destroy(); }
} // namespace


void ConstructAiLogBuf() { BuildStreambuf(console::g_aiLogBuf, 1, DestroyAiLogBuf); }
void ConstructEventLogBuf() { BuildStreambuf(console::g_eventLogBuf, 2, DestroyEventLogBuf); }
void ConstructGraphicsLogBuf() { BuildStreambuf(console::g_graphicsLogBuf, 4, DestroyGraphicsLogBuf); }
void ConstructRpgLogBuf() { BuildStreambuf(console::g_rpgLogBuf, 8, DestroyRpgLogBuf); }
void ConstructUnitAcksLogBuf() { BuildStreambuf(console::g_unitAcksLogBuf, 0x10, DestroyUnitAcksLogBuf); }
void ConstructNetLogBuf() { BuildStreambuf(console::g_netLogBuf, 0x20, DestroyNetLogBuf); }
void ConstructAllLogBuf() { BuildStreambuf(console::g_allLogBuf, 0xFFFFFFFF, DestroyAllLogBuf); }
void ConstructConsoleBuf() { BuildStreambuf(console::g_consoleBuf, 0, DestroyConsoleBuf); }

void ConstructAiLogOut() { console::g_aiLogOut.Attach(&console::g_aiLogBuf.Get()); }
void ConstructEventLogOut() { console::g_eventLogOut.Attach(&console::g_eventLogBuf.Get()); }
void ConstructGraphicsLogOut() { console::g_graphicsLogOut.Attach(&console::g_graphicsLogBuf.Get()); }
void ConstructRpgLogOut() { console::g_rpgLogOut.Attach(&console::g_rpgLogBuf.Get()); }
void ConstructNetLogOut() { console::g_netLogOut.Attach(&console::g_netLogBuf.Get()); }
void ConstructUnitAcksLogOut() { console::g_unitAcksLogOut.Attach(&console::g_unitAcksLogBuf.Get()); }
void ConstructAllLogOut() { console::g_allLogOut.Attach(&console::g_allLogBuf.Get()); }
void ConstructConsoleOut() { console::g_consoleOut.Attach(&console::g_consoleBuf.Get()); }
