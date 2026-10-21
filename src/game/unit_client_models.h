#pragma once

// How the client dresses a unit: the figure parts of the armour it wears and the composed skin
// texture of party units. CUnitClientSpecific is the client-side controller of a party unit
// (GetClassName "CUnitClientSpecific", class id 0x3D, vtable 0073ECD8); names provisional. See
// docs/notes/units.md ("Creation, databases, save").

#include "console.h"
#include "engine_object.h"

#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CParticleSource;   // effects.h
class CTexture;          // texture.h
struct tagRECT;          // windows.h RECT
class CUnit;
struct AIPath;    // ai_map.h
struct FigureAnimation;   // map_object_figures.h
struct Vector3;   // maths.h

// unit_client_blink.cpp (A-U3). 00533980 (__fastcall, RET 8): blits rectangle `from` of `source`
// to `at` (x, y) of `target`, then the same halved on each next level of both mip chains until
// either ends (DirectDraw surfaces; nothing without them).
void __fastcall BlitMipChain(CTexture* target, const int* at, CTexture* source, const tagRECT* from);

// An entry of a client controller's speech queue (0x14 bytes; the value of a node of the STLport
// list at +0x9C; 005380D0 plays one).
// What a unit says (the record slot 51 is given; 0x28 bytes, an acks.db entry, acks_db.h): its
// sound file name at +0x04.
struct SpeechRecord {
    int chance;                     // +0x00 eligible when at least the roll (0..99, acks_db.cpp)
    stlp::string sound;             // +0x04 the sound file
    stlp::string text;              // +0x10
    stlp::string name;              // +0x1C named speeches (006D8A40)
};
static_assert(offsetof(SpeechRecord, name) == 0x1C);
static_assert(sizeof(SpeechRecord) == 0x28);

struct ClientSpeech {
    const SpeechRecord* id;         // +0x00 (005380D0 compares it with the last speech, +0x94)
    int kind;                       // +0x04 2 / 0x1C: not over a speech that still plays (+0x90)
    int delay;                      // +0x08 ticks until it plays; negative: playing
    int sound;                      // +0x0C its sound handle (-1 none)
    std::uint8_t talking;           // +0x10 the server unit has flag 0x800000 while it plays
    std::uint8_t unused_11[3];
};
static_assert(sizeof(ClientSpeech) == 0x14);

// What the speech queue's filters compare (slot 51 builds one for each walk; unit_client_speech.cpp).
struct SpeechFilter {
    // 00537F60. Does a speech of the filter's kind replace `queued`? (EAX 1 / 0; RET 4.)
    int Replaced(const ClientSpeech* queued) const;
    // 00537FD0. Has `queued`'s sound ended (it had one)? A talking speech then clears the server
    // unit's flag 0x800000 (the filter's unit id). AL; RET 4.
    bool Finished(const ClientSpeech* queued) const;
    int unitId;                     // +0x00 SpeechFinished: the unit whose talking flag ends
    int kind;                       // +0x04 SpeechReplaced: the new speech's kind
};

struct ClientSpeechNode {
    ClientSpeechNode* next;
    ClientSpeechNode* prev;
    ClientSpeech value;             // +0x08
};
static_assert(sizeof(ClientSpeechNode) == 0x1C);

// The figure part names of the three armour groups (seven char[4] each): a body
// armour covers "bd" and the arm pieces, trousers "hp" and the legs, a helmet the hair "hr" and
// the head "hd" (the rest of that row is empty). Armour slots 0 / 1 / 2 use rows 2 / 0 / 1.
inline constexpr char kArmourPartNames[3][7][4] = /*0073EC24*/ {
    {"bd", "rh1", "rh2", "rh3", "lh1", "lh2", "lh3"},
    {"hp", "rl1", "rl2", "rl3", "ll1", "ll2", "ll3"},
    {"hr", "hd", "", "", "", "", ""},
};

// One layer of a composed skin (0xC bytes): what it is (0x32 the unit's own skin, 0x3005 armour,
// 0x3004 a weapon; 0 none), the packed item id or prototype index, and the armour slot / weapon
// type. MFC CArray of them (vtable 0073EEA0); its SetSize is 00539580.
struct SkinLayer {
    int kind;
    std::uint32_t id;
    int slot;
};
static_assert(sizeof(SkinLayer) == 0xC);

using SkinLayerArray = mfc::CArray<SkinLayer>;

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

// 00693180 (thiscall on the texture manager, RET 0x1C; texture_compose.cpp): the skin `request`
// composed into the cached texture `name` (or `fallback`), its name into `out`; 1 / 0.
int __fastcall ComposeTexture(void* manager, void*, mfc::CString* out, const SkinRequest* request,
                              const mfc::CString* name, const mfc::CString* fallback, unsigned mipCount,
                              std::uint32_t fill, std::uint32_t format);

struct AttackParams;   // combat.h

class CUnitClientSpecific : public CEngineObject {
public:
    // 00535850 (unit_client_path.cpp; RET 8). Places the unit on its path at `time`, turning by `rate`.
    void PlaceOnPath(float time, float rate);
    // 00538960 (unit_client_attack.cpp). Virtual slot 50 (thiscall, RET 4; registered as a free
    // function, rewrite-guide rule 4): the attack figures the interface shows for a party unit. A
    // text-key unit with a weapon in hand (weaponIds[activeWeapon]) takes damage, type, damage type
    // and speed from the weapon and attack / defence from the weapon's +0x84 / +0x88 plus
    // UnitParams::WeaponSkillBonus (truncated); otherwise they come from the packed parameters
    // (CUnit + 0x330), with the weapon's type and damage type, or type 8 and the model's first
    // weighted damage type. Bows and crossbows have no defence; the damage is scaled by the unit's
    // effects (005285D0, max first), the attack spread is the parameters' byte, and 0052E0D0 scales
    // attack and defence by the difficulty for units without a text key.
    void GetAttackParams(AttackParams* out);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073ECD8);
    // The controller base class's vtable (the destructor body ends with it) and the list +0x58's.
    static constexpr std::uintptr_t kBaseVtableAddress = std::uintptr_t(0x0073EDBC);
    static constexpr std::uintptr_t kList58VtableAddress = std::uintptr_t(0x0073EDB0);

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
    // part "rh3.arrow00" gets flag 0x80, which +0x75 records (+0x75 is left alone without a new
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

    // 00535770 (unit_client_wounds.cpp, A-U4; RET 4). The point the weapon
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
    // unit_client_blink.cpp (A-U3). 00538ED0 / 00539010 (RET 4): the blink. With a face texture
    // (+0xCC) and a skin texture (the client half's material), unless the eyes already are so
    // (`force` copies anyway), the face's closed / open eyes are copied into the skin's eye
    // rectangle (the model's +0x158..+0x164 in 256ths of the skin, 00533980 down the mip chain);
    // eyesClosed becomes 1 / 0.
    void BlinkClose(bool force);
    void BlinkOpen(bool force);
    // 00538DB0 (unit_client_wounds.cpp, A-U4). The face texture the blink uses (+0xCC), loaded
    // once: the texture manager's texture of kind 0x3D named "<model>mouth_<skin, 2 digits>"
    // (heroes, the prototype's +0x0C) or "<model>mouth", loaded (006922A0); one that did not load
    // (flags_ bit 8 clear) is dropped and blinking stops. False (AL) without a prototype or a
    // loaded face.
    bool LoadFaceTexture();
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
    // 00538C20. For heroes with a bow in hand: the arrow part "rh3.arrow00" shows (flags_ 0x80
    // cleared, +0x75 = 0) when the animation (flag 1 in its +0x18) passes its frame +0x28, and
    // is hidden again (+0x75 = 1) outside the window up to frame +0x2C, or always for animations
    // without the flag.
    void UpdateArrow();

    // unit_client_animation.cpp (A-U2).
    // 00535B30. Virtual slot 40: the unit moves on to world time `time`. Before the last time (or
    // NaN) it is only put back on the ground (+0x320 above, the world's MoveObject); otherwise the
    // step (more than 500 becomes 500..600, one IsaacRandom draw) runs the invisible / stunned
    // pulses, places the unit on its path (00535850) unless +0x260 (then the animation speed is
    // 1), and advances the animation by the step (times the animation speed when +0x25C is 2;
    // not when +0x25C is 1).
    void AdvanceTime(float time);
    // 005352B0. Virtual slot 14: unless the unit is frozen (flags_ 0x10000000) the animation queue
    // is emptied and the unit's animations start again: the special idle (StartIdleAnimation(1))
    // for +0x255 == -2, else the entries +0x254 (played) and +0x255 (queued behind it, or played
    // alone); then the animation is advanced to `time` + 1 - +0x258 when that is above 0 (+0x2C /
    // +0x30 take the state), the last time becoming +0x258 plus it, else +0x258.
    void RestartAnimation(float time);
    // 00535480. Unless frozen, finds an idle animation (CUnit::FindAnimation with any of the kind)
    // for the weapon in hand (bit of its prototype's type; no bit for an unknown id) or the natural
    // weapon (+0x34F, when below 7), with 0x10000 for non-heroes and 0x80580000 for +0x255 == -2;
    // plays it (`play`) or queues it.
    void StartIdleAnimation(std::uint8_t play);

    // unit_client_effects.cpp (A-U2).
    // 00537500. Virtual slot 46, a hit on body location `location` (0 head .. 5 right leg) of
    // strength `amount`; nothing for flag 8 or an amount up to 0.0001. The hit sound
    // "weapons\<class>\<material>\1.wav" (class by the attacker's weapon, UnitParams +0x20: a
    // kind below 0x10, else the prototype's hitSoundKind; material: the model's hitMaterial, for
    // heroes the first armour worn over the location (0073EC78) as "stone" (crystal, bone, stone),
    // "Metal" or "leather") plays at 1000 from the unit; for a location's part ("hd", "bd", "lh2",
    // "rh2", "ll2", "rl2") of loaded figures, blood particles of the model's bloodKind (0x200F,
    // 0x2031..0x2033) attach to the unit, and blood kinds 1..4 leave a decal near the part (two
    // rand() offsets within the unit's size, a rand() variant; 0056CE90).
    void HitEffect(int location, float amount, std::uint32_t flags);
    // 00537C00. Virtual slot 47 (the argument is not read): sparks (particles 0x2043, 10 alpha)
    // attach to the unit, sized by its radius; the parts flash (0051C9F0 with 1) unless they are
    // already, and the flash lasts at least 4 more ticks.
    void SparkEffect(int unused);
    // 005373D0. Virtual slot 48 (death): the companion's light goes out (005119B0); blood kinds
    // 1..4 leave a decal under the body part "bd" (or the unit, sized 0.75 x its smaller extent).
    void DeathEffect();
    // 00534140. Virtual slot 18 (the unit's player changed): with a player the companion shows the
    // party light (colour 0079BEA0, alpha 1, radius 0079BEAC; 00511830).
    void OwnerPlayerChanged(void* player);

    // unit_client_speech.cpp (A-U2).
    // 00537D80. Virtual slot 51: the unit says `speech` (nothing for null), a speech of `kind`.
    // Queued speeches the new kind replaces (SpeechReplaced) and finished ones (SpeechFinished)
    // leave the queue; unless the queue is empty or the kind is one of 0x1D-0x1F, 0x23-0x26, 0x2A
    // nothing more happens. The speech (delay 3; `talking` for kind 0x2B) joins the queue's end;
    // into an empty queue it plays at once (delay 0; a talking one sets the server unit's flag
    // 0x800000; kinds 2 / 0x1C only when the last waiting speech sound has ended, and their
    // sound becomes +0x90).
    void Say(const SpeechRecord* speech, int kind);
    // 00538040. Virtual slot 52: every queued speech's sound stops (talking ones clear the server
    // unit's flag 0x800000) and the queue is emptied.
    void StopSpeech();
    // 005380D0. Plays `speech`'s sound (2D, priority 1, volume 100, as speech) into its +0x0C and
    // remembers it with the time. False (AL) when the same speech played less than 5 s ago.
    bool Speak(ClientSpeech* speech);

    // unit_client_lifetime.cpp (A-U2).
    // 00533FE0. Virtual slot 2 (Create): no sources, times, animation or motion order (AIPath::Reset
    // on +0x38), weapon / arrow shown, +0x76 = 1 (snap the heading), fully faded in, no sounds,
    // speech, flash, footprint or blink, every armour and weapon model -1, the face texture
    // released. Returns 1.
    int CreateClient();
    // 00533E80. Virtual slot 0, the deleting destructor: the destructor body (00533EA0), then the
    // memory freed for bit 0 of `flags`. Returns this.
    CUnitClientSpecific* DeleteClient(std::uint32_t flags);
    // 00533EA0 (unit_client_lifetime.cpp, A-U4). The destructor body: the three particle sources
    // stop and are released, the face texture released (no blink), the speech queue's nodes and
    // header freed, the MFC list +0x58 emptied, then the base (vtable 0073EDBC, no unit) and
    // CEngineObject's destructor body.
    void DestructClientBody();
    // 00533AD0 (unit_client_lifetime.cpp, A-U4). The constructor: the engine object's fields, no
    // unit, the list +0x58 (vtable 0073EDB0, block size 0x80), an empty speech queue (its header
    // node allocated), no face texture, fully faded in, the vtable 0073ECD8. Returns this.
    CUnitClientSpecific* ConstructClient();
    // 005340C0. Virtual slot 3 (Destroy): the three particle sources stop (CParticleSource::Stop)
    // and are released, the face texture released; no blink.
    void DestroyClient();

    // object_small_slots.cpp (A-U2). 005391B0, virtual slot 36: 1 when a weapon model is held
    // (any weapon id not -1). 005391E0, slot 37: 1 when one of the first three armour slots (body,
    // trousers, helmet) holds a model.
    int HasWeaponModels();
    int HasArmourModels();

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

    // unit_client_wounds.cpp (A-U4).
    // 00536EA0 (RET 4). The wound level of each of the six body parts (head, body, hands, legs)
    // into (*levels)[0..5]: 0 for a part without state or maximum, at full durability, or at 75%
    // or more; 1 above 33%, 2 above one point, 3 at one point or less. Heroes (type_ 0x32) show
    // none under armour whose record hides them (ArmorRecord::showsWounds; trousers: legs, body
    // armour: body and hands, helmet: head); others show each pair of hands / legs at the worse
    // level. True (AL) when any part was wounded before the armour test.
    bool WoundLevels(std::uint8_t** levels);
    // 005370F0 (plain RET). The unit's wounds texture: with any wound, a skin request of the six
    // levels and the textures "<model><part>w<level>.mmp" of the wounded parts (128 pixels for
    // heroes, else 64) is composed by the texture manager (00693180) as "mo<id>w". Returns its
    // result, or 0 without wounds.
    int ComposeWoundsTexture();
    // unit_controller_slots.cpp (A-U12).
    // 00535240 (RET 8; the path marker message). The two marker sources (sources[1], sources[2])
    // stopped (00485380), released and forgotten, then `path` and `marker` kept in their place
    // (each referenced; null: none).
    void SetMarkers(CParticleSource* path, CParticleSource* marker);
    // 00538EA0 (the client world's RemoveObject). The face texture forgotten, then released; no
    // blinks.
    void DropFaceTexture();

    AIPath* MotionPath() { return reinterpret_cast<AIPath*>(motionOrder); }

    CUnit* unit;                    // +0x1C
    CParticleSource* sources[3];    // +0x20 particle sources the controller keeps (counted)
    float lastAnimationTime;        // +0x2C the animation state's time (+0x60) at the last update
    const FigureAnimation* lastAnimation;   // +0x30 the animation at the last update
    float lastTime;                 // +0x34 the world time the unit was last advanced to (slot 40)
    std::uint8_t motionOrder[0x20]; // +0x38 the order part of a path (AIPath; slot 16): MotionOrder()
    mfc::CTypedPtrList<void> list58;        // +0x58 an MFC list (vtable 0073EDB0, block size 0x80)
    std::uint8_t weaponHidden;      // +0x74 ShowWeaponParts' state (cleared by a weapon change)
    std::uint8_t arrowHidden;       // +0x75 the bow's arrow part is flagged hidden
    std::uint8_t snapHeading;       // +0x76 the next path placement turns the unit to the path at once (00535850)
    std::uint8_t unused_077;
    float fadeIn;                   // +0x78 1 when fully shown; the drawn parts take it as alpha
    int flashCountdown;             // +0x7C the drawn parts get their colours back (0051C9F0 with 0) at 0
    int footprint;                  // +0x80 the last footprint (0056D150), -1 none
    int footprintRepeats;           // +0x84 further steps that may reuse it
    int idleSound;                  // +0x88 the idle animation's sound handle
    int otherSound;                     // +0x8C a sound handle, -1 after Create; nothing reads it
    int speechSound;                // +0x90 the sound of the last speech that waits (-1 none)
    const SpeechRecord* lastSpeech; // +0x94 the speech played last (005380D0)
    std::uint32_t lastSpeechTime;   // +0x98 when (timeGetTime)
    ClientSpeechNode* speech;       // +0x9C the speech queue (STLport list header node)
    std::uint32_t armourIds[7];     // +0xA0 packed ids of the armour worn, -1 none
    std::uint32_t weaponIds[4];     // +0xBC
    CEngineObject* faceTexture;     // +0xCC the face texture the blink changes (counted)
    std::uint8_t blinks;            // +0xD0
    std::uint8_t unused_0D1[3];
    int blinkTimer;                 // +0xD4 ticks to the next blink change
    int eyesClosed;                 // +0xD8 00538ED0 sets it, 00539010 clears it
};
static_assert(offsetof(CUnitClientSpecific, lastAnimationTime) == 0x2C);
static_assert(offsetof(CUnitClientSpecific, lastAnimation) == 0x30);
static_assert(offsetof(CUnitClientSpecific, lastTime) == 0x34);
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
static_assert(sizeof(CUnitClientSpecific) == 0xDC);

// 00535CB0 (__fastcall). The figure part names a unit of prototype `prototype` shows with the
// armour pieces `armourIds` (appended to `names`: "<part>.armor<number>", the hair as
// "hr.<hair>.armor<number>" unless the prototype has no hair, +0x10 = -1), the parts of the
// weapons `weaponIds` (006A1780, also into `names`), and every plain part of the three rows
// (into `plainNames`, the hair as "hr.<hair>"; the empty names of the helmet row too).

void __fastcall CollectArmourPartNames(const char* prototype, const stlp::vector<std::uint32_t>* armourIds,
                                       const stlp::vector<std::uint32_t>* weaponIds,
                                       stlp::vector<stlp::string>* names, stlp::vector<stlp::string>* plainNames);
