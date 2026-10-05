// Checks for map_object_parts.cpp (A-U): the 3D object's parts by name, their removal and their
// world position.
// - FindPartByPrefix (00516A80) and FindPart (005169A0) only read: both run, the parts compared.
// - PartWorldPosition (005188B0) writes only `out`: the original into a scratch vector, ours
//   into `out`, the three coordinates CheckNear.
// - RemovePart (005167E0) and RemovePartNamed (005168F0) release the part: ours only. The part
//   was found where it was before (parts first, then pending), the list is one shorter with the
//   next entry moved down, and the result says whether it was there; RemovePartNamed returns
//   what FindPart found before the call.
// Reached by: the client's unit models (ChangeWeaponModel 00535590, Wear / RemoveArmourModel
// 005381B0 / 005385A0: the hero's starting equipment and every weapon or armour change: units,
// unit_create, combat), EffectPoint 00532860 and the particle callbacks (spells_cast, effects,
// magic_traps), the footstep and hit sounds of the client controller (00534B60, 00537500: every
// moving or fighting unit).
#include "verify.h"

#include "../../src/game/map_object_figures.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

int IndexIn(const AlignedPtrArray& parts, const FigurePart* part) {
    for (int i = 0; i < parts.size; ++i)
        if (parts.data[i] == part) return i;
    return -1;
}

// Where `part` sits before the removal and what must hold afterwards.
struct RemovalCheck {
    const AlignedPtrArray* list = nullptr;
    int index = -1;
    int size = 0;
    void* next = nullptr;

    void Before(const CObject3D* object, const FigurePart* part) {
        list = &object->parts;
        index = IndexIn(object->parts, part);
        if (index < 0) {
            list = &object->pendingParts;
            index = IndexIn(object->pendingParts, part);
        }
        if (index < 0) return;
        size = list->size;
        next = index + 1 < size ? list->data[index + 1] : nullptr;
    }
    bool After(int result) const {
        if (index < 0) return result == 0;
        return result == 1 && list->size == size - 1 && (index >= list->size || list->data[index] == next);
    }
};
} // namespace

// 00516A80: PUSH EBX; PUSH EBP; PUSH ESI; MOV EBP,[ESP+0x10] (7 bytes).
static FigurePart*(__fastcall* s_findPartByPrefix)(CObject3D*, void*, const char*);
static FigurePart* __fastcall FindPartByPrefixVerify(CObject3D* self, void*, const char* prefix) {
    FigurePart* expected = s_findPartByPrefix(self, nullptr, prefix);
    FigurePart* actual = self->FindPartByPrefix(prefix);
    ei::verify::Check("CObject3D::FindPartByPrefix", Address(expected), Address(actual), "object %d '%s'", self->id_,
                      prefix ? prefix : "");
    return actual;
}
EI_VERIFY(0x00516A80, FindPartByPrefixVerify, 7, s_findPartByPrefix);

// 005169A0: PUSH EBX; MOV EBX,[ESP+8] (5 bytes).
static FigurePart*(__fastcall* s_findPart)(CObject3D*, void*, const char*);
static FigurePart* __fastcall FindPartVerify(CObject3D* self, void*, const char* name) {
    FigurePart* expected = s_findPart(self, nullptr, name);
    FigurePart* actual = self->FindPart(name);
    ei::verify::Check("CObject3D::FindPart", Address(expected), Address(actual), "object %d '%s'", self->id_,
                      name ? name : "");
    return actual;
}
EI_VERIFY(0x005169A0, FindPartVerify, 5, s_findPart);

// 005167E0. Ours only (releases the part).
static int __fastcall RemovePartVerify(CObject3D* self, void*, FigurePart* part) {
    RemovalCheck check;
    check.Before(self, part);
    int result = self->RemovePart(part);
    ei::verify::Check("CObject3D::RemovePart", true, check.After(result), "object %d: index %d of %d, result %d",
                      self->id_, check.index, check.size, result);
    return result;
}
EI_VERIFY(0x005167E0, RemovePartVerify, 0, s_unused);

// 005168F0. Ours only (releases the part).
static FigurePart* __fastcall RemovePartNamedVerify(CObject3D* self, void*, const char* name) {
    FigurePart* found = self->FindPart(name);
    RemovalCheck check;
    check.Before(self, found);
    FigurePart* result = self->RemovePartNamed(name);
    bool ok = result == found && (!found || check.After(1));
    ei::verify::Check("CObject3D::RemovePartNamed", true, ok, "object %d '%s'", self->id_, name ? name : "");
    return result;
}
EI_VERIFY(0x005168F0, RemovePartNamedVerify, 0, s_unused);

// 005188B0: SUB ESP,0x60; PUSH ESI; LEA EDX,[ESP+0xC] (8 bytes).
static float*(__fastcall* s_partWorldPosition)(CObject3D*, void*, float*, const FigurePart*);
static float* __fastcall PartWorldPositionVerify(CObject3D* self, void*, float* out, const FigurePart* part) {
    float expected[3];
    s_partWorldPosition(self, nullptr, expected, part);
    float* result = self->PartWorldPosition(out, part);
    for (int i = 0; i < 3; ++i)
        ei::verify::CheckNear("CObject3D::PartWorldPosition", expected[i], out[i], "object %d part %d [%d]: %g vs %g",
                              self->id_, part->id_, i, expected[i], out[i]);
    return result;
}
EI_VERIFY(0x005188B0, PartWorldPositionVerify, 8, s_partWorldPosition);
