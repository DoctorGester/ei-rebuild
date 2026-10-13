// Check for ui_help_page.cpp (A-H26): the help / message window's page, CHelpWindow::ShowPage
// (00659420; Rebirth-patched, so hooked in the vanilla launch only).
// Side by side on the real window. The function starts by emptying the window (both lists, the
// surfaces, regions and scene objects) and builds everything again from the texts, so the second
// run replaces what the first made; the first run's result is recorded before: the regions and
// sprites (ui_panel_shot.h), the surfaces' places and sizes, which sprites the two lists hold (by
// their place among the scene objects, the sprites themselves being new each time), the text
// reader's position and the interface's pending value (set when the texts are missing; put back
// between the runs). The pictures are copied into the same cells of the same texture twice. The
// surfaces' pixels are not compared.
// Scenario: ui_help (F1 on the battle screen: the first page, then Enter and the arrows).
#include "verify.h"

#include "ui_panel_shot.h"

#include "../../src/game/item_db_text.h"
#include "../../src/game/ui.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace ui_shot;

struct SurfaceShot {
    RECT rect;
    int width;
    int height;
};

struct PageShot {
    std::vector<SurfaceShot> surfaces;
    std::vector<int> pulsing;
    std::vector<int> pictures;
    bool textOpen;
    int cursor;
    std::uint32_t textLength;
    bool pendingFlag;
    std::uint32_t pendingValue;
    int page;
};

// The sprite's place among the window's scene objects (-1: not there).
int IndexOf(const CUiWindow* w, const CUiMesh* sprite) {
    for (int i = 0; i < w->sceneObjects_.size; ++i)
        if (reinterpret_cast<const CUiMesh*>(w->sceneObjects_.data[i]) == sprite) return i;
    return -1;
}

std::vector<int> IndicesOf(const CUiWindow* w, const stlp::list<CUiMesh*>& sprites) {
    std::vector<int> indices;
    for (auto* node = sprites.First(); node != sprites.End(); node = node->next) indices.push_back(IndexOf(w, node->data));
    return indices;
}

PageShot TakePage(const CHelpWindow* w) {
    PageShot shot = {};
    for (int i = 0; i < w->surfaces_.size; ++i) {
        const CUiSurface* surface = w->surfaces_.data[i];
        shot.surfaces.push_back(SurfaceShot{surface->rect_, surface->width_, surface->height_});
    }
    shot.pulsing = IndicesOf(w, w->pulsing_);
    shot.pictures = IndicesOf(w, w->pictures_);
    const CTextManager* texts = GameTexts();
    shot.textOpen = texts->text_ != nullptr;
    shot.cursor = texts->text_ && texts->cursor_ ? static_cast<int>(texts->cursor_ - texts->text_) : -1;
    shot.textLength = texts->text_ ? texts->length_ : 0;
    shot.pendingFlag = w->interface_->pendingFlag_;
    shot.pendingValue = w->interface_->pendingValue_;
    shot.page = w->page_;
    return shot;
}

bool SamePage(const PageShot& a, const PageShot& b) {
    if (a.surfaces.size() != b.surfaces.size()) return false;
    for (std::size_t i = 0; i < a.surfaces.size(); ++i) {
        if (std::memcmp(&a.surfaces[i].rect, &b.surfaces[i].rect, sizeof(RECT)) != 0 || a.surfaces[i].width != b.surfaces[i].width ||
            a.surfaces[i].height != b.surfaces[i].height)
            return false;
    }
    return a.pulsing == b.pulsing && a.pictures == b.pictures && a.textOpen == b.textOpen && a.cursor == b.cursor &&
           a.textLength == b.textLength && a.pendingFlag == b.pendingFlag && a.pendingValue == b.pendingValue && a.page == b.page;
}

} // namespace

// 00659420: MOV EAX,FS:[0] (6 bytes).
static void(__fastcall* s_showPage)(CHelpWindow*);
static void __fastcall ShowPageVerify(CHelpWindow* self) {
    const bool pendingFlag = self->interface_->pendingFlag_;
    const std::uint32_t pendingValue = self->interface_->pendingValue_;

    s_showPage(self);
    const PanelShot expected = TakePanel(self);
    const PageShot expectedPage = TakePage(self);

    self->interface_->pendingFlag_ = pendingFlag;
    self->interface_->pendingValue_ = pendingValue;

    self->CHelpWindow::ShowPageVanilla();
    const PanelShot actual = TakePanel(self);
    const PageShot actualPage = TakePage(self);
    const int region = FirstRegionDifference(expected, actual);
    const int sprite = FirstSpriteDifference(expected, actual);
    const bool rest = SamePage(expectedPage, actualPage);
    ei::verify::Check("CHelpWindow::ShowPage", true, region < 0 && sprite < 0 && rest,
                      "page %d of %d: regions %d / %d (first difference %d), sprites %d / %d (first difference %d), surfaces %d / "
                      "%d, pulsing %d / %d, pictures %d / %d, the rest %d",
                      self->page_, self->lines_.size, static_cast<int>(expected.regions.size()),
                      static_cast<int>(actual.regions.size()), region, static_cast<int>(expected.sprites.size()),
                      static_cast<int>(actual.sprites.size()), sprite, static_cast<int>(expectedPage.surfaces.size()),
                      static_cast<int>(actualPage.surfaces.size()), static_cast<int>(expectedPage.pulsing.size()),
                      static_cast<int>(actualPage.pulsing.size()), static_cast<int>(expectedPage.pictures.size()),
                      static_cast<int>(actualPage.pictures.size()), rest);
}
EI_VERIFY(0x00659420, ShowPageVerify, 6, s_showPage);
