// The console's static objects: its two command maps, the eight streambufs (one per message filter) and the eight
// streams on them, plain globals of ours. The streambufs are built when the exe starts and destroyed after the
// game's exit functions; a stream only names its streambuf (constant-initialised). See docs/notes/console.md.
#include "console.h"

namespace console {

CommandMap g_publicCommands;
CommandMap g_cheatCommands;

namespace {

ConsoleStreambuf g_consoleBuf(0);
ConsoleStreambuf g_allLogBuf(0xFFFFFFFF);
ConsoleStreambuf g_netLogBuf(0x20);
ConsoleStreambuf g_unitAcksLogBuf(0x10);
ConsoleStreambuf g_rpgLogBuf(8);
ConsoleStreambuf g_graphicsLogBuf(4);
ConsoleStreambuf g_eventLogBuf(2);
ConsoleStreambuf g_aiLogBuf(1);

} // namespace

constinit Stream g_consoleOut(g_consoleBuf);
constinit Stream g_allLogOut(g_allLogBuf);
constinit Stream g_unitAcksLogOut(g_unitAcksLogBuf);
constinit Stream g_netLogOut(g_netLogBuf);
constinit Stream g_rpgLogOut(g_rpgLogBuf);
constinit Stream g_graphicsLogOut(g_graphicsLogBuf);
constinit Stream g_eventLogOut(g_eventLogBuf);
constinit Stream g_aiLogOut(g_aiLogBuf);

} // namespace console
