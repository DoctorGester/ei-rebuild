// The device object (CTextureDevice, texture.h): its constructor and destructor. There is no DirectDraw or Direct3D
// object: the constructor starts the new renderer (src/render), which draws and presents. See docs/notes/renderer.md (section 9).
#include "texture.h"

#include "device_material.h"
#include "model.h"               // CTransformPipeline
#include "settings_file.h"

#include "render/render.h"

#include <float.h>

#include <cstdint>

namespace {

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

// The render quality (docs/notes/renderer.md section 11, "Render quality"): "Msaa" (the world view's samples: 1 off,
// 2, 4 or 8) and "Anisotropy" (its models' anisotropic sampling: 1 off .. 16), DWORDs of the same key. A value missing
// (or not a DWORD) is written with its default, the look the user's dgVoodoo setup gave, so the file shows both. Read
// only in the product, as above (render::QualitySettingsSupported).
constexpr unsigned long kDefaultMsaa = 8;
constexpr unsigned long kDefaultAnisotropy = 16;

unsigned long QualitySetting(const char* name, unsigned long fallback) {
    unsigned long type = 0, value = 0, size = sizeof(value);
    if (settings_file::ReadValue("Software\\Nival Interactive\\EvilIslands\\Standalone", name, &type, &value, &size) == 0 &&
        type == 4 /* REG_DWORD */)
        return value;
    settings_file::Set(nullptr, "Software\\Nival Interactive\\EvilIslands\\Standalone", name, 4 /* REG_DWORD */, &fallback,
                       sizeof(fallback));
    return fallback;
}

} // namespace

CTextureDevice::CTextureDevice(HWND newWindow, bool newFullScreen, int width, int height, int newBitsPerPixel)
    : fullScreen(newFullScreen), screen{0, 0, width, height}, window(newWindow), bitsPerPixel(newBitsPerPixel) {
    // (The original required MMX and loaded 2DintMMX.dll, its pixel converter, here; nothing needs it now.)
    // The new renderer presents; when it cannot start, the game ends there (render::detail::Fatal).
    render::StartPresenting();
    render::SetPerfOverlay(PerfOverlaySetting());
    if (render::VsyncSwitchSupported()) render::SetVsync(VsyncSetting());
    if (render::QualitySettingsSupported()) {
        render::SetMsaa(static_cast<int>(QualitySetting("Msaa", kDefaultMsaa)));
        render::SetAnisotropy(static_cast<int>(QualitySetting("Anisotropy", kDefaultAnisotropy)));
        settings_file::Flush();
    }

    // The game's Direct3D 7 device set the calling thread's x87 precision to 24 bits (the game asked for
    // DDSCL_FPUSETUP), and the game's arithmetic runs with it from then on.
    unsigned int x87 = 0;
    __control87_2(_PC_24, _MCW_PC, &x87, nullptr);
}

CTextureDevice::~CTextureDevice() {
    if (pipeline) pipeline->Release();
    if (material) material->Release();
}
