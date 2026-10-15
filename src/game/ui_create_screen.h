#pragma once

// Screens 8 and 9 of CInterface3D::CreateScreen (ui_create_screen.cpp).

class CInterface3D;

// 005F6F30 (thiscall, RET 4). Screen 9's constructor; returns `self`.
void* __fastcall Screen9Construct(void* self, void* unused, CInterface3D* ui);
// 005F6D50 (thiscall, RET 0xC). Screen 8's constructor; returns `self`.
void* __fastcall Screen8Construct(void* self, void* unused, CInterface3D* ui, int flag, const char* text);
// 005F6E40 (thiscall). Screen 8's destructor body.
void __fastcall Screen8Destroy(void* self);

// Screen 8, the titles (vtable 00740C3C): only its deleting destructor is a method yet.
struct CTitlesScreen {
    // 005F6E20 (slot 0; ui_window_delete.cpp). Screen8Destroy, then the memory freed when bit 0 of
    // `flags` is set; returns this.
    CTitlesScreen* TitlesDelete(unsigned flags);
};
