#pragma once

// The battle camera's key input (ui_camera_keys.cpp).

// 005EADD0 (thiscall on the input object, RET 0xC; slot 9 of vtable 00740208): see
// ui_camera_keys.cpp.
void __fastcall CameraKeysOnKeyDown(void* self, void* unused, int key, int repeat, int flags);
