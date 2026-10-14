// Checks for the device object's present (device_present.cpp).
#include "verify.h"

#include "../../src/game/texture.h"

long __fastcall DevicePresent(CTextureDevice* device);

// 004EF610 flips or copies the frame to the screen: ours only. Checked: the render target is still
// the device object's after the call, and the result is 0 or a DirectDraw error code.
static void* s_devicePresent;
static long __fastcall DevicePresentVerify(CTextureDevice* device) {
    CDXSurface* target = device->renderTarget_;
    long result = DevicePresent(device);
    bool ok = device->renderTarget_ == target &&
              (result == 0 || (static_cast<unsigned long>(result) >> 16) == 0x8876u ||
               (static_cast<unsigned long>(result) >> 16) == 0x8007u);
    ei::verify::Check("DevicePresent", true, ok, "result %08lX", static_cast<unsigned long>(result));
    return result;
}
EI_VERIFY(0x004EF610, DevicePresentVerify, 0, s_devicePresent);
