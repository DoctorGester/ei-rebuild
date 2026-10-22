#pragma once

// MFC's globals that only game.exe's static initialisers and their exit functions reach (C18u): the generated
// static_init.cpp / static_exit.cpp name them (tools/gen_static_init.py's GENERATED_NAMES) where the original's code
// has their addresses. Nothing else of ours reads them: the code that does in game.exe (AfxThrowUserException,
// AfxGetThreadState's locals, _AfxCbtFilterHook) is not ported, the image's MFC state is ImageModuleState and
// thread_local handle maps (mfc_state.h). docs/notes/standalone.md, "Named data (C18)".

#include <cstdint>

#include <windows.h>

#include "lib/mfc_exception.h"
#include "lib/mfc_state.h"
#include "lib/static_object.h"

namespace mfc::impl {

// The once-only guards of the exit functions that destroy MFC's static fixed allocators and windows: bit 1 CTempDC's
// allocator (007C8D18), bit 2 CTempGdiObject's (007C8CF0); CTempWnd's (007CA940); CTempMenu's (007CAAA8); bits 1, 2,
// 4, 8 the static windows wndTop, wndBottom, wndTopMost, wndNoTopMost (007CAA28, 007CA9E8, 007CA9A8, 007CA968).
extern std::uint8_t g_gdiAllocExitGuards;
extern std::uint8_t g_tempWndAllocExitGuard;
extern std::uint8_t g_tempMenuAllocExitGuard;
extern std::uint8_t g_staticWindowsExitGuards;

// _simpleUserException: built by its initialiser (ConstructUser, AFX_IDS_USER_EXCEPTION), destroyed at exit.
extern lib::StaticObject<CSimpleException> g_simpleUserException;

// The thread and process locals nothing fills in the image (their destructors find an empty slot or object):
// _afxThreadState, _afxBaseModuleState, _afxCtl3dThread, _afxCtl3dState, _afxWinState.
extern CThreadLocalObject g_threadStateLocal;
extern CProcessLocalObject g_baseModuleStateLocal;
extern CThreadLocalObject g_ctl3dThreadLocal;
extern CProcessLocalObject g_ctl3dStateLocal;
extern CProcessLocalObject g_winStateLocal;

// _afxDBCS: AfxInitDbcs's result, stored by its initialiser (read in game.exe only by _AfxCbtFilterHook, 00704BE6:
// no game window is a CWnd).
extern BOOL g_afxDbcs;

} // namespace mfc::impl
