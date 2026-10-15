// Checks for directdraw_holder.cpp and the two CDXSurface ports of dx_surface.cpp (A-S13). All on
// the main thread, at start-up (every scenario) unless noted.
//  - Side by side:
//    004EE4A0 (driver list): ours fills the list, its drivers are noted, then, inside a heap::Aside
//      (no block of the reservation moves), the original fills a twin list put in the global's place
//      (freed; the real list put back).
//    004ED610 (its callback; reached through the original 004EE4A0 above, on the twin list): ours
//      appends to the list it is given, then the original to a scratch list (inside a heap::Aside,
//      freed); the two new records are compared.
//    004EE5D0 (the hardware driver): no state; the two results compared.
//    004EEA90 (the DirectDraw object and its display modes): the original into a scratch object
//      inside a heap::Aside (its DirectDraw object released again and its mode list freed before
//      ours runs: nothing of it is alive, nor was any of it in the reservation), then ours; the mode
//      lists compared.
//    004EFEA0 (a holder's size from its surface): the holder's size and result put back in between.
//    004EFA60 (a holder from a description): ours first; for the first 48 descriptions that are not
//      a primary surface the original then makes a second surface into a scratch holder, the two
//      holders are compared and the second surface released. Reached at start-up (the depth
//      surfaces, the render target) and by every texture made (any scene).
//  - Ours only, the state left checked (the original cannot run next to ours: one cooperative level,
//    one primary surface, one display mode):
//    004EECF0 (cooperative level), 004EEE30 (primary and render target holders), 004EDD70 (the
//    base's constructor), 004EE330 (its destructor body; at exit), 004EEBC0 (display mode; a full
//    screen mode change only: no scenario), 004EEC60 (display mode restored; full screen only: the
//    windowed scenarios run its first test).
//    What 004EDD70 and its parts leave is also compared between a run with ours and a run with the
//    originals through the device-state lines (verify_device_state.h).
#include "verify.h"
#include "harness_heap.h"

#include "../../src/game/device.h"
#include "../../src/game/dx_surface.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
void* s_unused;

// ---- The driver list ----

struct DriverCopy {
    bool hasGuid;
    GUID guid;
    std::string name;
    std::string description;
    HMONITOR monitor;
    bool operator==(const DriverCopy&) const = default;
};
DriverCopy Copy(const DirectDrawDriver& driver) {
    DriverCopy copy{};
    copy.hasGuid = driver.guid != nullptr;
    if (driver.guid) copy.guid = *driver.guid;
    copy.name = driver.name.data();
    copy.description = driver.description.data();
    copy.monitor = driver.monitor;
    return copy;
}
std::vector<DriverCopy> Drivers(const stlp::list<DirectDrawDriver>& list) {
    std::vector<DriverCopy> drivers;
    for (auto* node = list.First(); node != list.End(); node = node->next) drivers.push_back(Copy(node->data));
    return drivers;
}

// 004EE4A0: MOV EAX,[007AEE64] (5). Ours fills the list first; the original then fills a twin list
// put in the global's place (its sentinel, nodes and strings allocated after ours' and freed), and
// the real list is put back.
int(__cdecl* s_enumerateDrivers)();
int __cdecl EnumerateDriversVerify() {
    stlp::list<DirectDrawDriver>& drivers = DirectDrawDrivers();
    const int result = EnumerateDirectDrawDrivers();
    const std::vector<DriverCopy> ours = Drivers(drivers);
    const stlp::list<DirectDrawDriver> real = drivers;
    std::vector<DriverCopy> original;
    int expected;
    {
        ei::verify::heap::Aside aside;
        drivers.Construct();
        expected = s_enumerateDrivers();
        original = Drivers(drivers);
        drivers.ClearWith([](DirectDrawDriver* driver) { driver->~DirectDrawDriver(); });
        drivers.Destroy();
    }
    drivers = real;
    ei::verify::Check("EnumerateDrivers (004EE4A0)", true, result == expected && ours == original,
                      "result %d / %d, drivers %u / %u", expected, result, static_cast<unsigned>(original.size()),
                      static_cast<unsigned>(ours.size()));
    return result;
}

// 004ED610: PUSH -1 (2), PUSH 0071BAC3 (5). Ours appends first; the original then to a scratch list
// (freed).
BOOL(__stdcall* s_driverCallback)(GUID*, char*, char*, void*, HMONITOR);
BOOL __stdcall DriverCallbackVerify(GUID* guid, char* description, char* name, void* context, HMONITOR monitor) {
    const BOOL result = DriverEnumCallback(guid, description, name, context, monitor);
    ei::verify::heap::Aside aside;
    stlp::list<DirectDrawDriver> scratch;
    scratch.Construct();
    const BOOL expected = s_driverCallback(guid, description, name, &scratch, monitor);
    const auto& drivers = *static_cast<const stlp::list<DirectDrawDriver>*>(context);
    const std::vector<DriverCopy> original = Drivers(scratch);
    const DirectDrawDriver& added = drivers.End()->prev->data;
    const DirectDrawDriver& made = scratch.End()->prev->data;
    // A driver with a GUID points at its own copy of it.
    const bool ownGuid = (!added.guid || added.guid == &added.guidValue) && (!made.guid || made.guid == &made.guidValue);
    ei::verify::Check("DriverEnumCallback (004ED610)", true,
                      result == expected && original.size() == 1 && !drivers.Empty() && ownGuid && Copy(added) == original[0],
                      "driver '%s' result %d / %d", description, expected, result);
    scratch.ClearWith([](DirectDrawDriver* driver) { driver->~DirectDrawDriver(); });
    scratch.Destroy();
    return result;
}

// 004EE5D0: SUB ESP,0x184 (6).
DirectDrawDriver*(__cdecl* s_firstHardwareDriver)();
DirectDrawDriver* __cdecl FirstHardwareDriverVerify() {
    DirectDrawDriver* expected = s_firstHardwareDriver();
    DirectDrawDriver* result = FirstHardwareDirectDrawDriver();
    ei::verify::Check("FirstHardwareDriver (004EE5D0)", static_cast<void*>(expected), static_cast<void*>(result), "driver '%s'",
                      result ? result->description.data() : "");
    return result;
}

// ---- The DirectDraw object and its display modes ----

struct ModeCopy {
    std::uint32_t flags, width, height, refreshRate, caps;
    DDPIXELFORMAT format;
};
bool operator==(const ModeCopy& a, const ModeCopy& b) { return std::memcmp(&a, &b, sizeof(ModeCopy)) == 0; }
std::vector<ModeCopy> Modes(const CTextureDevice* device) {
    std::vector<ModeCopy> modes;
    for (auto* node = device->modes_.First(); node != device->modes_.End(); node = node->next) {
        const DDSURFACEDESC2& mode = node->data;
        ModeCopy copy;
        std::memset(&copy, 0, sizeof(copy));
        copy.flags = mode.dwFlags;
        copy.width = mode.dwWidth;
        copy.height = mode.dwHeight;
        copy.refreshRate = mode.dwRefreshRate;
        copy.caps = mode.ddsCaps.dwCaps;
        copy.format = mode.ddpfPixelFormat;
        modes.push_back(copy);
    }
    return modes;
}

// 004EEA90: PUSH -1 (2), PUSH 0071BC56 (5). The scratch object has only what the function uses:
// its result, DirectDraw pointer, driver and mode list.
void(__fastcall* s_createDirectDraw)(CTextureDevice*, void*, GUID*);
void __fastcall CreateDirectDrawVerify(CTextureDevice* self, void*, GUID* driver) {
    alignas(CTextureDevice) static std::uint8_t storage[sizeof(CTextureDevice)];
    std::memset(storage, 0, sizeof(storage));
    auto* scratch = reinterpret_cast<CTextureDevice*>(storage);
    std::vector<ModeCopy> expected;
    bool originalMade;
    {
        ei::verify::heap::Aside aside;
        scratch->modes_.Construct();
        s_createDirectDraw(scratch, nullptr, driver);
        expected = Modes(scratch);
        originalMade = scratch->ddraw_ && scratch->lastLoadResult_ == 0 && scratch->driver_ == driver;
        if (scratch->ddraw_) device::DDraw(scratch)->Release();
        scratch->modes_.Destroy();
    }

    self->CreateDirectDraw(driver);
    const std::vector<ModeCopy> modes = Modes(self);
    ei::verify::Check("CTextureDevice::CreateDirectDraw (004EEA90)", true,
                      originalMade && self->ddraw_ && self->lastLoadResult_ == 0 && self->driver_ == driver && modes == expected,
                      "display modes %u / %u", static_cast<unsigned>(expected.size()), static_cast<unsigned>(modes.size()));
}

void __fastcall SetDisplayModeVerify(CTextureDevice* self, void*, int width, int height, int bitsPerPixel, int refreshRate) {
    self->SetDisplayMode(width, height, bitsPerPixel, refreshRate);
    ei::verify::Check("CTextureDevice::SetDisplayMode (004EEBC0)", 0, self->lastLoadResult_, "%d x %d, %d bits", width, height,
                      bitsPerPixel);
}

void __fastcall RestoreDisplayModeVerify(CTextureDevice* self) {
    const int before = self->lastLoadResult_;
    self->RestoreDisplayMode();
    // Windowed: nothing is touched. Full screen: the call's result, 0 here (a failure throws).
    ei::verify::Check("CTextureDevice::RestoreDisplayMode (004EEC60)", (self->flags_ & DDSCL_FULLSCREEN) ? 0 : before,
                      self->lastLoadResult_, "flags %X", self->flags_);
}

void __fastcall SetCooperativeLevelVerify(CTextureDevice* self, void*, int fullScreen, const RECT* screen) {
    self->SetCooperativeLevel(fullScreen, screen);
    RECT expected = {};
    if (screen) expected = *screen;
    const std::uint32_t flags = fullScreen ? (DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE) : DDSCL_NORMAL;
    ei::verify::Check("CTextureDevice::SetCooperativeLevel (004EECF0)", true,
                      self->flags_ == flags && self->lastLoadResult_ == 0 &&
                          std::memcmp(&self->screen_, &expected, sizeof(RECT)) == 0,
                      "full screen %d flags %X", fullScreen, self->flags_);
}

// ---- The surface holders ----

struct HolderSize {
    int width, height, bitsPerPixel;
    long result;
    bool operator==(const HolderSize&) const = default;
};
HolderSize Size(const CDXSurface* holder) { return {holder->width, holder->height, holder->bitsPerPixel, holder->result}; }

// 004EFEA0: SUB ESP,0x7C (3), PUSH ESI (1), MOV ESI,ECX (2).
int(__fastcall* s_describe)(CDXSurface*);
int __fastcall DescribeVerify(CDXSurface* holder) {
    const HolderSize before = Size(holder);
    const int expected = s_describe(holder);
    const HolderSize original = Size(holder);
    holder->width = before.width;
    holder->height = before.height;
    holder->bitsPerPixel = before.bitsPerPixel;
    holder->result = before.result;
    const int result = DXSurfaceDescribe(holder);
    ei::verify::Check("DXSurfaceDescribe (004EFEA0)", true, result == expected && Size(holder) == original,
                      "result %d / %d, %d x %d", expected, result, holder->width, holder->height);
    return result;
}

// 004EFA60: PUSH -1 (2), PUSH 0071BDF9 (5).
void*(__fastcall* s_fromDesc)(void*, void*, CTextureDevice*, const std::uint32_t*);
int g_fromDescCompared = 0;
CDXSurface* __fastcall FromDescVerify(CDXSurface* holder, void*, CTextureDevice* device, const std::uint32_t* desc) {
    CDXSurface* result = holder->ConstructFromDesc(device, desc);
    const auto* description = reinterpret_cast<const DDSURFACEDESC2*>(desc);
    const bool primary = (description->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
    const bool made = result == holder && holder->surface && holder->refCount == 0 && holder->device == device;
    if (primary || !made || g_fromDescCompared >= 48) {
        ei::verify::Check("DXSurfaceFromDesc (004EFA60)", true, made, "caps %08X", description->ddsCaps.dwCaps);
        return result;
    }
    ++g_fromDescCompared;
    // The original makes a second surface of the same description into a scratch holder.
    const int holders = device::HolderCount();
    const int deviceResult = device->lastLoadResult_;
    CDXSurface scratch;
    s_fromDesc(&scratch, nullptr, device, desc);
    const bool same = scratch.vtable == holder->vtable && scratch.refCount == holder->refCount &&
                      scratch.device == holder->device && scratch.surface != nullptr && Size(&scratch) == Size(holder) &&
                      device->lastLoadResult_ == deviceResult && device::HolderCount() == holders + 1;
    ei::verify::Check("DXSurfaceFromDesc (004EFA60)", true, same, "caps %08X: %d x %d %d bits / %d x %d %d bits",
                      description->ddsCaps.dwCaps, scratch.width, scratch.height, scratch.bitsPerPixel, holder->width,
                      holder->height, holder->bitsPerPixel);
    if (scratch.surface) device::Surface(&scratch)->Release();
    device::HolderCount() = holders;
    device->lastLoadResult_ = deviceResult;
    return result;
}

void __fastcall CreateSurfacesVerify(CTextureDevice* self, void*, unsigned backBuffers, int for3D) {
    self->CreateSurfaces(backBuffers, for3D);
    const CDXSurface* primary = self->primary_;
    const CDXSurface* target = self->renderTarget_;
    bool held = primary && target && primary->refCount == 1 && target->refCount == 1 && primary->surface &&
                target->surface && primary->device == self && target->device == self && target->width > 0 &&
                target->height > 0 && target->bitsPerPixel == primary->bitsPerPixel;
    if (held && !(self->flags_ & DDSCL_FULLSCREEN)) {
        // Windowed: an off-screen target of the screen rectangle's size, or the primary again.
        if (backBuffers != 0)
            held = target->surface != primary->surface && target->width == self->screen_.right - self->screen_.left &&
                   target->height == self->screen_.bottom - self->screen_.top;
        else
            held = target->surface == primary->surface;
    }
    ei::verify::Check("CTextureDevice::CreateSurfaces (004EEE30)", true, held, "back buffers %u for 3D %d", backBuffers, for3D);
}

void* VtableOf(const void* object) {
    void* vtable;
    std::memcpy(&vtable, object, sizeof(vtable));
    return vtable;
}

void __fastcall DestructDirectDrawVerify(CTextureDevice* self) {
    self->DestructDirectDraw();
    const bool left = VtableOf(self) == reinterpret_cast<void*>(0x0073D768) && !self->primary_ && !self->renderTarget_ &&
                      !self->converter_ && !self->converterLibrary_;
    ei::verify::Check("CTextureDevice::DestructDirectDraw (004EE330)", true, left, "flags %X", self->flags_);
}

// The switch-over's primary and render target (device.h): two holders without a surface, of the
// screen's size and colour depth.
bool ScreenHolders(const CTextureDevice* self, int width, int height, int bitsPerPixel) {
    for (const CDXSurface* holder : {self->primary_, self->renderTarget_}) {
        if (!holder || holder->surface || holder->refCount != 1 || holder->device != self || holder->width != width ||
            holder->height != height || holder->bitsPerPixel != bitsPerPixel)
            return false;
    }
    return self->primary_ != self->renderTarget_ && !self->videoFourCC_ && !self->overlayFourCC_;
}

CTextureDevice* __fastcall ConstructDirectDrawVerify(CTextureDevice* self, void*, HWND window, GUID* driver, int fullScreen,
                                                     int width, int height, int bitsPerPixel, int refreshRate,
                                                     unsigned backBuffers, int for3D) {
    CTextureDevice* result =
        self->ConstructDirectDraw(window, driver, fullScreen, width, height, bitsPerPixel, refreshRate, backBuffers, for3D);
    const RECT screen = {0, 0, width, height};
    const bool built = result == self && VtableOf(self) == reinterpret_cast<void*>(0x0073D770) && self->refs_ == 0 &&
                       self->window_ == window && self->driver_ == driver && self->ddraw_ && self->converter_ &&
                       self->converterLibrary_ && !self->clipper_ && self->primary_ && self->renderTarget_ &&
                       std::memcmp(&self->screen_, &screen, sizeof(RECT)) == 0 &&
                       self->flags_ == static_cast<std::uint32_t>(fullScreen ? (DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE) : DDSCL_NORMAL) &&
                       !self->modes_.Empty() && (!device::WithoutDirectDraw() || ScreenHolders(self, width, height, bitsPerPixel));
    ei::verify::Check("CTextureDevice::ConstructDirectDraw (004EDD70)", true, built, "%d x %d, %d bits, full screen %d", width,
                      height, bitsPerPixel, fullScreen);
    return result;
}

} // namespace

EI_VERIFY(0x004EE4A0, EnumerateDriversVerify, 5, s_enumerateDrivers);
EI_VERIFY(0x004ED610, DriverCallbackVerify, 7, s_driverCallback);
EI_VERIFY(0x004EE5D0, FirstHardwareDriverVerify, 6, s_firstHardwareDriver);
EI_VERIFY(0x004EEA90, CreateDirectDrawVerify, 7, s_createDirectDraw);
EI_VERIFY(0x004EEBC0, SetDisplayModeVerify, 0, s_unused);
EI_VERIFY(0x004EEC60, RestoreDisplayModeVerify, 0, s_unused);
EI_VERIFY(0x004EECF0, SetCooperativeLevelVerify, 0, s_unused);
EI_VERIFY(0x004EFEA0, DescribeVerify, 6, s_describe);
EI_VERIFY(0x004EFA60, FromDescVerify, 7, s_fromDesc);
EI_VERIFY(0x004EEE30, CreateSurfacesVerify, 0, s_unused);
EI_VERIFY(0x004EE330, DestructDirectDrawVerify, 0, s_unused);
EI_VERIFY(0x004EDD70, ConstructDirectDrawVerify, 0, s_unused);
