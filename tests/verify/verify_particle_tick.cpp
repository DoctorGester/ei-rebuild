// Differential checks for CParticleSource's tick and the particle pool (particle_tick.cpp).
//
// The tick allocates and frees particles and list nodes, draws random numbers and runs the
// per-type callbacks (which have their own verify wrappers), so ours runs on the real source.
// The original then replays the deterministic part on a scratch copy of the source as it was
// before the tick: the same particles (as ours left them after their update, followed by the ones
// ours emitted), each "updated" by a callback that keeps it, no elements, no emission. Its wind,
// motion, basis and distance (+0x98..+0xCC, +0x140..+0x150) and bounds (+0x164..+0x1A8) must match
// ours. Skipped when the tick detaches the source (the original would detach the copy too).
// The bounds are compared only when no freed particle was reused by an emission in the same tick.
// The pool and AddParticle change shared lists: ours only, with consistency checks.
#include "verify.h"

#include "../../src/game/effects.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

using Node = mfc::CTypedPtrList<Particle>::Node;

int ListLength(const mfc::CTypedPtrList<Particle>& list) {
    int n = 0;
    const Node* last = nullptr;
    for (const Node* node = list.head; node; node = node->next) {
        if (node->prev != last) return -1;
        last = node;
        ++n;
    }
    return last == list.tail ? n : -1;
}

// The scratch copy's update callback: the particle already holds ours' result.
int __fastcall KeepParticle(CParticleSource*, void*, Particle*) { return 1; }

bool SameRange(const void* a, const void* b, std::size_t from, std::size_t to) {
    return std::memcmp(static_cast<const char*>(a) + from, static_cast<const char*>(b) + from, to - from) == 0;
}

} // namespace

// 00496EB0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x6C (6 bytes).
static int(__fastcall* s_tick)(CParticleSource*);
static int __fastcall TickVerify(CParticleSource* self) {
    if (self->unknown_0E8 > 0x38 && (self->flags_ & 0x20)) return self->CParticleSource::Tick();
    bool detaching = self->attachedTo && (static_cast<const std::uint8_t*>(self->attachedTo)[8] & 1);

    alignas(16) unsigned char before[sizeof(CParticleSource)];
    std::memcpy(before, self, sizeof(before));
    std::vector<Particle*> old;
    int dead = 0;
    for (Node* node = self->particles.head; node; node = node->next) {
        old.push_back(node->data);
        if (node->data->dead) ++dead;
    }

    int result = self->CParticleSource::Tick();

    int length = ListLength(self->particles);
    ei::verify::Check("CParticleSource::Tick list", true, length == self->particles.count, "count %d, walked %d",
                      self->particles.count, length);
    if (detaching || length != self->particles.count) return result;

    // The emitted particles follow the surviving ones at the tail.
    std::vector<Particle*> added;
    int skip = static_cast<int>(old.size()) - dead;
    int index = 0;
    for (Node* node = self->particles.head; node; node = node->next, ++index)
        if (index >= skip) added.push_back(node->data);
    bool reused = false;
    for (Particle* p : added)
        if (std::find(old.begin(), old.end(), p) != old.end()) reused = true;

    std::vector<Particle*> order = old;
    order.insert(order.end(), added.begin(), added.end());
    std::vector<Particle> copies(order.size());
    std::vector<Node> nodes(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        std::memcpy(&copies[i], order[i], sizeof(Particle));
        copies[i].dead = 0;
        nodes[i].data = &copies[i];
        nodes[i].prev = i ? &nodes[i - 1] : nullptr;
        nodes[i].next = i + 1 < order.size() ? &nodes[i + 1] : nullptr;
    }

    alignas(16) unsigned char scratchBytes[sizeof(CParticleSource)];
    std::memcpy(scratchBytes, before, sizeof(scratchBytes));
    auto* scratch = reinterpret_cast<CParticleSource*>(scratchBytes);
    scratch->flags_ &= ~1u;
    scratch->elementCount = 0;
    scratch->updateFn = reinterpret_cast<void*>(&KeepParticle);
    scratch->particles.head = nodes.empty() ? nullptr : &nodes.front();
    scratch->particles.tail = nodes.empty() ? nullptr : &nodes.back();
    scratch->particles.count = static_cast<int>(nodes.size());
    s_tick(scratch);

    bool motion = SameRange(scratch, self, 0x98, 0xCC) && SameRange(scratch, self, 0x140, 0x150);
    bool bounds = reused || SameRange(scratch, self, 0x164, 0x1A8);
    ei::verify::Check("CParticleSource::Tick", true, motion && bounds,
                      "type %X: motion %d bounds %d, %u particles (%d dead, %u emitted)", self->type_, motion, bounds,
                      static_cast<unsigned>(old.size()), dead, static_cast<unsigned>(added.size()));
    return result;
}
EI_VERIFY(0x00496EB0, TickVerify, 6, s_tick);

// 00496A20: ours only. The particle is the new tail, alive, with its "prev" fields saved, and
// inside the bounds.
static void* s_unusedAddParticle;
static void __fastcall AddParticleVerify(CParticleSource* self, void*, Particle* p) {
    int count = self->particles.count;
    self->CParticleSource::AddParticle(p);
    const Vector3& lo = self->boundsMin;
    const Vector3& hi = self->boundsMax;
    bool inside = !(p->pos.x < lo.x) && !(p->pos.y < lo.y) && !(p->pos.z < lo.z) && !(p->pos.x > hi.x) &&
                  !(p->pos.y > hi.y) && !(p->pos.z > hi.z);
    bool ok = self->particles.count == count + 1 && self->particles.tail && self->particles.tail->data == p &&
              p->dead == 0 && std::memcmp(&p->prevPos, &p->pos, sizeof(Vector3)) == 0 && p->prevFrame == p->frame &&
              p->prevColor == p->color && inside;
    ei::verify::Check("CParticleSource::AddParticle", true, ok, "type %X count %d", self->type_, count);
}
EI_VERIFY(0x00496A20, AddParticleVerify, 0, s_unusedAddParticle);

// 00498870: ours only. The pool's head comes off (or a new particle when empty).
static void* s_unusedAcquire;
static Particle* __fastcall AcquireVerify(ParticlePool* self) {
    int count = self->freeParticles.count;
    Particle* head = count ? self->freeParticles.head->data : nullptr;
    Particle* p = self->ParticlePool::Acquire();
    bool ok = count ? (p == head && self->freeParticles.count == count - 1) : self->freeParticles.count == 0;
    ok = ok && ListLength(self->freeParticles) == self->freeParticles.count;
    ei::verify::Check("ParticlePool::Acquire", true, ok, "pool %d", count);
    return p;
}
EI_VERIFY(0x00498870, AcquireVerify, 0, s_unusedAcquire);

// 004987C0: ours only. The particle is the pool's new tail.
static void* s_unusedRelease;
static void __fastcall ReleaseVerify(ParticlePool* self, void*, Particle* p) {
    int count = self->freeParticles.count;
    self->ParticlePool::Release(p);
    bool ok = self->freeParticles.count == count + 1 && self->freeParticles.tail->data == p &&
              ListLength(self->freeParticles) == self->freeParticles.count;
    ei::verify::Check("ParticlePool::Release", true, ok, "pool %d", count);
}
EI_VERIFY(0x004987C0, ReleaseVerify, 0, s_unusedRelease);
