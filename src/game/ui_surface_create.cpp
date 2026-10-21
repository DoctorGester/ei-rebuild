// The window surface's constructors and destructor body (CUiSurface, 0x38 bytes, vtable 00740F28):
// 005FCF80 (a window showing an .mmp picture), 005FD120 (ReleaseContents), 005FCC00 (thiscall:
// interface, layout left, top, right, bottom, format, scale flag; RET 0x1C) and 005FCDD0 (thiscall:
// interface, layout rectangle, format; always scaled; RET 0xC). Both scale the rectangle to the
// screen (005F7CA0, Rebirth's wide-screen patch sits there), make a surface holder of its size
// (004EF7B0), add height * pitch to the surface-bytes total 007C7E78, clear the surface to 0 with a
// colour-fill Blt and set flag 4 of +0x0C.
//
// The holder has no DirectDraw surface, only the new renderer's memory (dx_surface.cpp, ticket R7):
// the surface is already zero (a new DIB section), so the colour fill is left out, and the
// descriptions the surface-bytes total takes (here and in 005FCF80 / 005FD120) come from that
// memory, for picture windows as well (their holders, 004EFC60 -> 004F0060, get such memory too).
// Making a window asks DirectDraw for nothing. (005FCF80 itself has no caller in game.exe or the
// mods: config/dead_code.csv.)
// The original's unwinding when 004EF7B0 throws (freeing the holder's memory) is not reproduced:
// that throw ends the game.
#include "app.h"
#include "ui.h"
#include "ui_native.h"

#include "base_manager.h"
#include "dx_surface.h"
#include "game_strings.h"
#include "texture.h"
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include "originals_engine.h"
#include "originals_render.h"
#include "game_data.h"
#include "game_singletons.h"

namespace {

// height * pitch of the surface's memory (DirectPixels: window and picture holders alike; the
// original's GetSurfaceDesc); 0 without memory.
std::uint32_t SurfaceBytes(CDXSurface* holder) {
    SurfacePixels pixels;
    DirectPixels(holder, &pixels, true);
    return static_cast<std::uint32_t>(pixels.height) * static_cast<std::uint32_t>(pixels.pitch);
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
    CDXSurface* memory = gamecrt::operator_new_as<CDXSurface>();
    // (The interface's renderer is the device object.)
    CDXSurface* made = memory ? memory->Construct(
                                    reinterpret_cast<CTextureDevice*>(self->interface_->renderer_), self->width_, self->height_)
                              : nullptr;
    ReplaceHolder(self, made);
    UiSurfaceBytes() += SurfaceBytes(self->holder_);
    self->kept_ = nullptr;
    self->flags_ |= CUiSurface::kShown;
    self->keptSize_ = 0;
    return self;
}

void Begin(CUiSurface* self, CInterface3D* ui, int format) {
    ui_native::Forget(self);   // (a surface made again where another was)
    self->holder_ = nullptr;
    self->flags_ = static_cast<std::uint32_t>(format);
    self->vtable_ = ei::GamePointer<void>(CUiSurface::kVtableAddress);
    self->background_ = 0;
    self->interface_ = ui;
}

CUiSurface* AsSurface(char* self) { return reinterpret_cast<CUiSurface*>(self); }

} // namespace

// 005FD120. The destructor body: frees the kept bytes (total 007C7E74), takes the surface's
// height * pitch off 007C7E78, drops the holder and empties the screen rectangle.
void CUiSurface::ReleaseContents() {
    vtable_ = ei::GamePointer<void>(kVtableAddress);
    ui_native::Forget(this);
    if (std::uint8_t* kept = kept_) {
        gamecrt::operator_delete(kept);
        kept_ = nullptr;
        UiSurfaceKeptBytes() -= keptSize_;
    }
    if (CDXSurface* holder = holder_) UiSurfaceBytes() -= SurfaceBytes(holder);
    CDXSurface* holder = holder_;
    holder_ = nullptr;
    if (holder) holder->Release();
    gameapi::SetRectEmpty(&rect_);
}
EI_REWRITTEN(0x005FD120, CUiSurface::ReleaseContents);

// 005FCF80 (thiscall: interface, picture name by value; RET 8). A window showing the picture
// "<name>.mmp" from the texture manager (007AF744 slots 19 / 20: GetFile, CloseFile), through a
// picture holder (004EFC60); its size is the picture's, the screen rectangle (0, 0, width, height),
// flag 1.
char* __fastcall UiSurfaceFromPicture(char* raw, void*, void* ui, mfc::CString name) {
    CUiSurface* self = AsSurface(raw);
    ui_native::Forget(self);
    self->holder_ = nullptr;
    self->vtable_ = ei::GamePointer<void>(CUiSurface::kVtableAddress);
    self->background_ = 0;
    self->interface_ = static_cast<CInterface3D*>(ui);
    CBaseManager* manager = TheTextureManager();
    void* picture;
    {
        mfc::CString file = name + game::MmpExtensionText();   // ".mmp"
        picture = manager->GetFile(*reinterpret_cast<const char* const*>(&file), nullptr);
    }
    CDXSurface* memory = gamecrt::operator_new_as<CDXSurface>();
    CDXSurface* made = memory ? memory->ConstructFromPicture(reinterpret_cast<CTextureDevice*>(self->interface_->renderer_), picture, 0)
                              : nullptr;
    CDXSurface* previous = self->holder_;
    self->holder_ = made;
    if (made) ++made->refCount;
    if (previous) previous->Release();   // 004E6BA0
    manager->CloseFile(picture);
    CDXSurface* holder = self->holder_;
    self->width_ = holder->width;
    self->height_ = holder->height;
    UiSurfaceBytes() += SurfaceBytes(holder);
    gameapi::SetRect(&self->rect_, 0, 0, self->holder_->width, self->holder_->height);
    self->kept_ = nullptr;
    self->keptSize_ = 0;
    self->flags_ |= CUiSurface::kFromPicture;
    return raw;
}
EI_REWRITTEN(0x005FCF80, UiSurfaceFromPicture);

// 005FCC00 (the scale flag is a byte).
CUiSurface* CUiSurface::Construct(CInterface3D* ui, int left, int top, int right, int bottom, int format, int flag) {
    Begin(this, ui, format);
    rect_.left = left;
    rect_.top = top;
    rect_.right = right;
    rect_.bottom = bottom;
    Finish(this, static_cast<char>(flag) != 0);
    return this;
}
EI_REWRITTEN(0x005FCC00, CUiSurface::Construct);

// 005FCDD0.
CUiSurface* CUiSurface::ConstructOnRect(CInterface3D* ui, const RECT* rect, int format) {
    Begin(this, ui, format);
    rect_ = *rect;
    Finish(this, true);
    return this;
}
EI_REWRITTEN(0x005FCDD0, CUiSurface::ConstructOnRect);
