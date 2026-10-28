// The renderer's scene bracket: BeginScene with the frame statistics, EndScene with the debug panel's FPS and
// counts. The main loop brackets each frame's drawing with them.
// The old Direct3D 7 path, kept as it is (docs/notes/renderer.md).
#include "device.h"   // the old device's calls
#include "model.h"     // CSceneRenderer
#include "texture.h"   // CTextureDevice, CTexture

#include "app.h"       // app::FatalErrorCode
#include "core_maths.h"
#include "ui.h"        // GameDeviceLock, strmap::g_debugPanel
#include "game_singletons.h"

#include "ei/x87.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <string>
#include "lib/str.h"

#include <d3d.h>


namespace {

constexpr std::uint32_t kLongestCounted = 1000;   // longer frames are not the longest
constexpr std::uint32_t kAverageWindow = 999;     // the average is taken past this many ms

IDirect3DDevice7* D3DDevice(CTextureDevice* device) { return device->d3dDevice_; }

// The device calls run under the game's device lock; the result goes to the device's +8.
template <typename Call>
long Locked(CTextureDevice* device, Call call) {
    gameapi::EnterCriticalSection(&g_deviceLock);
    device->lastLoadResult_ = call(D3DDevice(device));
    gameapi::LeaveCriticalSection(&g_deviceLock);
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
        result = DeviceEndScene(device);
        if (result != D3DERR_SCENE_NOT_IN_SCENE && result != 0) app::FatalErrorCode(result, "Can't end scene.\n");
        DeviceBeginScene(device);
        return 1;
    }
    if (result != 0) app::FatalErrorCode(result, "Can't begin scene.\n");
    return 1;
}

int CSceneRenderer::EndScene() {
    CTextureDevice* device = device_;
    long result = DeviceEndScene(device);
    if (result != D3DERR_SCENE_NOT_IN_SCENE && result != 0) app::FatalErrorCode(result, "Can't end scene.\n");
    std::string text;
    strmap::CDebugPanel* panel = &strmap::g_debugPanel.Get();
    text = str::Format("%g", 1000.0 / x87::FromUnsigned(frameTime_));
    panel->Set("FPS", text);
    text = str::Format("%d", device_->vertexCount_);
    panel->Set("num verts", text);
    text = str::Format("%d", device_->primitiveCount_);
    panel->Set("num trias", text);
    lastFrameStart_ = frameStart_;
    return 1;
}
