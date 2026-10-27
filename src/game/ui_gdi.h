#pragma once

// GDI drawing through the game's statically linked MFC 4.2 (src/lib/mfc_gdi.h), as the interface uses it
// on its off-screen surfaces: CUiSurface's GetDC returns a CDC, ReleaseDC
// gives it back. The interface draws with MFC's CDC, CPen, CBrush and CFont.

#include "lib/mfc_gdi.h"

#include <windows.h>

class CUiSurface;
// Declared in ui.h too): a surface's CDC and its release.
mfc::impl::CDC* __fastcall UiSurfaceGetDC(CUiSurface* surface);
void __fastcall UiSurfaceReleaseDC(CUiSurface* surface);
