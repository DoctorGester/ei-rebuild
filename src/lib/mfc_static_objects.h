#pragma once

// MFC's globals that only game.exe's static initialisers and their exit functions reach: the generated
// static_init.cpp / static_exit.cpp name them (tools/gen_static_init.py's GENERATED_NAMES) where game.exe's code
// has their addresses. Nothing else of ours reads them: the code that does in game.exe (AfxThrowUserException,
// AfxGetThreadState's locals, _AfxCbtFilterHook) is not in our source, the image's MFC state is ImageModuleState and
// thread_local handle maps (mfc_state.h). docs/notes/standalone.md, "Named data".

#include <cstdint>

#include <windows.h>

#include "lib/mfc_exception.h"
#include "lib/mfc_state.h"
#include "lib/static_object.h"

namespace mfc::impl {

// The once-only guard of the exit functions that destroy MFC's static windows: bits 1, 2, 4, 8 wndTop, wndBottom,
// wndTopMost, wndNoTopMost.
extern std::uint8_t g_staticWindowsExitGuards;

// _simpleUserException: built by its initialiser (CUserException::Construct, AFX_IDS_USER_EXCEPTION), destroyed at
// exit (DestroyUserException).
extern lib::StaticObject<CUserException> g_simpleUserException;

// The thread and process locals nothing fills in the image (their destructors find an empty slot or object):
// _afxThreadState, _afxBaseModuleState, _afxCtl3dThread, _afxCtl3dState, _afxWinState.
extern CThreadLocalObject g_threadStateLocal;
extern CProcessLocalObject g_baseModuleStateLocal;
extern CThreadLocalObject g_ctl3dThreadLocal;
extern CProcessLocalObject g_ctl3dStateLocal;
extern CProcessLocalObject g_winStateLocal;

// _afxDBCS: AfxInitDbcs's result, stored by its initialiser (read in game.exe only by _AfxCbtFilterHook:
// no game window is a CWnd).
extern BOOL g_afxDbcs;

} // namespace mfc::impl
