// The device object (CTextureDevice, texture.h): its constructor and destructor. There is no DirectDraw or Direct3D
// object: the constructor loads 2DintMMX.dll and starts the new renderer (src/render), which draws and presents. See docs/notes/renderer.md (section 9).
#include "texture.h"

#include "app.h"
#include "device_material.h"
#include "dx_errors.h"
#include "game_strings.h"
#include "model.h"               // CTransformPipeline
#include "settings_file.h"
#include "sys_info.h"

#include "lib/gameapi.h"
#include "render/render.h"

#include <float.h>

#include <cstdint>
#include <cstdlib>

namespace {

constexpr std::uint32_t kFeatureMmx = 1u << 23;     // CPUID.1:EDX bit 23

// The performance overlay's switch (docs/notes/renderer.md section 15): "PerfOverlay" (a DWORD, not 0: on) of
// Software\Nival Interactive\EvilIslands\Standalone in ei_settings.ini; off when absent or without the overlay.
bool PerfOverlaySetting() {
    if (!render::PerfOverlaySupported()) return false;
    unsigned long type = 0, value = 0, size = sizeof(value);
    return settings_file::ReadValue("Software\\Nival Interactive\\EvilIslands\\Standalone", "PerfOverlay", &type, &value,
                                    &size) == 0 &&
           type == 4 /* REG_DWORD */ && value != 0;
}

// The vertical blank's switch (docs/notes/renderer.md section 15): "VSync" (a DWORD, 0: off, anything else or
// absent: on) of the same key. Read only in the product, as above.
bool VsyncSetting() {
    unsigned long type = 0, value = 0, size = sizeof(value);
    return !(settings_file::ReadValue("Software\\Nival Interactive\\EvilIslands\\Standalone", "VSync", &type, &value, &size) == 0 &&
             type == 4 /* REG_DWORD */ && value == 0);
}

} // namespace

CTextureDevice::CTextureDevice(HWND window, bool fullScreen, int width, int height, int bitsPerPixel)
    : fullScreen_(fullScreen), screen_{0, 0, width, height}, window_(window), bitsPerPixel_(bitsPerPixel) {
    ReadProcessorInfo();
    if (!(g_processorInfo.features & kFeatureMmx)) {
        game::FatalError("MMX required in this version !!!.\n");
        std::exit(-1);
    }

    converterLibrary_ = gameapi::LoadLibraryA("2DintMMX.dll");
    game::DebugLog("MMX graphics release library loaded.\n");
    if (!converterLibrary_) ThrowGameError("Can't load 2D graphics library.\n");
    using GetGraphicsProcessor = CPixelConverter*(*)();
    const auto getProcessor = reinterpret_cast<GetGraphicsProcessor>(
        gameapi::GetProcAddress(converterLibrary_, "GetGraphicsProcessor"));
    if (!getProcessor) ThrowGameError("Loaded 2D graphics library have no function \"GetGraphicsProcessor\".\n");
    game::DebugLog("Acquiring graphics processor from graphics library.\n");
    converter_ = getProcessor();

    // The new renderer presents; when it cannot start, the game ends there (render::detail::Fatal).
    render::StartPresenting();
    render::SetPerfOverlay(PerfOverlaySetting());
    if (render::VsyncSwitchSupported()) render::SetVsync(VsyncSetting());

    // The game's Direct3D 7 device set the calling thread's x87 precision to 24 bits (the game asked for
    // DDSCL_FPUSETUP), and the game's arithmetic runs with it from then on.
    unsigned int x87 = 0;
    __control87_2(_PC_24, _MCW_PC, &x87, nullptr);
}

CTextureDevice::~CTextureDevice() {
    if (pipeline_) pipeline_->Release();
    if (material_) material_->Release();
    if (converterLibrary_) gameapi::FreeLibrary(converterLibrary_);
}
