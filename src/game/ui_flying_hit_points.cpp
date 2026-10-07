// The flying hit points' list (ui_flying_hit_points.h). docs/notes/ui.md, section 3.
#include "ui_flying_hit_points.h"

#include "engine_object.h"

#include "ei/hook.h"
#include "lib/gameapi.h"

namespace {
using Node = stlp::list<FlyingHitPoint>::Node;
constexpr std::uint32_t kDelayed = 8;
constexpr std::uint32_t kDelay = 500;   // ms
} // namespace

CFlyingHitPoints* CFlyingHitPoints::Construct() {
    auto* sentinel = static_cast<Node*>(stlp::allocate(sizeof(Node)));
    points_.head = sentinel;
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    object_ = nullptr;
    return this;
}
EI_REPLACE_METHOD(0x005E1710, CFlyingHitPoints::Construct);

void CFlyingHitPoints::Destruct() {
    if (object_) object_->Release();
    points_.ClearWith([](FlyingHitPoint*) {});
    stlp::deallocate(points_.head, sizeof(Node));
}
EI_REPLACE_METHOD(0x005E17E0, CFlyingHitPoints::Destruct);

void CFlyingHitPoints::Add(int target, int amount, std::uint32_t flags) {
    if (!target || amount < 0) return;
    std::uint32_t time = (flags & kDelayed) ? gameapi::timeGetTime() + kDelay : gameapi::timeGetTime();
    // list::insert(end(), value) (005E24E0, the node made by 005E2520).
    auto* node = static_cast<Node*>(stlp::allocate(sizeof(Node)));
    node->data = FlyingHitPoint{target, amount, flags, time};
    Node* head = points_.head;
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}
EI_REPLACE_METHOD(0x005E1870, CFlyingHitPoints::Add);
