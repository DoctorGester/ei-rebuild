// Differential checks for particle drawing (particle_render.cpp).
//
// CParticleSource::Draw: the original runs first, on the real source but with a stand-in for the
// renderer: a copy of it whose vtable binds the texture through the real renderer (slot 11) and,
// instead of drawing (slot 17), captures the flag and the contents of the two shared arrays. Its
// hidden-tick count is put back, then ours runs with the real renderer and draws; afterwards the
// arrays must hold exactly what the original passed, with the same flag and hidden-tick count.
// The sprite array's SetSize and the texture bind change shared state: ours only, checked.
#include "verify.h"

#include "../../src/game/effects.h"

#include <cstring>
#include <vector>

namespace {

ParticleVertexArray& Vertices() { return *reinterpret_cast<ParticleVertexArray*>(0x007A9BD0); }
ParticleSpriteArray& Sprites() { return *reinterpret_cast<ParticleSpriteArray*>(0x007A9BE0); }

struct DrawCapture {
    bool called = false;
    int flag = 0;
    const void* vertexArray = nullptr;
    const void* spriteArray = nullptr;
    std::vector<ParticleVertex> vertices;
    std::vector<ParticleSprite> sprites;
};
DrawCapture g_capture;
void* g_realRenderer = nullptr;

void** VtableOf(void* object) { return *static_cast<void***>(object); }

int __fastcall FakeBind(void*, void*, int stage, void* texture) {
    using BindFn = int(__fastcall*)(void*, void*, int, void*);
    return reinterpret_cast<BindFn>(VtableOf(g_realRenderer)[11])(g_realRenderer, nullptr, stage, texture);
}

int __fastcall FakeDraw(void*, void*, int flag, ParticleVertexArray* v, ParticleSpriteArray* s) {
    g_capture.called = true;
    g_capture.flag = flag & 0xFF;
    g_capture.vertexArray = v;
    g_capture.spriteArray = s;
    g_capture.vertices.assign(v->data, v->data + v->size);
    g_capture.sprites.assign(s->data, s->data + s->size);
    return 1;
}

struct FakeRenderer {
    void** vtable;
    unsigned char rest[0x3C];
};

} // namespace

// 004976B0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x60 (6 bytes).
static int(__fastcall* s_draw)(CParticleSource*, void*, void*);
static int __fastcall DrawVerify(CParticleSource* self, void*, void* renderer) {
    static void* fakeTable[32];
    std::memcpy(fakeTable, VtableOf(renderer), sizeof(fakeTable));
    fakeTable[11] = reinterpret_cast<void*>(&FakeBind);
    fakeTable[17] = reinterpret_cast<void*>(&FakeDraw);
    FakeRenderer fake;
    fake.vtable = fakeTable;
    std::memcpy(fake.rest, static_cast<unsigned char*>(renderer) + 4, sizeof(fake.rest));

    int hiddenBefore = self->hiddenTicks;
    g_realRenderer = renderer;
    g_capture = DrawCapture{};
    int expected = s_draw(self, nullptr, &fake);
    int hiddenExpected = self->hiddenTicks;
    self->hiddenTicks = hiddenBefore;

    int actual = self->CParticleSource::Draw(renderer);
    bool ok = self->hiddenTicks == hiddenExpected;
    if (g_capture.called) {
        int flag = 0;
        std::memcpy(&flag, &self->unknown_150, sizeof(flag));
        ok = ok && g_capture.flag == (flag != 0 ? 1 : 0) && g_capture.vertexArray == &Vertices() &&
             g_capture.spriteArray == &Sprites() &&
             static_cast<int>(g_capture.vertices.size()) == Vertices().size &&
             static_cast<int>(g_capture.sprites.size()) == Sprites().size &&
             std::memcmp(g_capture.vertices.data(), Vertices().data, g_capture.vertices.size() * sizeof(ParticleVertex)) == 0 &&
             std::memcmp(g_capture.sprites.data(), Sprites().data, g_capture.sprites.size() * sizeof(ParticleSprite)) == 0;
    } else {
        ok = ok && expected == actual;
    }
    ei::verify::Check("CParticleSource::Draw", true, ok, "type %X, %d particles, drawn %d", self->type_,
                      self->particles.count, g_capture.called ? 1 : 0);
    return actual;
}
EI_VERIFY(0x004976B0, DrawVerify, 6, s_draw);

// 00497D40: MOV ECX,[ESP+4]; MOV EAX,[ECX] (6 bytes). Pure apart from the position: ours on a copy.
static Particle**(__fastcall* s_nextParticle)(void*, void*, mfc::CTypedPtrList<Particle>::Node**);
static Particle** __fastcall NextParticleVerify(void* list, void*, mfc::CTypedPtrList<Particle>::Node** position) {
    mfc::CTypedPtrList<Particle>::Node* mine = *position;
    Particle** actual = NextParticle(list, nullptr, &mine);
    Particle** expected = s_nextParticle(list, nullptr, position);
    ei::verify::Check("NextParticle", true, actual == expected && mine == *position, "node %p", static_cast<void*>(mine));
    return expected;
}
EI_VERIFY(0x00497D40, NextParticleVerify, 6, s_nextParticle);

// 00497CE0: ours only (binds a texture on the device).
static void* s_unusedBind;
static int __fastcall BindParticleTextureVerify(void* renderer, void*, int stage, void** holder) {
    int result = BindParticleTexture(renderer, nullptr, stage, holder);
    ei::verify::Check("BindParticleTexture", true, holder != nullptr, "stage %d", stage);
    return result;
}
EI_VERIFY(0x00497CE0, BindParticleTextureVerify, 0, s_unusedBind);

// 00497D50: ours only (reallocates). Size and capacity follow, old elements are kept, new ones
// are zero.
static void* s_unusedSetSize;
static void __fastcall SetSizeVerify(ParticleSpriteArray* self, void*, int count, int growBy) {
    int oldSize = self->size;
    std::vector<ParticleSprite> old(self->data, self->data + (self->data ? oldSize : 0));
    self->ParticleSpriteArray::SetSize(count, growBy);
    bool ok = self->size == count && self->maxSize >= count && (count == 0) == (self->data == nullptr);
    int kept = oldSize < count ? oldSize : count;
    if (ok && kept > 0) ok = std::memcmp(old.data(), self->data, static_cast<std::size_t>(kept) * sizeof(ParticleSprite)) == 0;
    for (int i = kept; ok && i < count; ++i) ok = self->data[i].color == 0 && self->data[i].frame == 0;
    ei::verify::Check("ParticleSpriteArray::SetSize", true, ok, "%d -> %d (max %d)", oldSize, count, self->maxSize);
}
EI_VERIFY(0x00497D50, SetSizeVerify, 0, s_unusedSetSize);
