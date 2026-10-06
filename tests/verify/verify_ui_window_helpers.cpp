// Checks for the window helpers of ticket A-I5 (ui_window.cpp, ui_container.cpp): 005FEF20
// LoadTexture, 005FF0B0 CreateTexture, 006002C0 ShowSurfaces (slot 19), 00601AC0 ShowChildren (the
// containers' slot 19). All run ours only: they create textures or move surface contents.
// LoadTexture / CreateTexture are reached by every screen's Build (the battle screen's "battle00"
// and its two 256x256 textures in every game scenario); the slot 19 pair whenever a window is
// covered by another or enabled with notification (ui_hud's Esc menu over the battle screen, the
// camp's pages in ui_camp).
#include "verify.h"

#include "../../src/game/ui.h"

#include <vector>

namespace {
void* s_unused;

std::vector<CEngineObject*> Objects(const CUiWindow* window) {
    const auto& array = window->objects_;
    return std::vector<CEngineObject*>(array.data, array.data + array.size);
}

// The list is `before` with `added` appended (or unchanged without it).
bool AppendedOnly(const std::vector<CEngineObject*>& before, const CUiWindow* window, CEngineObject* added) {
    std::vector<CEngineObject*> after = Objects(window);
    std::vector<CEngineObject*> expected = before;
    if (added) expected.push_back(added);
    return after == expected;
}

// After slot 19: with `show` 0 no surface keeps a DirectDraw surface; otherwise every shown one
// has its surface back and nothing kept in memory.
bool SurfacesShownAs(const CUiWindow* window, int show) {
    for (int i = 0; i < window->surfaces_.size; ++i) {
        const CUiSurface* surface = window->surfaces_.data[i];
        if (show == 0) {
            if (surface->holder_) return false;
        } else if (surface->Shown() && (!surface->holder_ || surface->kept_)) {
            return false;
        }
    }
    return true;
}
} // namespace

static CEngineObject* __fastcall LoadTextureVerify(CUiWindow* self, void*, const char* name, int kind,
                                                   const char* directory) {
    std::vector<CEngineObject*> before = Objects(self);
    CEngineObject* texture = self->CUiWindow::LoadTexture(name, kind, directory);
    bool ok = AppendedOnly(before, self, texture) && (!texture || texture->refCount_ >= 1);
    ei::verify::Check("CUiWindow::LoadTexture", true, ok, "name %s kind %d dir %s -> %p", name ? name : "(null)",
                      kind, directory ? directory : "(null)", texture);
    return texture;
}
EI_VERIFY(0x005FEF20, LoadTextureVerify, 0, s_unused);

static CEngineObject* __fastcall CreateTextureVerify(CUiWindow* self, void*, int width, int height, int kind) {
    std::vector<CEngineObject*> before = Objects(self);
    CEngineObject* texture = self->CUiWindow::CreateTexture(width, height, kind);
    bool ok = AppendedOnly(before, self, texture) && (!texture || ((texture->flags_ & 0x40000) && texture->refCount_ >= 1));
    ei::verify::Check("CUiWindow::CreateTexture", true, ok, "%dx%d kind %d -> %p", width, height, kind, texture);
    return texture;
}
EI_VERIFY(0x005FF0B0, CreateTextureVerify, 0, s_unused);

static void __fastcall ShowSurfacesVerify(CUiWindow* self, void*, int show) {
    self->CUiWindow::ShowSurfaces(show);
    ei::verify::Check("CUiWindow::ShowSurfaces", true, SurfacesShownAs(self, show), "show %d surfaces %d", show,
                      self->surfaces_.size);
}
EI_VERIFY(0x006002C0, ShowSurfacesVerify, 0, s_unused);

static void __fastcall ShowChildrenVerify(CUiContainer* self, void*, int show) {
    self->CUiContainer::ShowChildren(show);
    ei::verify::Check("CUiContainer::ShowChildren", true, SurfacesShownAs(self, show), "show %d children %d", show,
                      self->children_.size);
}
EI_VERIFY(0x00601AC0, ShowChildrenVerify, 0, s_unused);
