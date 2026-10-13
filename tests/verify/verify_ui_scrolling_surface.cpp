// Checks for ui_scrolling_surface.cpp (A-H17, A-H25). Scenarios: ui_main_menu and
// render_titles_present (the titles: the surface is made with the window, Load renders the text
// once per line, then every frame of a scrolling line draws it).
// - 0065F020 draws into the surface through GDI and a RichEdit, so ours runs alone: the end offset
//   only ever becomes the current offset or 0 (with the speed 0 when it is cleared), and the
//   offsets themselves do not move.
// - The constructor, the destructor (body and slot) and LoadRtf create, destroy or fill the control,
//   and Draw blits: ours only, with what must hold afterwards (Draw: the offset the game time gives).
// - Load and Start leave the same state when repeated: the original, then ours from the same
//   members, compared.
#include "verify.h"

#include "../../src/game/resource.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/world.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {
void* s_unused;

std::uintptr_t VtableOf(const CUiScrollingSurface* surface) { return reinterpret_cast<std::uintptr_t>(surface->vtable_); }

void __fastcall RenderTextVerify(CUiScrollingSurface* self) {
    int endBefore = self->endOffset_;
    int offset = self->offset_;
    int offsetX = self->offsetX_;
    self->RenderText();
    bool ended = self->endOffset_ == 0 && endBefore > 0;
    bool ok = self->offset_ == offset && self->offsetX_ == offsetX &&
              (self->endOffset_ == endBefore || (endBefore <= 0 && self->endOffset_ == offset) ||
               (ended && !(self->speed_ < 0.0f || self->speed_ > 0.0f)));
    ei::verify::Check("CUiScrollingSurface::RenderText", true, ok, "surface %p: offset %d end %d -> %d", self, offset,
                      endBefore, self->endOffset_);
}

CUiScrollingSurface* __fastcall ConstructVerify(CUiScrollingSurface* self, void*, CInterface3D* ui, const RECT* rect) {
    CUiScrollingSurface* result = self->Construct(ui, rect);
    const bool ok = result == self && VtableOf(self) == CUiScrollingSurface::kScrollingVtableAddress &&
                    self->interface_ == ui && self->background_ == 0xFFFFFF && self->offsetX_ == 0 && self->offset_ == 0 &&
                    self->endOffset_ == 0 && !(self->speed_ < 0.0f || self->speed_ > 0.0f) &&
                    (self->richEdit_ != nullptr || self->richEditLibrary_ == nullptr);
    ei::verify::Check("CUiScrollingSurface::Construct", true, ok, "surface %p: control %p library %p",
                      static_cast<void*>(self), static_cast<void*>(self->richEdit_),
                      static_cast<void*>(self->richEditLibrary_));
    return result;
}

bool Destructed(const CUiScrollingSurface* self) {
    return VtableOf(self) == CUiSurface::kVtableAddress && self->richEditLibrary_ == nullptr;
}

void __fastcall DestructVerify(CUiScrollingSurface* self) {
    self->DestructBody();
    ei::verify::Check("CUiScrollingSurface::DestructBody", true, Destructed(self), "surface %p", static_cast<void*>(self));
}

CUiScrollingSurface* __fastcall DeleteVerify(CUiScrollingSurface* self, void*, unsigned flags) {
    CUiScrollingSurface* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CUiScrollingSurface::DeletingDestruct", true, result == self && Destructed(self), "surface %p",
                      static_cast<void*>(self));
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

// What Load, LoadRtf and Start write.
struct ScrollState {
    float startTime;
    int offset;
    float speed;
    int textLength;
    int endOffset;
};
ScrollState Take(const CUiScrollingSurface* s) { return {s->startTime_, s->offset_, s->speed_, s->textLength_, s->endOffset_}; }
void Put(CUiScrollingSurface* s, const ScrollState& v) {
    s->startTime_ = v.startTime;
    s->offset_ = v.offset;
    s->speed_ = v.speed;
    s->textLength_ = v.textLength;
    s->endOffset_ = v.endOffset;
}
bool Same(const ScrollState& a, const ScrollState& b) { return std::memcmp(&a, &b, sizeof(ScrollState)) == 0; }

// 0065EF30: MOV EDX,[ESP+8]; PUSH EBX (5).
bool(__fastcall* s_load)(CUiScrollingSurface*, void*, CMappedResource*, const char*);
bool __fastcall LoadVerify(CUiScrollingSurface* self, void*, CMappedResource* archive, const char* name) {
    const ScrollState before = Take(self);
    bool expected = s_load(self, nullptr, archive, name);
    const ScrollState theirs = Take(self);
    Put(self, before);
    bool actual = self->Load(archive, name);
    const ScrollState ours = Take(self);
    ei::verify::Check("CUiScrollingSurface::Load", true, expected == actual && Same(theirs, ours),
                      "\"%s\": %d / %d, %d / %d characters, end %d / %d", name, expected, actual, theirs.textLength,
                      ours.textLength, theirs.endOffset, ours.endOffset);
    return expected;
}

bool __fastcall LoadRtfVerify(CUiScrollingSurface* self, void*, const void* data, std::uint32_t size) {
    bool result = self->LoadRtf(data, size);
    ei::verify::Check("CUiScrollingSurface::LoadRtf", true, result && self->offset_ == 0 && self->textLength_ >= 0,
                      "%u bytes -> %d characters", size, self->textLength_);
    return result;
}

void __fastcall DrawVerify(CUiScrollingSurface* self) {
    const ScrollState before = Take(self);
    const bool moving = before.speed < 0.0f || before.speed > 0.0f;
    const float now = GameTime();
    const double expected = (static_cast<double>(now) - before.startTime) * before.speed + before.offset;
    self->Draw();
    bool ok;
    if (moving) {
        const double difference = static_cast<double>(self->offset_) - expected;
        ok = self->startTime_ == now && difference > -1.5 && difference < 1.5;
    } else {
        ok = Same(before, Take(self));
    }
    ei::verify::Check("CUiScrollingSurface::Draw", true, ok, "offset %d -> %d (expected %f), speed %f", before.offset,
                      self->offset_, expected, before.speed);
}

// 0065F1C0: PUSH ESI; MOV ESI,ECX; MOV CL,[ESP+0xC] (7).
void(__fastcall* s_start)(CUiScrollingSurface*, void*, float, char);
void __fastcall StartVerify(CUiScrollingSurface* self, void*, float speed, char fromBelow) {
    const ScrollState before = Take(self);
    s_start(self, nullptr, speed, fromBelow);
    const ScrollState theirs = Take(self);
    Put(self, before);
    self->Start(speed, fromBelow);
    const ScrollState ours = Take(self);
    ei::verify::Check("CUiScrollingSurface::Start", true, Same(theirs, ours), "speed %f below %d: offset %d / %d", speed,
                      fromBelow, theirs.offset, ours.offset);
}
} // namespace

EI_VERIFY(0x0065F020, RenderTextVerify, 0, s_unused);
EI_VERIFY(0x0065ED30, ConstructVerify, 0, s_unused);
EI_VERIFY(0x0065EDE0, DestructVerify, 0, s_unused);
EI_VERIFY(0x0065EDC0, DeleteVerify, 0, s_unused);
EI_VERIFY(0x0065EF30, LoadVerify, 5, s_load);
EI_VERIFY(0x0065EF80, LoadRtfVerify, 0, s_unused);
EI_VERIFY(0x0065F170, DrawVerify, 0, s_unused);
EI_VERIFY(0x0065F1C0, StartVerify, 7, s_start);
