#pragma once

// The old renderer's device layer (A-S13): what the ports of the device object (CTextureDevice,
// texture.h), its DirectDraw base and the weather draws share. DirectDraw 7 / Direct3D 7 are called
// through their COM interfaces as the original does: they are the boundary the new renderer sits
// behind (docs/notes/renderer.md, sections 10 and 12).

#include "texture.h"

#include <d3d.h>

#include <cstdint>

namespace device {

// The game's device lock (007AEE78), held around Direct3D draws and surface locks.
inline CRITICAL_SECTION* Lock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
// The surface holders alive (007AEE94; 004EFA60 and the other constructors count them).
inline int& HolderCount() { return *reinterpret_cast<int*>(0x007AEE94); }
// The texture manager (007AF744).
inline CTextureManager* TextureManager() { return *reinterpret_cast<CTextureManager* const*>(0x007AF744); }
// A string of the game's data.
inline const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

inline IDirect3DDevice7* D3D(const CTextureDevice* device) { return static_cast<IDirect3DDevice7*>(device->d3dDevice_); }
inline IDirectDraw7* DDraw(const CTextureDevice* device) { return static_cast<IDirectDraw7*>(device->ddraw_); }
inline IDirectDrawSurface7* Surface(const CDXSurface* holder) { return static_cast<IDirectDrawSurface7*>(holder->surface); }

} // namespace device
