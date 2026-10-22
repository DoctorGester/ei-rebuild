// The device object's DirectDraw base class (CTextureDevice, texture.h; vtable 0073D770, the
// members up to +0x50). A-S13: IDirectDraw7 is called as the original calls it, through device.h
// (the switch-over device answers: device_null.cpp). See docs/notes/renderer.md (sections 9, 12).
//
// A failed call throws the game's error object (ThrowGameError, dx_errors.h); nothing of ours is
// unwound on the way (the original's handlers free what the constructor had built), and every such
// throw ends the game in WinMain.
//
// The image builds the base without DirectDraw (004EDD70; R11a removed the fallback). The driver
// list (004EE4A0 / 004ED610 / 004EE5D0), the DirectDraw object (004EEA90), the display mode change
// (004EEBC0) and the primary and back surfaces (004EEE30) had no caller of ours left and went in
// R11b (config/dead_code.csv).
#include "app.h"
#include "device.h"
#include "game_strings.h"

#include "dx_errors.h"
#include "settings_file.h"
#include "sys_info.h"   // ProcessorInfo
#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
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


// Whether the processor has CPUID: the ID flag flips. As in the original it stays flipped.
bool HasCpuid() {
    const std::uint32_t before = static_cast<std::uint32_t>(__readeflags());
    __writeeflags(before ^ kEflagsId);
    return (static_cast<std::uint32_t>(__readeflags()) ^ before) != 0;
}

// IDirectDraw7::EnumDisplayModes's callback (the original's is 004ED7F0, RET 8): the mode appended
// to the list `context`.
HRESULT WINAPI DisplayModeCallback(DDSURFACEDESC2* mode, void* context) {
    static_cast<stlp::list<DDSURFACEDESC2>*>(context)->PushBack(*mode);
    return DDENUMRET_OK;
}

// The switch-over's holder of the screen (the primary or the render target; device.h): no surface,
// the screen's size and colour depth, counted like a holder of a surface.
CDXSurface* NewScreenHolder(CTextureDevice* device, int width, int height, int bitsPerPixel) {
    auto* holder = gamecrt::operator_new_as<CDXSurface>();
    if (!holder) return nullptr;
    holder->refCount = 0;
    holder->result = 0;
    holder->device = device;
    holder->surface = nullptr;
    holder->width = width;
    holder->height = height;
    holder->bitsPerPixel = bitsPerPixel;
    ei::SetVtable(holder, DXSurfaceClass::Vtable());
    ++device::HolderCount();
    return holder;
}

// The performance overlay's switch (R21; docs/notes/renderer.md section 15): "PerfOverlay" (a DWORD, not 0: on) of
// Software\Nival Interactive\EvilIslands\Standalone in ei_settings.ini; off when absent. Read only where the
// overlay exists: the harness build has none, and its pinned settings file lacks the value.
bool PerfOverlaySetting() {
    if (!render::PerfOverlaySupported()) return false;
    unsigned long type = 0, value = 0, size = sizeof(value);
    return settings_file::ReadValue("Software\\Nival Interactive\\EvilIslands\\Standalone", "PerfOverlay", &type, &value,
                                    &size) == 0 &&
           type == 4 /* REG_DWORD */ && value != 0;
}

// The vertical blank's switch (R23; docs/notes/renderer.md section 15): "VSync" (a DWORD, 0: off, anything else or
// absent: on) of the same key. Read only in the product, as above.
bool VsyncSetting() {
    unsigned long type = 0, value = 0, size = sizeof(value);
    return !(settings_file::ReadValue("Software\\Nival Interactive\\EvilIslands\\Standalone", "VSync", &type, &value, &size) == 0 &&
             type == 4 /* REG_DWORD */ && value == 0);
}

} // namespace

// ---- The DirectDraw base ------------------------------------------------------------------------

void CTextureDevice::RestoreDisplayMode() {
    if (!(flags_ & DDSCL_FULLSCREEN)) return;
    lastLoadResult_ = device::RestoreDisplayMode(device::DDraw(this));
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, "Can't restore display mode.\n");   // "Can't restore display mode.\n"
}
EI_REWRITTEN(0x004EEC60, CTextureDevice::RestoreDisplayMode);

void CTextureDevice::SetCooperativeLevel(int fullScreen, const RECT* screen) {
    // "CDXDirectDraw::SetCooperativeLevel.\n No valid window handle.\n"
    if (!window_) ThrowGameError("CDXDirectDraw::SetCooperativeLevel.\n No valid window handle.\n");
    flags_ = fullScreen ? (DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE) : DDSCL_NORMAL;
    lastLoadResult_ = device::SetCooperativeLevel(device::DDraw(this), window_, flags_ | DDSCL_FPUSETUP | DDSCL_MULTITHREADED);
    // "pDD->SetCooperativeLevel: DDSCL_FPUSETUP | dwCooperativeLevelFlags.\n"
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, "pDD->SetCooperativeLevel: DDSCL_FPUSETUP | dwCooperativeLevelFlags.\n");
    if (screen) screen_ = *screen;
    else gameapi::SetRectEmpty(&screen_);
}
EI_REWRITTEN(0x004EECF0, CTextureDevice::SetCooperativeLevel);

void CTextureDevice::DestructDirectDraw() {
    ei::SetVtable(this, DirectDrawClass::Vtable());
    if (flags_ & DDSCL_FULLSCREEN) {
        lastLoadResult_ = device::RestoreDisplayMode(device::DDraw(this));
        if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, "Can't restore display mode.\n");   // "Can't restore display mode.\n"
    }
    if (converterLibrary_) gameapi::FreeLibrary(converterLibrary_);
    converterLibrary_ = nullptr;
    converter_ = nullptr;
    if (clipper_) static_cast<IDirectDrawClipper*>(clipper_)->Release();
    device::AssignHolder(renderTarget_, nullptr);
    device::AssignHolder(primary_, nullptr);
    if (ddraw_) device::Release(device::DDraw(this));
    modes_.Destroy();
    ei::SetVtable(this, RendererBaseClass::Vtable());
}
EI_REWRITTEN(0x004EE330, CTextureDevice::DestructDirectDraw);

// Not kept: the original copies the processor's twelve bytes as a string into a local nothing reads.
CTextureDevice* CTextureDevice::ConstructDirectDraw(HWND window, GUID* driver, int fullScreen, int width, int height,
                                                    int bitsPerPixel, int /*refreshRate*/, unsigned /*backBuffers*/,
                                                    int /*for3D*/) {
    refs_ = 0;
    lastLoadResult_ = 0;
    ei::SetVtable(this, RendererBaseClass::Vtable());
    driver_ = nullptr;
    primary_ = nullptr;
    renderTarget_ = nullptr;
    clipper_ = nullptr;
    flags_ = 0;
    window_ = window;
    modes_.Construct();
    ei::SetVtable(this, DirectDrawClass::Vtable());
    const RECT screen = {0, 0, width, height};

    ProcessorInfo& processor = *TheProcessorInfo();   // renderer_base_create.cpp fills it the same way
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
        game::FatalError("MMX required in this version !!!.\n");   // "MMX required in this version !!!.\n"
        libcrt::Exit(-1);
    }

    converterLibrary_ = gameapi::LoadLibraryA("2DintMMX.dll");
    game::DebugLog("MMX graphics release library loaded.\n");                        // "MMX graphics release library loaded.\n"
    if (!converterLibrary_) ThrowGameError("Can't load 2D graphics library.\n");      // "Can't load 2D graphics library.\n"
    using GetGraphicsProcessor = void*(__cdecl*)();
    const auto getProcessor = reinterpret_cast<GetGraphicsProcessor>(
        gameapi::GetProcAddress(converterLibrary_, "GetGraphicsProcessor"));
    // "Loaded 2D graphics library have no function \"GetGraphicsProcessor\".\n"
    if (!getProcessor) ThrowGameError("Loaded 2D graphics library have no function \"GetGraphicsProcessor\".\n");
    game::DebugLog("Acquiring graphics processor from graphics library.\n");   // "Acquiring graphics processor from graphics library.\n"
    converter_ = getProcessor();

    // The switch-over (device.h): the new renderer presents; no DirectDraw object, display mode or
    // surfaces. The switch-over takes the cooperative level (the flags and the screen rectangle are
    // kept) and lists the one display mode; the primary and the render target are holders without
    // a surface; no video formats. Without the new renderer the game cannot run (R11a: no DirectDraw
    // fallback): the error ends it in WinMain.
    if (!render::StartPresenting()) ThrowGameError("Can't start the renderer (no Direct3D 11 device).\n");
    render::SetPerfOverlay(PerfOverlaySetting());
    if (render::VsyncSwitchSupported()) render::SetVsync(VsyncSetting());
    ddraw_ = device::StartWithoutDirectDraw(width, height, bitsPerPixel);
    driver_ = driver;
    modes_.ClearWith([](DDSURFACEDESC2*) {});
    device::EnumDisplayModes(device::DDraw(this), 0, nullptr, &modes_, &DisplayModeCallback);
    SetCooperativeLevel(fullScreen, &screen);
    device::AssignHolder(primary_, NewScreenHolder(this, width, height, bitsPerPixel));
    device::AssignHolder(renderTarget_, NewScreenHolder(this, width, height, bitsPerPixel));
    overlayFourCC_ = 0;
    videoFourCC_ = 0;
    overlayStretch_ = false;
    return this;
}
EI_REWRITTEN(0x004EDD70, CTextureDevice::ConstructDirectDraw);
