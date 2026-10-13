// A-W12: FigureMeshDrawer::Animate (005A7160). Compared: the original first, the mesh's texture
// offset (+0xCC / +0xD0, the only fields it writes) recorded and put back, then ours; bit for bit.
// Reached by every draw of an animated figure mesh (types 13..16; figure_part_draw*.cpp call it
// through its address).
// A-W14: the drawer's lifetime. Construct 005A7010 (MOV EAX,ECX; XOR ECX,ECX; MOV [EAX+4],ECX: 7
// bytes), the destructor bodies 005A7080 (XOR EAX,EAX; MOV [ECX],imm32: 8) and 005A70C0 (MOV
// [ECX],imm32: 6), Create 005A70E0 (SUB ESP,0xC; MOV [ECX+8],0: 10) and Destroy 005A7130 (XOR
// EAX,EAX; MOV [ECX+0x1C],EAX: 5): the original on a scratch copy of the 0x3C bytes, ours on the
// drawer, the bytes and EAX compared. The bodies only write the drawer (00455930 sets the engine
// object's vtable and clears the owner). The deleting destructors: ours only.
// Reached: Construct and Create by every figure made on the client (a map load; the factory
// 0042D700 and CBaseManager::CreateObject), Destroy and the class's destructors when a figure
// goes (leaving a map). The base's 005A70A0 / 005A70C0: no scenario (no object of the base class
// alone is made).
#include "verify.h"

#include "../../src/game/figure_mesh.h"
#include "../../src/game/link_figure.h"

#include <cstdint>
#include <cstring>

namespace {

// 005A7160: PUSH ESI; MOV ESI,ECX; PUSH EDI; MOV EDI,[ESI+0x1C] (7 bytes).
void(__fastcall* s_animate)(FigureMeshDrawer*, void*, float);

void __fastcall AnimateVerify(FigureMeshDrawer* self, void*, float time) {
    CFigureMesh* mesh = self->mesh_;
    const std::uint32_t u = mesh->unknown_0CC, v = mesh->unknown_0D0;
    s_animate(self, nullptr, time);
    const std::uint32_t expectedU = mesh->unknown_0CC, expectedV = mesh->unknown_0D0;
    mesh->unknown_0CC = u;
    mesh->unknown_0D0 = v;
    self->Animate(time);
    ei::verify::Check("FigureMeshDrawer::Animate", true, mesh->unknown_0CC == expectedU && mesh->unknown_0D0 == expectedV,
                      "type %u time %g", mesh->unknown_15C, static_cast<double>(time));
}

void* s_unused;

struct DrawerBytes {
    alignas(4) std::uint8_t bytes[sizeof(FigureMeshDrawer)];
    explicit DrawerBytes(const FigureMeshDrawer* drawer) { std::memcpy(bytes, drawer, sizeof(bytes)); }
    FigureMeshDrawer* get() { return reinterpret_cast<FigureMeshDrawer*>(bytes); }
    bool Same(const FigureMeshDrawer* drawer) const { return std::memcmp(bytes, drawer, sizeof(bytes)) == 0; }
};

FigureMeshDrawer*(__fastcall* s_construct)(FigureMeshDrawer*);
FigureMeshDrawer* __fastcall ConstructVerify(FigureMeshDrawer* self) {
    DrawerBytes scratch(self);
    FigureMeshDrawer* expected = s_construct(scratch.get());
    FigureMeshDrawer* result = self->Construct();
    ei::verify::Check("FigureMeshDrawer::Construct", true, expected == scratch.get() && result == self && scratch.Same(self), "%p", self);
    return result;
}

void(__fastcall* s_destruct)(FigureMeshDrawer*);
void __fastcall DestructVerify(FigureMeshDrawer* self) {
    DrawerBytes scratch(self);
    s_destruct(scratch.get());
    self->Destruct();
    ei::verify::Check("FigureMeshDrawer::Destruct", true, scratch.Same(self), "%p", self);
}

void(__fastcall* s_destructBase)(FigureMeshDrawer*);
void __fastcall DestructBaseVerify(FigureMeshDrawer* self) {
    DrawerBytes scratch(self);
    s_destructBase(scratch.get());
    self->DestructBase();
    ei::verify::Check("FigureMeshDrawer::DestructBase", true, scratch.Same(self), "%p", self);
}

int(__fastcall* s_create)(FigureMeshDrawer*);
int __fastcall CreateVerify(FigureMeshDrawer* self) {
    DrawerBytes scratch(self);
    int expected = s_create(scratch.get());
    int result = self->Create();
    ei::verify::Check("FigureMeshDrawer::Create", true, expected == result && scratch.Same(self), "%p result %d / %d", self,
                      expected, result);
    return result;
}

void(__fastcall* s_destroy)(FigureMeshDrawer*);
void __fastcall DestroyVerify(FigureMeshDrawer* self) {
    DrawerBytes scratch(self);
    s_destroy(scratch.get());
    self->Destroy();
    ei::verify::Check("FigureMeshDrawer::Destroy", true, scratch.Same(self), "%p", self);
}

FigureMeshDrawer* __fastcall DeleteVerify(FigureMeshDrawer* self, void*, unsigned flags) {
    FigureMeshDrawer* result = self->DeletingDestruct(flags);
    ei::verify::Check("FigureMeshDrawer::DeletingDestruct", true, result == self, "flags %u", flags);
    return result;
}
FigureMeshDrawer* __fastcall DeleteBaseVerify(FigureMeshDrawer* self, void*, unsigned flags) {
    FigureMeshDrawer* result = self->DeletingDestructBase(flags);
    ei::verify::Check("FigureMeshDrawer::DeletingDestructBase", true, result == self, "flags %u", flags);
    return result;
}

} // namespace

EI_VERIFY(0x005A7160, AnimateVerify, 7, s_animate);
EI_VERIFY(0x005A7010, ConstructVerify, 7, s_construct);
EI_VERIFY(0x005A7080, DestructVerify, 8, s_destruct);
EI_VERIFY(0x005A70C0, DestructBaseVerify, 6, s_destructBase);
EI_VERIFY(0x005A70E0, CreateVerify, 10, s_create);
EI_VERIFY(0x005A7130, DestroyVerify, 5, s_destroy);
EI_VERIFY(0x005A7060, DeleteVerify, 0, s_unused);
EI_VERIFY(0x005A70A0, DeleteBaseVerify, 0, s_unused);
