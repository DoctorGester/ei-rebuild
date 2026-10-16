// The device object's DirectDraw base class (CTextureDevice, texture.h; vtable 0073D770, the
// members up to +0x50) and the DirectDraw driver list (007AEE64). A-S13: DirectDraw 7 is called as
// the original calls it (DirectDrawEnumerateExA, DirectDrawCreateEx, IDirectDraw7 and
// IDirectDrawSurface7 methods); the COM interfaces are the boundary. See docs/notes/renderer.md
// (section 12).
//
// A failed call throws the game's error object (ThrowGameError, dx_errors.h); nothing of ours is
// unwound on the way (the original's handlers free what the constructor had built), and every such
// throw ends the game in WinMain.
#include "app.h"
#include "device.h"
#include "dx_surface.h"
#include "game_strings.h"

#include "dx_errors.h"
#include "originals_engine.h"    // game::FatalError
#include "originals_library.h"   // ExitGame
#include "originals_render.h"
#include "sys_info.h"   // ProcessorInfo
#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>

namespace {


constexpr std::uint32_t kEflagsId = 0x00200000;    // EFLAGS.ID: can be flipped when CPUID exists
constexpr std::uint32_t kFeatureMmx = 0x00800000;  // CPUID.1:EDX bit 23

// What CPUID function 1 reported (007CAED0, ProcessorInfo in sys_info.h; renderer_base_create.cpp
// fills it the same way).
ProcessorInfo& Processor() { return *reinterpret_cast<ProcessorInfo*>(0x007CAED0); }

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

// A holder made from a surface description (004EFA60), on the game's heap.
CDXSurface* NewHolder(CTextureDevice* device, DDSURFACEDESC2* desc) {
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    if (!memory) return nullptr;
    return static_cast<CDXSurface*>(
        static_cast<CDXSurface*>(memory)->ConstructFromDesc(device, reinterpret_cast<const std::uint32_t*>(desc)));
}

// A holder of a surface that exists already (the constructor the original inlines): one more
// reference to the surface, its size read, the holder counted. No surface: reported, and the
// holder is left empty and not counted.
CDXSurface* NewHolderOf(CTextureDevice* device, IDirectDrawSurface7* surface) {
    auto* holder = static_cast<CDXSurface*>(gamecrt::operator_new(sizeof(CDXSurface)));
    if (!holder) return nullptr;
    holder->refCount = 0;
    holder->result = 0;
    holder->device = device;
    holder->surface = surface;
    holder->width = 0;
    holder->height = 0;
    holder->bitsPerPixel = 0;
    ei::SetVtable(holder, 0x0073D774);
    if (surface) {
        surface->AddRef();
        DXSurfaceDescribe(holder);
        ++device::HolderCount();
    } else {
        game::FatalError(game::Text(0x0078E73C));   // "Creating DXDDSurface from empty DDSurface!\n"
    }
    return holder;
}

// The switch-over's holder of the screen (the primary or the render target; device.h): no surface,
// the screen's size and colour depth, counted like a holder of a surface.
CDXSurface* NewScreenHolder(CTextureDevice* device, int width, int height, int bitsPerPixel) {
    auto* holder = static_cast<CDXSurface*>(gamecrt::operator_new(sizeof(CDXSurface)));
    if (!holder) return nullptr;
    holder->refCount = 0;
    holder->result = 0;
    holder->device = device;
    holder->surface = nullptr;
    holder->width = width;
    holder->height = height;
    holder->bitsPerPixel = bitsPerPixel;
    ei::SetVtable(holder, 0x0073D774);
    ++device::HolderCount();
    return holder;
}

// The YUV formats tried for video, in this order.
constexpr std::uint32_t kVideoFourCCs[6] = {
    MAKEFOURCC('Y', 'U', 'Y', '2'), MAKEFOURCC('U', 'Y', 'V', 'Y'), MAKEFOURCC('U', 'Y', 'N', 'V'),
    MAKEFOURCC('Y', 'U', 'N', 'V'), MAKEFOURCC('Y', 'V', '1', '2'), MAKEFOURCC('Y', 'V', 'U', '9')};

// The first of the YUV formats the DirectDraw object makes a 100 x 100 video-memory surface of with
// the capabilities `caps` (made under the device lock and released at once; each result in the
// device's +8). 0 when it makes none.
std::uint32_t FirstVideoFormat(CTextureDevice* device, std::uint32_t caps) {
    for (const std::uint32_t fourCC : kVideoFourCCs) {
        IDirectDrawSurface7* surface = nullptr;
        DDSURFACEDESC2 desc = {};
        desc.dwSize = sizeof(desc);
        desc.ddsCaps.dwCaps = caps;
        desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
        desc.dwWidth = 100;
        desc.dwHeight = 100;
        desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        desc.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
        desc.ddpfPixelFormat.dwFourCC = fourCC;
        gameapi::EnterCriticalSection(device::Lock());
        device->lastLoadResult_ = device::CreateSurface(device::DDraw(device), &desc, &surface);
        gameapi::LeaveCriticalSection(device::Lock());
        if (surface) surface->Release();
        if (device->lastLoadResult_ == 0) return desc.ddpfPixelFormat.dwFourCC;
    }
    return 0;
}

} // namespace

// ---- The driver list --------------------------------------------------------------------------

// The original appends a copy of a record with two empty strings (its other fields not set; ours
// zeroes them) and fills the copy.
BOOL __stdcall DriverEnumCallback(GUID* guid, char* description, char* name, void* context, HMONITOR monitor) {
    auto& drivers = *static_cast<stlp::list<DirectDrawDriver>*>(context);
    DirectDrawDriver empty{};
    stlp::string::Reserve(empty.name, 8);
    stlp::string::Reserve(empty.description, 8);
    drivers.PushBack(empty);
    DirectDrawDriver& driver = drivers.End()->prev->data;
    driver.name.Assign(name, name + std::strlen(name));
    driver.description.Assign(description, description + std::strlen(description));
    if (guid) {
        driver.guidValue = *guid;
        driver.guid = &driver.guidValue;
    } else {
        driver.guid = nullptr;
    }
    driver.monitor = monitor;
    return 1;
}
EI_REPLACE(0x004ED610, DriverEnumCallback);

int __cdecl EnumerateDirectDrawDrivers() {
    stlp::list<DirectDrawDriver>& drivers = DirectDrawDrivers();
    drivers.ClearWith([](DirectDrawDriver* driver) { driver->~DirectDrawDriver(); });
    const HRESULT result = device::DirectDrawEnumerateExA(&DriverEnumCallback, &drivers,
                                                          DDENUM_ATTACHEDSECONDARYDEVICES | DDENUM_NONDISPLAYDEVICES);
    if (result != 0) return game::FatalError(game::Text(0x0078E49C), result);   // "Can't enumerate direct draw drivers.\n"
    return 1;
}
EI_REPLACE(0x004EE4A0, EnumerateDirectDrawDrivers);

// Original detail kept: a driver whose DirectDraw object cannot be made is reported, and when the
// report says go on its capabilities are asked through the null pointer.
DirectDrawDriver* __cdecl FirstHardwareDirectDrawDriver() {
    using Node = stlp::list<DirectDrawDriver>::Node;
    stlp::list<DirectDrawDriver>& drivers = DirectDrawDrivers();
    Node* found = drivers.End();
    for (Node* node = drivers.First(); node != drivers.End(); node = node->next) {
        IDirectDraw7* ddraw = nullptr;
        HRESULT result = device::DirectDrawCreateEx(node->data.guid, reinterpret_cast<void**>(&ddraw),
                                                    device::IidDirectDraw7(), nullptr);
        // "Can't create IDirectDraw# object for driver \"%s\".\n"
        if (result != 0) app::FatalErrorCode(result, game::Text(0x0078E500), node->data.description.data());
        DDCAPS caps = {};
        caps.dwSize = sizeof(caps);
        result = ddraw->GetCaps(&caps, nullptr);
        // "Can't get caps from IDirectDraw# object for driver \"%s\".\n"
        if (result != 0) app::FatalErrorCode(result, game::Text(0x0078E4C4), node->data.description.data());
        if (ddraw) ddraw->Release();
        if ((caps.dwCaps & DDCAPS_3D) && (caps.ddsCaps.dwCaps & DDSCAPS_TEXTURE)) {
            if (caps.dwCaps2 & DDCAPS2_NONLOCALVIDMEM) return &node->data;
            found = node;
        }
    }
    if (found == drivers.End()) {
        game::FatalError(game::Text(0x0078E534));   // "Can't find any driver with HW 3D.\n"
        libcrt::Exit(-1);
    }
    return &found->data;
}
EI_REPLACE(0x004EE5D0, FirstHardwareDirectDrawDriver);

// ---- The DirectDraw base ------------------------------------------------------------------------

void CTextureDevice::CreateDirectDraw(GUID* driver) {
    lastLoadResult_ = device::DirectDrawCreateEx(driver, &ddraw_, device::IidDirectDraw7(), nullptr);
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, game::Text(0x0078E5CC));   // "Can't create IDirectDraw# object.\n"
    driver_ = driver;
    modes_.ClearWith([](DDSURFACEDESC2*) {});
    lastLoadResult_ = device::EnumDisplayModes(device::DDraw(this), 0, nullptr, &modes_, &DisplayModeCallback);
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, game::Text(0x0078E5AC));   // "Can't enumerate display modes.\n"
}
EI_REPLACE_METHOD(0x004EEA90, CTextureDevice::CreateDirectDraw);

void CTextureDevice::SetDisplayMode(int width, int height, int bitsPerPixel, int refreshRate) {
    lastLoadResult_ = device::SetDisplayMode(device::DDraw(this), 
        static_cast<DWORD>(width), static_cast<DWORD>(height), static_cast<DWORD>(bitsPerPixel),
        static_cast<DWORD>(refreshRate), 0);
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, game::Text(0x0078E3D8));   // "Can't set display mode.\n"
}
EI_REPLACE_METHOD(0x004EEBC0, CTextureDevice::SetDisplayMode);

void CTextureDevice::RestoreDisplayMode() {
    if (!(flags_ & DDSCL_FULLSCREEN)) return;
    lastLoadResult_ = device::RestoreDisplayMode(device::DDraw(this));
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, game::Text(0x0078E47C));   // "Can't restore display mode.\n"
}
EI_REPLACE_METHOD(0x004EEC60, CTextureDevice::RestoreDisplayMode);

void CTextureDevice::SetCooperativeLevel(int fullScreen, const RECT* screen) {
    // "CDXDirectDraw::SetCooperativeLevel.\n No valid window handle.\n"
    if (!window_) ThrowGameError(game::Text(0x0078E638));
    flags_ = fullScreen ? (DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE) : DDSCL_NORMAL;
    lastLoadResult_ = device::SetCooperativeLevel(device::DDraw(this), window_, flags_ | DDSCL_FPUSETUP | DDSCL_MULTITHREADED);
    // "pDD->SetCooperativeLevel: DDSCL_FPUSETUP | dwCooperativeLevelFlags.\n"
    if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, game::Text(0x0078E5F0));
    if (screen) screen_ = *screen;
    else gameapi::SetRectEmpty(&screen_);
}
EI_REPLACE_METHOD(0x004EECF0, CTextureDevice::SetCooperativeLevel);

// Original detail kept: IDirectDrawSurface7::GetAttachedSurface's own reference to the back buffer
// is never released (its holder takes another).
void CTextureDevice::CreateSurfaces(unsigned backBuffers, int for3D) {
    // "Probably, this is a PROGRAMMERS ERROR - too many back buffer surfaces requested (>10).\n"
    if (backBuffers > 10) ThrowGameError(game::Text(0x0078E7D0));
    if (backBuffers > 3) game::DebugLog(game::Text(0x0078E7A0));   // "Check correctness of the back buffer count!\n"
    DDSURFACEDESC2 desc = {};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    if (flags_ & DDSCL_FULLSCREEN) {
        if (backBuffers != 0) {
            desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
            desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
            desc.dwBackBufferCount = backBuffers;
        }
        if (for3D) desc.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
        device::AssignHolder(primary_, NewHolder(this, &desc));
        if (backBuffers == 0) {
            device::AssignHolder(renderTarget_, NewHolderOf(this, device::Surface(primary_)));
            return;
        }
        DDSCAPS2 caps = {};
        caps.dwCaps = DDSCAPS_BACKBUFFER;
        IDirectDrawSurface7* back = nullptr;
        lastLoadResult_ = device::Surface(primary_)->GetAttachedSurface(&caps, &back);
        // "Can't get back buffer, attached to primary surface.\n"
        if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, game::Text(0x0078E768));
        device::AssignHolder(renderTarget_, NewHolderOf(this, back));
        return;
    }

    device::AssignHolder(primary_, NewHolder(this, &desc));
    if (backBuffers == 0) {
        device::AssignHolder(renderTarget_, NewHolderOf(this, device::Surface(primary_)));
        return;
    }
    // An off-screen surface of the screen rectangle's size in the primary's format.
    desc = {};
    desc.dwHeight = static_cast<DWORD>(screen_.bottom - screen_.top);
    desc.dwWidth = static_cast<DWORD>(screen_.right - screen_.left);
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    if (for3D) desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY | DDSCAPS_3DDEVICE;
    CDXSurface* primary = primary_;
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    primary->result = device::Surface(primary)->GetPixelFormat(&desc.ddpfPixelFormat);
    if (primary->result != 0 && !app::FatalErrorCode(primary->result, game::Text(0x0078E714)))   // "Can't get pixel format from surface.\n"
        ThrowGameError(game::Text(0x0078E6E4));   // "Can't get pixel format from primary surface.\n"
    device::AssignHolder(renderTarget_, NewHolder(this, &desc));

    CDXSurface* target = renderTarget_;
    desc = {};
    desc.dwSize = sizeof(desc);
    target->result = device::Surface(target)->GetSurfaceDesc(&desc);
    if (target->result != 0 && !app::FatalErrorCode(target->result, game::Text(0x0078E6C4)))   // "Can't get surface description.\n"
        ThrowGameError(game::Text(0x0078E6A0));   // "pDXSRender->GetSurfaceDesc (back).\n"
    if (!(desc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY)) ThrowGameError(game::Text(0x0078E678));   // "Back buffer is not in video memory.\n"
}
EI_REPLACE_METHOD(0x004EEE30, CTextureDevice::CreateSurfaces);

void CTextureDevice::DestructDirectDraw() {
    ei::SetVtable(this, 0x0073D770);
    if (flags_ & DDSCL_FULLSCREEN) {
        lastLoadResult_ = device::RestoreDisplayMode(device::DDraw(this));
        if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, game::Text(0x0078E47C));   // "Can't restore display mode.\n"
    }
    if (converterLibrary_) gameapi::FreeLibrary(converterLibrary_);
    converterLibrary_ = nullptr;
    converter_ = nullptr;
    if (clipper_) static_cast<IDirectDrawClipper*>(clipper_)->Release();
    device::AssignHolder(renderTarget_, nullptr);
    device::AssignHolder(primary_, nullptr);
    if (ddraw_) device::DDraw(this)->Release();
    modes_.Destroy();
    ei::SetVtable(this, 0x0073D768);
}
EI_REPLACE_METHOD(0x004EE330, CTextureDevice::DestructDirectDraw);

// The parts are called through their addresses (originals_render.h), as the original calls them.
// Not kept: the original copies the processor's twelve bytes as a string into a local nothing reads.
CTextureDevice* CTextureDevice::ConstructDirectDraw(HWND window, GUID* driver, int fullScreen, int width, int height,
                                                    int bitsPerPixel, int refreshRate, unsigned backBuffers, int for3D) {
    refs_ = 0;
    lastLoadResult_ = 0;
    ei::SetVtable(this, 0x0073D768);
    driver_ = nullptr;
    primary_ = nullptr;
    renderTarget_ = nullptr;
    clipper_ = nullptr;
    flags_ = 0;
    window_ = window;
    modes_.Construct();
    ei::SetVtable(this, 0x0073D770);
    const RECT screen = {0, 0, width, height};

    ProcessorInfo& processor = Processor();
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
        game::FatalError(game::Text(0x0078E348));   // "MMX required in this version !!!.\n"
        libcrt::Exit(-1);
    }

    converterLibrary_ = gameapi::LoadLibraryA(game::Text(0x0078E46C));   // "2DintMMX.dll"
    game::DebugLog(game::Text(0x0078E444));                        // "MMX graphics release library loaded.\n"
    if (!converterLibrary_) ThrowGameError(game::Text(0x0078E36C));      // "Can't load 2D graphics library.\n"
    using GetGraphicsProcessor = void*(__cdecl*)();
    const auto getProcessor = reinterpret_cast<GetGraphicsProcessor>(
        gameapi::GetProcAddress(converterLibrary_, game::Text(0x0078E42C)));   // "GetGraphicsProcessor"
    // "Loaded 2D graphics library have no function \"GetGraphicsProcessor\".\n"
    if (!getProcessor) ThrowGameError(game::Text(0x0078E390));
    game::DebugLog(game::Text(0x0078E3F4));   // "Acquiring graphics processor from graphics library.\n"
    converter_ = getProcessor();

    // The switch-over (device.h): the new renderer presents; no DirectDraw object, display mode or
    // surfaces. The stand-in takes the cooperative level (the flags and the screen rectangle are
    // kept) and lists the one display mode; the primary and the render target are holders without
    // a surface; no video formats.
    if (render::StartPresenting()) {
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

    CreateDirectDraw(driver);
    SetCooperativeLevel(fullScreen, &screen);
    if (fullScreen) {
        lastLoadResult_ = device::SetDisplayMode(device::DDraw(this), 
            static_cast<DWORD>(width), static_cast<DWORD>(height), static_cast<DWORD>(bitsPerPixel),
            static_cast<DWORD>(refreshRate), 0);
        if (lastLoadResult_ != 0) ThrowGameErrorResult(lastLoadResult_, game::Text(0x0078E3D8));   // "Can't set display mode.\n"
    }
    CreateSurfaces(backBuffers, for3D);

    // The YUV formats for video: one a video-memory surface can have (when the driver blits
    // four-character-code formats) and one an overlay can have (when it has such overlays).
    DDCAPS caps = {};
    caps.dwSize = sizeof(caps);
    device::GetCaps(device::DDraw(this), &caps, nullptr);
    overlayFourCC_ = 0;
    videoFourCC_ = 0;
    if (caps.dwCaps & DDCAPS_BLTFOURCC) videoFourCC_ = FirstVideoFormat(this, DDSCAPS_VIDEOMEMORY);
    overlayStretch_ = false;
    if (caps.dwCaps & DDCAPS_OVERLAYFOURCC) {
        overlayFourCC_ = FirstVideoFormat(this, DDSCAPS_VIDEOMEMORY | DDSCAPS_OVERLAY);
        overlayStretch_ = (caps.dwCaps & DDCAPS_OVERLAYSTRETCH) != 0;
    }
    return this;
}
EI_REPLACE_METHOD(0x004EDD70, CTextureDevice::ConstructDirectDraw);
