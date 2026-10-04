// The software mouse cursor in the frame (005E54A0, thiscall on the cursor object 007C7BD8; called
// by the frame function 00458010 after the scene and the interface, before the present, when the
// display option's software cursor bit (the renderer's +4 bit 0) is set). The cursor's screen
// rectangle (+0x30, updated by 005E52E0) is copied from the render target into the background
// surface (+0x60); the cursor image (+0x10, the part +0x40 of it) is combined with it in the
// composition surface (+0x64, CCursor::Blit: (background & mask) | image) and that is copied back
// to the render target; +0x20 keeps the rectangle drawn. Without an image (+0x0C 0) in mode 2 the
// background is saved from the primary surface instead. The cursor thread (005E5690) redraws the
// cursor on the primary between frames. The new renderer (src/render) gets the image and its mask
// for the same rectangle. See docs/notes/ui.md ("Cursor") and docs/notes/renderer.md.
#include "ui_cursor.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}

constexpr unsigned long kBltWaitAsync = 0x1000200;   // DDBLT_WAIT | DDBLT_ASYNC
constexpr int kBlt = 5;                              // IDirectDrawSurface7::Blt

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// 00500960: the renderer object (007AF060).
__declspec(naked) std::uint8_t* __cdecl Renderer() EI_ORIGINAL(0x00500960)
// 005E52E0 (thiscall): moves the cursor's rectangles to the mouse position; nonzero when shown.
__declspec(naked) char __fastcall UpdatePosition(CCursor*) EI_ORIGINAL(0x005E52E0)
// 005E5640 (thiscall on a surface holder): Blt into it, under the device lock [dest rect, source
// holder, source rect, flags, effects].
__declspec(naked) long __fastcall HolderBlt(std::uint8_t*, void*, RECT*, std::uint8_t*, RECT*, unsigned long, void*)
    EI_ORIGINAL(0x005E5640)
// 005E5C40 (thiscall): the composition (ui_cursor.cpp, through its address).
__declspec(naked) void __fastcall CursorBlit(CCursor*, void*, CursorImage*, RECT*, RECT*) EI_ORIGINAL(0x005E5C40)

// A surface holder: +0x0C its device object (+0x14 the primary's holder, +0x18 the render
// target's), +0x10 the IDirectDrawSurface7.
void* Surface(std::uint8_t* holder) { return At<void*>(holder, 0x10); }
std::uint8_t* RenderTarget(std::uint8_t* holder) { return At<std::uint8_t*>(At<std::uint8_t*>(holder, 0x0C), 0x18); }
std::uint8_t* Primary(std::uint8_t* holder) { return At<std::uint8_t*>(At<std::uint8_t*>(holder, 0x0C), 0x14); }

} // namespace

// 005E54A0.
void __fastcall CursorDrawFrame(CCursor* self) {
    if (!(At<std::uint8_t>(Renderer(), 4) & 1)) return;
    if (!At<std::uint8_t>(self, 0x91)) return;
    UpdatePosition(self);
    RECT* rect = &At<RECT>(self, 0x30);
    if (gameapi::IsRectEmpty(rect)) return;
    RECT local{0, 0, rect->right - rect->left, rect->bottom - rect->top};
    if (At<int>(self, 0x0C) != 0) {
        std::uint8_t* under = self->under_;
        std::uint8_t* screen = RenderTarget(under);
        if (Surface(screen) != Surface(under)) HolderBlt(under, nullptr, &local, screen, rect, kBltWaitAsync, nullptr);
        local = RECT{0, 0, rect->right - rect->left, rect->bottom - rect->top};
        CursorImage* image = At<CursorImage*>(self, 0x10);
        RECT* from = &At<RECT>(self, 0x40);
        CursorBlit(self, nullptr, image, &local, from);
        if (render::Active()) {
            render::Cursor(image->holder, image->saved, render::Rect{from->left, from->top, from->right, from->bottom},
                           render::Rect{rect->left, rect->top, rect->right, rect->bottom});
        }
        local = RECT{0, 0, rect->right - rect->left, rect->bottom - rect->top};
        std::uint8_t* composed = self->target_;
        std::uint8_t* target = RenderTarget(composed);
        if (Surface(target) != Surface(composed)) HolderBlt(target, nullptr, rect, composed, &local, kBltWaitAsync, nullptr);
        At<RECT>(self, 0x20) = *rect;
        return;
    }
    if (At<int>(self, 0) != 2) return;
    std::uint8_t* under = self->under_;
    std::uint8_t* primary = Primary(under);
    if (Surface(primary) == Surface(under)) return;
    gameapi::EnterCriticalSection(DeviceLock());
    ComCall(Surface(under), kBlt, &local, Surface(primary), rect, kBltWaitAsync, static_cast<void*>(nullptr));
    gameapi::LeaveCriticalSection(DeviceLock());
}
EI_REPLACE(0x005E54A0, CursorDrawFrame);
