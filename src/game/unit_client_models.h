#pragma once

// How the client dresses a unit: the figure parts of the armour it wears and the composed skin
// texture of party units. CUnitClientSpecific is the client-side controller of a party unit
// (GetClassName "CUnitClientSpecific", class id 0x3D, vtable 0073ECD8); names provisional. See
// docs/notes/units.md ("Fourth round").

#include "console.h"
#include "engine_object.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CUnit;
struct AIPath;    // ai_map.h
struct FigureAnimation;   // map_object_figures.h
struct Vector3;   // maths.h

// An entry of a client controller's speech queue (0x14 bytes; the value of a node of the STLport
// list at +0x9C; 005380D0 plays one).
struct ClientSpeech {
    int id;                         // +0x00 (005380D0 compares it with the last speech, +0x94)
    int kind;                       // +0x04 2 / 0x1C: not over a speech that still plays (+0x90)
    int delay;                      // +0x08 ticks until it plays; negative: playing
    int sound;                      // +0x0C its sound handle (-1 none)
    std::uint8_t talking;           // +0x10 the server unit has flag 0x800000 while it plays
    std::uint8_t unknown_11[3];
};
static_assert(sizeof(ClientSpeech) == 0x14);

struct ClientSpeechNode {
    ClientSpeechNode* next;
    ClientSpeechNode* prev;
    ClientSpeech value;             // +0x08
};
static_assert(sizeof(ClientSpeechNode) == 0x1C);

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

    // unit_client_update.cpp (A-U).
    // 005341A0. Virtual slot 6, the per-tick update on the client: the motion order's times
    // shift by one tick while +0x260 is 1, +0x258 counts while +0x25C is 1, the weapon in hand is
    // hidden during animations whose key has no low 15 bits (ShowWeaponParts); flags_ 8 (not
    // drawn) for a unit dead for more than 50 ticks past +0x254 or without an animation, or in
    // one of movement bits 0x30000 that is not a mode transition (0x240000); the fade-in
    // (+0x78) goes to the drawn parts and grows by 1/15 up to 1; the colour flash countdown (+0x7C);
    // the blink (+0xD0: every 2..9 ticks 00538ED0 or 00539010); the bow's arrow (00538C20);
    // the step sounds (00534B60); the speech queue (+0x9C); for +0x255 == -2 with animation
    // state +0x50 below 3, 00535480(0). Returns 1.
    int UpdateClient();
    // 005344E0. Virtual slot 13 (figures loaded): the drawn parts take the fade-in as alpha.
    // Returns 1.
    int FiguresLoaded();
    // 005344A0. Virtual slot 43: the fade-in starts again from 0 (the drawn parts take it). Returns
    // what the original leaves in EAX (0, the walk's end).
    int RestartFadeIn();
    // 00539160. Virtual slot 24: the level from the packed experience (UnitParams +0x04, CUnit +0x334):
    // ln(experience / 007C5488) / ln(007C548C) stored as a float and rounded to nearest.
    int Level();
    // 00537360. Virtual slot 31 (statistics changed): when the wounds texture is needed (005370F0)
    // the client half shows "mo<unit id>w" (0051C3D0), else none (0051C920). Returns 1.
    int RefreshWounds();
    // 00537CB0. Hides (`hidden`) or shows the figure parts of the weapon in hand (flags_ 0x80;
    // its part names from 006A0FF0, the parts found by FindPart) unless +0x74 already says so;
    // +0x74 and +0x75 take the new state.
    void ShowWeaponParts(bool hidden);
    // 00538C20. For heroes with a bow in hand: the arrow part "rh3_arrow00" shows (flags_ 0x80
    // cleared, +0x75 = 0) when the animation (flag 1 in its +0x18) passes its frame +0x28, and
    // is hidden again (+0x75 = 1) outside the window up to frame +0x2C, or always for animations
    // without the flag.
    void UpdateArrow();

    // unit_client_equipment.cpp (A-U).
    // 00536100. Virtual slot 29 (equipment changed): for heroes the composed skins are renewed:
    // the detailed one (ComposeSkin(1)) into the first part set's texture (client half +0x20, read
    // unchecked) and the simple one (ComposeSkin(0)) into the second's (+0x24, skipped when
    // missing). A texture takes the new name and is reloaded (flags_ 8 cleared, its manager's
    // LoadTexture) when ComposeSkin returned non-zero or the name differs. Returns 1.
    int RefreshSkins();
    // 00536220. Virtual slot 30 (weapon changed): the simple skin only. Returns 1.
    int RefreshSimpleSkin();
    // 00538140. Virtual slot 32: every armour slot taken off (slot 27), the ids cleared, then the
    // first min(count, 7) of `ids` put on (slot 26).
    void SetArmourModels(const std::uint32_t* ids, int count);
    // 00538860. Virtual slot 33: the weapon ids cleared, then the first min(count, 4) of `ids`
    // added (slot 34).
    void SetWeaponModels(const std::uint32_t* ids, int count);
    // 005388C0. Virtual slot 34: `id` (packed; neither half 0xFFFF) goes to the first free weapon
    // slot. False (AL) for an invalid id or no free slot.
    bool AddWeaponModel(std::uint32_t id);
    // 00538920. Virtual slot 35: the weapon slot holding `id` is emptied. False (AL) when none does.
    bool RemoveWeaponModel(std::uint32_t id);

    // unit_client_sounds.cpp (A-U).
    // 00534B60. The sounds of the animation playing (state +0x40) between the last update's frame
    // (+0x2C; 0 when the animation changed) and now: attacks (stop the idle sound; at the action's
    // moment the prototype's +0x30 shakes the camera; heroes with +0x256 play the miss sound at
    // 1000) and deaths (stop the idle and speech sounds) and skills play ActionSound at priority
    // 1000, idles and hits at 0; walk / run (0x100000 / 0x140000) at a step frame play the footstep
    // of the ground (unless silenced, effect 0x10) and, when not crawling, shake by the prototype's
    // +0x30 and leave a footprint (model +0x10C / +0x110, part "ll<n>" / "rl<n>" by the step and
    // the model's +0x11C) through the client world (0056D150). An attack's sound waits for the
    // speech sound (+0x90) and becomes it; an idle's becomes +0x88. Then +0x30 / +0x2C take the
    // animation and frame.
    void PlayStepSounds();
    // 00534590. The footstep sound on ground kind `terrain` into `out` (CString::Format), for units
    // whose prototype's model has sounds for it (+0x144 counts): on a platform 0.01 or more away
    // from the ground the kind is 2; crawling (movement mode 0) except on kinds 6 / 14 gives
    // "steps\human\crawl\<1..6>.wav", else "<the kind's name (+0x138)>\<1..count>.wav" (one rand()
    // each). Returns 1, or 0 (no prototype, model or sounds; `out` untouched).
    int FootstepSound(int terrain, mfc::CString* out);
    // 00534760. The sound of `animation`'s action into `out` (emptied first) when its sound frame
    // (+0x44, not 0) lies in [from, to): "weapons\miss.wav" for `miss`, else one of its sound
    // numbers (+0x48, rand()) in the folder of the second prototype (+0x34) or its model (+0x124):
    // idle (kind 0x180000, by the model's idle chance), skill sounds (0x40000: tame, steal, science,
    // loot), attack (0x80000: "weapons\<n>.wav" for a hero with a bow or crossbow, else by the
    // attack chance), death (0x1C0000), hit (0x200000). Returns 1 for those kinds (also when a
    // chance left `out` empty), else 0.
    int ActionSound(const FigureAnimation* animation, float from, float to, mfc::CString* out, std::uint8_t miss);

    AIPath* MotionPath() { return reinterpret_cast<AIPath*>(motionOrder); }

    CUnit* unit;                    // +0x1C
    std::uint8_t unknown_020[0x2C - 0x20];
    float lastAnimationTime;        // +0x2C the animation state's time (+0x60) at the last update
    const FigureAnimation* lastAnimation;   // +0x30 the animation at the last update
    std::uint8_t unknown_034[4];
    std::uint8_t motionOrder[0x20]; // +0x38 the order part of a path (AIPath; slot 16): MotionOrder()
    std::uint8_t unknown_058[0x74 - 0x58];   // +0x58 an MFC list (vtable 0073EDB0, block size 0x80)
    std::uint8_t weaponHidden;      // +0x74 ShowWeaponParts' state (cleared by a weapon change)
    std::uint8_t arrowHidden;       // +0x75 the bow's arrow part is flagged hidden
    std::uint8_t unknown_076[2];
    float fadeIn;                   // +0x78 1 when fully shown; the drawn parts take it as alpha
    int flashCountdown;             // +0x7C the drawn parts get their colours back (0051C9F0 with 0) at 0
    int footprint;                  // +0x80 the last footprint (0056D150), -1 none
    int footprintRepeats;           // +0x84 further steps that may reuse it
    int idleSound;                  // +0x88 the idle animation's sound handle
    std::uint8_t unknown_08C[4];
    int speechSound;                // +0x90 the sound of the last speech that waits (-1 none)
    std::uint8_t unknown_094[0x9C - 0x94];   // +0x94 last speech id, +0x98 its time (005380D0)
    ClientSpeechNode* speech;       // +0x9C the speech queue (STLport list header node)
    std::uint32_t armourIds[7];     // +0xA0 packed ids of the armour worn, -1 none
    std::uint32_t weaponIds[4];     // +0xBC
    std::uint8_t unknown_0CC[0xD0 - 0xCC];   // +0xCC the face texture the blink changes
    std::uint8_t blinks;            // +0xD0
    std::uint8_t unknown_0D1[3];
    int blinkTimer;                 // +0xD4 ticks to the next blink change
    int eyesClosed;                 // +0xD8 00538ED0 sets it, 00539010 clears it
};
static_assert(offsetof(CUnitClientSpecific, lastAnimationTime) == 0x2C);
static_assert(offsetof(CUnitClientSpecific, lastAnimation) == 0x30);
static_assert(offsetof(CUnitClientSpecific, footprint) == 0x80);
static_assert(offsetof(CUnitClientSpecific, motionOrder) == 0x38);
static_assert(offsetof(CUnitClientSpecific, weaponHidden) == 0x74);
static_assert(offsetof(CUnitClientSpecific, fadeIn) == 0x78);
static_assert(offsetof(CUnitClientSpecific, speechSound) == 0x90);
static_assert(offsetof(CUnitClientSpecific, speech) == 0x9C);
static_assert(offsetof(CUnitClientSpecific, armourIds) == 0xA0);
static_assert(offsetof(CUnitClientSpecific, weaponIds) == 0xBC);
static_assert(offsetof(CUnitClientSpecific, blinks) == 0xD0);
static_assert(offsetof(CUnitClientSpecific, eyesClosed) == 0xD8);

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
