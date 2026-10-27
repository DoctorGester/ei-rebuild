// The window surface's constructors and destructor (CUiSurface). The holder has no DirectDraw surface, only the new
// renderer's memory (dx_surface.cpp): the surface is already zero, so the original's colour fill is left out. Making a
// window asks DirectDraw for nothing. The picture constructor has no caller (config/dead_code.csv). See docs/notes/ui.md,
// "Surface construction".
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

// Puts `made` (counted once more, may be null) into the surface's holder and drops the previous one's
// reference through the inlined body of the reference release (the deleting destructor at zero).
void ReplaceHolder(CUiSurface* self, CDXSurface* made) {
    CDXSurface* previous = self->holder_;
    self->holder_ = made;
    if (made) ++made->refs_;
    if (previous) previous->Release();
}

// The common tail: rect_ holds the layout rectangle, interface_ the interface, flags_ the format.
CUiSurface* Finish(CUiSurface* self, bool scale) {
    RECT* rect = &self->rect_;
    if (scale) self->interface_->ScaleRectToScreenEntry(rect);
    self->width_ = rect->right - rect->left;
    self->height_ = rect->bottom - rect->top;
    // (The interface's renderer is the device object.)
    CDXSurface* made = new CDXSurface(self->interface_->renderer_, self->width_, self->height_);
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
    self->background_ = 0;
    self->interface_ = ui;
}


} // namespace

void* CUiSurface::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CUiSurface::operator delete(void* block) { gamecrt::operator_delete(block); }

// The destructor: frees the kept bytes (a total), takes the surface's height * pitch off the surface-bytes
// total, drops the holder and empties the screen rectangle.
CUiSurface::~CUiSurface() {
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
EI_REWRITTEN_SPECIAL(0x005FD120, CUiSurface::~CUiSurface);

// (Interface, picture name by value.) A window showing the picture "<name>.mmp" from the texture manager
// (GetFile, CloseFile), through a picture holder; its size is the picture's, the screen rectangle (0, 0,
// width, height), flag 1.
CUiSurface::CUiSurface(CInterface3D* ui, mfc::CString name) {
    CUiSurface* self = this;
    ui_native::Forget(self);
    self->holder_ = nullptr;
    self->background_ = 0;
    self->interface_ = ui;
    CTextureManager* manager = TheTextureManager();
    MmpHeader* picture;
    {
        mfc::CString file = name + game::MmpExtensionText();   // ".mmp"
        picture = manager->GetFileData(file.GetString());
    }
    CDXSurface* made = new CDXSurface(self->interface_->renderer_, picture, 0);
    CDXSurface* previous = self->holder_;
    self->holder_ = made;
    if (made) ++made->refs_;
    if (previous) previous->Release();
    manager->CloseFile(picture);
    CDXSurface* holder = self->holder_;
    self->width_ = holder->width;
    self->height_ = holder->height;
    UiSurfaceBytes() += SurfaceBytes(holder);
    gameapi::SetRect(&self->rect_, 0, 0, self->holder_->width, self->holder_->height);
    self->kept_ = nullptr;
    self->keptSize_ = 0;
    self->flags_ |= CUiSurface::kFromPicture;
}
EI_REWRITTEN_SPECIAL(0x005FCF80, CUiSurface::CUiSurface);

// The scale flag is a byte.
CUiSurface::CUiSurface(CInterface3D* ui, int left, int top, int right, int bottom, int format, int flag) {
    Begin(this, ui, format);
    rect_.left = left;
    rect_.top = top;
    rect_.right = right;
    rect_.bottom = bottom;
    Finish(this, static_cast<char>(flag) != 0);
}
EI_REWRITTEN_SPECIAL(0x005FCC00, CUiSurface::CUiSurface);

CUiSurface::CUiSurface(CInterface3D* ui, const RECT* rect, int format) {
    Begin(this, ui, format);
    rect_ = *rect;
    Finish(this, true);
}
EI_REWRITTEN_SPECIAL(0x005FCDD0, CUiSurface::CUiSurface);
