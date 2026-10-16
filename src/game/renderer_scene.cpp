// The renderer's scene bracket and texture draw (the renderer at 007AF060, vtable 0073D93C): slot 4
// BeginScene (00503F40) with the frame statistics, slot 5 EndScene (005040A0) with the debug panel's
// FPS and counts, slot 21 DrawTexture (00504860). The main loop (00458010) brackets each frame's
// drawing with slots 4 and 5. The old Direct3D 7 path, kept as it is (docs/notes/renderer.md).
#include "device.h"   // the old device's calls
#include "model.h"     // CSceneRenderer
#include "texture.h"   // CTextureDevice, CTexture

#include "app.h"       // app::FatalErrorCode
#include "clip.h"      // TLVertex
#include "core_maths.h"
#include "ui.h"        // GameDeviceLock, DebugInfoPanel
#include "originals_render.h"
#include "originals_ui.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <d3d.h>

#include <cstdint>

namespace {

constexpr std::uint32_t kLongestCounted = 1000;   // longer frames are not the longest
constexpr std::uint32_t kAverageWindow = 999;     // the average is taken past this many ms
constexpr DWORD kTlVertexFormat = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1;   // 0x1C4
constexpr float kTextureDepth = 0.9f;             // 0x3F666666
constexpr std::uint32_t kWhite = 0xFFFFFFFF;
constexpr std::uint32_t kNoSpecular = 0xFF000000;

IDirect3DDevice7* D3DDevice(CTextureDevice* device) { return static_cast<IDirect3DDevice7*>(device->d3dDevice_); }

// The device calls run under the game's device lock; the result goes to the device's +8.
template <typename Call>
long Locked(CTextureDevice* device, Call call) {
    gameapi::EnterCriticalSection(GameDeviceLock());
    device->lastLoadResult_ = call(D3DDevice(device));
    gameapi::LeaveCriticalSection(GameDeviceLock());
    return device->lastLoadResult_;
}

long DeviceBeginScene(CTextureDevice* device) {
    return Locked(device, [](IDirect3DDevice7* d3d) { return device::BeginScene(d3d); });
}
long DeviceEndScene(CTextureDevice* device) {
    return Locked(device, [](IDirect3DDevice7* d3d) { return device::EndScene(d3d); });
}

} // namespace

int CSceneRenderer::BeginScene() {
    if (!(flags_ & 1)) return 0;
    std::uint32_t now = gameapi::timeGetTime();
    frameStart_ = now;
    std::uint32_t elapsed = now - lastFrameStart_;
    frameTime_ = elapsed;
    if (elapsed < shortestFrame_ && elapsed != 0) shortestFrame_ = elapsed;
    if (longestFrame_ < elapsed && elapsed < kLongestCounted) longestFrame_ = elapsed;
    std::uint32_t window = windowTime_ + elapsed;
    std::uint32_t frames = windowFrames_ + 1;
    ++frameCount_;
    totalTime_ += elapsed;
    windowFrames_ = frames;
    windowTime_ = window;
    if (window > kAverageWindow) {
        averageFrame_ = frames == 0 ? 0 : window / frames;
        windowTime_ = 0;
        windowFrames_ = 0;
    }
    ClearFrame();
    CTextureDevice* device = device_;
    device->vertexCount_ = 0;
    device->primitiveCount_ = 0;
    device->drawCount_ = 0;
    device->textureChanges_ = 0;
    long result = DeviceBeginScene(device);
    if (result == D3DERR_SCENE_IN_SCENE) {
        static_cast<CTextureDevice*>(device)->FlushBatch();
        static_cast<CTextureDevice*>(device)->FlushBatch2();
        result = DeviceEndScene(device);
        if (result != D3DERR_SCENE_NOT_IN_SCENE && result != 0) app::FatalErrorCode(result, "Can't end scene.\n");
        DeviceBeginScene(device);
        return 1;
    }
    if (result != 0) app::FatalErrorCode(result, "Can't begin scene.\n");
    return 1;
}
EI_REPLACE_METHOD(0x00503F40, CSceneRenderer::BeginScene);

int CSceneRenderer::EndScene() {
    CTextureDevice* device = device_;
    static_cast<CTextureDevice*>(device)->FlushBatch();
    static_cast<CTextureDevice*>(device)->FlushBatch2();
    long result = DeviceEndScene(device);
    if (result != D3DERR_SCENE_NOT_IN_SCENE && result != 0) app::FatalErrorCode(result, "Can't end scene.\n");
    mfc::CString text;
    void* panel = DebugInfoPanel();
    text.Format("%g", 1000.0 / x87::FromUnsigned(frameTime_));
    DebugPanelSetOriginal(panel, nullptr, "FPS", text);
    text.Format("%d", device_->vertexCount_);
    DebugPanelSetOriginal(panel, nullptr, "num verts", text);
    text.Format("%d", device_->primitiveCount_);
    DebugPanelSetOriginal(panel, nullptr, "num trias", text);
    lastFrameStart_ = frameStart_;
    return 1;
}
EI_REPLACE_METHOD(0x005040A0, CSceneRenderer::EndScene);

int CSceneRenderer::DrawTexture(CTexture* texture, float x, float y, float scale) {
    if (!texture) return 0;
    // The width's product is stored to a float before x is added; the height's is not.
    float scaledWidth = static_cast<float>(x87::FromUnsigned(texture->width_) * scale);
    float bottom = static_cast<float>(x87::FromUnsigned(texture->height_) * scale + y);
    float right = scaledWidth + x;
    TLVertex strip[4] = {
        {x, y, kTextureDepth, 1.0f, kWhite, kNoSpecular, 0.0f, 0.0f},
        {x, bottom, kTextureDepth, 1.0f, kWhite, kNoSpecular, 0.0f, 1.0f},
        {right, y, kTextureDepth, 1.0f, kWhite, kNoSpecular, 1.0f, 0.0f},
        {right, bottom, kTextureDepth, 1.0f, kWhite, kNoSpecular, 1.0f, 1.0f},
    };
    SetTexture(0, texture);
    CTextureDevice* device = device_;
    Locked(device, [&](IDirect3DDevice7* d3d) {
        return device::DrawPrimitive(d3d, D3DPT_TRIANGLESTRIP, kTlVertexFormat, strip, 4, 0);
    });
    device->vertexCount_ += 4;
    device->primitiveCount_ += 1;
    device->drawCount_ += 1;
    return 1;
}
EI_REPLACE_METHOD(0x00504860, CSceneRenderer::DrawTexture);
