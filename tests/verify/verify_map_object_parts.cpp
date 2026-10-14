// Checks for map_object_parts.cpp (A-U): the 3D object's parts by name, their removal and their
// world position.
// - FindPartByPrefix (00516A80) and FindPart (005169A0) only read: both run, the parts compared.
// - PartWorldPosition (005188B0) writes only `out`: the original into a scratch vector, ours
//   into `out`, the three coordinates CheckNear.
// - RemovePart (005167E0) and RemovePartNamed (005168F0) release the part: ours only. The part
//   was found where it was before (parts first, then pending), the list is one shorter with the
//   next entry moved down, and the result says whether it was there; RemovePartNamed returns
//   what FindPart found before the call.
// - Animations (00518CA0) loads the database on first use: with it loaded both run and compare,
//   else ours only (the result is what it stored). AnimationAt (00518F80) begins PUSH ESI; MOV
//   ESI,ECX; CALL 00518CA0, too short for a trampoline: the original runs through a stub repeating
//   them; compared. FindSkillAnimation (00518FC0), AnimationFactor (00518D30, CheckNear) and
//   FindPartIndex (00517990) read only (the database loaded by whichever runs first): compared.
// Reached by: the client's unit models (ChangeWeaponModel 00535590, Wear / RemoveArmourModel
// 005381B0 / 005385A0: the hero's starting equipment and every weapon or armour change: units,
// unit_create, combat), EffectPoint 00532860 and the particle callbacks (spells_cast, effects,
// magic_traps), the footstep and hit sounds of the client controller (00534B60, 00537500: every
// moving or fighting unit). Animations / AnimationAt: every unit's animation entries (CUnit::
// AnimationEntry 00530240: every scenario with units); AnimationFactor: CUnit::AnimationRatio and
// the animation state (0050D3E0 / 0050D450 / 0050D7C0: every drawn unit); FindPartIndex:
// BodyExtent (CUnitServer slot 23: units, combat, unit_create) and CAIMap::MarkFootprint;
// FindSkillAnimation: the hero preview (00616E20: ui_hero_screen) and the interface's models
// (005FC3B0).
#include "verify.h"

#include "../../src/game/map_object_figures.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

int IndexIn(const AlignedPtrArray& parts, const CFigureMesh* part) {
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

    void Before(const CObject3D* object, const CFigureMesh* part) {
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
static CFigureMesh*(__fastcall* s_findPartByPrefix)(CObject3D*, void*, const char*);
static CFigureMesh* __fastcall FindPartByPrefixVerify(CObject3D* self, void*, const char* prefix) {
    CFigureMesh* expected = s_findPartByPrefix(self, nullptr, prefix);
    CFigureMesh* actual = self->FindPartByPrefix(prefix);
    ei::verify::Check("CObject3D::FindPartByPrefix", Address(expected), Address(actual), "object %d '%s'", self->id_,
                      prefix ? prefix : "");
    return actual;
}
EI_VERIFY(0x00516A80, FindPartByPrefixVerify, 7, s_findPartByPrefix);

// 005169A0: PUSH EBX; MOV EBX,[ESP+8] (5 bytes).
static CFigureMesh*(__fastcall* s_findPart)(CObject3D*, void*, const char*);
static CFigureMesh* __fastcall FindPartVerify(CObject3D* self, void*, const char* name) {
    CFigureMesh* expected = s_findPart(self, nullptr, name);
    CFigureMesh* actual = self->FindPart(name);
    ei::verify::Check("CObject3D::FindPart", Address(expected), Address(actual), "object %d '%s'", self->id_,
                      name ? name : "");
    return actual;
}
EI_VERIFY(0x005169A0, FindPartVerify, 5, s_findPart);

// 005167E0. Ours only (releases the part).
static int __fastcall RemovePartVerify(CObject3D* self, void*, CFigureMesh* part) {
    RemovalCheck check;
    check.Before(self, part);
    int result = self->RemovePart(part);
    ei::verify::Check("CObject3D::RemovePart", true, check.After(result), "object %d: index %d of %d, result %d",
                      self->id_, check.index, check.size, result);
    return result;
}
EI_VERIFY(0x005167E0, RemovePartVerify, 0, s_unused);

// 005168F0. Ours only (releases the part).
static CFigureMesh* __fastcall RemovePartNamedVerify(CObject3D* self, void*, const char* name) {
    CFigureMesh* found = self->FindPart(name);
    RemovalCheck check;
    check.Before(self, found);
    CFigureMesh* result = self->RemovePartNamed(name);
    bool ok = result == found && (!found || check.After(1));
    ei::verify::Check("CObject3D::RemovePartNamed", true, ok, "object %d '%s'", self->id_, name ? name : "");
    return result;
}
EI_VERIFY(0x005168F0, RemovePartNamedVerify, 0, s_unused);

// 005188B0: SUB ESP,0x60; PUSH ESI; LEA EDX,[ESP+0xC] (8 bytes).
static float*(__fastcall* s_partWorldPosition)(CObject3D*, void*, float*, const CFigureMesh*);
static float* __fastcall PartWorldPositionVerify(CObject3D* self, void*, float* out, const CFigureMesh* part) {
    float expected[3];
    s_partWorldPosition(self, nullptr, expected, part);
    float* result = self->PartWorldPosition(out, part);
    for (int i = 0; i < 3; ++i)
        ei::verify::CheckNear("CObject3D::PartWorldPosition", expected[i], out[i], "object %d part %d [%d]: %g vs %g",
                              self->id_, part->id_, i, expected[i], out[i]);
    return result;
}
EI_VERIFY(0x005188B0, PartWorldPositionVerify, 8, s_partWorldPosition);

// --- The animation database -----------------------------------------------------------------

// 00518CA0: PUSH -1; PUSH 0x0071D758 (7 bytes).
static AnimationDatabase*(__fastcall* s_animations)(CObject3D*);
static AnimationDatabase* __fastcall AnimationsVerify(CObject3D* self) {
    if (!self->animations) {
        AnimationDatabase* loaded = self->Animations();
        ei::verify::Check("CObject3D::Animations (load)", Address(self->animations), Address(loaded), "object %d",
                          self->id_);
        return loaded;
    }
    AnimationDatabase* expected = s_animations(self);
    AnimationDatabase* actual = self->Animations();
    ei::verify::Check("CObject3D::Animations", Address(expected), Address(actual), "object %d", self->id_);
    return actual;
}
EI_VERIFY(0x00518CA0, AnimationsVerify, 7, s_animations);

// 00518F80 begins PUSH ESI; MOV ESI,ECX; CALL 00518CA0: no 5-byte trampoline. The original runs
// through this stub, which repeats them (the call through a register; it reaches the hooked
// 00518CA0) and jumps past the hook's 5 bytes to 00518F88.
__declspec(naked) static FigureAnimation* __fastcall OriginalAnimationAt(CObject3D*, void*, std::uint32_t) {
    __asm {
        push esi
        mov esi, ecx
        mov eax, 0x00518CA0
        call eax
        mov ecx, 0x00518F88
        jmp ecx
    }
}

static FigureAnimation* __fastcall AnimationAtVerify(CObject3D* self, void*, std::uint32_t key) {
    FigureAnimation* expected = OriginalAnimationAt(self, nullptr, key);
    FigureAnimation* actual = self->AnimationAt(key);
    ei::verify::Check("CObject3D::AnimationAt", Address(expected), Address(actual), "object %d key 0x%08X", self->id_,
                      key);
    return actual;
}
EI_VERIFY(0x00518F80, AnimationAtVerify, 0, s_unused);

// 00518FC0: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,ECX (6 bytes).
static FigureAnimation*(__fastcall* s_findSkillAnimation)(CObject3D*, void*, int);
static FigureAnimation* __fastcall FindSkillAnimationVerify(CObject3D* self, void*, int number) {
    FigureAnimation* expected = s_findSkillAnimation(self, nullptr, number);
    FigureAnimation* actual = self->FindSkillAnimation(number);
    ei::verify::Check("CObject3D::FindSkillAnimation", Address(expected), Address(actual), "object %d number %d",
                      self->id_, number);
    return actual;
}
EI_VERIFY(0x00518FC0, FindSkillAnimationVerify, 6, s_findSkillAnimation);

// 00518D30: PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI] (5 bytes).
static double(__fastcall* s_animationFactor)(CObject3D*, void*, float);
static double __fastcall AnimationFactorVerify(CObject3D* self, void*, float time) {
    double expected = s_animationFactor(self, nullptr, time);
    double actual = self->AnimationFactor(time);
    ei::verify::CheckNear("CObject3D::AnimationFactor", expected, actual, "object %d time %g: %g vs %g", self->id_,
                          time, expected, actual);
    return actual;
}
EI_VERIFY(0x00518D30, AnimationFactorVerify, 5, s_animationFactor);

// 00517990: PUSH EBX; PUSH ESI; PUSH EDI; MOV EDI,ECX (5 bytes).
static int(__fastcall* s_findPartIndex)(CObject3D*, void*, const char*);
static int __fastcall FindPartIndexVerify(CObject3D* self, void*, const char* partName) {
    int expected = s_findPartIndex(self, nullptr, partName);
    int actual = self->FindPartIndex(partName);
    ei::verify::Check("CObject3D::FindPartIndex", expected, actual, "object %d '%s'", self->id_,
                      partName ? partName : "");
    return actual;
}
EI_VERIFY(0x00517990, FindPartIndexVerify, 5, s_findPartIndex);
