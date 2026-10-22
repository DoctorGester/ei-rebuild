#pragma once

// Screens 8 and 9 of CInterface3D::CreateScreen (ui_create_screen.cpp).

class CInterface3D;

// Screen 8's and 9's constructors are CCreditsScreen::Construct and CExchangeScreen::Construct
// (ui_screens.h).
// 005F6E40 (thiscall). Screen 8's destructor body.
void __fastcall Screen8Destroy(void* self);

// Screen 8, the titles (vtable 00740C3C): only its deleting destructor is a method yet.
struct CTitlesScreen {
    // 005F6E20 (slot 0; ui_window_delete.cpp). Screen8Destroy, then the memory freed when bit 0 of
    // `flags` is set; returns this.
    CTitlesScreen* TitlesDelete(unsigned flags);
};
