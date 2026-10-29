#pragma once

// How the client dresses a unit: the figure parts of the armour it wears and the composed skin texture of
// party units. CUnitClientSpecific is the client-side controller of a party unit (class id 0x3D). See
// docs/notes/units.md ("Creation, databases, save").

#include "ai_map.h"
#include "engine_object.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

class CParticleSource;
class CTexture;
struct tagRECT;
struct UnitPrototypeRecord;

// Blits rectangle `from` of `source` to `at` (x, y) of `target`, then the same halved on each next level of
// both mip chains until either ends (DirectDraw surfaces; nothing without them).
void BlitMipChain(CTexture* target, const int* at, CTexture* source, const tagRECT* from);

// What a unit says: an acks.db entry with its sound file name.
struct SpeechRecord {
    int chance;                     // eligible when at least the roll (0..99)
    std::string sound;              // the sound file
    std::string text;
    std::string name;               // named speeches
};

struct ClientSpeech {
    const SpeechRecord* id;         // (compared with lastSpeech)
    AckKind kind;                   // Attack / DecidedToAttack: not over a speech that still plays
    int delay;                      // ticks until it plays; negative: playing
    int sound;                      // its sound handle (-1 none)
    std::uint8_t talking;           // the server unit has flag 0x800000 while it plays
};

// What the speech queue's filters compare.
struct SpeechFilter {
    bool Replaced(const ClientSpeech* queued) const;
    // Has `queued`'s sound ended (it had one)? A talking speech then clears the server unit's flag 0x800000.
    bool Finished(const ClientSpeech* queued) const;
    int unitId;                     // SpeechFinished: the unit whose talking flag ends
    AckKind kind;                   // SpeechReplaced: the new speech's kind
};

// The figure part names of the three armour groups: a body armour covers "bd" and the arm pieces, trousers
// "hp" and the legs, a helmet the hair "hr" and the head "hd" (the rest of that row is empty). Armour slots 0
// / 1 / 2 use rows 2 / 0 / 1.
inline constexpr char kArmourPartNames[3][7][4] = /*0073EC24*/ {
    {"bd", "rh1", "rh2", "rh3", "lh1", "lh2", "lh3"},
    {"hp", "rl1", "rl2", "rl3", "ll1", "ll2", "ll3"},
    {"hr", "hd", "", "", "", "", ""},
};

// One layer of a composed skin: what it is (0x32 the unit's own skin, ItemKind::Armor armour, ItemKind::Weapon a weapon; 0
// none), the packed item id or prototype index, and the armour slot / weapon type.
struct SkinLayer {
    int kind = 0;
    std::uint32_t id = 0xFFFFFFFFu;
    int slot = -1;
};

// What the texture manager composes a skin from: the layers as bytes in 32-byte aligned storage, the layer
// texture names, and a flag (0x100 detailed, 0x80 simple).
struct SkinRequest {
    std::vector<std::uint8_t> layers;
    std::vector<std::string> textures;
    int flags;
};

struct AttackParams;

class CUnitClientSpecific : public CUnitController {
public:
    CUnitClientSpecific();
    ~CUnitClientSpecific() override;

    // No sources, times, animation or motion order, weapon / arrow shown, heading snapped, fully faded in, no sounds,
    // speech, flash, footprint or blink, every armour and weapon model -1, the face texture released. Returns 1.
    void Create() override;
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    PathOrder* Path() override { return &motion; }
    void Unused17() override {}

    void PlaceOnPath(float time, float rate);
    // The attack figures the interface shows for a party unit: a text-key unit with a weapon in hand takes damage,
    // type, damage type and speed from the weapon and attack / defence from the weapon plus
    // UnitParams::WeaponSkillBonus (truncated); otherwise they come from the packed parameters, with the weapon's
    // type and damage type, or type 8 and the model's first weighted damage type. Bows and crossbows have no defence;
    // the damage is scaled by the unit's effects (max first), the attack spread is the parameters' byte; attack and
    // defence are scaled by the difficulty for units without a text key.
    void GetAttackParams(AttackParams* out) override;

    // The unit puts on armour piece `itemId` (packed: prototype index in the high half, number in the low half). For
    // a body armour, trousers or helmet not already worn, each part of its group is replaced by
    // "<part>.armor<number>" when the unit's figure has it, or kept / restored to the plain part (the hair keeps
    // "hr.<hair>"). False for an invalid id, an armour slot beyond 6 or an unknown figure.
    bool ArmourPutOn(std::uint32_t itemId) override;

    // The unit takes off the armour in armour slot `slot`: the slot is emptied; for a body armour, trousers or helmet
    // each part of its group is put back plain (the hair as "hr.<hair>", skipped without hair), unless it is the only
    // one and already plain. False for a slot beyond 6, an empty or invalid id, or an unknown figure (the slot is
    // emptied then too).
    bool ArmourTakenOff(std::uint32_t slot) override;

    // The unit takes the weapon of weapon slot `slot` in hand: the parts of the weapon in hand are removed by name,
    // the active weapon becomes `slot`, the new weapon's parts (none for an empty slot or a slot beyond 3) are added
    // from the unit's model, and for a bow (type 5) the part "rh3.arrow00" gets flag 0x80. Then RefreshModel. Returns
    // 1.
    int ShowWeapon(std::uint32_t slot) override;

    // The skin layers: `detailed`: the unit's own skin (0x32) and its seven armour slots; else two layers,
    // armour slot 0 and the weapon in hand.
    void BuildSkinLayers(int detailed, std::vector<SkinLayer>* out);

    // Composes the unit's skin texture through the texture manager from the skin layers and the textures
    // "<prototype>skin_<nn>.mmp" and "<prototype><armour or weapon texture>.mmp", named "mo<id>p" /
    // "prot<prototype>p" (shorter when not `detailed`). Returns the texture.
    void* ComposeSkin(int detailed, void* target);

    // The point the weapon in hand acts from: the node named by the weapon's model ("baserh3" without one) in the
    // unit's figure animation, copied to `out` (relative to the unit, not turned by its orientation). False when the
    // animation has no such node.
    bool WeaponPoint(Vector3* out);

    // The per-tick update on the client; see docs/notes/units.md, "CUnitClientModels per-tick update". Returns 1.
    bool RunTick() override;
    // The blink. With a face texture and a skin texture (the client half's material), unless the eyes already
    // are so (`force` copies anyway), the face's closed / open eyes are copied into the skin's eye rectangle
    // (the model's eye fields in 256ths of the skin, down the mip chain); eyesClosed becomes 1 / 0.
    void BlinkClose(bool force);
    void BlinkOpen(bool force);
    // The face texture the blink uses, loaded once: the texture manager's texture of kind 0x3D named
    // "<model>mouth_<skin, 2 digits>" (heroes) or "<model>mouth"; one that did not load (flags_ bit 8 clear)
    // is dropped and blinking stops. False without a prototype or a loaded face.
    bool LoadFaceTexture();
    int FiguresLoaded() override;
    int RestartFadeIn() override;
    // The level from the packed experience: ln(experience / considerModifier) / ln(considerLogBase) stored as
    // a float and rounded to nearest (0x80000000 for experience 0).
    int Level() override;
    // When the wounds texture is needed the client half shows "mo<unit id>w", else none. Returns 1.
    int RefreshWounds() override;
    // Hides (`hidden`) or shows the figure parts of the weapon in hand (flags_ 0x80) unless weaponHidden
    // already says so; weaponHidden and arrowHidden take the new state.
    void ShowWeaponParts(bool hidden);
    // For heroes with a bow in hand: the arrow part "rh3.arrow00" shows when the animation (flag 1) passes
    // its arrowFrom frame, and is hidden again outside the window up to arrowTo, or always for animations
    // without the flag.
    void UpdateArrow();

    // The unit moves on to world time `time`. Before the last time (or NaN) it is only put back on the ground
    // (plus heightOffset, the world's MoveObject); otherwise the step (more than 500 becomes 500..600, one
    // IsaacRandom draw) runs the invisible / stunned pulses, places the unit on its path unless pathTimeShift
    // (then the animation speed is 1), and advances the animation by the step (times the animation speed in
    // motion mode 2; not in mode 1).
    void AdvanceTime(float time) override;
    // Unless the unit is frozen (flags_ 0x10000000) the animation queue is emptied and the unit's animations
    // start again: the special idle (StartIdleAnimation(1)) for secondEntry -2, else animationEntry (played)
    // and secondEntry (queued behind it, or played alone); then the animation is advanced to `time` + 1 -
    // animationStart when that is above 0, the last time becoming animationStart plus it.
    void RestartAnimation(float time) override;
    // Unless frozen, finds an idle animation (CUnit::FindAnimation with any of the kind) for the weapon in
    // hand (bit of its prototype's type) or the natural weapon (when below 7), with 0x10000 for non-heroes
    // and 0x80580000 for secondEntry -2; plays it (`play`) or queues it.
    void StartIdleAnimation(std::uint8_t play);

    // A hit on body location `location` (0 head .. 5 right leg) of strength `amount`; the hit sound and blood
    // particles; see docs/notes/units.md, "StatChanged on the client".
    void StatChanged(int location, float amount, std::uint32_t flags) override;
    // Sparks (particles 0x2043, 10 alpha) attach to the unit, sized by its radius; the parts flash unless
    // they are already, and the flash lasts at least 4 more ticks.
    void Wound() override;
    // The companion's light goes out; blood kinds 1..4 leave a decal under the body part "bd" (or the unit,
    // sized 0.75 x its smaller extent).
    void Died() override;
    // With a player the companion shows the party light (colour, alpha 1, radius).
    void PlayerChanged(void* player) override;
    void MotionChanged() override;

    // The unit says `speech` (nothing for null), a speech of `kind`. Queued speeches the new kind replaces
    // and finished ones leave the queue; unless the queue is empty or the kind is one of 0x1D-0x1F,
    // 0x23-0x26, 0x2A nothing more happens. The speech (delay 3; `talking` for kind 0x2B) joins the queue's
    // end; into an empty queue it plays at once (delay 0; a talking one sets the server unit's flag 0x800000;
    // kinds 2 / 0x1C only when the last waiting speech sound has ended, and their sound becomes speechSound).
    void Say(const SpeechRecord* speech, AckKind kind) override;
    // Every queued speech's sound stops (talking ones clear the server unit's flag 0x800000) and the queue is
    // emptied.
    void StopSpeech() override;
    // Plays `speech`'s sound (2D, priority 1, volume 100, as speech) and remembers it with the time. False
    // when the same speech played less than 5 s ago.
    bool Speak(ClientSpeech* speech);

    // 1 when a weapon model is held (any weapon id not -1). HasMainArmour: 1 when one of the first three
    // armour slots (body, trousers, helmet) holds a model.
    bool HasOtherArmour() override;
    bool HasMainArmour() override;

    // For heroes the composed skins are renewed: the detailed one (ComposeSkin(1)) into the first part set's
    // texture (read unchecked) and the simple one (ComposeSkin(0)) into the second's (skipped when missing).
    // A texture takes the new name and is reloaded (flags_ 8 cleared, its manager's LoadTexture) when
    // ComposeSkin returned non-zero or the name differs.
    void RefreshEquipment() override;
    int RefreshModel() override;
    // Every armour slot taken off, the ids cleared, then the first min(count, 7) of `ids` put on.
    void SetArmourModels(const std::uint32_t* ids, int count) override;
    void SetWeaponModels(const std::uint32_t* ids, int count) override;
    bool WeaponAdded(std::uint32_t id) override;
    bool RemoveWeaponModel(std::uint32_t id) override;

    // The sounds of the animation playing between the last update's frame and now (attacks, deaths, skills, idles, hits,
    // footsteps); see docs/notes/units.md, "Animation sounds".
    void PlayStepSounds();
    // The footstep sound on ground kind `terrain` into `out`, for units whose prototype's model has sounds
    // for it: on a platform 0.01 or more away from the ground the kind is 2; crawling (movement mode 0)
    // except on kinds 6 / 14 gives "steps\human\crawl\<1..6>.wav", else "<the kind's name>\<1..count>.wav"
    // (one rand() each). Returns 1, or 0 (no prototype, model or sounds; `out` untouched).
    int FootstepSound(int terrain, std::string* out);
    // The sound of `animation`'s action into `out` (emptied first) when its sound frame (not 0) lies in
    // [from, to): "weapons\miss.wav" for `miss`, else one of its sound numbers (rand()) in the folder of the
    // second prototype or its model: idle (kind 0x180000, by the model's idle chance), skill sounds (0x40000:
    // tame, steal, science, loot), attack (0x80000: "weapons\<n>.wav" for a hero with a bow or crossbow, else
    // by the attack chance), death (0x1C0000), hit (0x200000). Returns 1 for those kinds (also when a chance
    // left `out` empty), else 0.
    int ActionSound(const FigureAnimation* animation, float from, float to, std::string* out, std::uint8_t miss);

    // The wound level of each of the six body parts (head, body, hands, legs) into (*levels)[0..5]: 0 for a
    // part without state or maximum, at full durability, or at 75% or more; 1 above 33%, 2 above one point, 3
    // at one point or less. Heroes show none under armour whose record hides them (ArmorRecord::showsWounds;
    // trousers: legs, body armour: body and hands, helmet: head); others show each pair of hands / legs at
    // the worse level. True when any part was wounded before the armour test.
    bool WoundLevels(std::uint8_t** levels);
    // With any wound, a skin request of the six levels and the textures "<model><part>w<level>.mmp" of the
    // wounded parts (128 pixels for heroes, else 64) is composed by the texture manager as "mo<id>w". Returns
    // its result, or 0 without wounds.
    int ComposeWoundsTexture();
    // The two marker sources (sources[1], sources[2]) stopped, released and forgotten, then `path` and
    // `marker` kept in their place (each referenced; null: none).
    void SetMarkers(CParticleSource* path, CParticleSource* marker);
    void DropFaceTexture();


    CParticleSource* sources[3];    // particle sources the controller keeps (counted)
    float lastAnimationTime;        // the animation state's time at the last update
    const FigureAnimation* lastAnimation;   // the animation at the last update
    float lastTime;                 // the world time the unit was last advanced to
    PathOrder motion;               // the order part of a path
    std::uint8_t weaponHidden;      // ShowWeaponParts' state (cleared by a weapon change)
    std::uint8_t arrowHidden;       // the bow's arrow part is flagged hidden
    std::uint8_t snapHeading;       // the next path placement turns the unit to the path at once
    float fadeIn;                   // 1 when fully shown; the drawn parts take it as alpha
    int flashCountdown;             // the drawn parts get their colours back at 0
    int footprint;                  // the last footprint, -1 none
    int footprintRepeats;           // further steps that may reuse it
    int idleSound;                  // the idle animation's sound handle
    int otherSound;                     // a sound handle, -1 after Create; nothing reads it
    int speechSound;                // the sound of the last speech that waits (-1 none)
    const SpeechRecord* lastSpeech; // the speech played last
    std::uint32_t lastSpeechTime;   // when (timeGetTime)
    std::list<ClientSpeech> speech; // the speech queue
    std::uint32_t armourIds[7];     // packed ids of the armour worn, -1 none
    std::uint32_t weaponIds[4];
    Ref<CTexture> faceTexture;      // the face texture the blink changes
    std::uint8_t blinks;
    int blinkTimer;                 // ticks to the next blink change
    int eyesClosed;
};

// The figure part names a unit of prototype `prototype` shows with the armour pieces `armourIds` (appended to
// `names`: "<part>.armor<number>", the hair as "hr.<hair>.armor<number>" unless the prototype has no hair,
// -1), the parts of the weapons `weaponIds` (also into `names`), and every plain part of the three rows (into
// `plainNames`, the hair as "hr.<hair>"; the empty names of the helmet row too).

void CollectArmourPartNames(const UnitPrototypeRecord* prototype, const std::vector<std::uint32_t>* armourIds,
                                       const std::vector<std::uint32_t>* weaponIds,
                                       std::vector<std::string>* names, std::vector<std::string>* plainNames);

inline CUnitClientSpecific* CUnit::ClientController() const { return static_cast<CUnitClientSpecific*>(controller); }
