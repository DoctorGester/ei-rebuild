#pragma once

// How the client dresses a unit: the figure parts of the armour it wears and the composed skin
// texture of party units. CUnitClientSpecific is the client-side controller of a party unit
// (GetClassName "CUnitClientSpecific", class id 0x3D, vtable 0073ECD8); names provisional. See
// docs/notes/units.md ("Fourth round").

#include "console.h"
#include "engine_object.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CUnit;
struct Vector3;   // maths.h

// The figure part names of the three armour groups (0073EC24: seven char[4] each): a body
// armour covers "bd" and the arm pieces, trousers "hp" and the legs, a helmet the hair "hr" and
// the head "hd" (the rest of that row is empty). Armour slots 0 / 1 / 2 use rows 2 / 0 / 1.
using PartNameRow = char[7][4];
inline const PartNameRow* ArmourPartNames() { return reinterpret_cast<const PartNameRow*>(0x0073EC24); }

// One layer of a composed skin (0xC bytes): what it is (0x32 the unit's own skin, 0x3005 armour,
// 0x3004 a weapon; 0 none), the packed item id or prototype index, and the armour slot / weapon
// type. MFC CArray of them (vtable 0073EEA0); its SetSize is 00539580.
struct SkinLayer {
    int kind;
    std::uint32_t id;
    int slot;
};
static_assert(sizeof(SkinLayer) == 0xC);

struct SkinLayerArray {
    void* vtable;
    SkinLayer* data;
    int size;
    int capacity;
    int growBy;
};
static_assert(sizeof(SkinLayerArray) == 0x14);

// What the texture manager composes a skin from (00693180): the layers as bytes in 32-byte
// aligned storage, the layer texture names, and a flag (0x100 detailed, 0x80 simple).
struct SkinRequest {
    std::uint8_t* layers;           // +0x00 aligned (the block is kept at layers[-4])
    int layerBytes;                 // +0x04
    int capacity;                   // +0x08
    int growBy;                     // +0x0C
    console::CStringArray textures; // +0x10
    int flags;                      // +0x24
};
static_assert(offsetof(SkinRequest, textures) == 0x10);
static_assert(offsetof(SkinRequest, flags) == 0x24);

class CUnitClientSpecific : public CEngineObject {
public:
    // 005381B0. Virtual slot 26: the unit puts on armour piece `itemId` (packed: prototype index
    // in the high half, number in the low half). For a body armour, trousers or helmet not
    // already worn, each part of its group is replaced by "<part>.armor<number>" when the unit's
    // figure has it, or kept / restored to the plain part (the hair keeps "hr.<hair>"). False
    // (AL) for an invalid id, an armour slot beyond 6 or an unknown figure; true otherwise.
    bool WearArmourModel(std::uint32_t itemId);

    // 005385A0. Virtual slot 27: the unit takes off the armour in armour slot `slot`. The slot is
    // emptied; for a body armour, trousers or helmet each part of its group is put back plain
    // (the hair as "hr.<hair>", skipped without hair): the figure's parts of that name are
    // removed and the plain part added, unless it is the only one and already plain. False (AL)
    // for a slot beyond 6, an empty or invalid id, or an unknown figure (the slot is emptied
    // then too); true otherwise.
    bool RemoveArmourModel(std::uint32_t slot);

    // 00535590. Virtual slot 25: the unit takes the weapon of weapon slot `slot` in hand. The
    // figure parts of the weapon in hand (weaponIds[unit->activeWeapon], 006A0FF0) are removed by
    // name (005168F0), the unit's active weapon becomes `slot` and the byte +0x74 is cleared; the
    // new weapon's parts (none for an empty slot or a slot beyond 3) are added from the unit's
    // model (00517240, each animated with the figure through 00518820), and for a bow (type 5) the
    // part "rh3_arrow00" gets flag 0x80, which +0x75 records (+0x75 is left alone without a new
    // weapon). Then slot 30. Returns 1.
    int ChangeWeaponModel(std::uint32_t slot);

    // 00536300. The skin layers: `detailed`: the unit's own skin (0x32) and its seven armour
    // slots; else two layers, armour slot 0 and the weapon in hand.
    void BuildSkinLayers(int detailed, SkinLayerArray* out);

    // 00536670. Composes the unit's skin texture through the texture manager (007AF744,
    // 00693180) from the skin layers and the textures "<prototype>skin_<nn>.mmp" and
    // "<prototype><armour or weapon texture>.mmp", named "mo<id>p" / "prot<prototype>p" (or
    // ...s when not `detailed`). Returns the texture.
    void* ComposeSkin(int detailed, void* target);

    // 00535770 (not rewritten; forwarded from unit_figure_queries.cpp, RET 4). The point the weapon
    // in hand acts from: the node named by the weapon's model (weaponIds[unit->activeWeapon], 006A0CC0;
    // "baserh3" without one) in the unit's figure animation (0050CC20 on the unit's +0x98), its +0x74
    // copied to `out` (relative to the unit, not turned by its orientation). False (AL) when the
    // animation has no such node (`out` untouched).
    bool WeaponPoint(Vector3* out);

    CUnit* unit;                    // +0x1C
    std::uint8_t unknown_020[0xA0 - 0x20];
    std::uint32_t armourIds[7];     // +0xA0 packed ids of the armour worn, -1 none
    std::uint32_t weaponIds[4];     // +0xBC
};
static_assert(offsetof(CUnitClientSpecific, armourIds) == 0xA0);
static_assert(offsetof(CUnitClientSpecific, weaponIds) == 0xBC);

struct AttackParams;   // combat.h

// 00538960 (unit_client_attack.cpp). Virtual slot 50 (thiscall, RET 4; registered as a free
// function, rewrite-guide rule 4): the attack figures the interface shows for a party unit. A
// text-key unit with a weapon in hand (weaponIds[activeWeapon]) takes damage, type, damage type
// and speed from the weapon and attack / defence from the weapon's +0x84 / +0x88 plus
// UnitParams::WeaponSkillBonus (truncated); otherwise they come from the packed parameters
// (CUnit + 0x330), with the weapon's type and damage type, or type 8 and the model's first
// weighted damage type. Bows and crossbows have no defence; the damage is scaled by the unit's
// effects (005285D0, max first), the attack spread is the parameters' byte, and 0052E0D0 scales
// attack and defence by the difficulty for units without a text key.
void __fastcall ClientGetAttackParams(CUnitClientSpecific* self, void* unused, AttackParams* out);

// 00535CB0 (__fastcall). The figure part names a unit of prototype `prototype` shows with the
// armour pieces `armourIds` (appended to `names`: "<part>.armor<number>", the hair as
// "hr.<hair>.armor<number>" unless the prototype has no hair, +0x10 = -1), the parts of the
// weapons `weaponIds` (006A1780, also into `names`), and every plain part of the three rows
// (into `plainNames`, the hair as "hr.<hair>"; the empty names of the helmet row too).
void __fastcall CollectArmourPartNames(const char* prototype, const stlp::vector<std::uint32_t>* armourIds,
                                       const stlp::vector<std::uint32_t>* weaponIds,
                                       stlp::vector<stlp::string>* names, stlp::vector<stlp::string>* plainNames);
