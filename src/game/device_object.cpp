// The device object itself (CTextureDevice, texture.h): its constructor, its destructor body and the texture
// format table. IDirect3DDevice7 is called through device.h (the switch-over device answers). The Direct3D
// device enumeration callback and the depth buffer are gone (config/dead_code.csv). The base class is in See
// docs/notes/renderer.md (sections 9, 12). A failed call throws the game's error object (ThrowGameError,
// dx_errors.h); every such throw ends the game in WinMain.
#include "app.h"
#include "device.h"
#include "game_strings.h"
#include "device_material.h"

#include "dx_errors.h"
#include "engine_object.h"
#include "model.h"               // CTransformPipeline
#include "texture.h"

#include "lib/crt.h"
#include "game_data.h"

#include <float.h>

#include <cstdint>
#include <cstring>
#include <list>
#include <memory>

DWORD g_stageBlocks[4] = /*007AEDD0*/ {};
static_assert(kStageBlockCount == 4);

namespace {


// The four state blocks of texture stage 0's colour and alpha operations, in the order they are
// recorded (the terrain's first, the scene's last): their handle and what differs between them. Every
// block has colour argument 1 the texture and the colour operation modulate.
struct StageBlockSetup {
    StageBlock block;
    DWORD colourArg2, alphaArg1, alphaOp, alphaArg2;
};
const StageBlockSetup kStageBlocks[kStageBlockCount] = {
    {kTerrainBlock, D3DTA_CURRENT, D3DTA_TEXTURE, D3DTOP_SELECTARG1, D3DTA_CURRENT},
    {kAlphaPassBlock, D3DTA_DIFFUSE, D3DTA_TEXTURE, D3DTOP_MODULATE, D3DTA_DIFFUSE},
    {kTextureAlphaBlock, D3DTA_DIFFUSE, D3DTA_TEXTURE, D3DTOP_SELECTARG1, D3DTA_CURRENT},
    {kSceneBlock, D3DTA_DIFFUSE, D3DTA_DIFFUSE, D3DTOP_SELECTARG1, D3DTA_CURRENT},
};

constexpr std::uint32_t kDxtCode = MAKEFOURCC('D', 'X', 'T', 0);   // a DXT format's code without its digit
// The format ids of the RGB layouts (MmpHeader::format).
constexpr std::uint32_t kFormat565 = 0x5650, kFormat555 = 0x5550, kFormat1555 = 0x5551, kFormat4444 = 0x4444,
                        kFormat8888 = 0x8888;

// IDirect3DDevice7::EnumTextureFormats's and IDirect3D7::EnumZBufferFormats's callback: the format appended to the
// list `context`.
HRESULT CALLBACK PixelFormatCallback(DDPIXELFORMAT* format, void* context) {
    PixelFormat copy;
    std::memcpy(&copy, format, sizeof(copy));
    static_cast<std::list<PixelFormat>*>(context)->push_back(copy);
    return D3DENUMRET_OK;
}

// The four state blocks of texture stage 0 (kStageBlocks) recorded on the device.
void RecordStageBlocks(CTextureDevice* device) {
    for (const StageBlockSetup& block : kStageBlocks) {
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
        device->lastLoadResult_ = device::EndStateBlock(d3d, &g_stageBlocks[block.block]);
        if (device->lastLoadResult_ != 0 && !app::FatalErrorCode(device->lastLoadResult_, "Can't end state block.\n"))   // "Can't end state block.\n"
            ThrowGameError("Can't end state block.\n");
    }
}

// The rest of the constructor in the switch-over: no Direct3D object, device list, depth formats, depth
// buffer or render target; the IDirect3DDevice7 token in their place (with the HAL device's id). The texture
// formats come from the switch-over's fixed table, the state blocks are recorded on it (their handles tokens
// the new renderer's mirror knows), the viewport is the screen's.
void ConstructWithoutDirect3D(CTextureDevice* device) {
    device->d3dDevice_ = device::Direct3DDeviceToken();
    device->deviceGuid_ = device::kIidHalDevice;
    device->zEnable_ = D3DZB_TRUE;
    // Direct3D 7 makes its device with the calling thread's x87 precision at 24 bits (the game sets
    // DDSCL_FPUSETUP), and the game's arithmetic runs with it from then on.
    unsigned int x87 = 0;
    __control87_2(_PC_24, _MCW_PC, &x87, nullptr);
    device->BuildFormatTable();
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

} // namespace

void CTextureDevice::BuildFormatTable() {
    textureFormats_.clear();
    lastLoadResult_ = device::EnumTextureFormats(device::D3D(this), &PixelFormatCallback, &textureFormats_);
    // "Choosen HW device have no texture support.\n"
    if (lastLoadResult_ == D3DERR_TEXTURE_NO_SUPPORT) ThrowGameErrorResult(lastLoadResult_, "Choosen HW device have no texture support.\n");
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, "Can't enumerate texture formats.\n");   // "Can't enumerate texture formats.\n"

    formats_.Clear();
    largestFormatId_ = 0;
    formats_.Resize(0x40);
    largestFormatId_ = 0;
    for (PixelFormat& entry : textureFormats_) {
        PixelFormat* format = &entry;
        const auto* described = reinterpret_cast<const DDPIXELFORMAT*>(format);
        std::uint32_t id;
        if (described->dwFlags & DDPF_FOURCC) {
            const std::uint32_t code = described->dwFourCC;
            if ((code & 0x00FFFFFF) != kDxtCode) {
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

CTextureDevice::~CTextureDevice() {
    for (const StageBlockSetup& block : kStageBlocks) {
        const DWORD handle = g_stageBlocks[block.block];
        lastLoadResult_ = device::DeleteStateBlock(device::D3D(this), handle);
        if (lastLoadResult_ != 0) app::FatalErrorCode(lastLoadResult_, "Can't delete state block %d.\n", handle);   // "Can't delete state block %d.\n"
    }
    device::AssignHolder(depth2_, nullptr);
    device::AssignHolder(depth_, nullptr);
    if (CTransformPipeline* pipeline = pipeline_) {
        pipeline_ = nullptr;
        pipeline->Release();
    }
    if (d3dDevice_) device::Release(device::D3D(this));
    if (d3d_) device::Direct3D(this)->Release();

    if (material_) material_->Release();
    formats_.Destroy();
    devices_.clear();
    if (pipeline_) pipeline_->Release();
    if (depth2_) depth2_->Release();
    if (depth_) depth_->Release();
}

// The base's constructor and the format table first. The game then makes the Direct3D object, takes the HAL device
// (the T&L HAL is skipped by its id), the depth buffer and the render target; none of that exists in the
// switch-over.
CTextureDevice::CTextureDevice(HWND window, GUID* driver, int fullScreen, int width, int height, int bitsPerPixel,
                               int refreshRate, unsigned backBuffers, int /*depthBits*/, int /*stencilBits*/)
    : CDXDirectDraw(window, driver, fullScreen, width, height, bitsPerPixel, refreshRate, backBuffers, 1),
      depth_(nullptr), depth2_(nullptr), deviceGuid_{}, pipeline_(nullptr) {
    formats_.InitBuckets(100);
    largestFormatId_ = 0;
    material_ = nullptr;
    d3dDevice_ = nullptr;
    d3d_ = nullptr;

    // The switch-over: no Direct3D object or device, no DirectDraw fallback.
    ConstructWithoutDirect3D(this);
}
