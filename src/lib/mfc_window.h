#pragma once

// MFC 4.2's AfxMessageBox as the game reaches it (ticket S8, docs/notes/standalone.md section 2).
// The game constructs no CWinApp (CWinApp's constructor 0070D654 has no caller) and has no MFC
// window, so the module state's application is always null: AfxMessageBox runs
// CWinApp::DoMessageBox with no object, EnableModeless and CWnd::GetSafeOwner_ find no window, and
// the box has no owner, no help context and the module's file name as its title. That is the body
// here (registered in mfc_window.cpp; tests/verify/verify_mfc_window.cpp checks the assumption
// against the original's state whenever a box is shown).

namespace mfc::impl {

// AfxMessageBox(LPCTSTR, UINT type, UINT helpId): MessageBoxA's answer. MB_ICONEXCLAMATION is added
// to an OK, OK / Cancel, Yes / No / Cancel or Yes / No box that names no icon.
int __stdcall MessageBoxText(const char* text, unsigned type, unsigned helpId);   // 0070AC94

} // namespace mfc::impl
