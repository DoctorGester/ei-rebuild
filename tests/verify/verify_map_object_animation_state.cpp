// Checks for map_object_animation_state.cpp (A-U2).
// - SetTime (0050D600): when the animation playing does not end (its frame count reaches the new
//   frame, nothing NaN) it only sets the frame and the changed flag: the original runs, the state's
//   bytes +0x40..+0x78 are kept and put back, ours runs, bytes compared. Otherwise (callbacks, the
//   queue, the figure manager) ours only: afterwards nothing plays or the frame lies within the
//   animation, and a changed animation sets `changed`.
// - Play (0050CA50): ours only; `current` is the animation, the frame 0, and every part's previous
//   animator is its (existing) animator.
// - Queue (0050CBA0): ours only; one more queued, the animation at the tail, the node returned.
// - Advance (0050D5E0): ours only; when the same animation keeps playing within its length, the
//   frame is the old one plus `frames` (as a float).
// Reached by: every unit animation on the client (units, combat, movement: CUnitClientSpecific's
// slots 14 / 40 and the idle restart 00535480 play and advance them); 0067CEF0 (the interface's
// unit models, ui_*) advances them too.
#include "verify.h"

#include "../../src/game/map_object_figures.h"

#include "ei/x87.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unusedPlay;
void* s_unusedQueue;
void* s_unusedAdvance;

constexpr int kComparedFrom = 0x40;
constexpr int kComparedTo = 0x78;

std::uint8_t* Bytes(CFigureAnimationState* state) { return reinterpret_cast<std::uint8_t*>(state); }

// The call cannot pass the animation's end (no callback, queue or figure manager involved).
bool StaysWithin(const CFigureAnimationState* state, float frame) {
    return state->current && !x87::IsNaN(frame) && !x87::IsNaN(state->time) &&
           x87::FromUnsigned(state->current->frameCount) >= frame;
}
} // namespace

// 0050D600: SUB ESP,0x10; PUSH EBX; PUSH EBP (5 bytes).
static void(__fastcall* s_setTime)(CFigureAnimationState*, void*, float);
static void __fastcall SetTimeVerify(CFigureAnimationState* self, void*, float frame) {
    if (!StaysWithin(self, frame)) {
        const FigureAnimation* before = self->current;
        self->SetTime(frame);
        bool ok = !self->current || x87::FromUnsigned(self->current->frameCount) >= self->time ||
                  x87::IsNaN(self->time);
        if (before != self->current) ok = ok && self->changed == 1;
        ei::verify::Check("CFigureAnimationState::SetTime", true, ok, "frame %g -> %g, %d queued", frame, self->time,
                          self->queue.count);
        return;
    }
    std::uint8_t before[kComparedTo - kComparedFrom];
    std::memcpy(before, Bytes(self) + kComparedFrom, sizeof(before));
    s_setTime(self, nullptr, frame);
    std::uint8_t expected[sizeof(before)];
    std::memcpy(expected, Bytes(self) + kComparedFrom, sizeof(expected));
    std::memcpy(Bytes(self) + kComparedFrom, before, sizeof(before));
    self->SetTime(frame);
    bool same = std::memcmp(expected, Bytes(self) + kComparedFrom, sizeof(expected)) == 0;
    ei::verify::Check("CFigureAnimationState::SetTime", true, same, "frame %g of %u", frame,
                      self->current->frameCount);
}
EI_VERIFY(0x0050D600, SetTimeVerify, 5, s_setTime);

static void __fastcall PlayVerify(CFigureAnimationState* self, void*, FigureAnimation* animation) {
    self->Play(animation);
    bool ok = self->current == animation && self->time == 0.0f;
    for (auto* node = self->parts.head; node; node = node->next)
        ok = ok && node->data->animator && node->data->previousAnimator == node->data->animator;
    ei::verify::Check("CFigureAnimationState::Play", true, ok, "%d parts, frame %g", self->parts.count, self->time);
}
EI_VERIFY(0x0050CA50, PlayVerify, 0, s_unusedPlay);

static void* __fastcall QueueVerify(CFigureAnimationState* self, void*, FigureAnimation* animation) {
    int count = self->queue.count;
    void* node = self->Queue(animation);
    bool ok = self->queue.count == count + 1 && self->queue.tail == node && self->queue.tail->data == animation &&
              self->queue.head;
    ei::verify::Check("CFigureAnimationState::Queue", true, ok, "%d queued", self->queue.count);
    return node;
}
EI_VERIFY(0x0050CBA0, QueueVerify, 0, s_unusedQueue);

static void __fastcall AdvanceVerify(CFigureAnimationState* self, void*, float frames) {
    volatile float sum = frames + self->time;
    float expected = sum;
    bool within = StaysWithin(self, expected);
    const FigureAnimation* before = self->current;
    self->Advance(frames);
    bool ok = !within || (self->current == before && std::memcmp(&self->time, &expected, sizeof(float)) == 0);
    ei::verify::Check("CFigureAnimationState::Advance", true, ok, "+%g -> %g (expected %g)", frames, self->time,
                      expected);
}
EI_VERIFY(0x0050D5E0, AdvanceVerify, 0, s_unusedAdvance);
