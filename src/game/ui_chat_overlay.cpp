// The in-game chat overlay's per-frame slots (vtable 007400DC: 24 update, 22 redraw). Its
// destructor is in ui_window_delete.cpp; adding lines and the key / character handlers (005E2A30,
// 005E3230, 005E3550) are not rewritten. docs/notes/ui.md, section 3.
#include "ui.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"

namespace {
constexpr int kLineLifetime = 20000;   // ms
constexpr int kLeft = 10;
constexpr int kTop = 10;
constexpr int kLineHeight = 16;
} // namespace

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
