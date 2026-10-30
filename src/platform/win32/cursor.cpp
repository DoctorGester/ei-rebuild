// The system's cursor on Windows (platform/cursor.h). Through game.exe's import slots: the harness's own mouse answers
// GetCursorPos and keeps SetCursorPos from moving the user's cursor.
#include "platform/cursor.h"

#include "lib/gameapi.h"

#include <windows.h>

namespace platform {
namespace {

PointMap g_toGame = nullptr;
PointMap g_toScreen = nullptr;

} // namespace

CursorShape CurrentCursor() { return gameapi::GetCursor(); }

void SetCursor(CursorShape shape) { gameapi::SetCursor(static_cast<HCURSOR>(shape)); }

void MapCursorPoints(PointMap toGame, PointMap toScreen) {
    g_toGame = toGame;
    g_toScreen = toScreen;
}

bool CursorPosition(Point* at) {
    POINT p;
    if (!gameapi::GetCursorPos(&p)) return false;
    Point point{static_cast<int>(p.x), static_cast<int>(p.y)};
    if (g_toGame) g_toGame(&point);
    *at = point;
    return true;
}

void SetCursorPosition(Point at) {
    if (g_toScreen) g_toScreen(&at);
    gameapi::SetCursorPos(at.x, at.y);
}

} // namespace platform
