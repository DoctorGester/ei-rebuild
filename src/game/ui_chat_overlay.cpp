// The in-game chat overlay: its constructor and per-frame slots (vtable 007400DC: 24 update, 22
// redraw). Its destructor is in ui_window_delete.cpp. docs/notes/ui.md, section 3.
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "originals_ui.h"

#include <new>

namespace {
constexpr int kLineLifetime = 20000;   // ms
constexpr int kLeft = 10;
constexpr int kTop = 10;
constexpr int kLineHeight = 16;
// The typed line's surface.
constexpr int kTypedLeft = 10, kTypedTop = 0x23D, kTypedRight = 0x316, kTypedBottom = 0x24E, kTypedFormat = 4;
void** ChatOverlayVtable() { return reinterpret_cast<void**>(0x007400DC); }
void** ChatOverlayInputVtable() { return reinterpret_cast<void**>(0x007400A8); }
} // namespace

// --- 005E26F0: the constructor ---------------------------------------------------------------------

CChatOverlay* CChatOverlay::Construct(CInterface3D* ui) {
    CUiWindow::Construct(ui);
    using Node = stlp::list<ChatLine>::Node;
    auto* sentinel = static_cast<Node*>(stlp::allocate(sizeof(Node)));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    lines_.head = sentinel;
    new (&typed_) mfc::CString;
    vtable_ = ChatOverlayVtable();
    inputVtable_ = ChatOverlayInputVtable();
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, kTypedLeft, kTypedTop, kTypedRight,
                                                                    kTypedBottom, kTypedFormat, 1)
                      : nullptr);
    chatOpen_ = false;
    caret_ = 0;
    ChatOverlayDrawTypedOriginal(this);
    return this;
}
EI_REPLACE_METHOD(0x005E26F0, CChatOverlay::Construct);

void CChatOverlay::Update() {
    std::uint32_t now = gameapi::timeGetTime();
    using Node = stlp::list<ChatLine>::Node;
    Node* head = lines_.head;
    for (Node* node = head->next; node != head;) {
        Node* next = node->next;
        if (static_cast<int>(now - node->data.time) > kLineLifetime) {
            // list::erase (005E3AD0): unlinked, destroyed, the node back to the pool.
            node->prev->next = next;
            next->prev = node->prev;
            node->data.Destroy();
            stlp::deallocate(node, sizeof(Node));
        }
        node = next;
    }
}
EI_REPLACE_METHOD(0x005E2990, CChatOverlay::Update);

void CChatOverlay::Redraw() {
    int y = kTop;
    for (auto* node = lines_.First(); node != lines_.End(); node = node->next) {
        node->data.surface->MoveTo(kLeft, y);
        node->data.surface->Blit();
        y += kLineHeight;
    }
    RedrawBase();
}
EI_REPLACE_METHOD(0x005E29E0, CChatOverlay::Redraw);
