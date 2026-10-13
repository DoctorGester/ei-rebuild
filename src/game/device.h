#pragma once

// The old renderer's device layer (A-S13): what the ports of the device object (CTextureDevice,
// texture.h), its DirectDraw base and the weather draws share. DirectDraw 7 / Direct3D 7 are called
// through their COM interfaces as the original does: they are the boundary the new renderer sits
// behind (docs/notes/renderer.md, sections 10 and 12).

#include "texture.h"

#include <d3d.h>

#include <cstdint>

// A Direct3D device of the driver, as IDirect3D7::EnumDevices lists them (0x104 bytes; the
// device object's list at +0xB0).
struct Direct3DDeviceRecord {
    stlp::string description;   // +0x00
    stlp::string name;          // +0x0C
    D3DDEVICEDESC7 desc;        // +0x18
};
static_assert(sizeof(Direct3DDeviceRecord) == 0x104 && offsetof(Direct3DDeviceRecord, desc) == 0x18);

namespace device {

// DDRAW.dll through game.exe's import table (as lib/gameapi.h for the other libraries).
inline auto& DirectDrawEnumerateExA = *reinterpret_cast<decltype(&::DirectDrawEnumerateExA)*>(0x0073B038);
inline auto& DirectDrawCreateEx = *reinterpret_cast<decltype(&::DirectDrawCreateEx)*>(0x0073B03C);
// The interface and device ids as game.exe has them.
inline const GUID& IidDirectDraw7() { return *reinterpret_cast<const GUID*>(0x00745C90); }
inline const GUID& IidDirect3D7() { return *reinterpret_cast<const GUID*>(0x00745A80); }
inline const GUID& IidHalDevice() { return *reinterpret_cast<const GUID*>(0x00745A50); }
inline const GUID& IidTnLHalDevice() { return *reinterpret_cast<const GUID*>(0x00745A10); }

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
inline IDirect3D7* Direct3D(const CTextureDevice* device) { return static_cast<IDirect3D7*>(device->d3d_); }

// A counted holder pointer's assignment: the new holder counted, the old one released.
inline void AssignHolder(CDXSurface*& slot, CDXSurface* holder) {
    CDXSurface* old = slot;
    slot = holder;
    if (holder) ++holder->refCount;
    if (old) old->Release();
}

} // namespace device
