// The harness's mouse is the game's only mouse (harness_input.h).
#define DIRECTINPUT_VERSION 0x0700
#include "harness_input.h"

#include <dinput.h>
#include <windowsx.h>

#include "../../src/hook/log.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace ei::verify::input {

namespace {

constexpr std::uintptr_t kDirectInputCreateExSlot = 0x0073B044;   // DINPUT.dll, the thunk 006E61F0
constexpr int kCreateDeviceExSlot = 9;    // IDirectInput7A
constexpr int kGetDeviceStateSlot = 9;    // IDirectInputDevice7A

using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
using CreateDeviceExFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInput7A*, REFGUID, REFIID, void**, IUnknown*);
using GetDeviceStateFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice7A*, DWORD, void*);

CreateFn g_create;
CreateDeviceExFn g_createDeviceEx;
GetDeviceStateFn g_getDeviceState;
std::atomic<IDirectInputDevice7A*> g_device{nullptr};   // the game's mouse
// GUID_SysMouse (dinput.h declares it; its definition is in dxguid.lib).
constexpr GUID kSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

std::atomic<unsigned> g_reads{0};        // state reads of the game's mouse
std::atomic<unsigned> g_moved{0};        // ... that had real motion or buttons, zeroed
std::atomic<unsigned> g_hitTests{0};     // the real cursor over the game window

template <class T>
void PatchSlot(void* address, T replacement, T& original) {
    auto* slot = static_cast<T*>(address);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
}

void** Vtable(void* object) { return *static_cast<void***>(object); }

// The game's mouse reads nothing of the hardware: the harness's mouse moves by messages and
// GetCursorPos only.
HRESULT STDMETHODCALLTYPE QuietGetDeviceState(IDirectInputDevice7A* device, DWORD size, void* data) {
    const HRESULT result = g_getDeviceState(device, size, data);
    if (device != g_device.load() || !data) return result;
    ++g_reads;
    bool any = false;
    for (DWORD i = 0; i < size && !any; ++i) any = static_cast<const unsigned char*>(data)[i] != 0;
    if (any) {
        const unsigned moved = ++g_moved;
        if (moved <= 3 && size >= sizeof(DIMOUSESTATE)) {
            const auto* state = static_cast<const DIMOUSESTATE*>(data);
            Log("harness: input: the real mouse moved (DirectInput %ld, %ld, %ld, buttons %02X%02X): left out",
                state->lX, state->lY, state->lZ, state->rgbButtons[0], state->rgbButtons[1]);
        }
    }
    std::memset(data, 0, size);
    return result;
}

HRESULT STDMETHODCALLTYPE RoutedCreateDeviceEx(IDirectInput7A* input, REFGUID guid, REFIID iid, void** out,
                                               IUnknown* outer) {
    const HRESULT result = g_createDeviceEx(input, guid, iid, out, outer);
    if (FAILED(result) || !out || !*out || guid != kSysMouse) return result;
    auto* device = static_cast<IDirectInputDevice7A*>(*out);
    g_device = device;
    void** slot = &Vtable(device)[kGetDeviceStateSlot];
    if (*slot != reinterpret_cast<void*>(&QuietGetDeviceState)) {
        PatchSlot(slot, &QuietGetDeviceState, g_getDeviceState);
        Log("harness: input: the game's DirectInput mouse reads no motion or buttons (the harness's mouse only)");
    }
    return result;
}

HRESULT WINAPI RoutedCreate(HINSTANCE instance, DWORD version, REFIID iid, void** out, IUnknown* outer) {
    const HRESULT result = g_create(instance, version, iid, out, outer);
    if (FAILED(result) || !out || !*out) return result;
    void** slot = &Vtable(*out)[kCreateDeviceExSlot];
    if (*slot != reinterpret_cast<void*>(&RoutedCreateDeviceEx)) PatchSlot(slot, &RoutedCreateDeviceEx, g_createDeviceEx);
    return result;
}

} // namespace

void Install() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    PatchSlot(reinterpret_cast<void*>(kDirectInputCreateExSlot), &RoutedCreate, g_create);
}

bool RealMouse(HWND, UINT message, WPARAM, LPARAM lParam, LRESULT* result) {
    if (message != WM_NCHITTEST) return false;
    if (++g_hitTests == 1)
        Log("harness: input: the real cursor is over the game window (%d, %d): no client mouse messages from it",
            GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    *result = HTNOWHERE;
    return true;
}

void Report(FILE* f) {
    const unsigned moved = g_moved.load(), hits = g_hitTests.load();
    if (!moved && !hits)
        std::fprintf(f, "\nMOUSE: the harness's only (%u DirectInput reads, the real mouse never moved or came over the window)\n",
                     g_reads.load());
    else
        std::fprintf(f, "\nMOUSE: the harness's only; the real mouse left out: %u of %u DirectInput reads moved, %u hit tests "
                        "over the window\n",
                     moved, g_reads.load(), hits);
}

} // namespace ei::verify::input
