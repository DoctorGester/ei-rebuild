#pragma once

// The old renderer's device layer (A-S13): what the ports of the device object (CTextureDevice,
// texture.h), its DirectDraw base and the weather draws share. DirectDraw 7 / Direct3D 7 are called
// through their COM interfaces as the original does: they are the boundary the new renderer sits
// behind (docs/notes/renderer.md, sections 10 and 12).

#include "game_strings.h"
#include "device_switchover.h"   // device::SwitchOverRenderState
#include "texture.h"
#include "game_data.h"

#include <d3d.h>

#include <cstdint>

// A Direct3D device of the driver, as IDirect3D7::EnumDevices lists them (0x104 bytes; the
// device object's list at +0xB0).
struct Direct3DDeviceRecord {
    stlp::string name;          // +0x00 e.g. "Direct3D HAL"
    stlp::string description;   // +0x0C
    D3DDEVICEDESC7 desc;        // +0x18
};
static_assert(sizeof(Direct3DDeviceRecord) == 0x104 && offsetof(Direct3DDeviceRecord, desc) == 0x18);

namespace device {

// The interface and device ids as game.exe has them.
inline const GUID& IidDirect3D7() { return ei::GameData<const GUID>(0x00745A80); }
inline const GUID& IidHalDevice() { return ei::GameData<const GUID>(0x00745A50); }
inline const GUID& IidTnLHalDevice() { return ei::GameData<const GUID>(0x00745A10); }

// The game's device lock (007AEE78), held around Direct3D draws and surface locks.
// The surface holders alive (007AEE94; 004EFA60 and the other constructors count them).
inline int& HolderCount() { return ei::GameData<int>(0x007AEE94); }
// The texture manager (007AF744).

inline IDirect3DDevice7* D3D(const CTextureDevice* device) { return static_cast<IDirect3DDevice7*>(device->d3dDevice_); }
inline IDirectDraw7* DDraw(const CTextureDevice* device) { return static_cast<IDirectDraw7*>(device->ddraw_); }
inline IDirect3D7* Direct3D(const CTextureDevice* device) { return static_cast<IDirect3D7*>(device->d3d_); }

// A counted holder pointer's assignment: the new holder counted, the old one released.
inline void AssignHolder(CDXSurface*& slot, CDXSurface* holder) {
    CDXSurface* old = slot;
    slot = holder;
    if (holder) ++holder->refCount;
    if (old) old->Release();
}

// Whether two holders stand for the same surface (the original compares their DirectDraw surfaces:
// a window's holder and the render target's, a movie's and the primary's). No holder has a
// DirectDraw surface: the same only when they are one holder.
inline bool SameSurface(const CDXSurface* a, const CDXSurface* b) { return a == b; }

// ---- The switch-over device (device_null.cpp; ticket R9a, docs/notes/renderer.md section 9) ----
//
// The device object is built without DirectDraw and
// Direct3D: no DirectDraw object, display mode, primary, render target or depth surface and no
// Direct3D device; the new renderer presents through its own swap chain on the game window. The
// object is filled for the game: the screen rectangle, holders without a surface for the primary
// and the render target (the screen's width, height and bits), 2DintMMX's converter, the texture
// formats from a fixed table, the four state blocks as tokens. In place of the IDirectDraw7 and
// IDirect3DDevice7 pointers it holds two stand-ins that draw nothing: they keep the render and
// texture stage states the game sets (and feed them to the new renderer's mirror, render_state.h),
// record and apply state blocks, answer the capability, display mode and memory queries with fixed
// values, refuse to make surfaces, and log every method once per calling place ("old device: ...")
// so that the remaining callers can be guarded and the stand-ins removed (ticket R9b).

// The device object is being built without DirectDraw for a `width` x `height` screen of
// `bitsPerPixel` (16 or 32): from now on the calls below answer for the stand-ins. Returns the
// IDirectDraw7 stand-in.
IDirectDraw7* StartWithoutDirectDraw(int width, int height, int bitsPerPixel);
// The IDirect3DDevice7 stand-in, its states back at a new device's.
IDirect3DDevice7* NullDirect3DDevice();
// The screen's pixel format for a colour depth: 16 bits 5:6:5, otherwise X8R8G8B8.
void ScreenFormat(int bitsPerPixel, DDPIXELFORMAT* format);
// The texture formats of the switch-over device (in place of IDirect3DDevice7::EnumTextureFormats):
// 5:6:5, 1:5:5:5, 4:4:4:4, 8:8:8:8, DXT1 and DXT3, as the old device listed them.
const DDPIXELFORMAT* SwitchOverTextureFormats(int* count);

// ---- The rewritten code's calls into the old device (ticket R9b) ----
//
// Each is the IDirect3DDevice7 / IDirectDraw7 method of the same name on the interface given (the
// device object's +0x54 / +0x0C), as the original calls it. Once the device object is built
// (StartWithoutDirectDraw), when the interface is the device object's stand-in (or null), it is not used and the call has the
// same result without a device (any other interface, such as a verify wrapper's recording device,
// is called as before): the render and
// texture stage states, state blocks, bound textures, viewport and scene bracket are kept in the
// switch-over device's state (the states also go to the new renderer's mirror, render_state.h), the
// capability, display mode, texture format and video memory queries answer the fixed values, and
// the draws, clears, mode and cooperative level changes and surface restores do nothing (D3D_OK /
// DD_OK: the new renderer draws from the hand-over and presents on its own swap chain).
HRESULT GetCaps(IDirect3DDevice7* d3d, D3DDEVICEDESC7* desc);
HRESULT EnumTextureFormats(IDirect3DDevice7* d3d, LPD3DENUMPIXELFORMATSCALLBACK callback, void* context);
HRESULT BeginScene(IDirect3DDevice7* d3d);
HRESULT EndScene(IDirect3DDevice7* d3d);
HRESULT Clear(IDirect3DDevice7* d3d, DWORD count, D3DRECT* rects, DWORD flags, D3DCOLOR colour, D3DVALUE z,
              DWORD stencil);
HRESULT SetViewport(IDirect3DDevice7* d3d, D3DVIEWPORT7* viewport);
HRESULT SetRenderTarget(IDirect3DDevice7* d3d, IDirectDrawSurface7* target, DWORD flags);
HRESULT SetRenderState(IDirect3DDevice7* d3d, D3DRENDERSTATETYPE state, DWORD value);
HRESULT GetRenderState(IDirect3DDevice7* d3d, D3DRENDERSTATETYPE state, DWORD* value);
HRESULT SetTextureStageState(IDirect3DDevice7* d3d, DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value);
HRESULT GetTextureStageState(IDirect3DDevice7* d3d, DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD* value);
HRESULT BeginStateBlock(IDirect3DDevice7* d3d);
HRESULT EndStateBlock(IDirect3DDevice7* d3d, DWORD* handle);
HRESULT ApplyStateBlock(IDirect3DDevice7* d3d, DWORD handle);
HRESULT DeleteStateBlock(IDirect3DDevice7* d3d, DWORD handle);
HRESULT SetTexture(IDirect3DDevice7* d3d, DWORD stage, IDirectDrawSurface7* texture);
HRESULT DrawPrimitive(IDirect3DDevice7* d3d, D3DPRIMITIVETYPE type, DWORD fvf, void* vertices, DWORD count,
                      DWORD flags);
HRESULT DrawIndexedPrimitive(IDirect3DDevice7* d3d, D3DPRIMITIVETYPE type, DWORD fvf, void* vertices, DWORD count,
                             WORD* indices, DWORD indexCount, DWORD flags);
HRESULT GetCaps(IDirectDraw7* ddraw, DDCAPS* driver, DDCAPS* emulated);
HRESULT GetDisplayMode(IDirectDraw7* ddraw, DDSURFACEDESC2* desc);
HRESULT EnumDisplayModes(IDirectDraw7* ddraw, DWORD flags, DDSURFACEDESC2* filter, void* context,
                         LPDDENUMMODESCALLBACK2 callback);
HRESULT RestoreDisplayMode(IDirectDraw7* ddraw);
HRESULT SetCooperativeLevel(IDirectDraw7* ddraw, HWND window, DWORD flags);
HRESULT GetAvailableVidMem(IDirectDraw7* ddraw, DDSCAPS2* caps, DWORD* total, DWORD* free);
HRESULT RestoreAllSurfaces(IDirectDraw7* ddraw);
HRESULT FlipToGDISurface(IDirectDraw7* ddraw);
HRESULT WaitForVerticalBlank(IDirectDraw7* ddraw, DWORD flags, HANDLE event);

} // namespace device
