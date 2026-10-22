// The console's static objects (game.exe's static initialisers 005DC050-005DC5E0): its two command maps, the eight
// streambufs (one per message filter) and the eight ostreams on them. Each is built by its static initialiser at the
// list's place and destroyed by the exit function that initialiser registers with the game's atexit; both are ported
// on the names and registered at their addresses (docs/notes/standalone.md, "The static initialisers' objects").
// See docs/notes/console.md.
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

// A command map: built empty (map(), 005E0A90), destroyed by ~map (005DC080).
void BuildCommands(lib::StaticObject<console::CommandMap>& map, ExitFunction exit) {
    lib_trees::Commands::Construct(&map.Get());
    libcrt::AtExit(exit);
}
// A streambuf: built for its message filter (005DC200), destroyed by 005DC260.
void BuildStreambuf(StreambufObject& buffer, unsigned filter, ExitFunction exit) {
    buffer.Construct(filter);
    libcrt::AtExit(exit);
}
// An ostream on its streambuf (ostream(streambuf*, 1), 006FC42F); destroyed through its ios, ostream's part
// (006FC502) and then ios's (006FCEDC).
void BuildStream(OStreamObject& stream, StreambufObject& buffer, ExitFunction exit) {
    stream.Construct(&buffer.Get(), 1);
    libcrt::AtExit(exit);
}
void DestroyStream(OStreamObject& stream) {
    stream.ios.DestructOstream();
    stream.ios.Destruct();
}

void __cdecl DestroyCheatCommands() { lib_trees::Commands::Destroy(&console::g_cheatCommands.Get()); }   // 005DC070
void __cdecl DestroyPublicCommands() { lib_trees::Commands::Destroy(&console::g_publicCommands.Get()); }  // 005DC140
void __cdecl ConstructCheatCommands() { BuildCommands(console::g_cheatCommands, DestroyCheatCommands); }     // 005DC050
void __cdecl ConstructPublicCommands() { BuildCommands(console::g_publicCommands, DestroyPublicCommands); }  // 005DC120

void __cdecl DestroyAiLogBuf() { console::g_aiLogBuf.Destroy(); }                    // 005DC2C0
void __cdecl DestroyEventLogBuf() { console::g_eventLogBuf.Destroy(); }              // 005DC2F0
void __cdecl DestroyGraphicsLogBuf() { console::g_graphicsLogBuf.Destroy(); }        // 005DC320
void __cdecl DestroyRpgLogBuf() { console::g_rpgLogBuf.Destroy(); }                  // 005DC350
void __cdecl DestroyUnitAcksLogBuf() { console::g_unitAcksLogBuf.Destroy(); }        // 005DC380
void __cdecl DestroyNetLogBuf() { console::g_netLogBuf.Destroy(); }                  // 005DC3B0
void __cdecl DestroyAllLogBuf() { console::g_allLogBuf.Destroy(); }                  // 005DC3E0
void __cdecl DestroyConsoleBuf() { console::g_consoleBuf.Destroy(); }                // 005DC410
void __cdecl ConstructAiLogBuf() { BuildStreambuf(console::g_aiLogBuf, 1, DestroyAiLogBuf); }                      // 005DC1E0
void __cdecl ConstructEventLogBuf() { BuildStreambuf(console::g_eventLogBuf, 2, DestroyEventLogBuf); }             // 005DC2D0
void __cdecl ConstructGraphicsLogBuf() { BuildStreambuf(console::g_graphicsLogBuf, 4, DestroyGraphicsLogBuf); }    // 005DC300
void __cdecl ConstructRpgLogBuf() { BuildStreambuf(console::g_rpgLogBuf, 8, DestroyRpgLogBuf); }                   // 005DC330
void __cdecl ConstructUnitAcksLogBuf() { BuildStreambuf(console::g_unitAcksLogBuf, 0x10, DestroyUnitAcksLogBuf); } // 005DC360
void __cdecl ConstructNetLogBuf() { BuildStreambuf(console::g_netLogBuf, 0x20, DestroyNetLogBuf); }                // 005DC390
void __cdecl ConstructAllLogBuf() { BuildStreambuf(console::g_allLogBuf, 0xFFFFFFFF, DestroyAllLogBuf); }          // 005DC3C0
void __cdecl ConstructConsoleBuf() { BuildStreambuf(console::g_consoleBuf, 0, DestroyConsoleBuf); }                // 005DC3F0

void __cdecl DestroyAiLogOut() { DestroyStream(console::g_aiLogOut); }               // 005DC440
void __cdecl DestroyEventLogOut() { DestroyStream(console::g_eventLogOut); }         // 005DC480
void __cdecl DestroyGraphicsLogOut() { DestroyStream(console::g_graphicsLogOut); }   // 005DC4C0
void __cdecl DestroyRpgLogOut() { DestroyStream(console::g_rpgLogOut); }             // 005DC500
void __cdecl DestroyNetLogOut() { DestroyStream(console::g_netLogOut); }             // 005DC540
void __cdecl DestroyUnitAcksLogOut() { DestroyStream(console::g_unitAcksLogOut); }   // 005DC580
void __cdecl DestroyAllLogOut() { DestroyStream(console::g_allLogOut); }             // 005DC5C0
void __cdecl DestroyConsoleOut() { DestroyStream(console::g_consoleOut); }           // 005DC600
void __cdecl ConstructAiLogOut() { BuildStream(console::g_aiLogOut, console::g_aiLogBuf, DestroyAiLogOut); }                  // 005DC420
void __cdecl ConstructEventLogOut() { BuildStream(console::g_eventLogOut, console::g_eventLogBuf, DestroyEventLogOut); }       // 005DC460
void __cdecl ConstructGraphicsLogOut() {                                                                                    // 005DC4A0
    BuildStream(console::g_graphicsLogOut, console::g_graphicsLogBuf, DestroyGraphicsLogOut);
}
void __cdecl ConstructRpgLogOut() { BuildStream(console::g_rpgLogOut, console::g_rpgLogBuf, DestroyRpgLogOut); }              // 005DC4E0
void __cdecl ConstructNetLogOut() { BuildStream(console::g_netLogOut, console::g_netLogBuf, DestroyNetLogOut); }              // 005DC520
void __cdecl ConstructUnitAcksLogOut() {                                                                                    // 005DC560
    BuildStream(console::g_unitAcksLogOut, console::g_unitAcksLogBuf, DestroyUnitAcksLogOut);
}
void __cdecl ConstructAllLogOut() { BuildStream(console::g_allLogOut, console::g_allLogBuf, DestroyAllLogOut); }              // 005DC5A0
void __cdecl ConstructConsoleOut() { BuildStream(console::g_consoleOut, console::g_consoleBuf, DestroyConsoleOut); }          // 005DC5E0

} // namespace

EI_REPLACE(0x005DC050, ConstructCheatCommands);
EI_REPLACE(0x005DC070, DestroyCheatCommands);
EI_REPLACE(0x005DC120, ConstructPublicCommands);
EI_REPLACE(0x005DC140, DestroyPublicCommands);
EI_REPLACE(0x005DC1E0, ConstructAiLogBuf);
EI_REPLACE(0x005DC2C0, DestroyAiLogBuf);
EI_REPLACE(0x005DC2D0, ConstructEventLogBuf);
EI_REPLACE(0x005DC2F0, DestroyEventLogBuf);
EI_REPLACE(0x005DC300, ConstructGraphicsLogBuf);
EI_REPLACE(0x005DC320, DestroyGraphicsLogBuf);
EI_REPLACE(0x005DC330, ConstructRpgLogBuf);
EI_REPLACE(0x005DC350, DestroyRpgLogBuf);
EI_REPLACE(0x005DC360, ConstructUnitAcksLogBuf);
EI_REPLACE(0x005DC380, DestroyUnitAcksLogBuf);
EI_REPLACE(0x005DC390, ConstructNetLogBuf);
EI_REPLACE(0x005DC3B0, DestroyNetLogBuf);
EI_REPLACE(0x005DC3C0, ConstructAllLogBuf);
EI_REPLACE(0x005DC3E0, DestroyAllLogBuf);
EI_REPLACE(0x005DC3F0, ConstructConsoleBuf);
EI_REPLACE(0x005DC410, DestroyConsoleBuf);
EI_REPLACE(0x005DC420, ConstructAiLogOut);
EI_REPLACE(0x005DC440, DestroyAiLogOut);
EI_REPLACE(0x005DC460, ConstructEventLogOut);
EI_REPLACE(0x005DC480, DestroyEventLogOut);
EI_REPLACE(0x005DC4A0, ConstructGraphicsLogOut);
EI_REPLACE(0x005DC4C0, DestroyGraphicsLogOut);
EI_REPLACE(0x005DC4E0, ConstructRpgLogOut);
EI_REPLACE(0x005DC500, DestroyRpgLogOut);
EI_REPLACE(0x005DC520, ConstructNetLogOut);
EI_REPLACE(0x005DC540, DestroyNetLogOut);
EI_REPLACE(0x005DC560, ConstructUnitAcksLogOut);
EI_REPLACE(0x005DC580, DestroyUnitAcksLogOut);
EI_REPLACE(0x005DC5A0, ConstructAllLogOut);
EI_REPLACE(0x005DC5C0, DestroyAllLogOut);
EI_REPLACE(0x005DC5E0, ConstructConsoleOut);
EI_REPLACE(0x005DC600, DestroyConsoleOut);
