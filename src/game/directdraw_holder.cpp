// The device object's DirectDraw base class (CDXDirectDraw, texture.h). The image builds it without DirectDraw:
// IDirectDraw7 is called through device.h and the switch-over device answers. A failed call throws the game's
// error object, which ends the game in WinMain. See docs/notes/renderer.md (sections 9, 12).
#include "app.h"
#include "device.h"
#include "game_strings.h"

#include "dx_errors.h"
#include "settings_file.h"
#include "sys_info.h"
#include "texture.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "game_data.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>

namespace {


constexpr std::uint32_t kEflagsId = 0x00200000;    // EFLAGS.ID: can be flipped when CPUID exists
constexpr std::uint32_t kFeatureMmx = 1u << 23;     // CPUID.1:EDX bit 23


// Whether the processor has CPUID: the ID flag flips. It stays flipped, as in the game.
bool HasCpuid() {
    const std::uint32_t before = static_cast<std::uint32_t>(__readeflags());
    __writeeflags(before ^ kEflagsId);
    return (static_cast<std::uint32_t>(__readeflags()) ^ before) != 0;
}

// IDirectDraw7::EnumDisplayModes's callback: the mode appended to the list `context`.
HRESULT WINAPI DisplayModeCallback(DDSURFACEDESC2* mode, void* context) {
    static_cast<stlp::list<DDSURFACEDESC2>*>(context)->PushBack(*mode);
    return DDENUMRET_OK;
}

// The switch-over's holder of the screen (the primary or the render target; device.h): no surface,
// the screen's size and colour depth, counted like a holder of a surface.
CDXSurface* NewScreenHolder(CTextureDevice* device, int width, int height, int bitsPerPixel) {
    auto* holder = new CDXSurface(device);
    if (!holder) return nullptr;
    holder->width = width;
    holder->height = height;
    holder->bitsPerPixel = bitsPerPixel;
    ++device::HolderCount();
    return holder;
}

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

// ---- The DirectDraw base ------------------------------------------------------------------------

void CDXDirectDraw::RestoreDisplayMode() {
    if (!(flags_ & DDSCL_FULLSCREEN)) return;
    lastLoadResult_ = device::RestoreDisplayMode(ddraw_);
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, "Can't restore display mode.\n");
}
EI_REWRITTEN(0x004EEC60, CDXDirectDraw::RestoreDisplayMode);

void CDXDirectDraw::SetCooperativeLevel(int fullScreen, const RECT* screen) {
    if (!window_) ThrowGameError("CDXDirectDraw::SetCooperativeLevel.\n No valid window handle.\n");
    flags_ = fullScreen ? (DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE) : DDSCL_NORMAL;
    lastLoadResult_ = device::SetCooperativeLevel(ddraw_, window_, flags_ | DDSCL_FPUSETUP | DDSCL_MULTITHREADED);
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, "pDD->SetCooperativeLevel: DDSCL_FPUSETUP | dwCooperativeLevelFlags.\n");
    if (screen) screen_ = *screen;
    else gameapi::SetRectEmpty(&screen_);
}
EI_REWRITTEN(0x004EECF0, CDXDirectDraw::SetCooperativeLevel);

// (The display mode's restore cannot fail in the switch-over, device_null.cpp; its throw would end the game here
// through std::terminate, the destructor being noexcept, where the game's handler in WinMain ends it.)
CDXDirectDraw::~CDXDirectDraw() {
    if (flags_ & DDSCL_FULLSCREEN) {
        lastLoadResult_ = device::RestoreDisplayMode(ddraw_);
        if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, "Can't restore display mode.\n");
    }
    if (converterLibrary_) gameapi::FreeLibrary(converterLibrary_);
    converterLibrary_ = nullptr;
    converter_ = nullptr;
    if (clipper_) static_cast<IDirectDrawClipper*>(clipper_)->Release();
    device::AssignHolder(renderTarget_, nullptr);
    device::AssignHolder(primary_, nullptr);
    if (ddraw_) device::Release(ddraw_);
    modes_.Destroy();
}
EI_REWRITTEN_SPECIAL(0x004EE330, CDXDirectDraw::~CDXDirectDraw);
EI_REWRITTEN_SPECIAL(0x004EE310, CDXDirectDraw::~CDXDirectDraw);

// Not kept: the game copies the processor's twelve bytes as a string into a local nothing reads.
CDXDirectDraw::CDXDirectDraw(HWND window, GUID* driver, int fullScreen, int width, int height, int bitsPerPixel,
                             int /*refreshRate*/, unsigned /*backBuffers*/, int /*for3D*/)
    : driver_(nullptr), primary_(nullptr), renderTarget_(nullptr), clipper_(nullptr), flags_(0), window_(window) {
    modes_.Construct();
    const RECT screen = {0, 0, width, height};

    ProcessorInfo& processor = *TheProcessorInfo();
    processor.terminator = 0;
    if (HasCpuid()) {
        int registers[4];
        __cpuid(registers, 1);
        processor.features = static_cast<std::uint32_t>(registers[3]);
        processor.signature = static_cast<std::uint32_t>(registers[0]);
        processor.brand = static_cast<std::uint32_t>(registers[1]);
        processor.features2 = static_cast<std::uint32_t>(registers[2]);
    }
    if (!(processor.features & kFeatureMmx)) {
        game::FatalError("MMX required in this version !!!.\n");
        libcrt::Exit(-1);
    }

    converterLibrary_ = gameapi::LoadLibraryA("2DintMMX.dll");
    game::DebugLog("MMX graphics release library loaded.\n");
    if (!converterLibrary_) ThrowGameError("Can't load 2D graphics library.\n");
    using GetGraphicsProcessor = CPixelConverter*(__cdecl*)();
    const auto getProcessor = reinterpret_cast<GetGraphicsProcessor>(
        gameapi::GetProcAddress(converterLibrary_, "GetGraphicsProcessor"));
    if (!getProcessor) ThrowGameError("Loaded 2D graphics library have no function \"GetGraphicsProcessor\".\n");
    game::DebugLog("Acquiring graphics processor from graphics library.\n");
    converter_ = getProcessor();

    // The switch-over: the new renderer presents; no DirectDraw object, display mode or surfaces. Without it the
    // game cannot run: the error ends it in WinMain.
    if (!render::StartPresenting()) ThrowGameError("Can't start the renderer (no Direct3D 11 device).\n");
    render::SetPerfOverlay(PerfOverlaySetting());
    if (render::VsyncSwitchSupported()) render::SetVsync(VsyncSetting());
    ddraw_ = device::StartWithoutDirectDraw(width, height, bitsPerPixel);
    driver_ = driver;
    modes_.ClearWith([](DDSURFACEDESC2*) {});
    device::EnumDisplayModes(ddraw_, 0, nullptr, &modes_, &DisplayModeCallback);
    SetCooperativeLevel(fullScreen, &screen);
    // (The holders keep the device object: only a CTextureDevice is ever built on this base.)
    auto* deviceObject = static_cast<CTextureDevice*>(this);
    device::AssignHolder(primary_, NewScreenHolder(deviceObject, width, height, bitsPerPixel));
    device::AssignHolder(renderTarget_, NewScreenHolder(deviceObject, width, height, bitsPerPixel));
    overlayFourCC_ = 0;
    videoFourCC_ = 0;
    overlayStretch_ = false;
}
EI_REWRITTEN_SPECIAL(0x004EDD70, CDXDirectDraw::CDXDirectDraw);
