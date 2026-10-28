// The device object itself (CTextureDevice, texture.h): its constructor and its destructor body. IDirect3DDevice7 is
// called through device.h (the switch-over device answers). The Direct3D device enumeration callback and the depth
// buffer are gone (config/dead_code.csv); the texture formats are one fixed list (texture.h, TextureFormat). See
// docs/notes/renderer.md (sections 9, 12). A failed call throws the game's error object (ThrowGameError,
// dx_errors.h); every such throw ends the game in WinMain.
#include "device.h"
#include "device_material.h"

#include "engine_object.h"
#include "model.h"               // CTransformPipeline
#include "texture.h"

#include <float.h>

namespace {

// The rest of the constructor in the switch-over: no Direct3D object, device list, depth formats, depth
// buffer or render target; the IDirect3DDevice7 token in their place (with the HAL device's id). The viewport is
// the screen's.
void ConstructWithoutDirect3D(CTextureDevice* device) {
    device->d3dDevice_ = device::Direct3DDeviceToken();
    device->deviceGuid_ = device::kIidHalDevice;
    device->zEnable_ = D3DZB_TRUE;
    // Direct3D 7 makes its device with the calling thread's x87 precision at 24 bits (the game sets
    // DDSCL_FPUSETUP), and the game's arithmetic runs with it from then on.
    unsigned int x87 = 0;
    __control87_2(_PC_24, _MCW_PC, &x87, nullptr);
    const RECT& screen = device->screen_;
    D3DVIEWPORT7 viewport;
    viewport.dwX = static_cast<DWORD>(screen.left);
    viewport.dwY = static_cast<DWORD>(screen.top);
    viewport.dwWidth = static_cast<DWORD>(screen.right - screen.left);
    viewport.dwHeight = static_cast<DWORD>(screen.bottom - screen.top);
    viewport.dvMinZ = 0.0f;
    viewport.dvMaxZ = 1.0f;
    device::SetViewport(device::D3D(device), &viewport);
    for (Surface*& holder : device->stageSurfaces_) holder = nullptr;
    device->colour_ = 0xFF000000;
}

} // namespace

CTextureDevice::~CTextureDevice() {
    device::AssignHolder(depth2_, nullptr);
    device::AssignHolder(depth_, nullptr);
    if (CTransformPipeline* pipeline = pipeline_) {
        pipeline_ = nullptr;
        pipeline->Release();
    }
    if (d3dDevice_) device::Release(device::D3D(this));
    if (d3d_) device::Direct3D(this)->Release();

    if (material_) material_->Release();
    devices_.clear();
    if (pipeline_) pipeline_->Release();
    if (depth2_) depth2_->Release();
    if (depth_) depth_->Release();
}

// The base's constructor first. The game then makes the Direct3D object, takes the HAL device (the T&L HAL is skipped
// by its id), the depth buffer and the render target; none of that exists in the switch-over.
CTextureDevice::CTextureDevice(HWND window, GUID* driver, int fullScreen, int width, int height, int bitsPerPixel,
                               int refreshRate, unsigned backBuffers, int /*depthBits*/, int /*stencilBits*/)
    : CDXDirectDraw(window, driver, fullScreen, width, height, bitsPerPixel, refreshRate, backBuffers, 1),
      depth_(nullptr), depth2_(nullptr), deviceGuid_{}, pipeline_(nullptr) {
    material_ = nullptr;
    d3dDevice_ = nullptr;
    d3d_ = nullptr;

    // The switch-over: no Direct3D object or device, no DirectDraw fallback.
    ConstructWithoutDirect3D(this);
}
