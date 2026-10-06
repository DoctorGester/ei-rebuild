#pragma once

// Units (creatures; GetClassName "CUnit", class id 0x50, vtable 0073EA8C; the CFakeUnit vtable
// 0073BFE8 derives from it). Field and method names are provisional, from how the code uses them.
// The AI map (ai_map.h) and the sector map use the position, sector cell and size class fields.

#include "engine_object.h"
#include "item_db.h"
#include "maths.h"

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class CItem;   // item.h
class CParty;   // party.h
class CStream;   // core_streams.h
struct UnitStats;
struct UnitParams;
struct DamageInfo;   // spell.h
struct FootprintPart;   // ai_map.h

// The stream the game's Load / Save slots read and write (the same interface as unit_ai.h's
// AIStream): slot 1 reads, slot 2 writes; +0x08 is the format version of a stream being loaded.
struct UnitStream {
    virtual void Slot0() = 0;
    virtual int Read(void* buffer, unsigned size) = 0;          // 1: nonzero when read
    virtual void Write(const void* buffer, unsigned size) = 0;   // 2

    int unknown_04;
    int version;            // +0x08
};

constexpr int kClassUnit = 0x50;    // CEngineObject::GetClassId() of units (creatures)

// CEngineObject::type_ of the player's own units (other kinds: 0x33 ...).
constexpr int kUnitTypeHero = 0x32;

// Class id of the unit's controller (CUnit + 0x240) for the units in a player's party
// (GetClassName "CUnitClientSpecific"); their items live in the player's inventory records.
constexpr int kClassPartyController = 0x3D;

// An entry of a unit's active effect list (+0x198, 0x14 bytes). The low byte of `kind` is the
// effect type (0x20: one that stops spell casting).
struct UnitEffect {
    std::uint32_t kind;
    std::uint32_t unknown_04[4];
};
static_assert(sizeof(UnitEffect) == 0x14);

// The record the player keeps for each unit of the party (0x784 bytes, found by unit id through
// 006621B0; a party's record array, party.h). Each item list is an aligned pointer array (data,
// count, capacity, grow-by; AlignedPtrArray's layout) of the record's own copies of the items.
struct PartyUnitRecord {
    // 0065F5F0 (party_lifetime.cpp). Constructor body: seven empty strings, empty item lists, the
    // defaults below (stats left as they are), ClearItems, the defaults again. Returns this.
    PartyUnitRecord* Construct();
    // 0065F740. Destructor body: ClearItems, the defaults, the lists' storage freed (none is left),
    // the strings destroyed (the last first).
    void Destruct();
    // 0065F860. Releases every item of the four lists (slot 4), then empties them (SetSize(0, -1),
    // inlined for the first two).
    void ClearItems();

    // 006608D0 / 00660AB0 / 00660C90 / 00660E70 (RET 4; party_items.cpp). A copy of `item` (its
    // Clone with the world's spell manager), counted and appended to the weapons / quick items /
    // armour / spells; returns the copy.
    CItem* AddWeapon(CItem* item);
    CItem* AddQuickItem(CItem* item);
    CItem* AddArmor(CItem* item);
    CItem* AddSpell(CItem* item);
    // 00660A50 / 00660C30 / 00660E10 / 00660FF0 (RET 4). Releases `entry` and takes it out of the
    // list (nothing when it is not there).
    void RemoveWeapon(CItem* entry);
    void RemoveQuickItem(CItem* entry);
    void RemoveArmor(CItem* entry);
    void RemoveSpell(CItem* entry);

    // 00660500 (RET 8; party_members.cpp). ClearItems, then each of the four lists sized to
    // `source`'s and filled with `party`'s copies (CParty slot 24) of its items, each with the
    // source item's count (+0x48) and a reference.
    void CopyItemsFrom(const PartyUnitRecord* source, CParty* party);
    // 00664950 (RET 4). operator=: the strings assigned, everything else copied (the item lists'
    // pointers too: the lists are shared, not copied). Returns this.
    PartyUnitRecord* Assign(const PartyUnitRecord* source);

    // 0065F9B0 (RET 4; party_save.cpp). ClearItems and the defaults, then texts 0, 2-5 (text 6
    // becomes a copy of text 5), +0x1C, the stats (005287A0), +0x724..0x72F and the four lists
    // (a count, then item ids resolved in the item manager of the party being loaded, 007C8344,
    // and counted), then text 1; from version 0x51 +0x770, +0x774 and the byte +0x778, from 0x5B
    // +0x77C, from 0x61 +0x780, from 0x6B text 6.
    void Load(CStream* stream);
    // 0065FDE0 (RET 4). Writes what Load reads (newest version): strings with their zero, item
    // ids (-1 for an empty entry).
    void Save(CStream* stream);

    mfc::CString texts[7];          // +0x000 names (empty when constructed)
    std::uint32_t unknown_01C;      // +0x01C saved and loaded
    std::uint8_t stats[0x724 - 0x20];   // +0x20 the unit's statistics (UnitStats)
    float unknown_724;              // +0x724 0.8 when constructed
    float unknown_728;              // +0x728 0.2 when constructed
    float height;                   // +0x72C the hero's height (1.0 when constructed; the hero screen shows it * 100)
    CEngineObject** weapons;        // +0x730
    std::uint32_t weaponCount;      // +0x734
    int weaponCapacity;             // +0x738
    int weaponGrowBy;               // +0x73C
    CEngineObject** quickItems;     // +0x740 (an entry counts only with +0x48 > 0)
    std::uint32_t quickItemCount;   // +0x744
    int quickItemCapacity;          // +0x748
    int quickItemGrowBy;            // +0x74C
    CEngineObject** armor;          // +0x750
    std::uint32_t armorCount;       // +0x754
    int armorCapacity;              // +0x758
    int armorGrowBy;                // +0x75C
    CEngineObject** spells;         // +0x760
    std::uint32_t spellCount;       // +0x764
    int spellCapacity;              // +0x768
    int spellGrowBy;                // +0x76C
    int unknown_770;                // +0x770 2 when constructed
    std::uint32_t weaponIndex;      // +0x774 the weapon shown in the camp (0 when constructed)
    std::uint8_t unknown_778;       // +0x778 0 when constructed
    std::uint8_t unknown_779[3];
    int unknown_77C;                // +0x77C -1 when constructed
    int unknown_780;                // +0x780 1 when constructed
};
static_assert(offsetof(PartyUnitRecord, stats) == 0x20);
static_assert(offsetof(PartyUnitRecord, unknown_724) == 0x724);
static_assert(offsetof(PartyUnitRecord, weaponIndex) == 0x774);
static_assert(offsetof(PartyUnitRecord, unknown_780) == 0x780);
static_assert(sizeof(PartyUnitRecord) == 0x784);
static_assert(offsetof(PartyUnitRecord, weapons) == 0x730);
static_assert(offsetof(PartyUnitRecord, quickItems) == 0x740);
static_assert(offsetof(PartyUnitRecord, armor) == 0x750);
static_assert(offsetof(PartyUnitRecord, spells) == 0x760);

// The player's list of party unit records (player + 0x70).
struct PartyRecords {
    // 006621B0. The record of the unit with id `unitId`, or null (not rewritten).
    PartyUnitRecord* Find(int unitId);
};

// One of a unit's six body part records (+0x374, 0x1C bytes) as the unit panel reads them: the
// part's state (0 none: not shown; 1 drawn in red; 3 with a bar of `durability` / `maxDurability`).
struct UnitBodyPart {
    int state;                      // +0x00
    std::int16_t durability;        // +0x04
    std::uint16_t maxDurability;    // +0x06
    std::uint8_t unknown_08[0x1C - 0x08];
};
static_assert(sizeof(UnitBodyPart) == 0x1C);

struct AIPath;       // ai_map.h
struct UnitMotion;   // ai_motion.h
class CObject3D;     // map_object_figures.h
class CMapObject;    // map_object.h

// The unit's controller (CUnit + 0x240) as the unit code calls it: CUnitServer (class id 0x3E,
// vtable 0073EF38, combat.h) on the server, CUnitClientSpecific (0x3D, 0073ECD8,
// unit_client_models.h) for party units on the client; their abstract base has vtable 0073EDBC.
// Only the slots the rewritten unit code calls are named. The controller classes do not derive
// from this declaration (their rewritten slots are registered by address); CUnit::Controller()
// reaches it. unit_ai.h's UnitController names fields of the same objects.
class CUnitController : public CEngineObject {
public:
    virtual void Slot13();
    virtual void Slot14();
    virtual void Slot15();
    virtual AIPath* Path();                 // 16: server +0x3C (00546090), client +0x38 (00533DF0)
    virtual void Slot17();
    virtual void PlayerChanged(void* player);   // 18: client 00534140, base empty (00533BC0)
    virtual void Slot19();
    virtual void Slot20();
    virtual void Slot21();
    virtual void Slot22();
    virtual void Slot23();
    virtual void Slot24();
    virtual void Slot25();
    virtual bool ArmourPutOn(std::uint32_t itemId);     // 26: client 005381B0 (WearArmourModel)
    virtual bool ArmourTakenOff(std::uint32_t slot);    // 27: client 005385A0 (RemoveArmourModel)
    virtual void Slot28();
    virtual void Slot29();
    virtual void Slot30();
    virtual void Slot31();
    virtual void Slot32();
    virtual void Slot33();
    virtual bool WeaponAdded(std::uint32_t itemId);     // 34: client 005388C0 (AddWeaponModel)
    virtual void Slot35();
    virtual void Slot36();
    virtual void Slot37();
    virtual void HostileMaskChanged();      // 38: server 00552CD0 (the brain), base empty (00533CA0)
};

class CUnit : public CEngineObject {
public:
    // 0050E930. Called when the unit leaves the sector map; not rewritten (sector_map.cpp).
    void OnLeftSectorMap();

    // 0050E9F0. Records that part `part` (0..15; negative: none) of the object changed, marks it
    // dirty (flags_ bit 1) and stamps it with the world's change counter. Returns what the original
    // leaves in EAX (0, or the world when the object is referenced). A method of the base class of
    // all map objects (it touches nothing beyond +0x1DE); declared here until that class exists.
    std::uintptr_t MarkChanged(int part);

    // 0050F630. Has the object an active effect of type `type` (low byte of UnitEffect::kind)?
    // Also a map object base method.
    BOOL HasEffect(std::uint32_t type);

    // 0052E940. Adds `mask` to the hostile mask (relationsA); marks part 11 changed when it grows.
    // Returns the new mask. (0052E970 sets the mask and tells the controller; not rewritten.)
    std::uintptr_t AddHostileMask(std::uint32_t mask);
    // 00530180. Sets relationsB; marks part 11.
    std::uintptr_t SetRelationsB(std::uint32_t mask);
    // 00530120. Sets the side; flags_ bit 0x80000; marks part 11 when the unit has a controller.
    std::uintptr_t SetSide(int side);
    // 00530150. Non-zero when this unit regards `other`'s side as hostile (the bit of other's
    // side in both relation masks).
    std::uint32_t IsHostileTo(const CUnit* other);

    // 0052E9B0. Sets unknown_438; marks part 13.
    std::uintptr_t SetUnknown438(int value);
    // 0052EA90. Sets the bytes at +0x254, +0x255, +0x256 and the int at +0x258; marks part 8.
    std::uintptr_t SetUnknown254(std::int8_t a, std::int8_t b, int c, std::int8_t d);
    // 0052EAD0 / 0052EAF0. Set unknown_25C / unknown_260; mark part 7 when they change.
    std::uintptr_t SetUnknown25C(int value);
    std::uintptr_t SetUnknown260(int value);
    // 0052EB10. Sets both movement modes (+0x264, +0x268); marks part 6.
    std::uintptr_t SetMoveModes(int mode, int mode2);
    // 0052EBE0. Clears (on) or sets (off) flags_ bit 0x40000000; marks part 5.
    std::uintptr_t SetFlag40000000Off(bool on);
    // 0052F7F0. Virtual slot 46: sets or clears flags_ bit 0x1000000. Returns the new flags.
    std::uint32_t SetFlag1000000(int on);

    // 0052EC10. Writes the item ids (+0x18) of the 4 weapons and then of the 7 armour pieces to
    // `out` (-1 for an empty slot).
    void GetEquipmentIds(int* out);

    // 0052F820. Can the unit cast the spell in slot `slot` (0..7)? 0 yes, 1 not enough mana, 3 no
    // spell, 4 an effect of type 0x20 prevents it. The second argument is not used.
    int CanCastSpell(std::uint32_t slot, int unused);
    // 0052F8B0. Can the quick item in slot `slot` be used? 3 none or not a spell carrier, 1 a
    // category 5 item with too little charge, else 0. The second argument is not used.
    int CanUseQuickItem(std::uint32_t slot, int unused);

    // 0052F920. Movement speeds for the current movement mode (+0x264): `forward` and `backward`
    // (the latter scaled by the global 007C5438).
    void GetMoveSpeeds(float* forward, float* backward);
    // 0052FA20. The speed of movement mode 2 (+0x268).
    double MoveSpeed2();
    // 0052FAF0. The speed of movement mode `mode` (0..4; other values: the base speed).
    double MoveSpeedFor(int mode);
    // 0052FBC0. 3 * value + 10 for units without a player (value capped at 10 when flags_ has
    // 0x4000000), else 1000000.
    double ScaledValue(float value);

    // 005302F0. The base speed (BaseSpeed) scaled by the movement mode: x0.2 when standing or
    // inactive, x0.7 in mode 1.
    double ModeSpeed();
    // 00530370. 1.8 * above[2] (+0x14C) when bit 1 of boundsFlags is set, else 0.7 * radius + center[2]
    // (a height from the figure's bounds, despite the name).
    double BaseSpeed();

    // 005307B0. Sum of +0x34 (weight) over all carried items.
    double TotalItemWeight();

    // 00530930. The unit's prototype record (index unknown_18), or null.
    char* GetPrototype();
    // 00530970. The record (0x168 bytes) the prototype's +0x08 indexes, or null.
    char* GetPrototypeModel();
    // 005309E0. The prototype record of index unknown_23C, or null.
    char* GetPrototype23C();
    // 005308A0. Stores the index of prototype record `record` in unknown_23C.
    // Returns what the original leaves in EAX (1 when `record` lies before the table).
    std::uint32_t SetPrototype23C(const char* record);

    // 00530FA0. Is the active weapon (+0x26C) of type 5 or 6 (ranged)? Returns AL only.
    bool HasRangedWeapon();

    // 00531070 / 00531180 / 00531820. Put an item into the first free weapon (4), quick item (4)
    // or spell (8) slot and take a reference: returns the slot, 1 if already there (sic), -1 if
    // full, 0 for null (weapons and quick items only).
    int AddWeapon(CEngineObject* item);
    int AddQuickItem(CEngineObject* item);
    int AddSpell(CEngineObject* item);
    // 005310D0 / 005311E0. Empty weapon / quick item slot `slot` (releasing the item); part 5.
    // Return what the original leaves in EAX (the slot when out of range).
    std::uintptr_t RemoveWeapon(std::uint32_t slot);
    std::uintptr_t RemoveQuickItem(std::uint32_t slot);

    // 005318B0 / 00531920 / 005319A0. The weapon / quick item / spell in slot `slot`, from the
    // player's party record for party units. Null when out of range.
    CEngineObject* GetWeapon(std::uint32_t slot);
    CEngineObject* GetQuickItem(std::uint32_t slot);
    CEngineObject* GetSpell(std::uint32_t slot);
    // 00531A10. The CSpell (+0x44) of the spell item in slot `slot`, or null.
    void* GetSpellObject(std::uint32_t slot);

    // 005327D0. Resistance factor against the damage type in bits 18-21 of hit + 0x14.
    float ResistanceTo(const void* hit);

    // 00532DA0. Virtual slot 18: for hero units, whether `target` + 0x18 has the bits the
    // controller asks for (0x10 by its slot 36, 0x20 by slot 37); true for other units.
    bool AcceptsTarget(const void* target);

    // 00532E00. Ratio of the 00518D30 values at +0xCC and +0x1B4.
    double AnimationRatio();

    // unit_update.cpp: virtual slots (registered by address; the vtable is not declared).
    // 0052F7D0. Slot 6, the per-tick update: the controller's update (its slot 6), then the map
    // object's (0050F1C0). Returns 1.
    int Tick();
    // 00532A80. Slot 27: the map object's scale recomputation (0050F2D0), then the controller's
    // slot 44. Returns what that leaves in EAX.
    std::uintptr_t RecomputeUnitScale();
    // 00532AB0. Slot 28 (effects changed): the controller's slot 49.
    std::uintptr_t EffectsChanged();
    // 00532AA0. Slot 30: the figures take the base scale (+0x1AC) again (00517700).
    std::uintptr_t ResetUnitScale();
    // 0052E910. Slot 26: loads the figures (00517500, unless flags_ has 0x80) and tells the
    // controller (its slot 13). Returns the loader's result, or 1.
    std::uintptr_t LoadUnitFigures();
    // 0052E870. Slot 19: a companion of type 0x48 becomes the controller (the old one is
    // detached and released; returns the new reference count); others go to the map object's
    // 0050E890.
    std::uintptr_t AttachCompanion(CEngineObject* companion);
    // 0052EB50. Slot 32 (statistics changed): the map object's 0050E970 (packs the server
    // object's vitals into +0x17C), then the controller's slot 22. Returns 1.
    int StatsChanged();
    // 0052EB30. Slot 33: the controller's slot 19 with `value` (0 without a controller).
    std::uintptr_t ScaleEffectChanged(int value);
    // 00530A20. Slot 35: a server controller (class 0x3E) copies `stats` in (00552F40). Returns
    // what is left in EAX (the controller's class id when it is not one).
    std::uintptr_t SetServerStats(const UnitStats* stats);
    // 00530A50. Slot 37: the controller's slot 23.
    std::uintptr_t UpdateSizeClasses();
    // 005303A0. Slot 48: `out` = the position with z = the ground (or water surface) height plus
    // the mode speed (ModeSpeed as a float). Returns `out`.
    float* StandPoint(float* out);
    // 0052F740. Slot 51: stores `offset` (+0x320) and puts the unit on the ground (or, for
    // movement kind 2, on the water surface plus its z or the offset).
    void PlaceUnitOnGround(float offset);
    // 00532A20. Sets the parameter block, the hit point maximum and the hit points from `stats`.
    void SetFromStats(UnitStats* stats);

    // 00532D40. The unit's level: the controller's (slot 24) for text-key units, else the
    // prototype's +0x50 (faults reading 0x50 without a prototype).
    int Level();

    // unit_hits.cpp.
    // 00530A60. For units with a player and no bow or crossbow in hand: is this unit behind
    // `target` (the direction to it against the target's facing from its orientation quaternion
    // at +0x34, below -0.5)? 0/1 in EAX.
    BOOL IsBehind(const CEngineObject* target);

    // unit_items.cpp.
    // 00530730. Puts armour piece `item` (condition +0x20 rounding above 0) into the armour slot
    // of its type (referenced; the controller's slot 26 gets its id) and releases the piece
    // that was there. Returns that piece (null for an item that does not qualify).
    CEngineObject* EquipArmour(CEngineObject* item);
    // 00530840. Empties armour slot `slot` (the controller's slot 27 gets the piece's type, the
    // piece is released). Returns the piece (released).
    CEngineObject* UnequipArmour(int slot);
    // 005315B0. Releases every item (weapons, armour with the controller's slots 27 and 28, quick
    // items, spells, the carried ones) and frees the carried array.
    void ReleaseAllItems();
    // 00531220. Adds `item` (referenced) to the carried items (an MFC CArray grown like
    // CArray::SetSize).
    void AddCarried(CEngineObject* item);
    // 005313B0. Releases carried item `index` and closes the gap.
    // Returns the new count (EAX).
    int RemoveCarried(int index);
    // 00531110. Sends the equipment ids to the controller (slot 32: the 7 armour ids, slot 33:
    // the 4 weapon ids). Returns what slot 33 leaves in EAX.
    std::uintptr_t SendEquipment();
    // 005308D0. unknown_23C = the index of the unit prototype named `name` (00514930), or -1.
    // Returns what the original leaves in EAX (-1, or the low half of its division product).
    std::uint32_t SetPrototype23CByName(const char* name);
    // 00530920. unknown_23C = `index`. Returns `index` (EAX).
    int SetPrototype23CIndex(int index);

    // map_object_lifetime.cpp.
    // 0052E110. The constructor: the map object (0050E2D0), the name, the AI memory sub-object
    // at +0x27C (005C6270), the carried array (vtable 0073EB84), then the vtable 0073EA8C and
    // Reset. Returns this.
    CUnit* Construct();
    // 0052E310. The destructor body: release (the vtable's slot 5 function, called directly),
    // the map object's Destroy (0050E840), Reset, the arrays and the name freed, then the map
    // object's destructor body.
    void DestructBody();
    // 0052E2A0. Virtual slot 0, the scalar deleting destructor: DestructBody, then the memory
    // freed when bit 0 of `flags` is set. Returns this.
    CUnit* DeletingDestructor(std::uint32_t flags);
    // 0052E5B0. Back to a blank unit: no name, prototypes, controller (released), relations,
    // player, side, items (the slots cleared without releasing) or carried array; standard
    // sizes and movement modes, the parameter block cleared (00523550), the network records
    // reset (0047FB70) and +0x438 = 1.
    void Reset();

    // unit_create.cpp (fourth round).
    // 00532AC0. After a hit: armour pieces and weapons whose condition (+0x20) rounds to 0 or
    // less leave their slots (armour: the controller's slot 27 with the type; weapons: message
    // 0x16 to the player, the slots above move down, the controller's slot 25 with -1 at the
    // end) and go to the owning player (00665470), then lose the unit's reference. Units without
    // a player keep everything.
    void DropBrokenEquipment();

    // unit_save.cpp (third round).
    // 00531A80. Virtual slot 9 (Load): the map object part, the controller, the unit's name,
    // relations, player, movement fields, items by id (counted references), the carried items,
    // and from version 0x4D the prototype names (unknown_18 / prototype23C).
    void LoadUnit(UnitStream* stream);
    // 005321C0. Virtual slot 10 (Save): the same fields in the same order (the active weapon
    // twice), items as ids (-1 for none), the prototype names as zero-terminated strings.
    void SaveUnit(UnitStream* stream);

    // unit_size_class.cpp (U5).
    // 00530570. After the controller's slot 45 (a server controller's movement factors): with a
    // server controller (class 0x3E), the size class of the movement mode (its +0x24C in mode 0,
    // +0x24E in mode 1, else +0x248), the standing height 0.9 x the larger of +0x144 / +0x148 (plus
    // 0.2 with a player, at most 2; 0.01 for size class 0 when the prototype's +0x24 is above 1.5)
    // and the radius +0x328 = 0.9 x that extent + 0.05. Other controllers: size class 0 for movement
    // kind 2, else 3, radius 0.75.
    void UpdateSizeClass();

    // unit_steal.cpp (U6).
    // 00531400. Stealing: takes the first carried item (the rest move down), or else a random
    // filled quick item slot (one IsaacRandom draw) is emptied; marks part 5 changed. Returns the
    // item, null when there is none (nothing changes then).
    CEngineObject* TakeRandomItem();

    // map_object_create.cpp (U6).
    // 0052E400. Virtual slot 2 (Create; a different name so it is no override): the map object's
    // Create (0050E770), Reset, then 14 network fields (the DeltaState at +0x1C4, SetSize 0050FA30
    // inlined). Returns 1.
    int CreateUnit();

    // unit_figure_queries.cpp (U11).
    // 005301B0. An animation entry of the unit's figure for `key` (bits as docs/notes/combat.md
    // lists them), only for units (type_ 0x32..0x34; null otherwise): a random entry matching the
    // key (005190C0), else one matching the key without its low 15 bits, else for movement bits
    // 0x8000 (alone among 0x38000) one without movement bits or with 0x10000 in place of 0x8000;
    // then, when `anyOfKind`, a random entry of the key's kind (00519380). Null when none.
    char* FindAnimation(std::uint32_t key, bool anyOfKind);
    // 00530240. The figure's animation entry `key & 0xFF` (00518F80; the key is passed whole). When
    // there is none, logs "Can't find animation entry index" through the DebugLog stub. Null then.
    char* AnimationEntry(std::uint32_t key);
    // 005304A0. The horizontal extent of the body: the box maximum's (x, y) length of the figure
    // part "bd" (else "chest"), doubled for the mosquito (type_ 0x33, prototype "unmosp"); 0 when
    // the figure has neither part. Returned in ST0 unrounded.
    double BodyExtent();
    // 00530280. The height of the ground at (x, y) (the world's slot 29); for units moving on the
    // water surface (movementKind 2) the surface height (slot 30) plus the unit's z (server units,
    // controller class 0x3E) or its heightOffset. Returned in ST0 unrounded.
    double GroundHeight(float x, float y);
    // 00532860. Where effects (lightning, particles) start from the unit: on a client, a hero's
    // right hand (part "rh3"), another unit's weapon point (the controller's 00535770) turned by
    // the orientation, or else its position plus the bounding box centre. Returns `out`. The server
    // and a hero without the part make the original rotate uninitialised stack (see the notes).
    Vector3* EffectPoint(Vector3* out);

    // unit_controller.cpp (A-U).
    // 0052E970. Sets the hostile mask (relationsA), tells the controller (slot 38), sets flags_
    // 0x80000 and marks part 11 changed.
    void SetHostileMask(std::uint32_t mask);
    // 0052E9D0. After the controller's path (slot 16) changed: the path forgets this unit as a
    // tracked object (005C7C90), the motion (+0x27C) takes the movement speeds (GetMoveSpeeds:
    // +0x3C / +0x40), its segment -1, and is rebuilt for the path (005C67D0); marks part 10.
    void RefreshMotion();
    // 005300D0. Moves the unit to `newPlayer` (nothing when it is the current one): the old
    // player loses it (00665B50), the new one gains it (00665A70), part 11 is marked and the
    // controller hears of it (slot 18).
    void SetPlayer(char* newPlayer);
    // 0052E740. Virtual slot 5 (the release): SetPlayer(null), the controller's slot 5, every
    // item slot released and emptied (quick items, weapons, spells, armour), the carried items
    // released (no null test) and their array freed (grow-by kept).
    void ReleaseUnit();
    // 0052E590. Virtual slot 3 (Destroy; another name so it is no override): the object's slot 5,
    // the map object's Destroy (0050E840), then Reset.
    void DestroyUnit();

    CUnitController* Controller() const { return static_cast<CUnitController*>(controller); }
    // The parameter block at +0x330 (the fields below repeat parts of it).
    UnitParams* Params() { return reinterpret_cast<UnitParams*>(unknown_330); }
    // The unit as the 3D object it starts with (map_object_figures.h; CUnit repeats its fields).
    CObject3D* Object3D() { return reinterpret_cast<CObject3D*>(this); }
    // The unit as the map object it starts with (map_object.h; CUnit repeats its fields).
    CMapObject* MapObject() { return reinterpret_cast<CMapObject*>(this); }
    UnitMotion* Motion() { return reinterpret_cast<UnitMotion*>(motion); }

    // Not rewritten (forwarders):
    // 00455950. The world the object lives in (owner_ + 0xAC, its slot 13), or null.
    void* World();
    // 00518D30. An animation time factor (1 for no animation).
    double AnimationFactor(float time);

    float positionX;                // +0x01C
    float positionY;                // +0x020
    float positionZ;                // +0x024
    std::uint8_t unknown_028[0x34 - 0x28];
    Quaternion orientation;         // +0x034
    std::uint8_t unknown_044[0xA4 - 0x44];
    const char* prototypeName;      // +0x0A4 e.g. "unhuma"
    std::uint8_t unknown_0A8[0xCC - 0xA8];
    float animationCC;              // +0x0CC an animation time
    std::uint8_t unknown_0D0[0x124 - 0xD0];
    // The figure parts' bounding box as CObject3D keeps it (map_object_figures.h, 00518310).
    std::uint32_t boundsFlags;      // +0x124 bits 1 and 2: the bounds are valid
    float center[3];                // +0x128 relative to the position
    float radius;                   // +0x134
    float below[3];                 // +0x138 box minimum - centre
    float above[3];                 // +0x144 box maximum - centre
    std::uint8_t unknown_150[0x160 - 0x150];
    FootprintPart* partBoxes;       // +0x160 per figure part (the index 00517990 finds by name)
    std::uint8_t unknown_164[0x178 - 0x164];
    float animationSpeed;           // +0x178 the animation's speed factor (the client's 00535850)
    std::uint8_t unknown_17C[0x17E - 0x17C];
    std::int16_t hitPoints;         // +0x17E
    std::int16_t maxHitPoints;      // +0x180
    std::uint16_t mana;             // +0x182
    std::uint16_t maxMana;          // +0x184
    std::uint8_t unknown_186[0x198 - 0x186];
    UnitEffect* effects;            // +0x198
    int effectCount;                // +0x19C
    std::uint8_t unknown_1A0[0x1B4 - 0x1A0];
    float animation1B4;             // +0x1B4 an animation time
    int sectorCellX;                // +0x1B8 cell the unit is registered in, -1 when not
    int sectorCellY;                // +0x1BC
    int changeStamp;                // +0x1C0 world + 0x68 when last changed
    std::uint8_t unknown_1C4[0x1DC - 0x1C4];
    std::uint16_t changedParts;     // +0x1DC bit per part changed (MarkChanged)
    std::uint8_t unknown_1DE[0x23C - 0x1DE];
    int prototype23C;               // +0x23C a second prototype index (-1 none)
    CEngineObject* controller;      // +0x240 (the script code calls it the perception object)
    std::uint32_t relationsA;       // +0x244 \ a unit regards side s as hostile when both have
    std::uint32_t relationsB;       // +0x248 / bit s set
    char* player;                   // +0x24C the player that owns the unit (null: none)
    int side;                       // +0x250
    std::int8_t unknown_254;
    std::int8_t unknown_255;
    std::int8_t unknown_256;
    std::uint8_t unknown_257;
    int unknown_258;
    int unknown_25C;
    int unknown_260;
    int moveMode;                   // +0x264 0..4
    int moveMode2;                  // +0x268
    std::uint32_t activeWeapon;     // +0x26C slot of the weapon in use
    int unknown_270;
    int dead;                       // +0x274 (virtual slot 55 returns it; CUnitServer::SetDead)
    std::uint32_t unknown_278;      // +0x278 ("large unit": slot 54 returns it)
    std::uint8_t motion[0xA4];      // +0x27C the unit's UnitMotion (ai_motion.h): Motion()
    float heightOffset;             // +0x320 height above the water surface (PlaceOnGround)
    int sizeClass;                  // +0x324 selects the 4-bit passability field in each AI map cell
    std::uint8_t unknown_328[4];
    float standingHeight;           // +0x32C; deeper water or higher ground than (2 - height) * 16 blocks it
    std::uint8_t unknown_330[0x33E - 0x330];
    std::uint8_t attackRate;        // +0x33E (25 / value: see UnitParamsPeriod)
    std::uint8_t movementKind;      // +0x33F 2: moves on the water surface
    std::uint8_t modeSpeeds[4];     // +0x340 speed bytes of movement modes 3 (and 2's backward), 2, 1, 0
    std::uint8_t baseSpeed;         // +0x344
    std::uint8_t unknown_345[0x374 - 0x345];
    UnitBodyPart bodyParts[6];      // +0x374 as the unit panel shows them (00647B70)
    std::uint8_t unknown_41C[0x438 - 0x41C];
    int unknown_438;                // +0x438
    std::uint32_t unknown_43C;
    CEngineObject* weapons[4];      // +0x440
    CEngineObject* quickItems[4];   // +0x450
    CEngineObject* spells[8];       // +0x460
    CEngineObject* armor[7];        // +0x480
    std::uint32_t unknown_49C;
    CEngineObject** carried;        // +0x4A0 CArray of further items
    int carriedCount;               // +0x4A4
    int carriedCapacity;            // +0x4A8
    int carriedGrowBy;              // +0x4AC (0: an eighth of the count, 4..1024)
    float resistances[6];           // +0x4B0 per damage type (see ResistanceTo)
    std::uint8_t unknown_4C8[0x4DC - 0x4C8];
};
static_assert(offsetof(CUnit, prototypeName) == 0xA4);
static_assert(offsetof(CUnit, orientation) == 0x34);
static_assert(offsetof(CUnit, boundsFlags) == 0x124);
static_assert(offsetof(CUnit, center) == 0x128);
static_assert(offsetof(CUnit, radius) == 0x134);
static_assert(offsetof(CUnit, above) == 0x144);
static_assert(offsetof(CUnit, partBoxes) == 0x160);
static_assert(offsetof(CUnit, hitPoints) == 0x17E);
static_assert(offsetof(CUnit, mana) == 0x182);
static_assert(offsetof(CUnit, maxMana) == 0x184);
static_assert(offsetof(CUnit, bodyParts) == 0x374);
static_assert(offsetof(CUnit, effects) == 0x198);
static_assert(offsetof(CUnit, sectorCellX) == 0x1B8);
static_assert(offsetof(CUnit, changedParts) == 0x1DC);
static_assert(offsetof(CUnit, controller) == 0x240);
static_assert(offsetof(CUnit, side) == 0x250);
static_assert(offsetof(CUnit, moveMode) == 0x264);
static_assert(offsetof(CUnit, activeWeapon) == 0x26C);
static_assert(offsetof(CUnit, sizeClass) == 0x324);
static_assert(offsetof(CUnit, standingHeight) == 0x32C);
static_assert(offsetof(CUnit, modeSpeeds) == 0x340);
static_assert(offsetof(CUnit, unknown_438) == 0x438);
static_assert(offsetof(CUnit, weapons) == 0x440);
static_assert(offsetof(CUnit, armor) == 0x480);
static_assert(offsetof(CUnit, carried) == 0x4A0);
static_assert(offsetof(CUnit, resistances) == 0x4B0);
static_assert(offsetof(CUnit, dead) == 0x274);
static_assert(offsetof(CUnit, heightOffset) == 0x320);
static_assert(sizeof(CUnit) == 0x4DC);    // operator new(0x4DC) in 00512730

constexpr std::uint32_t kInSectorMap = 0x8000;    // CEngineObject::flags_

// A body slot as UnitParams keeps it (0x1C bytes; UnitStatSlot rounded).
struct UnitParamsSlot {
    int type;                       // +0x00
    std::uint8_t vitalMask;         // +0x04
    std::uint8_t unknown_05;
    std::int16_t protection[7];     // +0x06 the three layers' absorb values summed, per damage type
    int kind;                       // +0x14
    std::int16_t current;           // +0x18
    std::int16_t maximum;           // +0x1A
};
static_assert(sizeof(UnitParamsSlot) == 0x1C);

// The unit's parameter block (CUnit + 0x330, 0x108 bytes): UnitStats rounded to bytes and
// shorts (00522CE0 writes it), what the client and the saved games see. Names provisional.
struct UnitParams {
    // 005271C0. 25 / attackRate.
    double Period();
    // 00523550. Zeroes the block; the slot types become 4 and +0x20 7. Returns 0 (EAX).
    int Reset();
    // 00527850 (unit_params_skill.cpp; the client's GetAttackParams 00538960 uses it for party
    // units). UnitStats::WeaponSkillBonus on the packed bytes, with the weapon's prototype record
    // `weapon` (or null) given: types 0-4 skill 0 + the weapon's +0x7B, 5-6 skill 1 + +0x7C, each
    // plus (the stat named by the skill's first letter - 25) and the level value of skill `type`;
    // type 8 skill 0 + +0x7B (+ the stat - 25); other types 0. Returned in ST0 unrounded.
    double WeaponSkillBonus(int type, const char* weapon);

    std::uint8_t unknown_00[4];
    int experience;                 // +0x04
    int unspentExperience;          // +0x08 experience + pending - spent
    std::uint8_t stat10;            // +0x0C
    std::uint8_t stat18;            // +0x0D
    std::uint8_t attackRate;        // +0x0E (UnitStats::stat20 rounded)
    std::uint8_t movementKind;      // +0x0F (UnitStats +0x60)
    std::uint8_t modeSpeeds[4];     // +0x10 (UnitStats +0x34.. * 255 / 2, at most 255)
    std::uint8_t baseSpeed;         // +0x14 (UnitStats::speed * 25.5, at most 255)
    std::uint8_t rate;              // +0x15
    std::int16_t capacity;          // +0x16
    std::uint8_t combat[5];         // +0x18 reach, attack, attack spread, defence, speed
    std::uint8_t damage;            // +0x1D (truncated)
    std::uint8_t damageSpread;      // +0x1E (truncated)
    std::uint8_t naturalWeapon;     // +0x1F
    std::int16_t unknown_20;        // +0x20 7 after Reset
    std::uint8_t valuesB0[6];       // +0x22 UnitStats::valuesB0[i].current
    std::uint8_t valuesE0[6];       // +0x28 UnitStats::valuesE0[i].current
    std::uint8_t unknown_2E[2];
    UnitParamsSlot slots[6];        // +0x30
    std::uint8_t attributes[3];     // +0xD8
    std::uint8_t skills[8];         // +0xDB
    std::uint8_t skillLevels[0x24]; // +0xE3
    std::uint8_t flags;             // +0x107 (UnitStats::flags, low byte)
};
static_assert(offsetof(UnitParams, attackRate) == 0x0E);
static_assert(offsetof(UnitParams, combat) == 0x18);
static_assert(offsetof(UnitParams, slots) == 0x30);
static_assert(offsetof(UnitParams, attributes) == 0xD8);
static_assert(offsetof(UnitParams, flags) == 0x107);
static_assert(sizeof(UnitParams) == 0x108);

// The hit points, mana and protection rounded (CUnit + 0x17C, 0x18 bytes; 00522C50 writes it).
struct UnitVitalsPacked {
    // 005233B0. Zeroes it. Returns 0 (EAX).
    int Reset();

    std::uint8_t unknown_00;
    std::uint8_t unknown_01;
    std::int16_t hitPoints;         // +0x02
    std::int16_t maxHitPoints;      // +0x04
    std::int16_t mana;              // +0x06
    std::int16_t maxMana;           // +0x08
    std::int16_t protection[7];     // +0x0A
};
static_assert(sizeof(UnitVitalsPacked) == 0x18);

// Unit prototypes (global vectors; see item_db.h for the lookup rule).
namespace unitdb {
constexpr int kPrototypeSize = 0x120;
constexpr int kModelSize = 0x168;
constexpr int kSkillSize = 0x18;
constexpr int kSkillLevelSize = 0x44;
inline const RecordTable& Prototypes() { return *reinterpret_cast<const RecordTable*>(0x007AF358); }
inline const RecordTable& Models() { return *reinterpret_cast<const RecordTable*>(0x007AF398); }
inline const RecordTable& Skills() { return *reinterpret_cast<const RecordTable*>(0x007AF268); }
inline const RecordTable& SkillLevels() { return *reinterpret_cast<const RecordTable*>(0x007AF274); }
// 0073E048: for each armour type, the three body slots it covers (7: none); UnitStats::AddArmourLayer.
inline const int* ArmourCoverage() { return reinterpret_cast<const int*>(0x0073E048); }
} // namespace unitdb

// One of the three protection layers of a body slot (0x48 bytes): what each of the seven damage
// types loses to it.
struct UnitStatLayer {
    // 005236A0. Zeroes it but unknown_20. Returns 0 (EAX).
    int Reset();

    int unknown_00;
    float absorb[7];                // +0x04 per damage type
    float unknown_20[7];            // +0x20
    int state;                      // +0x3C > 1: the layer is there
    int unknown_40;
    int unknown_44;
};
static_assert(sizeof(UnitStatLayer) == 0x48);

// One of the six body slots of UnitStats (0xF4 bytes; 0 head, 1 body, 2/3 hands, 4/5 legs, as
// the hit locations). Names provisional.
struct UnitStatSlot {
    // 00523600. Type 4, the rest 0 but the layers' unknown_20, unknown_EC and factor. Returns 0
    // (EAX).
    int Reset();

    int type;                       // +0x00 2 arms, 3 legs (00524110 / 00524060); reset to 4
    std::uint32_t vitalMask;        // +0x04 damage types whose full hit severs the slot
    UnitStatLayer layers[3];        // +0x08
    int kind;                       // +0xE0 0 unused, 1 severed, 2 disabled, 3 working (> 1: counts)
    float current;                  // +0xE4 its hit points
    float maximum;                  // +0xE8
    float unknown_EC;
    float factor;                   // +0xF0 its share of the unit's hit points
};
static_assert(sizeof(UnitStatSlot) == 0xF4);
static_assert(offsetof(UnitStatSlot, kind) == 0xE0);

// A value and what it currently is after modifiers.
struct UnitStatPair {
    float base;
    float current;
};

struct UnitStats;

// The hit points, mana and protection of a unit as the server keeps them (CMapObjectServer +
// 0x20, 0x58 bytes; party records keep one at +0x20 of theirs too). Names provisional.
struct UnitVitals {
    // 00523740 (unit_vitals.cpp). Called every world update for living units: every 15th call
    // mana (unless `inCombat`) and hit points (through the slots) regenerate. Returns AL: 1 when
    // something may have changed.
    bool Regenerate(UnitStats* stats, CUnit* unit, char inCombat);
    // 00524FE0 (unit_hits.cpp). A hit on a map object that is not a creature: each damage type
    // beyond the protection comes off the hit points, not below -5 times the maximum. Returns 1.
    BOOL TakeHit(const DamageInfo* hit);
    // 00523370. Zeroes everything but the dword at +0. Returns what the original leaves in EAX
    // (the end of the block).
    void* Reset();
    // 00522C50. The rounded (FISTP) values into `out`. Returns the end of `out`'s protection
    // (EAX).
    void* Pack(UnitVitalsPacked* out);

    int unknown_00;
    int regenTicks;                 // +0x04 counts 1..15
    float hitPoints;                // +0x08
    float maxHitPoints;             // +0x0C
    float hitPointRegen;            // +0x10 fraction of the maximum per regeneration
    float mana;                     // +0x14
    float maxMana;                  // +0x18
    float manaRegen;                // +0x1C
    UnitStatPair protection[7];     // +0x20 per damage type (current: what a hit loses to it)
};
static_assert(offsetof(UnitVitals, hitPoints) == 0x08);
static_assert(offsetof(UnitVitals, protection) == 0x20);
static_assert(sizeof(UnitVitals) == 0x58);

// A creature's computed statistics (0x704 bytes; built from the unit's parameters, often
// as a stack copy). Only the fields read here; names provisional.
struct UnitStats {
    // The float results below are returned unrounded in ST0 like the original (declared double:
    // the FPU runs above 24-bit precision under Rebirth, see CItemWeapon::RollDamage).
    // 00523CD0. 25 / unknown_20.
    double Period20();
    // 00525DF0. Attribute byte `index` (+0x6C8) as a float.
    float AttributeBase(int index);
    // 00525E10. Attribute `index` (0..2) with the bonus of the stat it depends on (stat - 25).
    double AttributeWithBonus(int index);
    // 00527030. Skill byte `index` (+0x6CB) as a float.
    float SkillBase(int index);
    // 00527050. Skill `index` plus (stat - 25) of the stat its name starts with ('s', 'd', 'i').
    double SkillWithBonus(std::uint32_t index);
    // 00526780. The weapon skill for weapon type `type`.
    float WeaponSkill(int type);
    // 005267E0. +0x34 of the level record of skill `index` (0 without one), exact in ST0.
    double SkillLevelValue(int index);
    // 00528270. The level record of skill `index` (level byte at +0x6D3), or null.
    char* SkillLevelRecord(int index);
    // 00524280. stat10 * 12 scaled by (1 + 0.01 * level value) of the level byte at +0x6E8.
    double Value6E8();
    // 00525CD0. global 007C5430 + 0.01 * level value of the byte at +0x6E9.
    double Value6E9();
    // 00525D60. The same with an unused argument (RET 4).
    double Value6E9Arg(int unused);
    // 005259B0. `value` minus what the six slots take from it (by their kind, current, maximum
    // and factor).
    double Reduce(float value);
    // 00524020. `value` minus value * factor of each slot record of kind 1.
    double ReduceKind1(float value);
    // 00528840. Two factors from stat10 and stat18 (relative to 15, over the global 007AF280).
    // The third argument is not used.
    void GetFactors(float* a, float* b, int unused);

    // unit_vitals.cpp: recomputation, regeneration, experience.
    // 00524560. Recomputes the current values from the bases, the skill levels, the unit's
    // effects (`unit` may be null for some; the text-key test reads its id), the carried weight
    // and the slots; writes the maxima and hit points into `vitals`.
    void Recompute(UnitVitals* vitals, CUnit* unit);
    // 005243D0. Only the hit point maximum and the hit points (and flag 8 when alive).
    void RecomputeHitPoints(UnitVitals* vitals, CUnit* unit);
    // 00525800. Heals the working-or-disabled slots below their maximum by `amount`, spread by
    // their factors over `scale` (the unit's maximum); a slot back above 0.001 works again.
    // Returns AL = 1.
    bool HealSlots(float scale, float amount);
    // 00525900. HealSlots(vitals' maximum, amount), then the hit points. Returns 1.
    BOOL Heal(UnitVitals* vitals, float amount);
    // 00525930. Working slots regenerate vitals' rate of their maximum; then the hit points.
    // Returns 1.
    BOOL RegenerateSlots(UnitVitals* vitals);
    // 00525A00. Each used slot's maximum becomes `maxHitPoints` times its share relative to the
    // body's (slot 1); the current value is clamped to it.
    void SetSlotMaxima(float maxHitPoints);
    // 00525A70. The same, keeping each slot's current value in proportion.
    void ScaleSlotMaxima(float maxHitPoints);
    // 00525AF0. Slots of kind 2 or more are healed fully. Returns EAX (the end of the slots).
    void* RefillSlots();
    // 00523C80. Every used slot works again at its maximum. Returns EAX (the end of the slots).
    void* RestoreSlots();
    // 00524060 / 00524110. The lowest current / maximum ratio of the used leg (type 3) / arm
    // (type 2) slots mapped to a factor: <= 007C5444 gives 007C5448, <= 007C543C gives 007C5440,
    // else 1. The argument is not used.
    float LegsFactor(int unused);
    float ArmsFactor(int unused);
    // 005241C0. 2 when `checkLoad` and the load exceeds the capacity, 1 when a leg is below
    // 007C544C of its maximum, else 0.
    int MovementState(char checkLoad);
    // 00523E80 / 00523F50. The hit point / mana maximum from the experience curve (007C5454..
    // / 007C5468..), strength / dexterity and skill levels 16 / 17. Unrounded (ST0).
    double HitPointCurve();
    double ManaCurve();
    // 00524320. (dexterity * 0.2 + 10) scaled by the arms and skill level 20. Unrounded (ST0).
    double RateValue();
    // 00523920. Adds `amount` to the pending experience; a positive total moves into the
    // experience.
    void AddExperience(float amount);
    // 00523960. The same with `amount` scaled by 1.04 ^ (intelligence - 25).
    void AddScaledExperience(float amount);
    // 00523C20. Takes `amount` off the pending experience.
    void TakePendingExperience(float amount);
    // 005239B0. For a unit with a server object: AddScaledExperience, then the maxima of its
    // vitals from the curves (the slots scaled along) and its hit points.
    void GainExperience(CUnit* unit, float amount);
    // 00528AB0 / 00528DA0. GiveSkill: raises skill `skill` (+0x6CB) / attribute `index` (+0x6C8)
    // by the low byte of `amount`; what the new level is worth over the old one (1.61 ^ (level /
    // 007C5480) * 007C547C, truncated) is booked as experience and as spent experience. Then the
    // maxima and Recompute. False (AL) when the unit has no server object.
    bool RaiseSkill(int skill, int amount, CUnit* unit);
    bool RaiseAttribute(int index, int amount, CUnit* unit);
    // 00529090 / 00529220 / 00529400. GiveStrength / GiveDexterity / GiveIntelligence: the base
    // value grows by `amount`, the maxima are recomputed (each its own way), strength makes the
    // unit 0.2 wider and dexterity 0.2 less high (base scale +0x1AC and figure scale +0xC4), then
    // Recompute. False (AL) when the unit has no server object.
    bool RaiseStrength(int amount, CUnit* unit);
    bool RaiseDexterity(int amount, CUnit* unit);
    bool RaiseIntelligence(int amount, CUnit* unit);
    // 005282D0. A copy of SkillLevelValue (exact, ST0).
    double SkillLevelValueCopy(int skill);

    // unit_hits.cpp.
    // 005254B0. Applies `hit` (from `weapon`'s attack, may be null): flags 2 (head) / 4, the
    // vitals' protection (scaled by the difficulty for units without a player), wear of the
    // weapon, then the hit location's slot (DamageSlot) or, for location 6, every slot by its
    // share. Returns whether a slot took damage (0 when nothing passed the protection).
    int ApplyHit(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, CEngineObject* weapon);
    // 00525070. The slot of the hit's location takes each damage type through its armour layers
    // (text-key units; each layer's armour piece wears), the weapon wears, a slot may be
    // severed (-5 times its maximum) or disabled; every damage type is logged (stream 007AF284).
    // Then the hit points. Returns whether any damage passed.
    int DamageSlot(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, CEngineObject* weapon);

    // unit_create_stats.cpp. 00524CD0. The current values from the bases plus the skills (15:
    // valuesB0[1] by its percentage; 23-25: the stats; unknown_044 = -2), the slot maxima from
    // the hit point curve (current values clamped to them), `load`, the capacity, the legs and
    // arms factors and the rate. NPC statistics are built with it (scale 1).
    void ResetToBase(float load);
    // unit_create_stats.cpp. 00526450. Learns the skill level `record` of the skill database
    // (index / 3 is the skill, index % 3 + 1 the level) unless the unit knows it at that level or
    // better; when the hit point maximum of the curve (before and after, both above 1) changes,
    // the slots are healed by what keeps the hit points in proportion (00525800).
    void LearnSkill(const void* database, const void* record);
    // unit_create_stats.cpp. 00526C70. The attack skill for weapon type `type` (0-4 use skill 0,
    // 5 and 6 skill 1, 8 the bonused skill 0; others 0): for text-key units the skill byte, the
    // weapon prototype's bonus (+0x7B / +0x7C) and the stat the skill's name starts with minus 25,
    // plus the level value of skill `type`. Unrounded (ST0).
    double WeaponSkillBonus(int type, CUnit* unit);
    // unit_create_stats.cpp. 005260A0. Can the unit learn skill level `record` of the
    // skill database: learnable (+0x40), the three stats at least the record's, the required
    // attribute (+0x14 name, +0x18 index, +0x20 value) and skill level (+0x0C name) present unless
    // named "none" or empty, no known level of the same skill id (+0x10) in another skill, and the
    // record's own level (by its name, +0x04) above the one known. 0/1 in EAX.
    BOOL CanLearn(const void* database, const char* record);
    // unit_skill_price.cpp. 00528050. The experience learning skill level `record` (of the level
    // records 007AF274) costs: what its +0x30 is worth over the level below's (1.61 ^ (value /
    // 007C5480) * 007C547C, truncated; the first level of a skill over 0), times 007C5484 ^ (the
    // sum of the known skill levels, a byte), rounded to one significant digit, or two when the
    // first digit is below 4; a price of 10 or less with a first digit below 4 is rounded up.
    int SkillPrice(const char* record);
    // unit_hit_location.cpp (U6). 00525B20. The hit location an attack aimed at `location` lands
    // on: `location` while its slot works (kind 3; read without a range check), else the first
    // working neighbour (head: 1, 2, 3; body: 0, 2-5; hands 2 / 3: the other hand, then the body;
    // legs 4 / 5: the other leg, then the body), else a random working slot (one IsaacRandom
    // draw), else 1.
    int PickLocation(int location);

    // unit_stats_pack.cpp.
    // 00522CE0. The rounded values into `params`. The first argument is not used.
    void Pack(CUnit* unused, UnitParams* params);
    // 005233E0. Resets the values that the builders fill. Returns 0 (EAX).
    int Reset();
    // 005232B0. Clears layers 1 and 2 of every slot. Returns 0 (EAX).
    int ClearOuterLayers();

    // unit_armour_layers.cpp (U9).
    // 005230C0. Adds the armour piece `armour` (a CItemArmor) to the slots its type covers (up to
    // three, table 0073E048; 7 none): for each covered slot that is used, layer GetTypeGroup()
    // gains the piece's absorb values (+0x4C for the first, +0x68 for the others), its +0x00
    // the piece's mask (+0x84 / +0x88) and its state 3.
    void AddArmourLayer(CEngineObject* armour);
    // 00523300. Zeroes layers 1 and 2 of every slot, then adds each piece `unit` wears whose
    // +0x20 (a float) rounds above 0.
    void RebuildArmourLayers(CUnit* unit);
    // 00523020. out[1..7]: each damage type's protection (vitals' current value) plus the body
    // slot's layers that are there (state >= 2; layers 2, 1, 0 for text-key units, else layer 0),
    // rounded to nearest. out[0] is left alone.
    void PackBodyProtection(CUnit* unit, const UnitVitals* vitals, std::int16_t* out);
    // 00525E90. Attribute `index` for skill checks: the attribute byte for units without a text
    // key; otherwise the attribute with its stat bonus (stat - 25; 0 for other indices) plus the
    // largest bonus (prototype +0x80 + index) of the unit's quick items. Unrounded (ST0).
    double AttributeWithItems(int index, CUnit* unit);
    // 00527EB0. The skill for spell school `school` (4 counts as 3, 6 and 7 as 5): skill byte 3
    // (schools 0-2), 4 (3-4) or 5 (5-7), plus the level value of skill school + 7, plus
    // intelligence - 25. Unrounded (ST0).
    double SpellSchoolSkill(int school);

    std::uint32_t unknown_000;
    float experience;               // +0x004
    float spentExperience;          // +0x008
    float stat10Base;               // +0x00C
    float stat10;                   // +0x010 's'
    float stat18Base;               // +0x014
    float stat18;                   // +0x018 'd'
    float stat20Base;               // +0x01C
    float stat20;                   // +0x020 'i'
    float speedBase;                // +0x024
    float speed;                    // +0x028 speedBase * loadFactor
    float rateBase;                 // +0x02C 15, or dexterity * 0.2 + 10 for text-key units
    float rate;                     // +0x030 at least 1
    float modeSpeeds[4];            // +0x034
    float unknown_044;              // +0x044 -2 for text-key units
    std::uint8_t unknown_048[8];
    float load;                     // +0x050 carried weight
    float capacity;                 // +0x054
    float loadFactor;               // +0x058 LegsFactor when +0x60 is 1, else 1
    float armsFactor;               // +0x05C
    int unknown_060;                // +0x060 1 has body slots; 2 no size classes (combat.h)
    std::uint8_t unknown_064[0xB0 - 0x64];  // the combat values (combat.h CombatStats)
    UnitStatPair valuesB0[6];       // +0x0B0
    UnitStatPair valuesE0[6];       // +0x0E0
    UnitStatSlot slots[6];          // +0x110
    std::uint8_t attributes[3];     // +0x6C8
    std::uint8_t skills[8];         // +0x6CB
    std::uint8_t skillLevels[0x25]; // +0x6D3 1-based level per skill, 0 none
    std::uint32_t flags;            // +0x6F8 1 multiplied hit, 2 hit at the head, 4, 8 alive
                                    //        after a recomputation, 0x10 hit landed (combat)
    float fixedMaxHitPoints;        // +0x6FC the maximum of units without a text key
    float pendingExperience;        // +0x700
};
static_assert(offsetof(UnitStats, speedBase) == 0x24);
static_assert(offsetof(UnitStats, load) == 0x50);
static_assert(offsetof(UnitStats, valuesB0) == 0xB0);
static_assert(offsetof(UnitStats, slots) == 0x110);
static_assert(offsetof(UnitStats, flags) == 0x6F8);

// unit_hits.cpp.
// 005253E0 (__fastcall). Wears `item` by `amount` (shared by its +0x48 pieces); the owner says
// `brokenRemark` when its condition rounds to 0 or less, `wornRemark` when it drops below
// 007C549C of its maximum.
void __fastcall WearItem(CEngineObject* item, CUnit* unit, float amount, int brokenRemark, int wornRemark);
// 00525720 (__stdcall; ECX is not read). Text-key units: the armour pieces covering hit location
// `location` at layer `layer` (1 or 2) wear by `amount`.
void __stdcall WearArmour(CUnit* unit, int location, int layer, float amount);

// unit_vitals.cpp. Scales by the unit's strengthen / weaken effects (types 0x1D / 0x1E):
// (1 + strengthen * k) / (1 + weaken * k), 1 without a unit. __stdcall (some callers set ECX to
// the stats, which is not read).
// 00523D30. `value` scaled with k = 0.003, at least 1.
float __stdcall ScaleDamageByEffects(CUnit* unit, float value);
// 00523DB0. The scale itself with k = 0.005, unrounded (ST0).
double __stdcall HitPointEffectScale(CUnit* unit);
// 00523E00. `value` scaled with k = 0.005, at least 1.
float __stdcall ScaleHitPointsByEffects(CUnit* unit, float value);
static_assert(offsetof(UnitStats, attributes) == 0x6C8);
static_assert(offsetof(UnitStats, skillLevels) == 0x6D3);
static_assert(sizeof(UnitStats) == 0x704);

// A key and the lock it is tested against (levers and traps keep their lock at +0x244):
// {mask, id, level}. Names provisional.
struct LockKey {
    // 00522A80. Do the masks overlap, and either (bit 3 in common) the ids match, or (bits 0-2 in
    // common) this level is at least the lock's (unsigned)? Rebirth's FixLeversActivate compares
    // signed, and for a lock of level 0 returns this level + 1.
    BOOL Opens(const LockKey* lock);

    std::uint32_t mask;
    std::uint32_t id;
    std::uint32_t level;
};

struct DamageInfo;   // spell.h
// 00522B40 (thiscall). Resets a hit: all weights 1, multiplier 1, +0x20/+0x24 = 3.4e36, +0x2C = 6,
// the rest 0. Returns what the original leaves in EAX (0x7C23D70A).
std::uint32_t __fastcall ResetDamageInfo(DamageInfo* info);
// 00522B80 (thiscall). Has the hit a non-zero weight for damage type 3, 4 or 5 (NaN counts as
// zero)?
BOOL __fastcall HasElementalDamage(const DamageInfo* info);

// The server-side state object of map objects (GetClassName "CMapObjectServer", class id 0x39,
// vtable 0073DD6C). Only the hit points.
class CMapObjectServer : public CEngineObject {
public:
    // map_object_lifetime.cpp. 00515610 / 00515720: constructor (the engine object fields, two
    // names, a string array, vtable 0073DD6C, vitals reset; returns this) and destructor body.
    CMapObjectServer* Construct();
    void DestructBody();

    // 005156A0. Virtual slot 17: hit points rounded (FISTP) to a 16-bit value are <= 0.
    BOOL IsDead();
    // 005156C0. Virtual slot 18: hit points below -5 times the maximum.
    BOOL IsOverkilled();

    // map_object_companion.cpp (+0x1C is the map object it belongs to, map_object.h):
    // 00515820. Virtual slot 6, the per-tick update: counts down the owner's effects and removes
    // the expired ones. Returns 0.
    int Update();
    // 00515950. Copies 0x58 bytes of `data` to +0x20 and marks the owner's part 0 changed.
    std::uintptr_t SetData(const void* data);
    // 00515980. Virtual slot 20: applies a hit to +0x20 (00524FE0); a dead object loses its
    // effects and gets flags_ bit 1. Returns 1 (0 if 00524FE0 refuses).
    BOOL TakeHit(const void* hit);

    // map_object_save.cpp (A-U2). 005159D0 / 00515AC0, virtual slots 9 / 10 (Load / Save): the
    // vitals block (+0x20, 0x58 bytes), the two names and the string array (a count, then each
    // string with its terminator; Load appends to the array).
    void LoadServer(CStream* stream);
    void SaveServer(CStream* stream);
    // object_small_slots.cpp (A-U2). 005157C0, virtual slot 2 (Create): no map object, the vitals
    // reset (00523370). Returns 1. 005157E0, slot 3 (Destroy): the vitals reset, the string array
    // emptied, no map object.
    int CreateServer();
    void DestroyServer();

    std::uint8_t unknown_1C[0x20 - 0x1C];   // +0x1C the map object (map_object.h CompanionOwner)
    UnitVitals vitals;      // +0x20 hit points +0x28, maximum +0x2C
    mfc::CString name78;    // +0x78
    mfc::CString name7C;    // +0x7C
    void* stringsVtable;    // +0x80 (MFC CStringArray: vtable, data, size, capacity, grow-by)
    mfc::CString* strings;  // +0x84
    int stringCount;        // +0x88
    int stringCapacity;     // +0x8C
    int stringGrowBy;       // +0x90
};
static_assert(offsetof(CMapObjectServer, name78) == 0x78);
static_assert(offsetof(CMapObjectServer, stringCount) == 0x88);
static_assert(sizeof(CMapObjectServer) == 0x94);
static_assert(offsetof(CMapObjectServer, vitals) == 0x20);

// The client-side companion of a map object (GetClassName "CMapObjectClientSpecific", vtable
// 0073DBEC); +0x1C is the object it belongs to (a unit for these two).
class CMapObjectClientSpecific : public CEngineObject {
public:
    // map_object_lifetime.cpp. 00510450: constructor (the engine object fields, the particle and
    // sound arrays, no light, vtable 0073DBEC; returns this). 00510560: destructor body (the
    // particles and the light removed, the arrays freed).
    CMapObjectClientSpecific* Construct();
    void DestructBody();

    // 005104D0. Virtual slot 17: the unit's hit points <= 0.
    BOOL IsDead();
    // 005104F0. Virtual slot 18: the unit's hit points below -5 times its maximum.
    BOOL IsOverkilled();

    // 00510890 (map_object_companion.cpp). Virtual slot 6, the client's per-tick update: effect
    // countdown and end sounds, looping effect sounds, the light following the object. Returns 1.
    int Update();

    CUnit* object;          // +0x1C
};
