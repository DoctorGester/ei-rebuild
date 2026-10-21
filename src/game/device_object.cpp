// The device object itself (CTextureDevice, texture.h; vtable 0073D760): its constructor (004E5E50),
// its destructor body (004E68A0) and the texture format table (004E6EE0). A-S13: IDirect3DDevice7 is
// called as the original calls it, through device.h (the switch-over device answers). The Direct3D device
// enumeration callback (004ED820) and the depth buffer (004E6C30) had no caller of ours left and
// went in R11b (config/dead_code.csv). The base class is in directdraw_holder.cpp. See
// docs/notes/renderer.md (sections 9, 12).
//
// A failed call throws the game's error object (ThrowGameError, dx_errors.h); nothing of ours is
// unwound on the way, and every such throw ends the game in WinMain.
#include "app.h"
#include "device.h"
#include "game_strings.h"
#include "device_material.h"

#include "dx_errors.h"
#include "engine_object.h"
#include "model.h"               // CTransformPipeline
#include "originals_engine.h"
#include "originals_render.h"
#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "game_data.h"

#include <float.h>

#include <cstdint>
#include <cstring>

namespace {


// The four state blocks of texture stage 0's colour and alpha operations, in the order they are
// recorded: their handles (007AEDDC texture x current with the texture's alpha, the terrain's;
// 007AEDD8 texture x diffuse for colour and alpha; 007AEDD4 texture x diffuse with the texture's
// alpha; 007AEDD0 texture x diffuse with the diffuse alpha, the scene's) and what differs between
// them. Every block has colour argument 1 the texture and the colour operation modulate.
struct StageBlock {
    DWORD* handle;
    DWORD colourArg2, alphaArg1, alphaOp, alphaArg2;
};
const StageBlock kStageBlocks[4] = {
    {ei::GamePointer<DWORD>(0x007AEDDC), D3DTA_CURRENT, D3DTA_TEXTURE, D3DTOP_SELECTARG1, D3DTA_CURRENT},
    {ei::GamePointer<DWORD>(0x007AEDD8), D3DTA_DIFFUSE, D3DTA_TEXTURE, D3DTOP_MODULATE, D3DTA_DIFFUSE},
    {ei::GamePointer<DWORD>(0x007AEDD4), D3DTA_DIFFUSE, D3DTA_TEXTURE, D3DTOP_SELECTARG1, D3DTA_CURRENT},
    {ei::GamePointer<DWORD>(0x007AEDD0), D3DTA_DIFFUSE, D3DTA_DIFFUSE, D3DTOP_SELECTARG1, D3DTA_CURRENT},
};

constexpr std::uint32_t kDxtCode = MAKEFOURCC('D', 'X', 'T', 0);   // a DXT format's code without its digit
// The format ids of the RGB layouts (MmpHeader::format).
constexpr std::uint32_t kFormat565 = 0x5650, kFormat555 = 0x5550, kFormat1555 = 0x5551, kFormat4444 = 0x4444,
                        kFormat8888 = 0x8888;

// IDirect3DDevice7::EnumTextureFormats's and IDirect3D7::EnumZBufferFormats's callback (the
// original's is 004E5E20, RET 8): the format appended to the list `context`.
HRESULT CALLBACK PixelFormatCallback(DDPIXELFORMAT* format, void* context) {
    PixelFormat copy;
    std::memcpy(&copy, format, sizeof(copy));
    static_cast<stlp::list<PixelFormat>*>(context)->PushBack(copy);
    return D3DENUMRET_OK;
}

// The four state blocks of texture stage 0 (kStageBlocks) recorded on the device.
void RecordStageBlocks(CTextureDevice* device) {
    for (const StageBlock& block : kStageBlocks) {
        IDirect3DDevice7* d3d = device::D3D(device);
        device->lastLoadResult_ = device::BeginStateBlock(d3d);
        if (device->lastLoadResult_ != 0 && !app::FatalErrorCode(device->lastLoadResult_, "Can't begin state block.\n"))   // "Can't begin state block.\n"
            ThrowGameError("Can't begin state block.\n");
        device::SetTextureStageState(d3d, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device::SetTextureStageState(d3d, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device::SetTextureStageState(d3d, 0, D3DTSS_COLORARG2, block.colourArg2);
        device::SetTextureStageState(d3d, 0, D3DTSS_ALPHAARG1, block.alphaArg1);
        device::SetTextureStageState(d3d, 0, D3DTSS_ALPHAOP, block.alphaOp);
        device::SetTextureStageState(d3d, 0, D3DTSS_ALPHAARG2, block.alphaArg2);
        device->lastLoadResult_ = device::EndStateBlock(d3d, block.handle);
        if (device->lastLoadResult_ != 0 && !app::FatalErrorCode(device->lastLoadResult_, "Can't end state block.\n"))   // "Can't end state block.\n"
            ThrowGameError("Can't end state block.\n");
    }
}

// The rest of the constructor in the switch-over (device.h): no Direct3D object, device list,
// depth formats, depth buffer or render target; the IDirect3DDevice7 token in their place (with the
// HAL device's id). The texture formats come from the switch-over's fixed table, the state blocks
// are recorded on it (their handles tokens the new renderer's mirror knows), the viewport is the
// screen's.
void ConstructWithoutDirect3D(CTextureDevice* device) {
    device->d3dDevice_ = device::Direct3DDeviceToken();
    device->deviceGuid_ = device::IidHalDevice();
    device->zEnable_ = D3DZB_TRUE;
    // Direct3D 7 makes its device with the calling thread's x87 precision at 24 bits (the game sets
    // DDSCL_FPUSETUP), and the game's arithmetic runs with it from then on.
    unsigned int x87 = 0;
    __control87_2(_PC_24, _MCW_PC, &x87, nullptr);
    static_cast<CTextureDevice*>(device)->BuildFormatTable();
    const RECT& screen = device->screen_;
    D3DVIEWPORT7 viewport;
    viewport.dwX = static_cast<DWORD>(screen.left);
    viewport.dwY = static_cast<DWORD>(screen.top);
    viewport.dwWidth = static_cast<DWORD>(screen.right - screen.left);
    viewport.dwHeight = static_cast<DWORD>(screen.bottom - screen.top);
    viewport.dvMinZ = 0.0f;
    viewport.dvMaxZ = 1.0f;
    device::SetViewport(device::D3D(device), &viewport);
    for (CDXSurface*& holder : device->stageSurfaces_) holder = nullptr;
    RecordStageBlocks(device);
    device->colour_ = 0xFF000000;
}

// One of the batches' lists emptied: each buffer released.
void ReleaseBuffers(stlp::list<CVertexBufferHolder*>& buffers) {
    buffers.ClearWith([](CVertexBufferHolder** buffer) {
        if (*buffer) (*buffer)->Release();
    });
}

// A batch's members destroyed (its list is empty by then).
void DestroyBatch(DeviceVertexBatch& batch) {
    batch.indices.FreeData();
    if (batch.buffer) batch.buffer->Release();
    batch.used.Destroy();
}

// A batch's members as the constructor leaves them (its vertex memory and vertex count are not set).
void ConstructBatch(DeviceVertexBatch& batch) {
    batch.used.Construct();
    batch.buffer = nullptr;
    batch.indices.Construct();
}

} // namespace

void CTextureDevice::BuildFormatTable() {
    textureFormats_.ClearWith([](PixelFormat*) {});
    lastLoadResult_ = device::EnumTextureFormats(device::D3D(this), &PixelFormatCallback, &textureFormats_);
    // "Choosen HW device have no texture support.\n"
    if (lastLoadResult_ == D3DERR_TEXTURE_NO_SUPPORT) ThrowGameErrorResult(lastLoadResult_, "Choosen HW device have no texture support.\n");
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, "Can't enumerate texture formats.\n");   // "Can't enumerate texture formats.\n"

    formats_.Clear();
    largestFormatId_ = 0;
    formats_.Resize(0x40);
    largestFormatId_ = 0;
    for (auto* node = textureFormats_.First(); node != textureFormats_.End(); node = node->next) {
        PixelFormat* format = &node->data;
        const auto* described = reinterpret_cast<const DDPIXELFORMAT*>(format);
        std::uint32_t id;
        if (described->dwFlags & DDPF_FOURCC) {
            const std::uint32_t code = described->dwFourCC;
            if ((code & 0x00FFFFFF) != kDxtCode) {
                // The code as text.
                char text[5];
                std::memcpy(text, &code, 4);
                text[4] = 0;
                game::DebugLog("Non-DXT FourCC format found: 0x%.8X = %s.\n", code, text);   // "Non-DXT FourCC format found: 0x%.8X = %s.\n"
                continue;
            }
            id = code;
        } else if (described->dwFlags & DDPF_RGB) {
            ChannelLayout layout;
            layout.Split(format);
            if (layout.redBits == 5) {
                if (layout.greenBits == 6) {
                    if (layout.blueBits != 5) continue;
                    id = kFormat565;
                } else if (layout.greenBits == 5 && layout.blueBits == 5) {
                    if (layout.alphaBits == 0) id = kFormat555;
                    else if (layout.alphaBits == 1) id = kFormat1555;
                    else continue;
                } else {
                    continue;
                }
            } else if (layout.redBits == 4) {
                if (layout.greenBits != 4 || layout.blueBits != 4 || layout.alphaBits != 4) continue;
                id = kFormat4444;
            } else if (layout.redBits == 8) {
                if (layout.greenBits != 8 || layout.blueBits != 8 || layout.alphaBits != 8) continue;
                id = kFormat8888;
            } else {
                continue;
            }
        } else {
            continue;
        }
        if (largestFormatId_ < id) largestFormatId_ = id;
        formats_[static_cast<int>(id)] = format;
    }
}
EI_REPLACE_METHOD(0x004E6EE0, CTextureDevice::BuildFormatTable);

void CTextureDevice::Destruct() {
    ei::SetVtable(this, 0x0073D760);
    for (const StageBlock& block : kStageBlocks) {
        const DWORD handle = *block.handle;
        lastLoadResult_ = device::DeleteStateBlock(device::D3D(this), handle);
        if (lastLoadResult_ != 0) app::FatalErrorCode(lastLoadResult_, "Can't delete state block %d.\n", handle);   // "Can't delete state block %d.\n"
    }
    ReleaseBuffers(batch_.used);
    ReleaseBuffers(batch2_.used);
    ReleaseBuffers(spareBuffers_);
    device::AssignHolder(depth2_, nullptr);
    device::AssignHolder(depth_, nullptr);
    if (CTransformPipeline* pipeline = pipeline_) {
        pipeline_ = nullptr;
        pipeline->Release();
    }
    if (d3dDevice_) device::Release(device::D3D(this));
    if (d3d_) device::Direct3D(this)->Release();

    // The members' destructors, last to first.
    if (material_) material_->Release();
    spareArray_.FreeData();
    if (spareBuffer_) spareBuffer_->Release();
    spareBuffers_.Destroy();
    DestroyBatch(batch2_);
    DestroyBatch(batch_);
    formats_.Destroy();
    textureFormats_.Destroy();
    depthFormats_.Destroy();
    devices_.ClearWith([](Direct3DDeviceRecord* record) { record->~Direct3DDeviceRecord(); });
    devices_.Destroy();
    if (pipeline_) pipeline_->Release();
    if (depth2_) depth2_->Release();
    if (depth_) depth_->Release();
    DestructDirectDraw();
}
EI_REPLACE_METHOD(0x004E68A0, CTextureDevice::Destruct);

// The base's constructor and the format table are called through their addresses
// (originals_render.h), as the original calls them. The original then makes the Direct3D object,
// takes the HAL device (the T&L HAL is skipped by its id), the depth buffer (004E6C30) and the render
// target; none of that exists in the switch-over.
CTextureDevice* CTextureDevice::Construct(HWND window, GUID* driver, int fullScreen, int width, int height, int bitsPerPixel,
                                          int refreshRate, unsigned backBuffers, int /*depthBits*/, int /*stencilBits*/) {
    ConstructDirectDraw(window, driver, fullScreen, width, height, bitsPerPixel, refreshRate,
                                    backBuffers, 1);
    depth_ = nullptr;
    depth2_ = nullptr;
    deviceGuid_ = GUID{};
    pipeline_ = nullptr;
    devices_.Construct();
    depthFormats_.Construct();
    textureFormats_.Construct();
    formats_.InitBuckets(100);
    largestFormatId_ = 0;
    ConstructBatch(batch_);
    ConstructBatch(batch2_);
    spareBuffers_.Construct();
    spareBuffer_ = nullptr;
    spareArray_.Construct();
    material_ = nullptr;
    ei::SetVtable(this, 0x0073D760);
    d3dDevice_ = nullptr;
    d3d_ = nullptr;

    // The switch-over (device.h): no Direct3D object or device (R11a: no DirectDraw fallback).
    ConstructWithoutDirect3D(this);
    return this;
}
EI_REPLACE_METHOD(0x004E5E50, CTextureDevice::Construct);
