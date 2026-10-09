// Checks for CInterface3D::CreateScreen (ui_create_screen.cpp) and CUiWindow::ReplaceSceneObject.
//
// Ours only (it makes a screen). For the screens whose constructor the original inlines, the
// window at the bottom of the window stack must carry the screen's vtable.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_create_screen.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

std::uintptr_t ExpectedVtable(int id) {
    switch (id) {
    case 1: return 0x00740764;
    case 2: return 0x00740688;
    case 3: return 0x007405AC;
    case 7: return 0x00740840;
    default: return 0;
    }
}
} // namespace

// 005F5BA0: ours only.
static void __fastcall CreateScreenVerify(CInterface3D* self, void*, int id) {
    self->CreateScreen(id);
    std::uintptr_t expected = ExpectedVtable(id);
    if (!expected) return;
    // The activation closes every window and pushes the new one: it is the bottom of the stack.
    void* active = self->stackBegin_ != self->stackEnd_ ? self->stackBegin_[0] : nullptr;
    std::uintptr_t vtable = 0;
    if (active) std::memcpy(&vtable, active, 4);
    ei::verify::Check("CInterface3D::CreateScreen", true, vtable == expected, "id %d: active %p vtable %08X", id, active,
                      static_cast<unsigned>(vtable));
}
EI_VERIFY(0x005F5BA0, CreateScreenVerify, 0, s_unused);

// 005FF460: ours only (it makes and releases objects). The new object must take the old one's
// place (same index, same count) with its position and orientation.
static CEngineObject* __fastcall ReplaceSceneObjectVerify(CUiWindow* self, void*, CEngineObject* object,
                                                          const char* name) {
    int index = -1;
    for (int i = 0; i < self->sceneObjects_.size; ++i)
        if (self->sceneObjects_.data[i] == object) {
            index = i;
            break;
        }
    std::uint8_t placement[28] = {};
    if (index >= 0) {
        std::memcpy(placement, reinterpret_cast<std::uint8_t*>(object) + 0x1C, 12);
        std::memcpy(placement + 12, reinterpret_cast<std::uint8_t*>(object) + 0x34, 16);
    }
    int count = self->sceneObjects_.size;
    CEngineObject* made = self->ReplaceSceneObject(object, name);
    if (index < 0 || !made) {
        ei::verify::Check("CUiWindow::ReplaceSceneObject", true, made == nullptr && self->sceneObjects_.size == count,
                          "%s: index %d, made %p", name, index, made);
        return made;
    }
    std::uint8_t now[28];
    std::memcpy(now, reinterpret_cast<std::uint8_t*>(made) + 0x1C, 12);
    std::memcpy(now + 12, reinterpret_cast<std::uint8_t*>(made) + 0x34, 16);
    bool ok = self->sceneObjects_.size == count && self->sceneObjects_.data[index] == made &&
              std::memcmp(now, placement, sizeof(now)) == 0;
    ei::verify::Check("CUiWindow::ReplaceSceneObject", true, ok, "%s: index %d, count %d -> %d", name, index, count,
                      self->sceneObjects_.size);
    return made;
}
EI_VERIFY(0x005FF460, ReplaceSceneObjectVerify, 0, s_unused);

// 005FE930: ours only (a destructor). Afterwards the window must hold nothing: +0xEC..+0xF4 and
// every array empty with no storage (the object's memory is freed only after this returns).
static void __fastcall DestroyBaseVerify(CUiWindow* self, void*) {
    self->DestroyBase();
    std::uint32_t held[3];
    std::memcpy(held, reinterpret_cast<std::uint8_t*>(self) + 0xEC, sizeof(held));
    bool ok = held[0] == 0 && held[1] == 0 && held[2] == 0 && !self->objects_.data && self->objects_.size == 0 &&
              !self->sceneObjects_.data && self->sceneObjects_.size == 0 && !self->surfaces_.data &&
              !self->regions_.data && !self->models_.data;
    ei::verify::Check("CUiWindow::DestroyBase", true, ok, "objects %d scene %d surfaces %d regions %d models %d",
                      self->objects_.size, self->sceneObjects_.size, self->surfaces_.size, self->regions_.size,
                      self->models_.size);
}
EI_VERIFY(0x005FE930, DestroyBaseVerify, 0, s_unused);
// Destructor bodies that only jump to it (ui_window_delete.cpp; A-I16).
EI_VERIFY(0x005EE040, DestroyBaseVerify, 0, s_unused);
EI_VERIFY(0x005EE410, DestroyBaseVerify, 0, s_unused);
EI_VERIFY(0x0060E280, DestroyBaseVerify, 0, s_unused);
EI_VERIFY(0x0060E300, DestroyBaseVerify, 0, s_unused);
EI_VERIFY(0x0060E330, DestroyBaseVerify, 0, s_unused);
EI_VERIFY(0x0060E5D0, DestroyBaseVerify, 0, s_unused);
EI_VERIFY(0x0060E670, DestroyBaseVerify, 0, s_unused);
EI_VERIFY(0x0060E6A0, DestroyBaseVerify, 0, s_unused);
EI_VERIFY(0x006157F0, DestroyBaseVerify, 0, s_unused);
EI_VERIFY(0x00635DA0, DestroyBaseVerify, 0, s_unused);
EI_VERIFY(0x00635DD0, DestroyBaseVerify, 0, s_unused);

// 005FE700: ours only (a constructor). The base window: its vtables, interface, empty arrays and
// both materials.
static CUiWindow* __fastcall WindowConstructVerify(CUiWindow* self, void*, CInterface3D* ui) {
    CUiWindow* result = self->Construct(ui);
    std::uint32_t materials[2];
    std::memcpy(materials, reinterpret_cast<std::uint8_t*>(self) + 0xEC, sizeof(materials));
    bool ok = result == self && self->vtable_ == reinterpret_cast<void**>(0x00740F60) &&
              self->inputVtable_ == reinterpret_cast<void**>(0x00740F2C) && self->interface_ == ui &&
              !self->objects_.data && !self->sceneObjects_.data && !self->surfaces_.data && !self->regions_.data &&
              !self->models_.data && materials[0] && materials[1];
    ei::verify::Check("CUiWindow::Construct", true, ok, "vtable %p interface %p materials %08X %08X", self->vtable_,
                      self->interface_, materials[0], materials[1]);
    return result;
}
EI_VERIFY(0x005FE700, WindowConstructVerify, 0, s_unused);

// 00600D30: ours only. A child is appended (null leaves the list alone).
static void __fastcall AddChildVerify(CUiContainer* self, void*, CUiWindow* child) {
    int before = self->children_.size;
    self->AddChild(child);
    bool ok = child ? self->children_.size == before + 1 && self->children_.data[before] == child
                    : self->children_.size == before;
    ei::verify::Check("CUiContainer::AddChild", true, ok, "children %d -> %d", before, self->children_.size);
}
EI_VERIFY(0x00600D30, AddChildVerify, 0, s_unused);

// 005F7350: ours only. The window must be on the stack afterwards and the mouse capture cleared.
static void __fastcall PushWindowVerify(CInterface3D* self, void*, CUiWindow* window, int a, int b) {
    self->PushWindow(window, a, b);
    bool found = false;
    for (CUiWindow** at = self->stackBegin_; at != self->stackEnd_; ++at)
        if (*at == window) found = true;
    ei::verify::Check("CInterface3D::PushWindow", true, found && self->mouseCapture_ == nullptr,
                      "window %p found %d, %d on the stack", window, found,
                      static_cast<int>(self->stackEnd_ - self->stackBegin_));
}
EI_VERIFY(0x005F7350, PushWindowVerify, 0, s_unused);

// 005F71C0: ours only. Every window closed, the new one at the bottom of the stack, no backdrop.
static void __fastcall ActivateVerify(CInterface3D* self, void*, CUiWindow* window) {
    self->Activate(window);
    void* backdrops[2];
    std::memcpy(backdrops, reinterpret_cast<std::uint8_t*>(self) + 0x54, sizeof(backdrops));
    bool ok = self->stackBegin_ != self->stackEnd_ && self->stackBegin_[0] == window && !backdrops[0] && !backdrops[1];
    ei::verify::Check("CInterface3D::Activate", true, ok, "window %p bottom %p, %d on the stack", window,
                      self->stackBegin_ != self->stackEnd_ ? self->stackBegin_[0] : nullptr,
                      static_cast<int>(self->stackEnd_ - self->stackBegin_));
}
EI_VERIFY(0x005F71C0, ActivateVerify, 0, s_unused);

// 005F6F30 / 005F6D50 / 005F6E40: ours only (constructors and a destructor body).
static void* __fastcall Screen9ConstructVerify(void* self, void*, CInterface3D* ui) {
    void* result = Screen9Construct(self, nullptr, ui);
    ei::verify::Check("Screen9Construct", true,
                      result == self && *static_cast<std::uintptr_t*>(self) == 0x00740D18 &&
                          static_cast<CUiWindow*>(self)->interface_ == ui,
                      "vtable %08X", static_cast<unsigned>(*static_cast<std::uintptr_t*>(self)));
    return result;
}
EI_VERIFY(0x005F6F30, Screen9ConstructVerify, 0, s_unused);
static void* __fastcall Screen8ConstructVerify(void* self, void*, CInterface3D* ui, int flag, const char* text) {
    void* result = Screen8Construct(self, nullptr, ui, flag, text);
    std::uint8_t* header;
    std::memcpy(&header, static_cast<std::uint8_t*>(self) + 0x1C8, 4);
    std::uint8_t* next;
    std::memcpy(&next, header, 4);
    ei::verify::Check("Screen8Construct", true,
                      result == self && *static_cast<std::uintptr_t*>(self) == 0x00740C3C && next == header,
                      "vtable %08X header %p next %p", static_cast<unsigned>(*static_cast<std::uintptr_t*>(self)),
                      header, next);
    return result;
}
EI_VERIFY(0x005F6D50, Screen8ConstructVerify, 0, s_unused);
static void __fastcall Screen8DestroyVerify(void* self) {
    Screen8Destroy(self);
    ei::verify::Check("Screen8Destroy", true, !static_cast<CUiWindow*>(self)->sceneObjects_.data, "scene %p",
                      static_cast<CUiWindow*>(self)->sceneObjects_.data);
}
EI_VERIFY(0x005F6E40, Screen8DestroyVerify, 0, s_unused);

// 005F78C0: ours only (keys reach windows and the console). A modifier key (not a repeat) must
// leave its flag set: +0x7C Ctrl, +0x7D Shift, +0x7E Alt.
static void __fastcall InterfaceKeyDownVerify(CInterface3D* self, void*, int key, int repeat, int flags) {
    self->OnKeyDown(key, repeat, flags);
    int scan = flags & 0x1FF;
    int offset = (scan == 0x1D || scan == 0x11D) ? 0x7C
                 : (scan == 0x2A || scan == 0x36) ? 0x7D
                 : (scan == 0x38 || scan == 0x138) ? 0x7E
                                                    : 0;
    if (!offset || (flags & 0x4000)) return;
    std::uint8_t flag = reinterpret_cast<const std::uint8_t*>(self)[offset];
    ei::verify::Check("CInterface3D::OnKeyDown", true, flag == 1, "scan %X: flag +%X = %d", scan, offset, flag);
}
EI_VERIFY(0x005F78C0, InterfaceKeyDownVerify, 0, s_unused);

// 005F8420: ours only (it consumes the stream). Nothing to compare; it runs only when a save is
// loaded.
static void __fastcall LoadStateVerify(CInterface3D* self, void*, void* stream) {
    self->LoadState(stream);
    ei::verify::Check("CInterface3D::LoadState", true, true, "version %d",
                      *reinterpret_cast<const int*>(static_cast<const std::uint8_t*>(stream) + 8));
}
EI_VERIFY(0x005F8420, LoadStateVerify, 0, s_unused);

// 005F8C90: PUSH -1; PUSH imm32 (7). Side by side: both texts must be equal (ours is destroyed).
static void*(__thiscall* s_messageText)(CInterface3D*, void*, int, int);
static void* __fastcall MessageTextVerify(CInterface3D* self, void*, void* out, int id, int value) {
    s_messageText(self, out, id, value);
    alignas(4) std::uint8_t storage[sizeof(mfc::CString)];
    self->MessageText(storage, id, value);
    const char* expected = *static_cast<const char* const*>(out);
    const char* actual = *reinterpret_cast<const char* const*>(storage);
    ei::verify::Check("CInterface3D::MessageText", true, std::strcmp(expected, actual) == 0, "id %d value %d: %s / %s",
                      id, value, expected, actual);
    reinterpret_cast<mfc::CString*>(storage)->~CString();
    return out;
}
EI_VERIFY(0x005F8C90, MessageTextVerify, 7, s_messageText);

// 006005F0: ours only (it may reallocate). The item is stored and the array is long enough.
static void __fastcall SetAtGrowVerify(UiPtrArray<CEngineObject>* self, void*, int index, CEngineObject* item) {
    int before = self->size;
    self->SetAtGrow(index, item);
    bool ok = self->data[index] == item && self->size == (index >= before ? index + 1 : before);
    ei::verify::Check("UiPtrArray::SetAtGrow", true, ok, "index %d size %d -> %d", index, before, self->size);
}
EI_VERIFY(0x006005F0, SetAtGrowVerify, 0, s_unused);

// 005F8A80: ours only (it opens a window). Without `force` and with an empty queue nothing opens.
static void __fastcall ShowMessageVerify(CInterface3D* self, void*, int force) {
    std::uint8_t* header;
    std::memcpy(&header, reinterpret_cast<std::uint8_t*>(self) + 0x84, 4);
    std::uint8_t* first;
    std::memcpy(&first, header, 4);
    bool empty = first == header && static_cast<std::uint8_t>(force) == 0;
    int windows = static_cast<int>(self->stackEnd_ - self->stackBegin_);
    self->ShowMessage(force);
    if (!empty) return;
    int after = static_cast<int>(self->stackEnd_ - self->stackBegin_);
    ei::verify::Check("CInterface3D::ShowMessage", true, after == windows, "windows %d -> %d", windows, after);
}
EI_VERIFY(0x005F8A80, ShowMessageVerify, 0, s_unused);

// 005F86F0: ours only (it opens a window). At most one window opens; none without an active one.
static void __fastcall OpenHelpVerify(CInterface3D* self, void*, int flag) {
    int windows = static_cast<int>(self->stackEnd_ - self->stackBegin_);
    self->OpenHelp(flag);
    int after = static_cast<int>(self->stackEnd_ - self->stackBegin_);
    bool ok = windows == 0 ? after == 0 : (after == windows || after == windows + 1);
    ei::verify::Check("CInterface3D::OpenHelp", true, ok, "flag %d: windows %d -> %d", flag & 0xFF, windows, after);
}
EI_VERIFY(0x005F86F0, OpenHelpVerify, 0, s_unused);
