// Check for ui_saveload_list.cpp (A-H26): the save/load window's list, CSaveLoadWindow::RefreshList
// (006567A0; Rebirth-patched, so hooked in the vanilla launch only).
// Side by side on the real window: both runs clear the surface and draw the same rows and details,
// so the second leaves what the first drew. What the function writes besides the picture (the
// clamped caret, the selection marker's visibility and place) is recorded after the original, put
// back and compared after ours. The pixels are not compared.
// Scenarios: ui_saveload, ui_saveload_delete and ui_hud (the Esc menu's Load and Save windows:
// built, scrolled, a save selected, a name typed).
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstdint>

namespace {
constexpr int kMarkerSprite = 4;
bool Near(float a, float b) { return ei::verify::NearlyEqual<float>(a, b, 4, 1e-5f); }
} // namespace

// 006567A0: MOV EAX,FS:[0] (6 bytes).
static void(__fastcall* s_refreshList)(CSaveLoadWindow*);
static void __fastcall RefreshListVerify(CSaveLoadWindow* self) {
    CUiMesh* marker = reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[kMarkerSprite]);
    const int cursor = self->cursor_;
    const int visible = marker->visible_;
    const Vector3 position = marker->position_;

    s_refreshList(self);
    const int expectedCursor = self->cursor_;
    const int expectedVisible = marker->visible_;
    const Vector3 expectedPosition = marker->position_;

    self->cursor_ = cursor;
    marker->visible_ = visible;
    marker->position_ = position;
    self->CSaveLoadWindow::RefreshListVanilla();
    const bool ok = self->cursor_ == expectedCursor && marker->visible_ == expectedVisible &&
                    Near(marker->position_.x, expectedPosition.x) && Near(marker->position_.y, expectedPosition.y) &&
                    Near(marker->position_.z, expectedPosition.z);
    ei::verify::Check("CSaveLoadWindow::RefreshList", true, ok,
                      "selected %d of %d from %d: caret %d -> %d / %d, marker %d / %d at (%g, %g) / (%g, %g)", self->selected_,
                      self->EntryCount(), self->firstVisible_, cursor, expectedCursor, self->cursor_, expectedVisible,
                      marker->visible_, expectedPosition.x, expectedPosition.y, marker->position_.x, marker->position_.y);
}
EI_VERIFY(0x006567A0, RefreshListVerify, 6, s_refreshList);
