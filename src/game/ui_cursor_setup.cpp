// The software cursor's set-up (CCursor, ui_cursor.h): Init 005E60B0 and AddShape 005E63F0, called
// by the cursor set-up 005DAEA0 (cursor_setup.cpp) at start and after a display reset. docs/notes/ui.md,
// section 3.
#include "ui_cursor.h"

#include "texture.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_render.h"

namespace {

// The cursor's size in pixels (007C7C90 / 007C7C94): 32, or 16 for the small cursors.
LONG& CursorWidth() { return *reinterpret_cast<LONG*>(0x007C7C90); }
LONG& CursorHeight() { return *reinterpret_cast<LONG*>(0x007C7C94); }
constexpr LONG kLargeSize = 0x20;
constexpr LONG kSmallSize = 0x10;

// A surface holder pointer set to `holder` (one reference more), the old one released.
void AssignHolder(CDXSurface*& slot, CDXSurface* holder) {
    CDXSurface* old = slot;
    slot = holder;
    if (holder) ++holder->refCount;
    if (old) old->Release();
}

// A new off-screen surface of the screen's format (004EF7B0 through its address); null when the
// allocation fails.
CDXSurface* NewSurface(CTextureDevice* device, LONG width, LONG height) {
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    if (!memory) return nullptr;
    return reinterpret_cast<CDXSurface*>(DXSurfaceConstructOriginal(memory, nullptr, device, width, height));
}

} // namespace

void CCursor::Init(CTextureDevice* device, int mode, int context, int smallCursors) {
    const LONG size = static_cast<std::uint8_t>(smallCursors) ? kSmallSize : kLargeSize;
    CursorWidth() = size;
    CursorHeight() = size;
    position_.x = 0;
    motion_.x = 0;
    position_.y = 0;
    motion_.y = 0;
    image_ = nullptr;
    unknown_14 = 0;
    unknown_98 = 0;
    shape_ = -1;
    imageShown_ = 1;
    enabled_ = 1;
    mode_ = 0;
    stopped_ = gameapi::CreateEventA(nullptr, FALSE, FALSE, nullptr);
    mode_ = mode;
    context_ = context;
    CTextureDevice* old = device_;
    device_ = device;
    if (device) device->AddRef();
    if (old) old->Release();
    gameapi::SetRectEmpty(&bounds_);
    if (device_->primary_) {
        bounds_.right = device_->renderTarget_->width;
        bounds_.bottom = device_->renderTarget_->height;
    }
    AssignHolder(under_, NewSurface(device_, CursorWidth(), CursorHeight()));
    AssignHolder(target_, NewSurface(device_, CursorWidth() * 2, CursorHeight() * 2));
}
EI_REPLACE_METHOD(0x005E60B0, CCursor::Init);

void CCursor::AddShape(const char* name, int hotX, int hotY, int flag) {
    CursorImage* image = nullptr;
    if (void* memory = gamecrt::operator_new(sizeof(CursorImage))) {
        mfc::CString file = mfc::CString(name) + ".mmp";
        image = CursorImageConstructOriginal(memory, nullptr, static_cast<const char*>(file), hotX, hotY, flag);
    }
    images_.Add(image);
}
EI_REPLACE_METHOD(0x005E63F0, CCursor::AddShape);
