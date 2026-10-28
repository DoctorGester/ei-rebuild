#pragma once

// Units (creatures; class id 0x50), their controllers, statistics and party records.

#include "core_streams.h"
#include "engine_object.h"
#include "item.h"
#include "item_db.h"
#include "lock_key.h"
#include "map_object.h"
#include "maths.h"
#include "object_tables.h"
#include "unit_motion.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include <cstddef>
#include <cstdint>
#include <string>

#include <list>
#include <vector>

#undef GetClassName

class CLight;
class CParty;
class CPlayer;
struct UnitStats;
struct UnitParams;
struct UnitPrototypeRecord;
struct UnitModelRecord;
struct SkillLevelRecord;
struct SkillDatabase;
struct DamageInfo;

constexpr int kClassUnit = 0x50;

// CEngineObject::type_ of the player's own units (other kinds: 0x33 ...).
constexpr int kUnitTypeHero = 0x32;

// Class id of the unit's controller for the units in a player's party (GetClassName "CUnitClientSpecific"); their
// items live in the player's inventory records.
constexpr int kClassPartyController = 0x3D;

// Eight damage or protection words (an AI attack option's damage, a combat controller's
// protection): word 0 apart, words 1..7 per damage type.
struct DamageWords {
    std::uint16_t words[8];

    // The sum over words 1..7 of what exceeds the matching protection word (a difference with its top bit set
    // counts 0), as 16 bits.
    std::uint32_t ExcessOver(const DamageWords* protection) const;
};
static_assert(sizeof(DamageWords) == 0x10);

// A body slot as UnitParams keeps it (UnitStatSlot rounded).
struct UnitParamsSlot {
    int type;
    std::uint8_t vitalMask;
    std::uint8_t unused_05;
    std::int16_t protection[7];     // the three layers' absorb values summed, per damage type
    // The part's state as the unit panel shows it: 0 none (not shown), 1 drawn in red, 3 with a bar of
    // current / maximum.
    int kind;
    std::int16_t current;
    std::uint16_t maximum;
};
static_assert(sizeof(UnitParamsSlot) == 0x1C);

// The unit's parameter block: UnitStats rounded to bytes and shorts, what the client and the saved games see.
struct UnitParams {
    double Period();
    // Zeroes the block; the slot types become 4 and lastHitSource 7.
    int Reset();
    // UnitStats::WeaponSkillBonus on the packed bytes (the client's party units), with the weapon's prototype
    // record `weapon` (or null). Unrounded.
    double WeaponSkillBonus(int type, const itemdb::WeaponRecord* weapon);

    std::uint8_t unused_00[4];
    int experience;
    int unspentExperience;          // experience + pending - spent
    std::uint8_t stat10;
    std::uint8_t stat18;
    std::uint8_t attackRate;        // (UnitStats::stat20 rounded)
    std::uint8_t movementKind;      // 2: moves on the water surface
    // Speed bytes of movement modes 3 (and 2's backward), 2, 1, 0 (UnitStats mode speeds * 255 / 2, at most 255).
    std::uint8_t modeSpeeds[4];
    std::uint8_t baseSpeed;         // (UnitStats::speed * 25.5, at most 255)
    std::uint8_t rate;
    std::int16_t capacity;
    std::uint8_t combat[5];         // reach, attack, attack spread, defence, speed
    std::uint8_t damage;            // (truncated)
    std::uint8_t damageSpread;      // (truncated)
    std::uint8_t naturalWeapon;
    std::int16_t lastHitSource;         // what last damaged the unit (7 after Reset)
    std::uint8_t valuesB0[6];       // UnitStats::valuesB0[i].current ([0] night sight range, [1] its day percentage)
    std::uint8_t valuesE0[6];       // UnitStats::valuesE0[i].current
    std::uint8_t unused_2E[2];
    UnitParamsSlot slots[6];
    std::uint8_t attributes[3];
    std::uint8_t skills[8];
    std::uint8_t skillLevels[0x24];
    std::uint8_t flags;             // (UnitStats::flags, low byte)

    // The sight range at `daylight` (0 night .. 1 day):
    // valuesB0[0] x ((1 - daylight) + valuesB0[1] x daylight x 0.01). Unrounded.
    double VisionRange(float daylight);

    // The seven protection values the unit panel shows, into `out`: for text-key units (`keyed`) the body slots'
    // protection weighted by kSlotWeights and the packed vitals' protection added; others get the packed protection
    // times the difficulty factor.
    void ProtectionBlock(const struct UnitVitalsPacked* vitals, float* out, std::uint8_t keyed);
};
static_assert(offsetof(UnitParams, attackRate) == 0x0E);
static_assert(offsetof(UnitParams, combat) == 0x18);
static_assert(offsetof(UnitParams, slots) == 0x30);
static_assert(offsetof(UnitParams, attributes) == 0xD8);
static_assert(offsetof(UnitParams, flags) == 0x107);
static_assert(sizeof(UnitParams) == 0x108);

struct PathOrder;
struct SpeechRecord;
class CUnit;
class CUnitServer;
class CUnitClientSpecific;

// The unit's controller: CUnitServer (class id 0x3E) on the server, CUnitClientSpecific (0x3D) for party
// units on the client; this base has the small defaults. Names: docs/notes/map_objects.md, "Virtual slot
// names".
class CUnitController : public CEngineObject {
public:
    CUnitController() {
        ClearFields();
        unit = nullptr;
    }
    ~CUnitController() override;
    int Create() override;
    void Destroy() override;

    virtual int FiguresLoaded();
    // Plays the unit's animationEntry / secondEntry from frame `time` (client).
    virtual void RestartAnimation(float) {}
    virtual void Unused15() {}                   // empty everywhere
    virtual PathOrder* Path() = 0;
    virtual void Unused17() = 0;                 // empty everywhere
    virtual void PlayerChanged(void*) {}
    // CUnit's ScaleEffectChanged passes its whole dword argument on; the bodies read the low byte.
    virtual int RefreshVitals(char withHitPoints);
    // Either unit may be null.
    virtual int TakeHit(DamageInfo* hit, CUnit* attacker, CUnit* source);
    virtual int Heal(float amount);
    virtual void RepackStats() {}
    virtual void UpdateSizeClasses() {}
    virtual int Level() = 0;
    virtual int ShowWeapon(std::uint32_t slot) = 0;   // the weapon the figure holds (-1 none)
    virtual bool ArmourPutOn(std::uint32_t itemId) = 0;
    virtual bool ArmourTakenOff(std::uint32_t slot) = 0;
    virtual int ClearOuterArmourLayers() { return 0; }
    // After the items changed; the client reloads a hero's two figure textures when their names change.
    virtual int RefreshEquipment() { return 0; }
    virtual int RefreshModel() { return 0; }     // after a weapon change
    virtual int RefreshWounds() { return 0; }
    // Count capped at 7.
    virtual void SetArmourModels(const std::uint32_t*, int) {}
    // Count capped at 4.
    virtual void SetWeaponModels(const std::uint32_t*, int) {}
    virtual bool WeaponAdded(std::uint32_t itemId);
    virtual bool RemoveWeaponModel(std::uint32_t itemId);
    virtual int HasOtherArmour() = 0;
    virtual int HasMainArmour() = 0;
    virtual void HostileMaskChanged() {}
    virtual int TamingStage() { return 0; }
    virtual void AdvanceTime(float) {}
    // The unit's path was replaced; the client places the unit on it.
    virtual void MotionChanged() {}
    // `object` is going away: the server's targets and brain forget it.
    virtual void ObjectDeleted(CEngineObject*) {}
    // The fade-in starts again (alpha 0 on every part), for units joining a session.
    virtual int RestartFadeIn() { return 0; }
    virtual void Resettle() {}
    virtual void UpdateMoveFactors() {}
    // A body part's condition changed by `change` (a fraction of its maximum) for stat `stat` (0..5); `flags`
    // is the unit's Params->flags byte (bit 3 skips it). The client shows text and a particle effect.
    virtual void StatChanged(int, float, std::uint32_t) {}
    // The hit took `lost` (a fraction of the hit points) with the wound flag; the client does not read it.
    virtual void Wound(float) {}
    virtual void Died() {}                       // the client: the light goes out, blood
    virtual void EffectsChanged() {}
    virtual void GetAttackParams(struct AttackParams* out) = 0;
    // Queues `speech` (an acks.db record) as `kind` (client).
    virtual void Say(const SpeechRecord*, int) {}
    virtual void StopSpeech() {}
    virtual void MoveFactorsChanged() {}

    CUnit* unit;   // the unit it controls (CUnit::AttachCompanion sets and clears it)
};

// The prototype name is CObject3D::name, the figure's build its scale.y, the packed vitals (hit points, mana)
// CMapObject::packedVitals.
class CUnit : public CMapObject {
public:
    CUnit();
    ~CUnit() override;

    int Create() override;
    void Destroy() override;
    void ClearState() override;
    int RunTick() override;
    const char* GetName() override;          // the script name
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    const char* GetClassName() override;
    int GetClassId() override;
    bool IsSynchronised() override;
    void ReceiveHit(DamageInfo* hit, CUnit* attacker, CUnit* caster) override;
    Vector3* EyePoint(Vector3* out) override;
    std::uint32_t ReadNetState(int first) override;
    void WriteNetState() override;
    // The position predicted along the path at world time `time`, the point the animation acts on.
    Vector3* PositionAt(Vector3* out, int time) override;
    Vector3* AimPoint(Vector3* out) override;
    int IsLargeUnit() override;
    int IsDead() override;
    // The controller's ForgetObject; nothing reads the result (0).
    int ForgetObject(CEngineObject* object) override;
    void UpdateMoveFactors() override;
    void RefreshWounds() override;
    double MoveSpeed() override;             // the forward speed (GetMoveSpeeds)
    float Radius() override;
    void SetScriptName(const char* newName) override;

    // The controller's AdvanceTime; a text-key (party) unit then marks its sight on the client world (its
    // VisionRange at the world's daylight, around its position).
    void AdvanceTime(float time) override;

    // Adds `mask` to the hostile mask (relationsA); marks part 11 changed when it grows.
    void AddHostileMask(std::uint32_t mask);
    void SetRelationsB(std::uint32_t mask);
    // Sets the side; flags_ bit 0x80000; marks part 11 when the unit has a controller.
    void SetSide(int side);
    // Non-zero when this unit regards `other`'s side as hostile (the bit of other's side in both relation
    // masks).
    std::uint32_t IsHostileTo(const CUnit* other);

    // Marks part 13.
    void SetUnknown438(int value);
    // Marks part 8.
    void SetAnimationEntries(std::int8_t entry, std::int8_t second, int start, std::int8_t sound);
    // Set motionMode / pathTimeShift; mark part 7 when they change.
    void SetMotionMode(int mode);
    void SetPathTimeShift(int shift);
    // Marks part 6.
    void SetMoveModes(int mode, int mode2);
    // Clears (on) or sets (off) flags_ bit 0x40000000; marks part 5.
    void SetFlag40000000Off(bool on);
    // Sets (stunned) or clears flags_ bit 0x1000000. Returns the new flags.
    std::uint32_t SetStunned(int on) override;

    // Writes the item ids of the 4 weapons and then of the 7 armour pieces to `out` (-1 for an empty slot).
    void GetEquipmentIds(std::uint32_t* out);

    // 0 yes, 1 not enough mana, 3 no spell in slot `slot` (0..7), 4 an effect of type 0x20 prevents it.
    int CanCastSpell(std::uint32_t slot, const float* unused);
    // 3 none or not a spell carrier, 1 a category 5 item with too little charge, else 0.
    int CanUseQuickItem(std::uint32_t slot, const float* unused);

    // Movement speeds for the current movement mode; `backward` scaled by the global.
    void GetMoveSpeeds(float* forward, float* backward);
    double MoveSpeed2();
    // The speed of movement mode `mode` (0..4; other values: the base speed).
    double MoveSpeedFor(int mode);
    // 3 * value + 10 for units without a player (value capped at 10 when flags_ has 0x4000000), else 1000000.
    double ScaledValue(float value);

    // BaseSpeed scaled by the movement mode: x0.2 when standing or inactive, x0.7 in mode 1.
    double ModeSpeed();
    // 1.8 * above[2] when bit 1 of boundsFlags is set, else 0.7 * radius + center[2] (a height from the
    // figure's bounds, despite the name).
    double BaseSpeed();

    double TotalItemWeight();

    UnitPrototypeRecord* GetPrototype();
    UnitModelRecord* GetPrototypeModel();
    UnitPrototypeRecord* GetPrototype23C();
    // Stores the index of prototype record `record` in prototype23C. Returns 1 when `record` lies before the
    // table.
    std::uint32_t SetPrototype23C(const UnitPrototypeRecord* record);

    // Puts an item into the first free weapon (4), quick item (4) or spell (8) slot and takes a reference:
    // returns the slot, 1 if already there (sic), -1 if full, 0 for null (weapons and quick items only).
    int AddWeapon(CItem* item);
    int AddQuickItem(CItem* item);
    int AddSpell(CItem* item);
    // Empties weapon / quick item slot `slot` (releasing the item); part 5. Nothing when out of range.
    void RemoveWeapon(std::uint32_t slot);
    void RemoveQuickItem(std::uint32_t slot);

    // From the player's party record for party units. Null when out of range.
    CItemWeapon* GetWeapon(std::uint32_t slot);
    CItem* GetQuickItem(std::uint32_t slot);
    CItem* GetSpell(std::uint32_t slot);
    CSpell* GetSpellObject(std::uint32_t slot);

    // The unit's speed factor for the action of animation `entry` (its key's bits 18-21: 0x80000 attack,
    // 0xC0000 cast, 0x240000 mode transition heroes: the global 0x40000 skill; others 1). Animation steps
    // divide the entry's frames by it.
    float ActionSpeed(const FigureAnimation* entry);

    // For hero units, whether the animation entry's flags have the bits the controller asks for (0x10 by
    // HasOtherArmour, 0x20 by HasMainArmour); true for other units.
    bool AcceptsAnimation(const FigureAnimation* entry) override;

    // Ratio of the animation factors of the scale's and the base scale's z.
    double AnimationRatio();

    // The per-tick update (another name, so it is no override): the controller's update, then the map object's.
    // Returns 1.
    int Tick();
    void RecomputeScale() override;
    void EffectsChanged() override;
    // The figures take the base scale again.
    void ApplyBaseScale() override;
    // Loads the figures (unless flags_ has 0x80) and tells the controller.
    void LoadFigures() override;
    // A companion of type 0x48 becomes the controller (the old one is detached and released); others go to the map object's AttachObject.
    void AttachObject(CEngineObject* object) override;
    // Statistics changed: the map object's (packs the server object's vitals), then the controller's.
    int StatsChanged() override;
    // The controller's RefreshVitals with `value` (0 without a controller).
    void ScaleEffectChanged(int value) override;
    // A server controller (class 0x3E) copies `stats` in.
    void SetStats(const UnitStats* stats) override;
    void UpdateSizeClasses() override;
    // `out` = the position with z = the ground (or water surface) height plus the mode speed. Returns `out`.
    float* StandPoint(float* out);
    // Stores `offset` and puts the unit on the ground (or, for movement kind 2, on the water surface plus its
    // z or the offset).
    void PlaceOnGround(float offset) override;
    void SetFromStats(UnitStats* stats);

    // The controller's for text-key units, else the prototype's (reads through null without a prototype).
    int Level();

    // For units with a player and no bow or crossbow in hand: is this unit behind `target` (the direction to
    // it against the target's facing below -0.5)? 0/1.
    BOOL IsBehind(const CMapObject* target);

    // Puts armour piece `item` (condition rounding above 0) into the armour slot of its type (referenced; the
    // controller's ArmourPutOn gets its id) and releases the piece that was there. Returns that piece (null
    // for an item that does not qualify).
    CItemArmor* EquipArmour(CItem* item);
    // The controller's ArmourTakenOff gets the piece's type, the piece is released. Returns the piece
    // (released).
    CItemArmor* UnequipArmour(int slot);
    // Releases every item (weapons, armour with ArmourTakenOff and ClearOuterArmourLayers, quick items,
    // spells, the carried ones) and frees the carried array.
    void ReleaseAllItems();
    // Adds `item` (referenced) to the carried items.
    void AddCarried(CItem* item);
    // Releases carried item `index` and closes the gap. Returns the new count.
    int RemoveCarried(int index);
    // Empties both lists, then adds every item the unit holds to `items` (empty slots skipped) and each one's
    // spell to `spells` when it has one.
    void CollectItems(std::list<CItem*>* items, std::list<CSpell*>* itemSpells);
    // Fresh vitals and statistics (reset, then the owner's BuildFromPrototype for `prototype`), packed into the
    // vitals and parameters.
    void TakePrototype(const char* prototype) override;
    // Sends the equipment ids to the controller (the 7 armour ids, then the 4 weapon ids).
    void SendEquipment();
    // prototype23C = the index of the unit prototype named `prototypeName`, or -1.
    std::uint32_t SetPrototype23CByName(const char* prototypeName);
    int SetPrototype23CIndex(int index);

    // Back to a blank unit: no name, prototypes, controller (released), relations, player, side, items (the slots
    // cleared without releasing) or carried array; standard sizes and movement modes, the parameter block cleared,
    // the network records reset.
    void Reset();

    // After a hit: armour pieces and weapons whose condition rounds to 0 or less leave their slots (armour:
    // ArmourTakenOff with the type; weapons: message 0x16 to the player, the slots above move down,
    // ShowWeapon(-1) at the end) and go to the owning player, then lose the unit's reference. Units without a
    // player keep everything.
    void DropBrokenEquipment();

    // The map object part, the controller, the unit's name, relations, player, movement fields, items by id
    // (counted references), the carried items, and from version 0x4D the prototype names (prototypeId_ /
    // prototype23C).
    void LoadUnit(CStream* stream);
    // The same fields in the same order (the active weapon twice), items as ids (-1 for none), the prototype
    // names as zero-terminated strings.
    void SaveUnit(CStream* stream);

    // After the controller's UpdateMoveFactors: with a server controller, the size class of the movement mode
    // (sizeClass20 in mode 0, sizeClass60 in mode 1, else sizeClass), the standing height 0.9 x the larger of
    // above.x / above.y (plus 0.2 with a player, at most 2; 0.01 for size class 0 when the prototype's height
    // is above 1.5) and the radius 0.9 x that extent + 0.05. Other controllers: size class 0 for movement
    // kind 2, else 3, radius 0.75.
    void UpdateSizeClass();

    // Stealing: takes the first carried item (the rest move down), or else a random filled quick item slot
    // (one IsaacRandom draw) is emptied; marks part 5. Returns the item, null when there is none (nothing
    // changes then).
    CItem* TakeRandomItem();

    // The map object's Create, Reset, then 14 network fields. Returns 1.
    int CreateUnit();

    // An animation entry of the unit's figure for `key` (bits: docs/notes/combat.md), only for units (type_
    // 0x32..0x34): a random entry matching the key, else one matching the key without its low 15 bits, else
    // for movement bits 0x8000 (alone among 0x38000) one without movement bits or with 0x10000 in place of
    // 0x8000; then, when `anyOfKind`, a random entry of the key's kind. Null when none.
    FigureAnimation* FindAnimation(std::uint32_t key, bool anyOfKind);
    // The figure's animation entry `key & 0xFF`; null (and "Can't find animation entry index" to the DebugLog
    // stub) when there is none.
    FigureAnimation* AnimationEntry(std::uint32_t key);
    // The horizontal extent of the body: the box maximum's (x, y) length of the figure part "bd" (else
    // "chest"), doubled for the mosquito (type_ 0x33, prototype "unmosp"); 0 when the figure has neither
    // part. Unrounded.
    double BodyExtent();
    // The ground height at (x, y); for units moving on the water surface (movementKind 2) the surface height
    // plus the unit's z (server units) or its heightOffset. Unrounded.
    double GroundHeight(float x, float y);
    // Where effects (lightning, particles) start from the unit: on a client, a hero's right hand (part
    // "rh3"), another unit's weapon point (the controller's) turned by the orientation, or else its position
    // plus the bounding box centre. The server and a hero without the part rotate uninitialised stack (see
    // the notes).
    Vector3* EffectPoint(Vector3* out);

    // Sets the hostile mask (relationsA), tells the controller, sets flags_ 0x80000 and marks part 11 changed.
    void SetHostileMask(std::uint32_t mask);
    // After the controller's path changed: the path forgets this unit as a tracked object, the motion takes the
    // movement speeds, its segment -1, and is rebuilt for the path; marks part 10.
    void RefreshMotion();
    // Moves the unit to `newPlayer` (nothing when it is the current one): the old player loses it, the new one gains
    // it, part 11 is marked and the controller hears of it.
    void SetPlayer(CPlayer* newPlayer);
    // As RefreshMotion with the speeds given (the motion's speed scale and base turn rate) and no part
    // marked.
    void SetMotionSpeeds(float speed, float turnRate);
    int HasPlayer() const;
    // Twice above.z, unrounded.
    double DoubleHeight() const;
    CItemArmor* WornItem(int slot) const;
    // SetPlayer(null), the controller's release, every item slot released and emptied (quick items, weapons, spells,
    // armour), the carried items released and their array freed.
    void ReleaseUnit();
    // The object's release, the map object's Destroy, then Reset.
    void DestroyUnit();

    CUnitController* Controller() const { return static_cast<CUnitController*>(controller); }
    CUnitServer* ServerController() const;
    CUnitClientSpecific* ClientController() const;
    UnitParams* Params() { return &params; }
    CMapObject* MapObject() { return this; }
    UnitMotion* Motion() { return &motion; }

    std::string scriptName;        // network field 9
    int prototype23C;              // a second prototype index (-1 none)
    CEngineObject* controller;      // (the script code calls it the perception object)
    std::uint32_t relationsA;       // \ a unit regards side s as hostile when both have
    std::uint32_t relationsB;       // / bit s set
    CPlayer* player;
    int side;
    // The animation entries (FigureAnimation::index) the unit shows: -1 none; the second -2 for the special idle. The
    // client's RestartAnimation plays them.
    std::int8_t animationEntry;
    std::int8_t secondEntry;
    std::int8_t actionSound;        // a hero's action sound (CUnitClientSpecific's sounds)
    int animationStart;             // the world tick the animation started at (+1 per paused tick)
    int motionMode;                 // 0 plays, 1 paused (the start moves on), 2 scaled
    int pathTimeShift;              // 1: the client shifts its motion path's time each update
    int moveMode;                   // 0..4
    int moveMode2;
    std::uint32_t activeWeapon;     // slot of the weapon in use
    int pendingWeapon;              // the weapon slot to take up (-1 none; TakeUpWeapon / ShowWeapon)
    int dead;
    std::uint32_t obstacle;             // the unit blocks others on the sector map
    UnitMotion motion;
    float heightOffset;             // height above the water surface (PlaceOnGround)
    int sizeClass;                  // selects the 4-bit passability field in each AI map cell
    float unitRadius;               // 0.75 after Reset
    float standingHeight;           // deeper water or higher ground than (2 - height) * 16 blocks it
    // The parameter block (UnitStats rounded): the attack rate, the movement kind (2 moves on the water surface), the
    // mode and base speeds and the six body slots (the unit panel's parts are their kind / current / maximum).
    UnitParams params;
    int fightAtWill;                    // 1: a party unit attacks on its own (the move panel's flag; PlayerMode::UpdateCombat), 0 not
    std::uint32_t altPassCosts;         // set: path searches use the AI map's alternative cost table
    CItem* weapons[4];
    CItem* quickItems[4];
    CItem* spells[8];
    CItemArmor* armor[7];           // a piece per armour type
    std::vector<CItem*> carried;    // further items, each holding a reference
    float actionSpeeds[6];          // per action kind (ActionSpeed; from the model record)
    float levels4C8[5];             // any at most 2 (or NaN) asks for a party refresh
};

inline CUnit* UnitOrNull(CEngineObject* object) {
    return object && object->GetClassId() == kClassUnit ? static_cast<CUnit*>(object) : nullptr;
}

// The object registered under a unit's id (the party's and the players' unit ids, a unit's speech), or null; taken as
// a unit without a class test.
inline CUnit* UnitWithId(const CBaseManager* objects, unsigned id) { return objects->FindObjectAs<CUnit>(id); }

constexpr std::uint32_t kInSectorMap = 0x8000;    // CEngineObject::flags_

// Unit prototypes (global vectors; see item_db.h for the lookup rule).
namespace unitdb {
UnitPrototypeRecord* PrototypeRecord(std::uint32_t index);
UnitModelRecord* ModelRecord(std::uint32_t index);
// The weight of a body slot's protection by slot type (head 10, body 30, ...): UnitParams::ProtectionBlock.
inline constexpr int kSlotTypes = 5;
inline constexpr float kSlotWeights[kSlotTypes] = /*0073E0FC*/ {10.0f, 30.0f, 15.0f, 15.0f, 15.0f};
} // namespace unitdb

// One of the three protection layers of a body slot: what each of the seven damage types loses to it.
struct UnitStatLayer {

    int layerMask;                      // the layer mask bits of the pieces added (AddArmourLayer)
    float absorb[7];                // per damage type
    float spareAbsorb[7];               // zeroed by ClearOuterLayers only; nothing reads it
    int state;                      // > 1: the layer is there
    int reserved[2];                    // zeroed by the builders, saved with the stats; nothing reads them
};
static_assert(sizeof(UnitStatLayer) == 0x48);

// One of the six body slots of UnitStats (0 head, 1 body, 2/3 hands, 4/5 legs, as the hit locations).
struct UnitStatSlot {

    int type;                       // 2 arms, 3 legs; reset to 4
    std::uint32_t vitalMask;        // damage types whose full hit severs the slot
    UnitStatLayer layers[3];
    int kind;                       // 0 unused, 1 severed, 2 disabled, 3 working (> 1: counts)
    float current;                  // its hit points
    float maximum;
    float share;                        // the model slot's share of the hit points
    float factor;                   // its share of the unit's hit points
};
static_assert(sizeof(UnitStatSlot) == 0xF4);
static_assert(offsetof(UnitStatSlot, kind) == 0xE0);

// A value and what it currently is after modifiers.
struct UnitStatPair {
    float base;
    float current;
};
static_assert(sizeof(UnitStatPair) == 8 && offsetof(UnitStatPair, current) == 4);

// The hit points, mana and protection of a unit as the server keeps them (party records keep one too).
struct UnitVitals {
    // Every 15th call mana (unless `inCombat`) and hit points (through the slots) regenerate. Returns 1 when
    // something may have changed.
    bool Regenerate(UnitStats* stats, CUnit* unit, char inCombat);
    // A hit on a map object that is not a creature: each damage type beyond the protection comes off the hit
    // points, not below -5 times the maximum. Returns 1.
    BOOL TakeHit(const DamageInfo* hit);
    // Zeroes everything but the first dword.
    void Reset();
    // The values rounded to nearest into `out`.
    void Pack(UnitVitalsPacked* out);

    int builtFlags;                     // bit 0 set when built from the prototype
    int regenTicks;                 // counts 1..15
    float hitPoints;
    float maxHitPoints;
    float hitPointRegen;            // fraction of the maximum per regeneration
    float mana;
    float maxMana;
    float manaRegen;
    UnitStatPair protection[7];     // per damage type (current: what a hit loses to it)
};
static_assert(offsetof(UnitVitals, hitPoints) == 0x08);
static_assert(offsetof(UnitVitals, protection) == 0x20);
static_assert(sizeof(UnitVitals) == 0x58);

// A creature's computed statistics (built from the unit's parameters, often as a stack copy).
struct UnitStats {
    // Indices of combat[].
    enum CombatIndex { kReach, kAttack, kAttackSpread, kDefence, kSpeed };
    // The float results below are returned unrounded (declared double: the FPU runs above 24-bit precision
    // under Rebirth, see CItemWeapon::RollDamage). Period20: 25 / stat20.
    double Period20();
    // The item panel's lines: stat10 over the setting "HP Val 1", stat18 over "MP Val 1".
    double StrengthRatio();
    double DexterityRatio();
    // pendingExperience 0, then the block read raw, as long as the stream's format version knew it (below
    // 0x45: 0x6F8 bytes, below 0x50: 0x6FC, below 0x63: 0x700, else 0x704). Returns the stream's result.
    int LoadFrom(CStream* stream);
    void SaveTo(CStream* stream);
    float AttributeBase(int index);
    // Attribute `index` (0..2) with the bonus of the stat it depends on (stat - 25).
    double AttributeWithBonus(int index);
    float SkillBase(int index);
    // Skill `index` plus (stat - 25) of the stat its name starts with ('s', 'd', 'i').
    double SkillWithBonus(std::uint32_t index);
    // The weapon skill for weapon type `type`.
    float WeaponSkill(int type);
    double SkillLevelValue(int index);
    ::SkillLevelRecord* SkillLevelRecord(int index);
    // Stat10 * 12 scaled by (1 + 0.01 * the level value of skill level 21).
    double Value6E8();
    // Global + 0.01 * the level value of skill level 22.
    double Value6E9();
    double Value6E9Arg(int unused);
    // `value` minus what the six slots take from it (by their kind, current, maximum and factor).
    double Reduce(float value);
    // `value` minus value * factor of each slot record of kind 1.
    double ReduceKind1(float value);
    // Two factors from stat10 and stat18 (relative to 15, over 20).
    void GetFactors(float* a, float* b, int unused);

    // Recomputes the current values from the bases, the skill levels, the unit's effects, the carried weight and the
    // slots; writes the maxima and hit points into `vitals`.
    void Recompute(UnitVitals* vitals, CUnit* unit);
    // Only the hit point maximum and the hit points (and flag 8 when alive).
    void RecomputeHitPoints(UnitVitals* vitals, CUnit* unit);
    // Heals the working-or-disabled slots below their maximum by `amount`, spread by their factors over `scale` (the
    // unit's maximum); a slot back above 0.001 works again. Returns 1.
    bool HealSlots(float scale, float amount);
    // HealSlots(vitals' maximum, amount), then the hit points. Returns 1.
    BOOL Heal(UnitVitals* vitals, float amount);
    // Each used slot's maximum becomes `maxHitPoints` times its share relative to the body's; the current
    // value is clamped to it.
    void SetSlotMaxima(float maxHitPoints);
    // The same, keeping each slot's current value in proportion.
    void ScaleSlotMaxima(float maxHitPoints);
    // Slots of kind 2 or more are healed fully.
    void* RefillSlots();
    // Every used slot works again at its maximum.
    void* RestoreSlots();
    // The lowest current / maximum ratio of the used leg (type 3) / arm (type 2) slots mapped to a factor.
    // The argument is not used.
    float LegsFactor(int unused);
    float ArmsFactor(int unused);
    // 2 when `checkLoad` and the load exceeds the capacity, 1 when a leg is below the damageLevelRunLimit
    // share of its maximum, else 0.
    int MovementState(char checkLoad);
    // The hit point / mana maximum from the experience curve ("HP Val" / "MP Val" settings), strength /
    // dexterity and skill levels 16 / 17. Unrounded.
    double HitPointCurve();
    double ManaCurve();
    // (dexterity * 0.2 + 10) scaled by the arms and skill level 20. Unrounded.
    double RateValue();
    // Adds `amount` to the pending experience; a positive total moves into the experience.
    void AddExperience(float amount);
    void AddScaledExperience(float amount);
    void TakePendingExperience(float amount);
    // For a unit with a server object: AddScaledExperience, then the maxima of its vitals from the curves
    // (the slots scaled along) and its hit points.
    void GainExperience(CUnit* unit, float amount);
    // Script GiveSkill: raises skill `skill` / attribute `index` by the low byte of `amount`; what the new
    // level is worth over the old one (the 1.61 ^ level curve, truncated) is booked as experience and as
    // spent experience. Then the maxima and Recompute. False when the unit has no server object.
    bool RaiseSkill(int skill, int amount, CUnit* unit);
    bool RaiseAttribute(int index, int amount, CUnit* unit);
    // GiveStrength / GiveDexterity / GiveIntelligence: the base value grows by `amount`, the maxima are recomputed
    // (each its own way), strength makes the unit 0.2 wider and dexterity 0.2 less high, then Recompute. False when
    // the unit has no server object.
    bool RaiseStrength(int amount, CUnit* unit);
    bool RaiseDexterity(int amount, CUnit* unit);
    bool RaiseIntelligence(int amount, CUnit* unit);
    double SkillLevelValueCopy(int skill);

    // Applies `hit` (from `weapon`'s attack, may be null): flags 2 (head) / 4, the
    // vitals' protection (scaled by the difficulty for units without a player), wear of the
    // weapon, then the hit location's slot (DamageSlot) or, for location 6, every slot by its
    // share. Returns whether a slot took damage (0 when nothing passed the protection).
    int ApplyHit(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, CItem* weapon);
    // The slot of the hit's location takes each damage type through its armour layers
    // (text-key units; each layer's armour piece wears), the weapon wears, a slot may be
    // severed (-5 times its maximum) or disabled; every damage type is logged (stream).
    // Then the hit points. Returns whether any damage passed.
    int DamageSlot(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, CItem* weapon);

    // The current values from the bases plus the skills (15: valuesB0[1] by its percentage; 23-25: the stats;
    // unknown_044 = -2), the slot maxima from the hit point curve (current values clamped to them), `load`,
    // the capacity, the legs and arms factors and the rate. NPC statistics are built with it (scale 1).
    void ResetToBase(float load);
    // Learns the skill level `record` of the skill database (index / 3 is the skill, index % 3 + 1 the level)
    // unless the unit knows it at that level or better; when the hit point maximum of the curve (before and
    // after, both above 1) changes, the slots are healed by what keeps the hit points in proportion.
    void LearnSkill(const SkillDatabase* database, const ::SkillLevelRecord* record);
    // The attack skill for weapon type `type` (0-4 use skill 0, 5 and 6 skill 1, 8 the bonused skill 0;
    // others 0): for text-key units the skill byte, the weapon prototype's bonus and the stat the skill's
    // name starts with minus 25, plus the level value of skill `type`. Unrounded.
    double WeaponSkillBonus(int type, CUnit* unit);
    // Can the unit learn skill level `record` of the skill database: learnable, the three stats at least the
    // record's, the required attribute and skill level present unless named "none" or empty, no known level
    // of the same skill id in another skill, and the record's own level above the one known. 0/1.
    BOOL CanLearn(const SkillDatabase* database, const ::SkillLevelRecord* record);
    // The experience learning skill level `record` (of the level records) costs: what its worth
    // (1.61 ^ (value / Skill Val 2) * Skill Val 1, the school curve) is over the level below's, truncated
    // (the first level of a skill over level 0), times perkPowerBase ^ (the sum of the known skill levels,
    // a byte), rounded to one significant digit, or two when the first digit is below 4; a price of 10 or
    // less with a first digit below 4 is rounded up.
    int SkillPrice(const ::SkillLevelRecord* record);
    // The hit location an attack aimed at `location` lands on: `location` while its slot works (kind 3; read
    // without a range check), else the first working neighbour (head: 1, 2, 3; body: 0, 2-5; hands 2 / 3: the
    // other hand, then the body; legs 4 / 5: the other leg, then the body), else a random working slot (one
    // IsaacRandom draw), else 1.
    int PickLocation(int location);

    // The rounded values into `params`. The first argument is not used.
    void Pack(CUnit* unused, UnitParams* params);
    int Reset();
    // Clears layers 1 and 2 of every slot.
    int ClearOuterLayers();

    // Adds the armour piece to the slots its type covers (up to three, table kArmourCoverage; 7 none): for
    // each covered slot that is used, layer GetTypeGroup() gains the piece's absorb values (one set for the
    // first slot, another for the others), its layerMask the piece's mask and its state 3.
    void AddArmourLayer(CItemArmor* piece);
    // Zeroes layers 1 and 2 of every slot, then adds each piece `unit` wears whose condition rounds above 0.
    void RebuildArmourLayers(CUnit* unit);
    // Out[1..7]: each damage type's protection (vitals' current value) plus the body slot's layers that are
    // there (state >= 2; layers 2, 1, 0 for text-key units, else layer 0), rounded to nearest. out[0] is left
    // alone.
    void PackBodyProtection(CUnit* unit, const UnitVitals* vitals, DamageWords* out);
    // Attribute `index` for skill checks: the attribute byte for units without a text key; otherwise the
    // attribute with its stat bonus (stat - 25; 0 for other indices) plus the largest bonus of the unit's
    // quick items. Unrounded.
    double AttributeWithItems(int index, CUnit* unit);
    // The skill for spell school `school` (4 counts as 3, 6 and 7 as 5): skill byte 3 (schools 0-2), 4 (3-4)
    // or 5 (5-7), plus the level value of skill school + 7, plus intelligence - 25. Unrounded.
    double SpellSchoolSkill(int school);

    std::uint32_t builtFlags;           // bit 0 set when built from the prototype
    float experience;
    float spentExperience;
    float stat10Base;
    float stat10;                   // 's'
    float stat18Base;
    float stat18;                   // 'd'
    float stat20Base;
    float stat20;                   // 'i'
    float speedBase;
    float speed;                    // speedBase * loadFactor
    float rateBase;                 // 15, or dexterity * 0.2 + 10 for text-key units
    float rate;                     // at least 1
    float modeSpeeds[4];
    float sightCone;                    // cosine of half the sight angle (-2 for text-key units: all around)
    float peripheralRange;              // the AI's peripheral sight range
    float sideCone;                     // cos(90 degrees) from the builders; nothing reads it
    float load;                     // carried weight
    float capacity;
    float loadFactor;               // LegsFactor when movementKind is 1, else 1
    float armsFactor;
    int movementKind;                   // the model's: 1 legs (load factor), 2 on the water (no size classes)
    // The combat values.
    std::uint32_t reachHeightScale; // from the model: a non-hero strikes low when the target's
                                    // standing height plus (this + 1) x its own radius passes the distance
    std::int16_t sizeClass;         // 1..4 by size, 5/6 when loaded above 0.85, 7 above 1.5
    std::int16_t unused_06A;
    std::int16_t sizeClass20;       // the same for 0.2 times the size
    std::int16_t sizeClass60;       // and for 0.6 times the size
    float combat[5];                // reach, attack, attack spread, defence, speed (the prototype's)
    float damage;
    float damageSpread;
    float attackCategory;           // the natural attack's category (UnitPrototypeRecord::attackCategory)
    float damageWeights[7];         // the natural attack's, per damage type
    int naturalWeapon;              // weapon type of the natural attack (0x10: none / spells only)
    UnitStatPair valuesB0[6];
    UnitStatPair valuesE0[6];
    UnitStatSlot slots[6];
    std::uint8_t attributes[3];
    std::uint8_t skills[8];
    std::uint8_t skillLevels[0x25]; // 1-based level per skill, 0 none
    std::uint32_t flags;            // 1 multiplied hit, 2 hit at the head, 4, 8 alive
                                    //        after a recomputation, 0x10 hit landed (combat)
    float fixedMaxHitPoints;        // the maximum of units without a text key
    float pendingExperience;
};
static_assert(offsetof(UnitStats, stat10Base) == 0x0C && offsetof(UnitStats, stat10) == 0x10);
static_assert(offsetof(UnitStats, stat18Base) == 0x14 && offsetof(UnitStats, stat18) == 0x18);
static_assert(offsetof(UnitStats, stat20Base) == 0x1C);
static_assert(offsetof(UnitStats, stat20) == 0x20);
static_assert(offsetof(UnitStats, speedBase) == 0x24);
static_assert(offsetof(UnitStats, load) == 0x50);
static_assert(offsetof(UnitStats, sizeClass) == 0x68 && offsetof(UnitStats, combat) == 0x70);
static_assert(offsetof(UnitStats, damageWeights) == 0x90 && offsetof(UnitStats, naturalWeapon) == 0xAC);
static_assert(offsetof(UnitStats, valuesB0) == 0xB0);
static_assert(offsetof(UnitStats, slots) == 0x110);
static_assert(offsetof(UnitStats, flags) == 0x6F8);
static_assert(sizeof(UnitStats) == 0x704);

// The record the player keeps for each unit of the party (found by unit id through a party's record array).
// Each item list is an aligned pointer array of the record's own copies of the items.
struct PartyUnitRecord {
    // Empty texts and lists and the defaults below; value-initialised (in the party's array) the rest is zeroed.
    PartyUnitRecord() = default;
    PartyUnitRecord(PartyUnitRecord&&) noexcept = default;
    // This record's items released first (the array's erase moves the records after a removed one down).
    PartyUnitRecord& operator=(PartyUnitRecord&& from) noexcept;
    PartyUnitRecord(const PartyUnitRecord&) = delete;
    PartyUnitRecord& operator=(const PartyUnitRecord&) = delete;
    // Releases the items.
    ~PartyUnitRecord() { ClearItems(); }

    // Releases every item of the four lists, then empties them.
    void ClearItems();

    // A copy of `item` (its Clone with the world's spell manager), counted and appended to the weapons / quick items /
    // armour / spells; returns the copy.
    CItem* AddWeapon(CItem* item);
    CItem* AddQuickItem(CItem* item);
    CItem* AddArmor(CItem* item);
    CItem* AddSpell(CItem* item);
    // Releases `entry` and takes it out of the list (nothing when it is not there).
    void RemoveWeapon(CItem* entry);
    void RemoveQuickItem(CItem* entry);
    void RemoveArmor(CItem* entry);
    void RemoveSpell(CItem* entry);

    // ClearItems, then each of the four lists sized to `source`'s and filled with `party`'s copies of its items, each
    // with the source item's count and a reference.
    void CopyItemsFrom(const PartyUnitRecord* source, CParty* party);
    // The weights of the entries of the four lists added up (empty entries skipped).
    float CarriedWeight() const;

    // ClearItems and the defaults, then texts 0, 2-5 (text 6 becomes a copy of text 5), unitId, the stats,
    // scaleX / scaleY / height and the four lists (a count, then item ids resolved in the item manager of the
    // party being loaded, and counted), then text 1; from version 0x51 moveMode2, weaponIndex and selected,
    // from 0x5B leaderId, from 0x61 fightAtWill, from 0x6B text 6.
    void Load(CStream* stream);
    // Writes what Load reads (newest version).
    void Save(CStream* stream);

    std::string texts[7];          // names (empty when constructed)
    std::uint32_t unitId = 0;           // the unit's id (saved and loaded)
    UnitStats stats;                    // the unit's statistics
    float scaleX = 0.8f;                // the figure's scale: x ...
    float scaleY = 0.2f;                // ... y; height is z
    float height = 1.0f;            // the hero's height (the hero screen shows it * 100)
    std::vector<CItem*> weapons;
    std::vector<CItem*> quickItems;   // (an entry counts only with count_ > 0)
    std::vector<CItem*> armor;
    std::vector<CItem*> spells;
    int moveMode2 = 2;                  // the unit's moveMode2
    std::uint32_t weaponIndex = 0;  // the weapon shown in the camp
    std::uint8_t selected = 0;          // the unit is selected (flags_ bit 4)
    int leaderId = -1;                  // the unit it follows (-1 none)
    int fightAtWill = 1;                // the unit's fightAtWill
};

// Wears `item` by `amount` (shared by its count_ pieces); the owner says `brokenRemark` when its condition
// rounds to 0 or less, `wornRemark` when it drops below the itemDurabilityCritical share of its maximum.
void WearItem(CItem* item, CUnit* unit, float amount, int brokenRemark, int wornRemark);
// `item` joins `items` and its spell `spells`.
void GatherItem(std::list<CItem*>* items, std::list<CSpell*>* spells, CItem* item);
// Text-key units: the armour pieces covering hit location `location` at layer `layer` (1 or 2) wear by
// `amount`.
void WearArmour(CUnit* unit, int location, int layer, float amount);
// The skill bonus byte of the weapon prototype `itemId >> 16`, as a float.
double WeaponPrototypeSkillBonus(std::uint32_t itemId);

// `value` scaled by the unit's strengthen / weaken effects (types 0x1D / 0x1E) with k = 0.003, at least 1: (1 +
// strengthen * k) / (1 + weaken * k), 1 without a unit.
float ScaleDamageByEffects(CUnit* unit, float value);
// The scale itself with k = 0.005, unrounded.
double HitPointEffectScale(CUnit* unit);
// `value` scaled with k = 0.005, at least 1.
float ScaleHitPointsByEffects(CUnit* unit, float value);
static_assert(offsetof(UnitStats, attributes) == 0x6C8);
static_assert(offsetof(UnitStats, skillLevels) == 0x6D3);
static_assert(sizeof(UnitStats) == 0x704);

// Resets a hit: all weights 1, multiplier 1, attackRoll and amount 3.4e36, location 6, the rest 0.
std::uint32_t ResetDamageInfo(DamageInfo* info);
// Has the hit a non-zero weight for damage type 3, 4 or 5 (NaN counts as zero)?
BOOL HasElementalDamage(const DamageInfo* info);

// The server-side state object of map objects (class id 0x39): the hit points.
class CMapObjectServer : public CMapObjectCompanion {
public:
    CMapObjectServer();
    ~CMapObjectServer() override;
    const char* GetClassName() override;
    int GetClassId() override;

    // Hit points rounded to a 16-bit value are <= 0.
    BOOL IsDead() override;
    // Hit points below -5 times the maximum.
    BOOL IsOverkilled() override;

    // Per-tick update: counts down the owner's effects and removes the expired ones. Returns 0.
    int RunTick() override;
    // Copies the vitals block (0x58 bytes) from `data` and marks the owner's part 0 changed.
    void SetData(const UnitVitals* data);
    // Applies a hit to the vitals; a dead object loses its effects and gets flags_ bit 1. Returns 0 when the
    // vitals take nothing, else 1.
    virtual BOOL TakeHit(const DamageInfo* hit);

    // The vitals block, the two names and the string array (a count, then each string with its terminator;
    // Load appends to the array).
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // No map object, the vitals reset. Returns 1.
    int Create() override;
    void Destroy() override;

    UnitVitals vitals;
    std::string name78;
    std::string name7C;
    std::vector<std::string> strings;
};
inline CMapObjectServer* CMapObject::ServerCompanion() const { return static_cast<CMapObjectServer*>(companion); }

class CParticleSource;

// One effect particle record of the client companion.
struct EffectParticle {
    // Out of line (map_object_companion_slots.cpp): releasing the source needs the complete CParticleSource.
    EffectParticle() = default;
    EffectParticle(EffectParticle&&) noexcept;
    EffectParticle& operator=(EffectParticle&&) noexcept;
    ~EffectParticle();

    int type = 0;                       // the effect type
    int power = 0x10;                   // the effect's power byte
    HeldPtr<CParticleSource> source;    // null when the effect has no lasting source
    std::uint8_t present = 0;           // set while the effect list is compared
};

// One looping effect sound record.
struct EffectSoundRecord {
    int kind = 0;               // the effect type
    int spareHandle = -1;       // nothing reads it
    int handle = -1;            // feeble mind's sound sequence, -1 none
    std::uint32_t timer = 0;    // ticks to the next stench sound
    std::uint8_t present = 0;   // set while the effect list is compared
};

// An effect's sound at `position`: "<folder of kind><suffix>" when `play` (types with a folder); stench
// (0x13) only clears the record's timer; feeble mind (0x20) starts its sound sequence into the record (and
// the bare suffix) or stops it and plays its end sound.
void PlayEffectSound(bool play, EffectSoundRecord* record, const float* position, int kind,
                                const char* suffix);

// The client-side companion of a map object (GetClassName "CMapObjectClientSpecific"); its owner is a unit for IsDead
// / IsOverkilled.
class CMapObjectClientSpecific : public CMapObjectCompanion {
public:
    CMapObjectClientSpecific();
    ~CMapObjectClientSpecific() override;
    const char* GetClassName() override;
    int GetClassId() override;

    BOOL IsDead() override;
    BOOL IsOverkilled() override;
    // A hit of `damage` with the hit-kind bits `kind` on a living object flagged 0x400000 shows its number
    // over it.
    void Hit(int damage, int kind) override;

    // No light state, the particle array emptied, no end sound, no map object. Returns 1.
    int Create() override;

    // Effect countdown and end sounds, looping effect sounds, the light following the object. Returns 1.
    int RunTick() override;

    // The object's effect list changed; the particles and the looping sounds follow it. `first` is set when
    // the list arrives with the object (no sounds; particles run 30 ticks ahead, the scale effects' one-shot
    // rings are left out).
    void EffectsChanged(bool first) override;
    // One particle record per effect (types merged to the highest power); records still matching are kept (a power
    // change rescales the source), new ones make the source of their type (0x0F invisibility and 0x19 stun set flags_
    // 0x10000 / 0x40000 on the object; 0x11 / 0x1D / 0x1E only spawn a ring); records whose effect is gone stop their
    // source and undo their flag when no other record of the type is left.
    void UpdateEffectParticles(bool first);
    // One sound record per effect type: a new type plays its "<sound>\start.wav", a vanished one its
    // "<sound>\end.wav" (unless the object is dead) and is removed.
    void UpdateEffectSounds(bool first);
    // A new point light (the light factory's class 0x31) of `colour` (rgb of an rgba) and `radius`, 2 above
    // the object, replaces the light (the old one hidden first); flags_ 0x580 (0x40 more with `flag`), then
    // shown.
    void CreateLight(const float* colour, float radius, bool flag);
    // A hidden light (flags_ 0x80) that is not held (0x200) is shown: the light manager adds it and a unit's
    // client half lights its figure.
    void ShowLight();
    // A shown light not held is hidden: the light manager drops it and the client half unlights the figure.
    void HideLight();
    // Mode 1: the light (a new default one when there is none) is shown and held (flags_ 0x200), its id added
    // to the light factory's list; mode 2: a light is hidden, its id removed from the list and the light
    // released.
    void SetLightMode(int mode) override;
    // Everything released: particles, light, no object.
    void Destroy() override;

    CUnit* Unit() const { return static_cast<CUnit*>(owner); }

    std::uint32_t lightStateA;          // cleared by Construct, Create and Destroy; nothing reads it
    std::uint32_t lightStateB;          // the same
    std::vector<EffectParticle> particles;  // the effect particles
    std::vector<EffectSoundRecord> sounds;        // the looping effect sounds
    HeldObject<CLight> light;       // counted
    int endSound;           // the end sound's handle (-1 none)
};
inline CMapObjectClientSpecific* CMapObject::ClientCompanion() const { return static_cast<CMapObjectClientSpecific*>(companion); }

