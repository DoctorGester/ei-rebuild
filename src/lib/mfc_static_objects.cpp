// MFC's globals that only the generated static initialisers and exit functions reach (mfc_static_objects.h).
#include "mfc_static_objects.h"

#include "gameapi.h"

namespace mfc::impl {

static_assert(sizeof(CThreadLocalObject) == 4 && sizeof(CProcessLocalObject) == 4);   // named_data.py's TYPES

constinit std::uint8_t g_gdiAllocExitGuards = /*007C8BB8*/ {};
constinit lib::StaticObject<CUserException> g_simpleUserException = /*007C8BC0*/ {};
constinit CProcessLocalObject g_baseModuleStateLocal = /*007C90BC*/ {};
constinit CThreadLocalObject g_threadStateLocal = /*007C90C0*/ {};
constinit CThreadLocalObject g_ctl3dThreadLocal = /*007CAA84*/ {};
constinit CProcessLocalObject g_ctl3dStateLocal = /*007CAA88*/ {};
constinit CProcessLocalObject g_winStateLocal = /*007CAA8C*/ {};
constinit BOOL g_afxDbcs = /*007CAA98*/ {};

// The DragList message's id (wincore.cpp's nMsgDragList): CWnd's message map entry for it (ON_REGISTERED_MESSAGE)
// points at it; nothing in the image reads it.
constinit unsigned g_dragListMessage = /*007CAA64*/ {};

// a static initialiser (cdecl; the list holds a jump to its body).
void RegisterDragListMessage() { g_dragListMessage = gameapi::RegisterWindowMessageA("commctrl_DragListMsg"); }

} // namespace mfc::impl
