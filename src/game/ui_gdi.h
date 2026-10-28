#pragma once

// GDI drawing on the interface's off-screen surfaces: CUiSurface's GetDC returns the surface's HDC, ReleaseDC gives
// it back. The interface draws on it with plain GDI calls, its pens, brushes and fonts held by lib/gdi.h.

#include "lib/gdi.h"

#include <windows.h>

class CUiSurface;
// Declared in ui.h too): a surface's DC and its release.
HDC UiSurfaceGetDC(CUiSurface* surface);
void UiSurfaceReleaseDC(CUiSurface* surface);
