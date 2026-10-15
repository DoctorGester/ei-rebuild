// MFC 4.2's AfxMessageBox, registered at its library address (mfc_window.h).
#include "mfc_window.h"

#include "ei/hook.h"
#include "gameapi.h"

namespace mfc::impl {

int __stdcall MessageBoxText(const char* text, unsigned type, unsigned) {
    if (!(type & MB_ICONMASK)) {
        const unsigned buttons = type & MB_TYPEMASK;
        if (buttons == MB_OK || buttons == MB_OKCANCEL || buttons == MB_YESNOCANCEL || buttons == MB_YESNO) type |= MB_ICONEXCLAMATION;
    }
    char title[MAX_PATH];
    gameapi::GetModuleFileNameA(nullptr, title, MAX_PATH);
    return gameapi::MessageBoxA(nullptr, text, title, type);
}

EI_REPLACE(0x0070AC94, MessageBoxText);

} // namespace mfc::impl
