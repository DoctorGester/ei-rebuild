// Checks for the flying hit points' list (ui_flying_hit_points.cpp). Ours only (they allocate and
// free nodes). Constructed with the world client (every scenario's zone), Add on every hit
// (combat scenarios, e.g. magic_traps), destroyed at exit.
#include "verify.h"

#include "../../src/game/clip.h"
#include "../../src/game/model.h"
#include "../../src/game/ui_flying_hit_points.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include <windows.h>

namespace {
void* s_unused;

int PointCount(const CFlyingHitPoints* self) {
    int n = 0;
    for (auto* node = self->points_.First(); node != self->points_.End(); node = node->next) ++n;
    return n;
}
} // namespace

static CFlyingHitPoints* __fastcall HitPointsConstructVerify(CFlyingHitPoints* self, void*) {
    CFlyingHitPoints* result = self->Construct();
    ei::verify::Check("CFlyingHitPoints::Construct", true,
                      result == self && !self->object_ && self->points_.Empty() && self->points_.head->prev == self->points_.head, "");
    return result;
}
EI_VERIFY(0x005E1710, HitPointsConstructVerify, 0, s_unused);

static void __fastcall HitPointsDestructVerify(CFlyingHitPoints* self, void*) {
    self->Destruct();
    ei::verify::Check("CFlyingHitPoints::Destruct", true, true, "");
}
EI_VERIFY(0x005E17E0, HitPointsDestructVerify, 0, s_unused);

// 005E1870: one more entry, the last, with the arguments, when the target is set and the amount
// not negative; otherwise nothing.
static void __fastcall HitPointsAddVerify(CFlyingHitPoints* self, void*, int target, int amount, std::uint32_t flags) {
    int before = PointCount(self);
    self->Add(target, amount, flags);
    bool added = target && amount >= 0;
    bool ok = PointCount(self) == before + (added ? 1 : 0);
    if (ok && added) {
        const FlyingHitPoint& last = self->points_.head->prev->data;
        ok = last.target == target && last.amount == amount && last.flags == flags;
    }
    ei::verify::Check("CFlyingHitPoints::Add", true, ok, "target %d amount %d flags %X", target, amount, flags);
}
EI_VERIFY(0x005E1870, HitPointsAddVerify, 0, s_unused);

// 005E18E0 Draw: side by side. The game's timeGetTime import (0073B6A0) is held at one value for
// both runs; the original runs first on a copy of the list with a stand-in renderer (a copy of
// the renderer's first 0x24 bytes, so its device and render target are the real ones, with
// slots 11 SetTexture and 12 DrawTriangles recording instead of drawing); then ours on the real
// list with the same stand-in, and its recorded calls are replayed on the real renderer. The
// results, the texture bindings, every draw (vertex and index counts, indices, colours exactly,
// positions and texture coordinates within a few ULPs) and the entries left must agree. Reached
// by every zone (each frame of the world client's draw, 00569E40); numbers only after hits and
// with option 17 on (combat scenarios, e.g. magic_traps). A rare mismatch at a rounding edge (the
// age times 120 / -100 / 255 truncated) is possible.
namespace {

struct RecordedDraw {
    std::vector<TLVertex> vertices;
    std::vector<std::uint16_t> indices;
};
struct Recording {
    std::vector<std::pair<int, void*>> textures;
    std::vector<RecordedDraw> draws;
};
Recording* s_recording;

int __fastcall RecordSetTexture(void*, void*, int stage, void* texture) {
    s_recording->textures.emplace_back(stage, texture);
    return 1;
}
int __fastcall RecordTriangles(void*, void*, const TLVertex* vertices, int vertexCount, const std::uint16_t* indices,
                               unsigned indexCount) {
    s_recording->draws.push_back(RecordedDraw{std::vector<TLVertex>(vertices, vertices + vertexCount),
                                              std::vector<std::uint16_t>(indices, indices + indexCount)});
    return 1;
}

constexpr int kRendererSlots = 26;
struct StandInRenderer {
    void* slots[kRendererSlots];
    alignas(4) std::uint8_t object[0x24];   // the renderer's fields up to the device (+0x20); +0 the vtable

    explicit StandInRenderer(CSceneRenderer* real) {
        std::memcpy(object, real, sizeof(object));
        std::memcpy(slots, *reinterpret_cast<void* const*>(real), sizeof(slots));
        slots[11] = reinterpret_cast<void*>(&RecordSetTexture);
        slots[12] = reinterpret_cast<void*>(&RecordTriangles);
        void* table = slots;
        std::memcpy(object, &table, sizeof(table));
    }
    CSceneRenderer* Renderer() { return reinterpret_cast<CSceneRenderer*>(object); }
};

// The game's timeGetTime, held at one value while both versions run.
using TimeFn = DWORD(WINAPI*)();
TimeFn* const kTimeGetTimeSlot = reinterpret_cast<TimeFn*>(0x0073B6A0);
DWORD s_heldNow;
DWORD WINAPI HeldTime() { return s_heldNow; }
void SetTimeSlot(TimeFn fn) {
    DWORD old;
    VirtualProtect(kTimeGetTimeSlot, sizeof(TimeFn), PAGE_READWRITE, &old);
    *kTimeGetTimeSlot = fn;
    VirtualProtect(kTimeGetTimeSlot, sizeof(TimeFn), old, &old);
}

using PointNode = stlp::list<FlyingHitPoint>::Node;
std::vector<FlyingHitPoint> Entries(const CFlyingHitPoints* self) {
    std::vector<FlyingHitPoint> out;
    for (PointNode* node = self->points_.First(); node != self->points_.End(); node = node->next) out.push_back(node->data);
    return out;
}
// A copy of `self` for the original: the same texture object, a list of its own from the pool.
void MakeScratch(CFlyingHitPoints* scratch, const CFlyingHitPoints* self) {
    scratch->Construct();
    scratch->object_ = self->object_;
    for (const FlyingHitPoint& p : Entries(self)) {
        auto* node = static_cast<PointNode*>(stlp::allocate(sizeof(PointNode)));
        node->data = p;
        PointNode* head = scratch->points_.head;
        node->next = head;
        node->prev = head->prev;
        head->prev->next = node;
        head->prev = node;
    }
}
void FreeScratch(CFlyingHitPoints* scratch) {
    scratch->object_ = nullptr;   // not the copy's to release
    scratch->Destruct();
}

bool SameEntries(const std::vector<FlyingHitPoint>& a, const std::vector<FlyingHitPoint>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(FlyingHitPoint)) != 0) return false;
    return true;
}
bool Near(float a, float b) { return ei::verify::NearlyEqual<float>(a, b, 4, 1e-4f); }
// The first difference between two recordings into `out` (empty when they agree).
void CompareDraws(const Recording& expected, const Recording& actual, char* out, std::size_t size) {
    out[0] = 0;
    if (expected.textures != actual.textures) {
        std::snprintf(out, size, "textures %u / %u", unsigned(expected.textures.size()), unsigned(actual.textures.size()));
        return;
    }
    if (expected.draws.size() != actual.draws.size()) {
        std::snprintf(out, size, "draws %u / %u", unsigned(expected.draws.size()), unsigned(actual.draws.size()));
        return;
    }
    for (std::size_t d = 0; d < expected.draws.size(); ++d) {
        const RecordedDraw& e = expected.draws[d];
        const RecordedDraw& a = actual.draws[d];
        if (e.vertices.size() != a.vertices.size() || e.indices != a.indices) {
            std::snprintf(out, size, "draw %u: vertices %u / %u, indices %u / %u", unsigned(d), unsigned(e.vertices.size()),
                          unsigned(a.vertices.size()), unsigned(e.indices.size()), unsigned(a.indices.size()));
            return;
        }
        for (std::size_t i = 0; i < e.vertices.size(); ++i) {
            const TLVertex& p = e.vertices[i];
            const TLVertex& q = a.vertices[i];
            bool same = Near(p.x, q.x) && Near(p.y, q.y) && Near(p.u, q.u) && Near(p.v, q.v) && p.z == q.z &&
                        p.rhw == q.rhw && p.color == q.color && p.specular == q.specular;
            if (!same) {
                std::snprintf(out, size, "draw %u vertex %u: (%g %g %g %g %08X) / (%g %g %g %g %08X)", unsigned(d),
                              unsigned(i), p.x, p.y, p.u, p.v, p.color, q.x, q.y, q.u, q.v, q.color);
                return;
            }
        }
    }
}

} // namespace

static int(__fastcall* s_draw)(CFlyingHitPoints*, void*, CSceneRenderer*);
static int __fastcall HitPointsDrawVerify(CFlyingHitPoints* self, void*, CSceneRenderer* renderer) {
    TimeFn saved = *kTimeGetTimeSlot;
    s_heldNow = saved();
    SetTimeSlot(&HeldTime);
    StandInRenderer standIn(renderer);

    Recording expected, actual;
    alignas(CFlyingHitPoints) std::uint8_t scratchBytes[sizeof(CFlyingHitPoints)];
    auto* scratch = reinterpret_cast<CFlyingHitPoints*>(scratchBytes);
    MakeScratch(scratch, self);
    s_recording = &expected;
    int expectedResult = s_draw(scratch, nullptr, standIn.Renderer());
    std::vector<FlyingHitPoint> expectedLeft = Entries(scratch);
    FreeScratch(scratch);

    s_recording = &actual;
    int result = self->Draw(standIn.Renderer());
    s_recording = nullptr;
    SetTimeSlot(saved);

    // What ours drew, on the real renderer.
    for (const auto& t : actual.textures) renderer->SetTexture(t.first, t.second);
    for (const RecordedDraw& d : actual.draws)
        renderer->DrawTriangles(d.vertices.data(), static_cast<int>(d.vertices.size()), d.indices.data(),
                                static_cast<unsigned>(d.indices.size()));

    char detail[160];
    CompareDraws(expected, actual, detail, sizeof(detail));
    std::vector<FlyingHitPoint> left = Entries(self);
    ei::verify::Check("CFlyingHitPoints::Draw", true, expectedResult == result && SameEntries(expectedLeft, left) && !detail[0],
                      "result %d / %d, entries %u / %u, %s", expectedResult, result, unsigned(expectedLeft.size()),
                      unsigned(left.size()), detail);
    return result;
}
EI_VERIFY(0x005E18E0, HitPointsDrawVerify, 7, s_draw);
