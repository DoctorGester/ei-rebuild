// The DirectInput mouse (CDirectInputMouse, 0065EAD0-0065ECD6): set up at the application's start,
// acquired with the window's activation and polled by the cursor (A-H25). See ui_cursor.h.
#define DIRECTINPUT_VERSION 0x0700
#include "app.h"
#include "ui_cursor.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "game_data.h"

#include <dinput.h>

#include <cstring>

namespace {

// The game's own copies of the interface ids, the system mouse's id and the mouse data format.
const GUID& InputInterfaceId() { return ei::GameData<const GUID>(0x00744F10); }    // IID_IDirectInput7A
const GUID& DeviceInterfaceId() { return ei::GameData<const GUID>(0x00744EA0); }   // IID_IDirectInputDevice7A
const GUID& SystemMouseId() { return ei::GameData<const GUID>(0x00744DD0); }       // GUID_SysMouse
// c_dfDIMouse's axes (the game's GUID_XAxis, GUID_YAxis, GUID_ZAxis).
const GUID* AxisId(std::uintptr_t address) { return ei::GamePointer<const GUID>(address); }

const char* const kNoInput = "Can't create direct input object\n";        // "Can't create direct input object\n"
const char* const kNoDevice = "Can't create direct input device for mouse\n";       // "Can't create direct input device for mouse\n"
const char* const kNoFormat = "Can't set data format for mouse device\n";       // "Can't set data format for mouse device\n"
const char* const kNoCooperation = "Can't set cooperative level for mouse device\n";  // "Can't set cooperative level for mouse device\n"
const char* const kNoAcquire = "Can't acquire mouse device\n";      // "Can't acquire mouse device\n"

constexpr DWORD kInputVersion = 0x0700;

// A counted pointer taking `value` (the original's smart pointer assignment, then the temporary's
// release, which is not null-checked).
template <typename T>
void Adopt(T*& slot, T* value) {
    T* previous = slot;
    slot = value;
    if (value) value->AddRef();
    if (previous) previous->Release();
    slot->Release();
}

template <typename T>
void Drop(T*& slot) {
    T* held = slot;
    slot = nullptr;
    if (held) held->Release();
}

int Reported(HRESULT result, const char* text) { return app::FatalErrorCode(result, text) != 0 ? 1 : 0; }

} // namespace

const void* CDirectInputMouse::MouseDataFormat() {
    constexpr DWORD kOptional = 0x80000000;   // DIDFT_OPTIONAL (DirectInput 8's headers only)
    static DIOBJECTDATAFORMAT objects[] = {
        {AxisId(0x00744E80), 0, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {AxisId(0x00744E70), 4, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {AxisId(0x00744E60), 8, DIDFT_AXIS | DIDFT_ANYINSTANCE | kOptional, 0},
        {nullptr, 12, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
        {nullptr, 13, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
        {nullptr, 14, DIDFT_BUTTON | DIDFT_ANYINSTANCE | kOptional, 0},
        {nullptr, 15, DIDFT_BUTTON | DIDFT_ANYINSTANCE | kOptional, 0},
    };
    static const DIDATAFORMAT format = {sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, 16,
                                        sizeof(objects) / sizeof(objects[0]), objects};
    return &format;
}

int CDirectInputMouse::Setup(HWND window, HINSTANCE instance) {
    void* input = nullptr;
    HRESULT result = libcrt::imports::DirectInputCreateEx(instance, kInputVersion, &InputInterfaceId(), &input, nullptr);
    if (result < 0) return Reported(result, kNoInput);
    Adopt(input_, static_cast<IDirectInput7A*>(input));

    void* device = nullptr;
    result = input_->CreateDeviceEx(SystemMouseId(), DeviceInterfaceId(), &device, nullptr);
    if (result < 0) return Reported(result, kNoDevice);
    Adopt(device_, static_cast<IDirectInputDevice7A*>(device));

    result = device_->SetDataFormat(static_cast<const DIDATAFORMAT*>(MouseDataFormat()));
    if (result < 0) return Reported(result, kNoFormat);
    result = device_->SetCooperativeLevel(window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    if (result < 0) return Reported(result, kNoCooperation);
    if (device_) {
        result = device_->Acquire();
        if (result < 0) app::FatalErrorCode(result, kNoAcquire);
    }
    return 1;
}
EI_REPLACE_METHOD(0x0065EAD0, CDirectInputMouse::Setup);

void CDirectInputMouse::Shutdown() {
    if (device_) device_->Unacquire();
    Drop(device_);
    Drop(input_);
}
EI_REPLACE_METHOD(0x0065EC30, CDirectInputMouse::Shutdown);

int CDirectInputMouse::SetActive(char active) {
    if (!device_) return 0;
    if (active) {
        HRESULT result = device_->Acquire();
        if (result < 0) return Reported(result, kNoAcquire);
    } else {
        device_->Unacquire();
    }
    return 1;
}
EI_REPLACE_METHOD(0x0065EC70, CDirectInputMouse::SetActive);

const LONG* CDirectInputMouse::Poll(int /*unused*/) {
    static_assert(sizeof(state_) == sizeof(DIMOUSESTATE));
    if (device_->GetDeviceState(sizeof(state_), state_) < 0) std::memset(state_, 0, sizeof(state_));
    return state_;
}
EI_REPLACE_METHOD(0x0065ECB0, CDirectInputMouse::Poll);
