// The device object's present (004EF610, __fastcall on the device object 007AF080; called once per
// frame by the frame function 00458010 after the scene, the interface and the software cursor are
// drawn): full screen flips the primary's chain, a window copies the render target to the
// primary at the client rectangle's corner. The new renderer (src/render) ends its frame here.
// See docs/notes/renderer.md.
#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

// IDirectDrawSurface7 slots.
constexpr int kBltFast = 7;
constexpr int kFlip = 11;
constexpr unsigned long kBltFastWait = 0x10;   // DDBLTFAST_WAIT

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

void* SurfaceOf(void* holder) { return At<void*>(holder, 0x10); }   // CDXSurface +0x10

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

} // namespace

// 004EF610. Returns the DirectDraw result (0 when there is nothing to present).
long __fastcall DevicePresent(char* device) {
    render::Present();
    void* primary = SurfaceOf(At<void*>(device, 0x14));
    void* target = At<void*>(device, 0x18);
    void* targetSurface = SurfaceOf(target);
    if (primary == targetSurface) return 0;
    if (At<std::uint8_t>(device, 0x20) & 1) return ComCall(primary, kFlip, targetSurface, 0ul);
    // A window: the render target's own device object's primary (+0xC -> +0x14).
    void* screen = SurfaceOf(At<void*>(At<void*>(target, 0xC), 0x14));
    if (screen == targetSurface) return 0;
    unsigned long x = At<unsigned long>(device, 0x24), y = At<unsigned long>(device, 0x28);
    gameapi::EnterCriticalSection(DeviceLock());
    long result = ComCall(screen, kBltFast, x, y, targetSurface, static_cast<void*>(nullptr), kBltFastWait);
    gameapi::LeaveCriticalSection(DeviceLock());
    return result;
}
EI_REPLACE(0x004EF610, DevicePresent);
