// Checks for CInterface3D::CreateScreen (ui_create_screen.cpp) and CUiWindow::ReplaceSceneObject.
//
// Ours only (it makes a screen). For the screens whose constructor the original inlines, the
// window at the bottom of the window stack must carry the screen's vtable.
#include "verify.h"

#include "../../src/game/ui.h"

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
    for (int i = 0; i < self->sceneObjects_.size_; ++i)
        if (self->sceneObjects_.data_[i] == object) {
            index = i;
            break;
        }
    std::uint8_t placement[28] = {};
    if (index >= 0) {
        std::memcpy(placement, reinterpret_cast<std::uint8_t*>(object) + 0x1C, 12);
        std::memcpy(placement + 12, reinterpret_cast<std::uint8_t*>(object) + 0x34, 16);
    }
    int count = self->sceneObjects_.size_;
    CEngineObject* made = self->ReplaceSceneObject(object, name);
    if (index < 0 || !made) {
        ei::verify::Check("CUiWindow::ReplaceSceneObject", true, made == nullptr && self->sceneObjects_.size_ == count,
                          "%s: index %d, made %p", name, index, made);
        return made;
    }
    std::uint8_t now[28];
    std::memcpy(now, reinterpret_cast<std::uint8_t*>(made) + 0x1C, 12);
    std::memcpy(now + 12, reinterpret_cast<std::uint8_t*>(made) + 0x34, 16);
    bool ok = self->sceneObjects_.size_ == count && self->sceneObjects_.data_[index] == made &&
              std::memcmp(now, placement, sizeof(now)) == 0;
    ei::verify::Check("CUiWindow::ReplaceSceneObject", true, ok, "%s: index %d, count %d -> %d", name, index, count,
                      self->sceneObjects_.size_);
    return made;
}
EI_VERIFY(0x005FF460, ReplaceSceneObjectVerify, 0, s_unused);
