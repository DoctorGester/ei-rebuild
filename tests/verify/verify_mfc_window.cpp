// Check for MFC's AfxMessageBox (src/lib/mfc_window.cpp, ticket S8).
//
// Ours only: the box is modal (and the harness ends a run that shows one), so it cannot run twice
// and no scenario reaches it. What the port assumes is checked when it runs: the original's module
// state has no application, and there is no main window or routing frame (read through the
// original's own lookups, the calls its AfxMessageBox makes).
#include "verify.h"

#include "../../src/lib/mfc_window.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

void* s_unused;

// AFX_MODULE_STATE: +0x04 m_pCurrentWinApp.
struct ModuleState {
    void* unused_00;
    void* app;
};

__declspec(naked) ModuleState* __cdecl ModuleStateAt() EI_ORIGINAL(0x0070CE8C)    // AfxGetModuleState
__declspec(naked) void* __cdecl MainWindowAt() EI_ORIGINAL(0x006E6AD6)             // AfxGetMainWnd
__declspec(naked) void* __cdecl RoutingFrameAt() EI_ORIGINAL(0x0070ADCD)           // CCmdTarget::GetRoutingFrame_

int __stdcall MessageBoxVerify(const char* text, unsigned type, unsigned helpId) {
    const void* app = ModuleStateAt()->app;
    const void* mainWindow = MainWindowAt();
    const void* frame = RoutingFrameAt();
    ei::verify::Check("AfxMessageBox", true, !app && !mainWindow && !frame, "application %p, main window %p, routing frame %p", app,
                      mainWindow, frame);
    return mfc::impl::MessageBoxText(text, type, helpId);
}

} // namespace

EI_VERIFY(0x0070AC94, MessageBoxVerify, 0, s_unused);
