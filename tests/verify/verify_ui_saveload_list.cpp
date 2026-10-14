// Check for ui_saveload_list.cpp (A-H26): the save/load window's list, CSaveLoadWindow::RefreshList
// (006567A0; Rebirth-patched: its WideScreen is reproduced, so in a Rebirth launch this compares with
// addon's code). Side by side on the real window: what the function writes besides the picture (the
// clamped caret, the selection marker's visibility and place) is put back before each run, recorded
// after the original and compared after ours; the surface's pixels are compared too
// (ui_surface_pixels.h: read and put back through its DC between the runs).
// Scenarios: ui_saveload, ui_saveload_delete and ui_hud (the Esc menu's Load and Save windows:
// built, scrolled, a save selected, a name typed; ui_widescreen_more: the Save window's caret).
#include "verify.h"
#include "ui_surface_pixels.h"

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

    int expectedCursor = cursor;
    int expectedVisible = visible;
    Vector3 expectedPosition = position;
    auto restore = [&] {
        self->cursor_ = cursor;
        marker->visible_ = visible;
        marker->position_ = position;
    };
    CUiSurface* surface = self->surfaces_.size > 0 ? self->surfaces_.data[0] : nullptr;
    ei::verify::gdi::DrawComparison c = ei::verify::gdi::CompareSurfaceDraws(
        surface,
        [&] {
            restore();
            s_refreshList(self);
            expectedCursor = self->cursor_;
            expectedVisible = marker->visible_;
            expectedPosition = marker->position_;
        },
        [&] {
            restore();
            self->CSaveLoadWindow::RefreshListVanilla();
        });
    const bool ok = self->cursor_ == expectedCursor && marker->visible_ == expectedVisible &&
                    Near(marker->position_.x, expectedPosition.x) && Near(marker->position_.y, expectedPosition.y) &&
                    Near(marker->position_.z, expectedPosition.z) && c.diff.count == 0;
    ei::verify::Check("CSaveLoadWindow::RefreshList", true, ok,
                      "selected %d of %d from %d: caret %d -> %d / %d, marker %d / %d at (%g, %g) / (%g, %g); %d pixels differ "
                      "in %d,%d-%d,%d%s",
                      self->selected_, self->EntryCount(), self->firstVisible_, cursor, expectedCursor, self->cursor_,
                      expectedVisible, marker->visible_, expectedPosition.x, expectedPosition.y, marker->position_.x,
                      marker->position_.y, c.diff.count, c.diff.left, c.diff.top, c.diff.right, c.diff.bottom,
                      ei::verify::gdi::Unread(c));
}
EI_VERIFY(0x006567A0, RefreshListVerify, 6, s_refreshList);
