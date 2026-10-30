// The mouse on Windows (platform/mouse.h): DirectInput 7 of the system's dinput.dll.
#define DIRECTINPUT_VERSION 0x0700
#include "platform/mouse.h"

#include <windows.h>

#include <dinput.h>

#include <cstring>

namespace {

// The game's own copies of the interface ids, the system mouse's id and c_dfDIMouse's axes.
constexpr GUID kGuidSysMouse = /*00744DD0*/ {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidZAxis = /*00744E60*/ {0xA36D02E2, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidYAxis = /*00744E70*/ {0xA36D02E1, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidXAxis = /*00744E80*/ {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr IID kIidDirectInputDevice7 = /*00744EA0*/ {0x57D7C6BC, 0x2356, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};
constexpr IID kIidDirectInput7 = /*00744F10*/ {0x9A4CB684, 0x236D, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};

constexpr DWORD kInputVersion = 0x0700;

// DirectInputCreateEx of the system's dinput.dll (System32: Rebirth's dinput.dll in the game folder is a proxy that
// loads addon.dll, which patches game.exe's code), loaded on the first call; null when it is missing. The SDK has
// no import library of DirectInput 7's dinput.dll.
decltype(&::DirectInputCreateEx) SystemDirectInputCreate() {
    static const auto create = [] {
        HMODULE dinput = LoadLibraryExW(L"dinput.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return dinput ? reinterpret_cast<decltype(&::DirectInputCreateEx)>(GetProcAddress(dinput, "DirectInputCreateEx"))
                      : nullptr;
    }();
    return create;
}

// The mouse data format the device gets (dinput.lib's c_dfDIMouse, as a DIDATAFORMAT), our own copy.
const DIDATAFORMAT* MouseDataFormat() {
    constexpr DWORD kOptional = 0x80000000;   // DIDFT_OPTIONAL (DirectInput 8's headers only)
    static DIOBJECTDATAFORMAT objects[] = {
        {&kGuidXAxis, 0, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {&kGuidYAxis, 4, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {&kGuidZAxis, 8, DIDFT_AXIS | DIDFT_ANYINSTANCE | kOptional, 0},
        {nullptr, 12, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
        {nullptr, 13, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
        {nullptr, 14, DIDFT_BUTTON | DIDFT_ANYINSTANCE | kOptional, 0},
        {nullptr, 15, DIDFT_BUTTON | DIDFT_ANYINSTANCE | kOptional, 0},
    };
    static const DIDATAFORMAT format = {sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, 16,
                                        sizeof(objects) / sizeof(objects[0]), objects};
    return &format;
}

// A counted pointer taking `value` (the game's smart pointer assignment, then the temporary's
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

} // namespace

void (*g_directInputObserver)(IDirectInput7A* input) = nullptr;

namespace platform {

int MouseDevice::Open(NativeWindow window, NativeInstance instance, MouseReport report) {
    void* newInput = nullptr;
    const auto create = SystemDirectInputCreate();
    HRESULT result = create ? create(static_cast<HINSTANCE>(instance), kInputVersion, kIidDirectInput7, &newInput, nullptr)
                            : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    if (result < 0) return report(MouseStep::Create, result);
    if (g_directInputObserver) g_directInputObserver(static_cast<IDirectInput7A*>(newInput));
    Adopt(input, static_cast<IDirectInput7A*>(newInput));

    void* newDevice = nullptr;
    result = input->CreateDeviceEx(kGuidSysMouse, kIidDirectInputDevice7, &newDevice, nullptr);
    if (result < 0) return report(MouseStep::Device, result);
    Adopt(device, static_cast<IDirectInputDevice7A*>(newDevice));

    result = device->SetDataFormat(MouseDataFormat());
    if (result < 0) return report(MouseStep::Format, result);
    result = device->SetCooperativeLevel(static_cast<HWND>(window), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    if (result < 0) return report(MouseStep::Cooperation, result);
    if (device) {
        result = device->Acquire();
        if (result < 0) report(MouseStep::Acquire, result);
    }
    return 1;
}

void MouseDevice::Close() {
    if (device) device->Unacquire();
    Drop(device);
    Drop(input);
}

MouseDevice::~MouseDevice() { Close(); }

int MouseDevice::SetActive(bool active, MouseReport report) {
    if (!device) return 0;
    if (active) {
        HRESULT result = device->Acquire();
        if (result < 0) return report(MouseStep::Acquire, result);
    } else {
        device->Unacquire();
    }
    return 1;
}

void MouseDevice::Read(std::int32_t state[4]) {
    static_assert(sizeof(std::int32_t[4]) == sizeof(DIMOUSESTATE));
    if (device->GetDeviceState(sizeof(DIMOUSESTATE), state) < 0) std::memset(state, 0, sizeof(DIMOUSESTATE));
}

} // namespace platform
