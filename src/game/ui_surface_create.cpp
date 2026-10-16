// The window surface's constructors and destructor body (CUiSurface, 0x38 bytes, vtable 00740F28):
// 005FCF80 (a window showing an .mmp picture), 005FD120 (ReleaseContents), 005FCC00 (thiscall:
// interface, layout left, top, right, bottom, format, scale flag; RET 0x1C) and 005FCDD0 (thiscall:
// interface, layout rectangle, format; always scaled; RET 0xC). Both scale the rectangle to the
// screen (005F7CA0, Rebirth's wide-screen patch sits there), make a surface holder of its size
// (004EF7B0), add height * pitch to the surface-bytes total 007C7E78, clear the surface to 0 with a
// colour-fill Blt and set flag 4 of +0x0C.
//
// With the new renderer's memory ("cpuwindows", dx_surface.cpp) the surface is already zero (a new
// DIB section) and the colour fill is left out; in the switch-over ("present") its description comes
// from that memory too, and the holder has no DirectDraw surface at all (ticket R7), so making a
// window asks DirectDraw for nothing.
// The surface descriptions 005FCF80 / 005FD120 take for the surface-bytes total come from that
// memory too in the switch-over, for picture windows as well: their holders (004EFC60 -> 004F0060,
// a system-memory picture surface) get game-owned memory like the others. (005FCF80 itself has
// no caller in game.exe or the mods: config/dead_code.csv.)
// The original's unwinding when 004EF7B0 throws (freeing the holder's memory) is not reproduced:
// that throw ends the game.
#include "app.h"
#include "ui.h"

#include "base_manager.h"
#include "dx_surface.h"
#include "game_strings.h"
#include "texture.h"
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <windows.h>
#include <ddraw.h>

#include <cstdint>
#include <cstring>
#include "originals_engine.h"
#include "originals_render.h"

namespace {

constexpr unsigned long kColourFillWait = DDBLT_COLORFILL | DDBLT_WAIT;   // 0x01000400

IDirectDrawSurface7* DirectDrawSurfaceOf(const CDXSurface* holder) {
    return static_cast<IDirectDrawSurface7*>(holder->surface);
}

// The surface's description: from its game-owned memory in the switch-over (DirectPixels: window
// and picture holders alike), else GetSurfaceDesc (the result in the holder's result, reported).
// `desc` must be zeroed with its size set.
void Describe(CDXSurface* holder, DDSURFACEDESC2* desc) {
    if (DirectPixels(holder, reinterpret_cast<std::uint32_t*>(desc))) return;
    long hr = DirectDrawSurfaceOf(holder)->GetSurfaceDesc(desc);
    holder->result = hr;
    if (hr != 0) app::FatalErrorCode(hr, game::CantGetSurfaceDescriptionText());
}

// height * pitch of the surface's description.
std::uint32_t SurfaceBytes(const DDSURFACEDESC2& desc) {
    return desc.dwHeight * static_cast<std::uint32_t>(desc.lPitch);
}

// Puts `made` (counted once more, may be null) into the surface's holder and drops the previous
// one's reference through 004E6BA0's inlined body (the deleting destructor at zero).
void ReplaceHolder(CUiSurface* self, CDXSurface* made) {
    CDXSurface* previous = self->holder_;
    self->holder_ = made;
    if (made) ++made->refCount;
    if (previous) previous->Release();
}

// The common tail: rect_ holds the layout rectangle, interface_ the interface, flags_ the format.
CUiSurface* Finish(CUiSurface* self, bool scale) {
    RECT* rect = &self->rect_;
    if (scale) self->interface_->CallScaleRectToScreen(rect);
    self->width_ = rect->right - rect->left;
    self->height_ = rect->bottom - rect->top;
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    CDXSurface* made = memory ? reinterpret_cast<CDXSurface*>(DXSurfaceConstructOriginal(
                                    memory, nullptr, self->interface_->renderer_, self->width_, self->height_))
                              : nullptr;
    ReplaceHolder(self, made);
    CDXSurface* holder = self->holder_;
    DDSURFACEDESC2 desc = {};
    desc.dwSize = sizeof(desc);   // 0x7C
    Describe(holder, &desc);
    UiSurfaceBytes() += SurfaceBytes(desc);
    if (!ClientBitmap(holder)) {
        DDBLTFX fx = {};   // fill colour 0
        fx.dwSize = sizeof(fx);   // 100
        gameapi::EnterCriticalSection(GameDeviceLock());
        DirectDrawSurfaceOf(holder)->Blt(nullptr, nullptr, nullptr, kColourFillWait, &fx);
        gameapi::LeaveCriticalSection(GameDeviceLock());
    }
    self->kept_ = nullptr;
    self->flags_ |= CUiSurface::kShown;
    self->keptSize_ = 0;
    return self;
}

void Begin(CUiSurface* self, CInterface3D* ui, int format) {
    self->holder_ = nullptr;
    self->flags_ = static_cast<std::uint32_t>(format);
    self->vtable_ = reinterpret_cast<void*>(CUiSurface::kVtableAddress);
    self->background_ = 0;
    self->interface_ = ui;
}

CUiSurface* AsSurface(char* self) { return reinterpret_cast<CUiSurface*>(self); }

} // namespace

// 005FD120 (thiscall). The destructor body (CUiSurface::ReleaseContents): frees the kept bytes
// (total 007C7E74), takes the surface's height * pitch off 007C7E78, drops the holder and empties
// the screen rectangle.
void __fastcall UiSurfaceRelease(char* raw) {
    CUiSurface* self = AsSurface(raw);
    self->vtable_ = reinterpret_cast<void*>(CUiSurface::kVtableAddress);
    if (std::uint8_t* kept = self->kept_) {
        gamecrt::operator_delete(kept);
        self->kept_ = nullptr;
        UiSurfaceKeptBytes() -= self->keptSize_;
    }
    if (CDXSurface* holder = self->holder_) {
        DDSURFACEDESC2 desc = {};
        desc.dwSize = sizeof(desc);
        Describe(holder, &desc);
        UiSurfaceBytes() -= SurfaceBytes(desc);
    }
    CDXSurface* holder = self->holder_;
    self->holder_ = nullptr;
    if (holder) holder->Release();
    gameapi::SetRectEmpty(&self->rect_);
}
EI_REPLACE(0x005FD120, UiSurfaceRelease);

// 005FCF80 (thiscall: interface, picture name by value; RET 8). A window showing the picture
// "<name>.mmp" from the texture manager (007AF744 slots 19 / 20: GetFile, CloseFile), through a
// picture holder (004EFC60); its size is the picture's, the screen rectangle (0, 0, width, height),
// flag 1.
char* __fastcall UiSurfaceFromPicture(char* raw, void*, void* ui, mfc::CString name) {
    CUiSurface* self = AsSurface(raw);
    self->holder_ = nullptr;
    self->vtable_ = reinterpret_cast<void*>(CUiSurface::kVtableAddress);
    self->background_ = 0;
    self->interface_ = static_cast<CInterface3D*>(ui);
    CBaseManager* manager = static_cast<CBaseManager*>(UiTextureManager());
    void* picture;
    {
        mfc::CString file = name + game::MmpExtensionText();   // ".mmp"
        picture = manager->GetFile(*reinterpret_cast<const char* const*>(&file), nullptr);
    }
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    CDXSurface* made = memory ? static_cast<CDXSurface*>(static_cast<CDXSurface*>(memory)->ConstructFromPicture(reinterpret_cast<CTextureDevice*>(self->interface_->renderer_), picture, 0))
                              : nullptr;
    CDXSurface* previous = self->holder_;
    self->holder_ = made;
    if (made) ++made->refCount;
    if (previous) previous->Release();   // 004E6BA0
    manager->CloseFile(picture);
    CDXSurface* holder = self->holder_;
    self->width_ = holder->width;
    self->height_ = holder->height;
    DDSURFACEDESC2 desc = {};
    desc.dwSize = sizeof(desc);
    Describe(holder, &desc);
    UiSurfaceBytes() += SurfaceBytes(desc);
    gameapi::SetRect(&self->rect_, 0, 0, self->holder_->width, self->holder_->height);
    self->kept_ = nullptr;
    self->keptSize_ = 0;
    self->flags_ |= CUiSurface::kFromPicture;
    return raw;
}
EI_REPLACE(0x005FCF80, UiSurfaceFromPicture);

// 005FCC00.
char* __fastcall UiSurfaceConstruct(char* raw, void*, void* ui, int left, int top, int right, int bottom, int format,
                                    char scale) {
    CUiSurface* self = AsSurface(raw);
    Begin(self, static_cast<CInterface3D*>(ui), format);
    self->rect_.left = left;
    self->rect_.top = top;
    self->rect_.right = right;
    self->rect_.bottom = bottom;
    Finish(self, scale != 0);
    return raw;
}
EI_REPLACE(0x005FCC00, UiSurfaceConstruct);

// 005FCDD0.
char* __fastcall UiSurfaceConstructRect(char* raw, void*, void* ui, const int* rect, int format) {
    CUiSurface* self = AsSurface(raw);
    Begin(self, static_cast<CInterface3D*>(ui), format);
    std::memcpy(&self->rect_, rect, sizeof(RECT));
    Finish(self, true);
    return raw;
}
EI_REPLACE(0x005FCDD0, UiSurfaceConstructRect);
