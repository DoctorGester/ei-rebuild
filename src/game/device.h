#pragma once

// The old renderer's device layer: what the device object (CTextureDevice, texture.h), its DirectDraw base
// and the weather draws share. DirectDraw 7 / Direct3D 7 are called through their COM interfaces: they are
// the boundary the new renderer sits behind (docs/notes/renderer.md, sections 10 and 12).

#include "texture.h"

#include <d3d.h>

#include <cstdint>

// A Direct3D device of the driver, as IDirect3D7::EnumDevices lists them.
struct Direct3DDeviceRecord {
    std::string name;           // e.g. "Direct3D HAL"
    std::string description;
    D3DDEVICEDESC7 desc;
};

namespace device {

inline constexpr GUID kIidHalDevice = /*00745A50*/ {0x84E63DE0, 0x46AA, 0x11CF, {0x81, 0x6F, 0x00, 0x00, 0xC0, 0x20, 0x15, 0x6E}};

// The surface holders alive (the holders' constructors count them).
inline int g_holderCount = /*007AEE94*/ {};

inline IDirect3DDevice7* D3D(const CTextureDevice* device) { return device->d3dDevice_; }
inline IDirectDraw7* DDraw(const CTextureDevice* device) { return device->ddraw_; }
inline IDirect3D7* Direct3D(const CTextureDevice* device) { return device->d3d_; }

// A counted holder pointer's assignment: the new holder counted, the old one released.
inline void AssignHolder(Surface*& slot, Surface* holder) {
    Surface* old = slot;
    slot = holder;
    if (holder) ++holder->refs_;
    if (old) old->Release();
}

// Whether two holders stand for the same surface. No holder has a DirectDraw surface any more, so only when they
// are one holder.
inline bool SameSurface(const Surface* a, const Surface* b) { return a == b; }

// ---- The switch-over device (device_null.cpp, docs/notes/renderer.md section 9) ----
// The device object is built without DirectDraw and Direct3D; in place of the IDirectDraw7 and IDirect3DDevice7
// pointers it holds two tokens that the calls below know. A method called through a token ends the run.

// The device object is being built without DirectDraw for a `width` x `height` screen of `bitsPerPixel` (16 or
// 32). Returns the IDirectDraw7 token (ddraw_).
IDirectDraw7* StartWithoutDirectDraw(int width, int height, int bitsPerPixel);
// The IDirect3DDevice7 token (d3dDevice_), the switch-over device reset to a new device's state.
IDirect3DDevice7* Direct3DDeviceToken();

// ---- The calls into the old device ----
// Each is the IDirect3DDevice7 / IDirectDraw7 method of the same name, given a token: queries answer fixed values,
// clears and mode changes do nothing. No call reaches an interface. (Render and texture stage states are the
// renderer's: render/render_state.h.)
HRESULT GetCaps(IDirect3DDevice7* d3d, D3DDEVICEDESC7* desc);
HRESULT BeginScene(IDirect3DDevice7* d3d);
HRESULT EndScene(IDirect3DDevice7* d3d);
HRESULT Clear(IDirect3DDevice7* d3d, DWORD count, D3DRECT* rects, DWORD flags, D3DCOLOR colour, D3DVALUE z,
              DWORD stencil);
HRESULT SetViewport(IDirect3DDevice7* d3d, D3DVIEWPORT7* viewport);
HRESULT SetTexture(IDirect3DDevice7* d3d, DWORD stage, IDirectDrawSurface7* texture);
HRESULT GetCaps(IDirectDraw7* ddraw, DDCAPS* driver, DDCAPS* emulated);
HRESULT EnumDisplayModes(IDirectDraw7* ddraw, DWORD flags, DDSURFACEDESC2* filter, void* context,
                         LPDDENUMMODESCALLBACK2 callback);
HRESULT RestoreDisplayMode(IDirectDraw7* ddraw);
HRESULT SetCooperativeLevel(IDirectDraw7* ddraw, HWND window, DWORD flags);
HRESULT GetAvailableVidMem(IDirectDraw7* ddraw, DDSCAPS2* caps, DWORD* total, DWORD* free);
HRESULT RestoreAllSurfaces(IDirectDraw7* ddraw);
HRESULT FlipToGDISurface(IDirectDraw7* ddraw);
void AddRef(IDirect3DDevice7* d3d);
void Release(IDirect3DDevice7* d3d);
void Release(IDirectDraw7* ddraw);

} // namespace device
