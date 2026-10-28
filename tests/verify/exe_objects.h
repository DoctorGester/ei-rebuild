#pragma once

// game.exe's objects as the harness reads them in game.exe's runs (--no-hooks, EI_VECTORS=1), at game.exe's own
// offsets (ticket H30b). Our classes (src/game) may reorder and retype their members freely: the harness reads the
// original's objects only through these views, so nothing here includes a header of src/game or src/lib, and a change
// of our classes does not change what the original's runs record (tools/run_all.py, reference_key).
//
// A view has the original's name, its size and the members the harness reads, under our names (the digest's reading
// code, harness_digest_read.inc, compiles against either); the rest is padding. Containers are game.exe's MFC 4.2
// and STLport 4 layouts. The only code called is game.exe's own (CEngineObject's slots 11 and 12). Offsets are game.exe
// 1.06's (they were our classes' when H30b split the two; docs/notes/cleanup-tooling.md, "The harness in game.exe").

#include "harness_original.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ei::verify::exe {

struct Vector3 {
    float x, y, z;
};
static_assert(sizeof(Vector3) == 0xC);

struct Quaternion {
    float w, x, y, z;
};
static_assert(sizeof(Quaternion) == 0x10);

// A Windows RECT.
struct Rect {
    int left, top, right, bottom;
};
static_assert(sizeof(Rect) == 0x10);

// MFC's CString: a pointer to the text (its header before it).
struct CString {
    const char* text;
    operator const char*() const { return text; }
};
static_assert(sizeof(CString) == 4);

// STLport's string: start, finish, end of storage. Ordered as STLport's operator< (the bytes, then the length).
struct StlpString {
    const char* start;
    const char* finish;
    const char* endOfStorage;
    const char* data() const { return start; }
    std::size_t size() const { return static_cast<std::size_t>(finish - start); }
    friend bool operator<(const StlpString& a, const StlpString& b) {
        const std::size_t n = a.size() < b.size() ? a.size() : b.size();
        if (const int c = std::memcmp(a.start, b.start, n)) return c < 0;
        return a.size() < b.size();
    }
};
static_assert(sizeof(StlpString) == 0xC);

// MFC's CArray<T> (CObject's vtable, data, size, capacity, grow-by).
template <class T>
struct Array {
    const void* vtable;
    T* data;
    int size;
    int maxSize;
    int growBy;
    int GetSize() const { return size; }
    const T& operator[](int i) const { return data[i]; }
};
static_assert(sizeof(Array<int>) == 0x14);

// A CArray<T> the rebuild holds as a std::vector: the same bytes, read through the vector's API (the shared readers
// name both sides alike).
template <class T>
struct Vector {
    const void* vtable_;
    T* data_;
    int size_;
    int maxSize_;
    int growBy_;
    std::size_t size() const { return static_cast<std::size_t>(size_); }
    bool empty() const { return size_ == 0; }
    const T* data() const { return data_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }
    const T& operator[](std::size_t i) const { return data_[i]; }
};
static_assert(sizeof(Vector<int>) == 0x14);

// MFC's CList<T> (CObject's vtable, head, tail, count, free list, blocks, block size); a node is next, prev, data.
template <class T>
struct List {
    struct Node {
        Node* next;
        Node* prev;
        T data;
    };
    const void* vtable;
    Node* head;
    Node* tail;
    int count;
    Node* free;
    void* blocks;
    int blockSize;
    int GetCount() const { return count; }
    // The std::list API of the lists the rebuild holds as std::list.
    struct Iterator {
        const Node* node;
        const T& operator*() const { return node->data; }
        Iterator& operator++() {
            node = node->next;
            return *this;
        }
        bool operator!=(const Iterator& other) const { return node != other.node; }
    };
    std::size_t size() const { return static_cast<std::size_t>(count); }
    bool empty() const { return count == 0; }
    Iterator begin() const { return {head}; }
    Iterator end() const { return {nullptr}; }
};
static_assert(sizeof(List<int>) == 0x1C);

// STLport's list<T>: the sentinel node.
template <class T>
struct StlpList {
    struct Node {
        Node* next;
        Node* prev;
        T data;
    };
    Node* head;
    Node* First() const { return head->next; }
    Node* End() const { return head; }
    bool Empty() const { return head->next == head; }
    // The std::list API of the lists the rebuild holds as std::list; a list not built yet (no sentinel) is
    // empty.
    struct Iterator {
        const Node* node;
        const T& operator*() const { return node->data; }
        Iterator& operator++() {
            node = node->next;
            return *this;
        }
        bool operator!=(const Iterator& other) const { return node != other.node; }
    };
    Iterator begin() const { return {head ? head->next : nullptr}; }
    Iterator end() const { return {head}; }
};
static_assert(sizeof(StlpList<int>) == 4);

// The game's aligned pointer array (data, count, capacity, grow-by).
template <class T>
struct AlignedArray {
    T* data;
    int size;
    int capacity;
    int growBy;
};
static_assert(sizeof(AlignedArray<int>) == 0x10);

// STLport's hashtable: the functor bytes, the bucket vector (start, finish, end of storage), the count. Walked
// like a std map (begin / end, ->first and ->second on the nodes) in iterator order: the buckets in order, each
// chain from its head.
template <class Node>
struct HashTable {
    std::uint8_t functors[4];
    Node** start;
    Node** finish;
    Node** endOfStorage;
    std::size_t count;

    class const_iterator {
    public:
        const Node& operator*() const { return *node_; }
        const Node* operator->() const { return node_; }
        const_iterator& operator++() {
            node_ = node_->next ? node_->next : table_->After(table_->start + table_->BucketOf(node_) + 1);
            return *this;
        }
        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.node_ == b.node_; }

    private:
        friend struct HashTable;
        const_iterator(const HashTable* table, const Node* node) : table_(table), node_(node) {}
        const HashTable* table_;
        const Node* node_;
    };

    std::size_t size() const { return count; }
    const_iterator begin() const { return {this, After(start)}; }
    const_iterator end() const { return {this, nullptr}; }

private:
    const Node* After(Node** bucket) const {
        for (; bucket != finish; ++bucket)
            if (*bucket) return *bucket;
        return nullptr;
    }
    // The bucket whose chain holds `node` (the view has no hash functor).
    std::size_t BucketOf(const Node* node) const {
        for (Node** bucket = start; bucket != finish; ++bucket)
            for (Node* n = *bucket; n; n = n->next)
                if (n == node) return static_cast<std::size_t>(bucket - start);
        return static_cast<std::size_t>(finish - start);
    }
};

template <class K, class V>
struct HashMapNode {
    HashMapNode* next;
    K first;
    V second;
};
template <class K, class V>
using HashMap = HashTable<HashMapNode<K, V>>;
static_assert(sizeof(HashMap<int, int>) == 0x14);

// The world's maps of objects by id: the map and the highest id put in.
struct CMapObject;
struct ObjectIdMap {
    HashMap<unsigned, CMapObject*> map;
    int maxId;
};
static_assert(sizeof(ObjectIdMap) == 0x18);

// A party's named floats: a hashtable of {CString, float}.
using NamedFloatTable = HashMap<CString, float>;
static_assert(sizeof(NamedFloatTable) == 0x14);

// An element of the network sessions' lists: a counted pointer.
struct RefPtr {
    void* ptr;
};

// Objects the harness only points at or counts.
struct AIHeardCall;
struct AILook;
struct AIMode;
struct AIReport;
struct CItem;
struct CPlayer;
struct MotionPoint;
struct MotionWaypoint;
struct SharedObject;
struct Terrain;
struct ZoneExit;

struct ScriptObjectRef;
// A script's group of objects: a CArray of object references.
struct ScriptGroup : Array<ScriptObjectRef> {};

// CEngineObject (engine_object.h): every world object's base. Slot 11 GetClassName, slot 12 GetClassId.
struct CEngineObject {
    const void* const* vtable;              // +0x00
    int refCount_;                          // +0x04
    std::uint32_t flags_;                   // +0x08
    int id_;                                // +0x0C
    int type_;                              // +0x10
    std::uint8_t unknown14[0x4];
    std::uint32_t prototypeId_;             // +0x18
    const char* GetClassName() { return CallSlot<const char*>(11); }
    int GetClassId() { return CallSlot<int>(12); }
    template <class R> R CallSlot(int slot) { return reinterpret_cast<R(__thiscall*)(CEngineObject*)>(vtable[slot])(this); }
};
static_assert(sizeof(CEngineObject) == 0x1C);

// A placed object: position and orientation.
struct CTransformObject : CEngineObject {
    Vector3 localPosition_;                 // +0x1C
    Vector3 worldPosition_;                 // +0x28
    Quaternion localRotation_;              // +0x34
    std::uint8_t unknown44[0x10];
};
static_assert(sizeof(CTransformObject) == 0x54);

// A texture (texture.h).
struct CTexture : CEngineObject {
    CString name_;                          // +0x1C
    std::uint8_t unknown20[0x48];
};
static_assert(sizeof(CTexture) == 0x68);

// A model's part set (map_object_shadow.h).
struct TextureParts {
    CTexture* texture;                      // +0x00
    std::uint8_t unknown4[0x10];
};
static_assert(sizeof(TextureParts) == 0x14);

// A 3D object's drawing half (map_object_shadow.h).
struct CObject3DClientSpecific : CEngineObject {
    std::uint8_t unknown1C[0x4];
    TextureParts* partSets[2];              // +0x20
    std::uint8_t unknown28[0xD4];
};
static_assert(sizeof(CObject3DClientSpecific) == 0xFC);

// A figure (link_figure.h).
struct CLinkMesh {
    std::uint8_t unknown0[0x8];
    StlpString name_;                       // +0x08
    std::uint8_t unknown14[0x1C];
    float morphWeights_[3];                 // +0x30
    std::uint8_t unknown3C[0x74];
};
static_assert(sizeof(CLinkMesh) == 0xB0);

// A figure in the world: its model's name and scale.
struct CObject3D : CTransformObject {
    std::uint8_t unknown54[0x40];
    CEngineObject* attached;                // +0x94
    std::uint8_t unknown98[0xC];
    CString name;                           // +0xA4
    std::uint8_t unknownA8[0x1C];
    Vector3 scale;                          // +0xC4
    std::uint8_t unknownD0[0x20];
    CLinkMesh* figure;                      // +0xF0
    std::uint8_t unknownF4[0x7C];
    CObject3DClientSpecific* ClientSpecific() const { return static_cast<CObject3DClientSpecific*>(attached); }
};
static_assert(sizeof(CObject3D) == 0x170);

// An effect on a map object (map_object.h).
struct MapEffect {
    std::uint8_t type;                      // +0x00
    std::uint8_t power;                     // +0x01
    std::uint8_t unknown2[0x2];
    int ticksLeft;                          // +0x04
    std::uint32_t value;                    // +0x08
    std::uint32_t strength;                 // +0x0C
    std::uint8_t unknown10[0x4];
};
static_assert(sizeof(MapEffect) == 0x14);

// A map object's vitals as the client sees them.
struct UnitVitalsPacked {
    std::uint8_t unknown0[0x2];
    std::int16_t hitPoints;                 // +0x02
    std::int16_t maxHitPoints;              // +0x04
    std::uint16_t mana;                     // +0x06
    std::uint16_t maxMana;                  // +0x08
    std::uint8_t unknownA[0xE];
};
static_assert(sizeof(UnitVitalsPacked) == 0x18);

// A map object (map_object.h): its server or client half, effects and sector cell.
struct CMapObject : CObject3D {
    CEngineObject* companion;               // +0x170
    int updateCountdown;                    // +0x174
    float animationSpeed;                   // +0x178
    UnitVitalsPacked packedVitals;          // +0x17C
    Vector<MapEffect> effects;              // +0x194 (map_object.h's effects)
    std::uint8_t unknown1A8[0x10];
    int sectorCellX;                        // +0x1B8
    int sectorCellY;                        // +0x1BC
    std::uint8_t unknown1C0[0x78];
};
static_assert(sizeof(CMapObject) == 0x238);

// Hit points and mana (unit.h).
struct UnitVitals {
    std::uint8_t unknown0[0x4];
    int regenTicks;                         // +0x04
    float hitPoints;                        // +0x08
    float maxHitPoints;                     // +0x0C
    std::uint8_t unknown10[0x4];
    float mana;                             // +0x14
    float maxMana;                          // +0x18
    std::uint8_t unknown1C[0x3C];
};
static_assert(sizeof(UnitVitals) == 0x58);

// A map object's server half (class 0x39).
struct CMapObjectServer : CEngineObject {
    std::uint8_t unknown1C[0x4];
    UnitVitals vitals;                      // +0x20
    std::uint8_t unknown78[0x1C];
};
static_assert(sizeof(CMapObjectServer) == 0x94);

// A unit's path and the segment it walks (unit_motion.h).
struct UnitMotion {
    Vector<MotionPoint> points;             // +0x00
    Vector<MotionWaypoint> waypoints;       // +0x14
    std::uint8_t unknown28[0x14];
    float speedScale;                       // +0x3C
    std::uint8_t unknown40[0x4];
    float turnRate;                         // +0x44
    std::uint8_t unknown48[0x4];
    float turn;                             // +0x4C
    float fromX;                            // +0x50
    float fromY;                            // +0x54
    std::uint8_t unknown58[0x8];
    float toX;                              // +0x60
    float toY;                              // +0x64
    std::uint8_t unknown68[0x8];
    float length;                           // +0x70
    float speed;                            // +0x74
    std::uint8_t unknown78[0x20];
    float segmentStart;                     // +0x98
    float segmentEnd;                       // +0x9C
    int segment;                            // +0xA0
};
static_assert(sizeof(UnitMotion) == 0xA4);

// A unit (unit.h, class 0x50).
struct CUnit : CMapObject {
    CString scriptName;                     // +0x238
    std::uint8_t unknown23C[0x4];
    CEngineObject* controller;              // +0x240
    std::uint32_t relationsA;               // +0x244
    std::uint32_t relationsB;               // +0x248
    CPlayer* player;                        // +0x24C
    int side;                               // +0x250
    std::int8_t animationEntry;             // +0x254
    std::int8_t secondEntry;                // +0x255
    std::uint8_t unknown256[0x2];
    int animationStart;                     // +0x258
    int motionMode;                         // +0x25C
    std::uint8_t unknown260[0x4];
    int moveMode;                           // +0x264
    int moveMode2;                          // +0x268
    std::uint32_t activeWeapon;             // +0x26C
    int pendingWeapon;                      // +0x270
    int dead;                               // +0x274
    std::uint32_t obstacle;                 // +0x278
    UnitMotion motion;                      // +0x27C
    float heightOffset;                     // +0x320
    int sizeClass;                          // +0x324
    std::uint8_t unknown328[0x110];
    int fightAtWill;                        // +0x438
    std::uint8_t unknown43C[0xA0];
    UnitMotion* Motion() { return &motion; }
};
static_assert(sizeof(CUnit) == 0x4DC);

// A unit controller's order (combat.h).
struct UnitAction {
    int kind;                               // +0x00
    std::uint8_t unknown4[0x4];
    CMapObject* target;                     // +0x08
    std::uint8_t unknownC[0x18];
};
static_assert(sizeof(UnitAction) == 0x24);

// A unit's statistics (unit.h).
struct UnitStats {
    std::uint8_t unknown0[0x4];
    float experience;                       // +0x04
    std::uint8_t unknown8[0x20];
    float speed;                            // +0x28
    std::uint8_t unknown2C[0x6CC];
    std::uint32_t flags;                    // +0x6F8
    std::uint8_t unknown6FC[0x8];
};
static_assert(sizeof(UnitStats) == 0x704);

// The AI's move target.
struct AIMoveOrder {
    Vector3 point;                          // +0x00
    Vector<AILook> looks;                   // +0x0C
};
static_assert(sizeof(AIMoveOrder) == 0x20);

// What the AI remembers (unit_ai.h).
struct AIMemory {
    std::uint8_t unknown0[0x10];
    float noticedX;                         // +0x10
    float noticedY;                         // +0x14
    int noticedLevel;                       // +0x18
    int noticedKind;                        // +0x1C
    std::uint8_t unknown20[0x4];
    List<CUnit*> nearby;                    // +0x24
    List<CUnit*> visible;                   // +0x40
    std::uint8_t unknown5C[0x1C];
    List<CUnit*> corpses;                   // +0x78
    Vector<AIReport> reports;               // +0x94
    Vector<AIHeardCall> calls;              // +0xA8
    std::uint8_t seesEnemy;                 // +0xBC
    std::uint8_t unknownBD[0x3];
};
static_assert(sizeof(AIMemory) == 0xC0);

// A unit controller's AI (unit_ai.h).
struct AIBrain {
    std::uint8_t unknown0[0x4];
    int action;                             // +0x04
    AIMoveOrder move;                       // +0x08
    int actionTime;                         // +0x28
    CMapObject* target;                     // +0x2C
    CMapObject* seen;                       // +0x30
    std::uint8_t alerted;                   // +0x34
    std::int8_t alarm;                      // +0x35
    std::int8_t alarmSlot;                  // +0x36
    std::uint8_t unknown37[0x1];
    float callRadius;                       // +0x38
    int alertMode;                          // +0x3C
    int alarmCountdown;                     // +0x40
    float nearX;                            // +0x44
    float nearY;                            // +0x48
    float decay;                            // +0x4C
    std::uint8_t unknown50[0x18];
    int standCooldown;                      // +0x68
    int followTimer;                        // +0x6C
    List<AIMode*> modes;                    // +0x70
    AIMode* current;                        // +0x8C
    std::uint8_t unknown90[0x40];
    AIMemory memory;                        // +0xD0
};
static_assert(sizeof(AIBrain) == 0x190);

// A unit's server controller (combat.h, class 0x3E).
struct CUnitServer : CEngineObject {
    std::uint8_t unknown1C[0x148];
    int state;                              // +0x164
    std::uint8_t unknown168[0xC];
    std::uint8_t busy;                      // +0x174
    std::uint8_t unknown175[0x17];
    std::uint8_t orderStopped;              // +0x18C
    std::uint8_t unknown18D[0x3];
    UnitAction action;                      // +0x190
    UnitAction action2;                     // +0x1B4
    std::uint8_t unknown1D8[0x8];
    UnitStats stats;                        // +0x1E0
    std::uint8_t unknown8E4[0x14];
    float moveFactors[4];                   // +0x8F8
    std::uint8_t unknown908[0x78];
    AIBrain brain;                          // +0x980
    std::uint8_t unknownB10[0x10];
    std::uint8_t Busy() const { return busy; }
};
static_assert(sizeof(CUnitServer) == 0xB20);

// A timed effect (world_effect.h).
struct CEffect : CTransformObject {
    std::uint8_t unknown54[0x40];
    int startTick_;                         // +0x94
    Vector3 velocity_;                      // +0x98
    int moving_;                            // +0xA4
    std::uint8_t unknownA8[0xA4];
};
static_assert(sizeof(CEffect) == 0x14C);

struct CEffectSound : CEffect {
    std::uint8_t unknown14C[0x100];
    float volume_;                          // +0x24C
    float radius_;                          // +0x250
    int looping_;                           // +0x254
    std::uint8_t unknown258[0xC];
};
static_assert(sizeof(CEffectSound) == 0x264);

struct CEffectParticle : CEffect {
    std::uint8_t unknown14C[0xC];
    int type_;                              // +0x158
    float size_;                            // +0x15C
    std::uint8_t unknown160[0x8];
    int attachedTo_;                        // +0x168
    Vector3 target_;                        // +0x16C
    std::uint8_t unknown178[0x4];
    int unitId_;                            // +0x17C
};
static_assert(sizeof(CEffectParticle) == 0x180);

struct CEffectPointLight : CEffect {
    Vector3 color_;                         // +0x14C
    float range_;                           // +0x158
    std::uint8_t unknown15C[0x8];
};
static_assert(sizeof(CEffectPointLight) == 0x164);

struct CEffectLightning : CEffect {
    Vector3 from_;                          // +0x14C
    Vector3 to_;                            // +0x158
    std::uint8_t unknown164[0x8];
    int toObject_;                          // +0x16C
    int fromObject_;                        // +0x170
    std::uint8_t unknown174[0x4];
};
static_assert(sizeof(CEffectLightning) == 0x178);

struct CEffectMissile : CEffect {
    int arrived_;                           // +0x14C
    int targetId_;                          // +0x150
    std::uint8_t unknown154[0x4];
};
static_assert(sizeof(CEffectMissile) == 0x158);

// A spell (spell.h).
struct CSpell : CEngineObject {
    std::uint8_t prototypeIndex_;           // +0x1C
    std::uint8_t unknown1D[0x3];
    std::uint32_t duration_;                // +0x20
    float damage_;                          // +0x24
    float radius_;                          // +0x28
    std::uint8_t unknown2C[0x10];
    std::uint32_t targetFlags_;             // +0x3C
    std::uint8_t unknown40[0x1C];
};
static_assert(sizeof(CSpell) == 0x5C);

// A spell on its way or lasting (spell.h): the server world's missiles.
struct CSpellEffect {
    const void* const* vtable;              // +0x00
    std::uint8_t unknown4[0x8];
    std::uint32_t flags;                    // +0x0C
    Vector3 position;                       // +0x10
    CMapObject* target;                     // +0x1C
    CUnit* caster;                          // +0x20
    Vector3 targetPoint;                    // +0x24
    int state;                              // +0x30
    CSpell spell_;                          // +0x34
    CEffect* visual;                        // +0x90
    CEffect* sound;                         // +0x94
    CEffectPointLight* light;               // +0x98
    std::uint8_t footprintStamped;          // +0x9C
    std::uint8_t unknown9D[0x3];
    int timer;                              // +0xA0
    std::uint8_t unknownA4[0x4];
    const CSpell* Spell() const { return &spell_; }
};
static_assert(sizeof(CSpellEffect) == 0xA8);

// An AI event (unit_ai.h).
struct AIEvent {
    float x;                                // +0x00
    float y;                                // +0x04
    std::uint32_t origin[2];                // +0x08
    int expiry;                             // +0x10
    std::uint8_t unknown14[0x8];
    int sourceId;                           // +0x1C
    int side;                               // +0x20
    std::uint8_t unknown24[0x8];
    AIEvent* nextInSlot;                    // +0x2C
};
static_assert(sizeof(AIEvent) == 0x30);

// The server world's AI events by wheel slot and side (unit_ai.h).
struct AIEventTable {
    std::uint8_t unknown0[0x4];
    int tick;                               // +0x04
    std::uint8_t unknown8[0x24];
    AIEvent* slots[0x400][16];              // +0x2C
    std::uint8_t unknown1002C[0x8];
    int width;                              // +0x10034
    int height;                             // +0x10038
};
static_assert(sizeof(AIEventTable) == 0x1003C);

// The server world's AI map (ai_map.h).
struct CAIMap {
    std::uint8_t unknown0[0x2694];
    int footprintTick;                      // +0x2694
    std::uint8_t unknown2698[0x30];
};
static_assert(sizeof(CAIMap) == 0x26C8);

struct WorldAlarm {
    std::uint8_t active;                    // +0x00
    std::uint8_t unknown1[0x3];
    int time;                               // +0x04
    float x;                                // +0x08
    float y;                                // +0x0C
};
static_assert(sizeof(WorldAlarm) == 0x10);

struct CDiplomacy {
    std::uint32_t allied_[32];              // +0x00
    std::uint32_t hostile_[32];             // +0x80
};
static_assert(sizeof(CDiplomacy) == 0x100);

struct ScriptObjectRef {
    CEngineObject* object;                  // +0x00
};
static_assert(sizeof(ScriptObjectRef) == 0x4);

// A script's variables: values and names of the four kinds (script.h).
template <typename Value>
struct ScriptNamedValues {
    Array<Value> values;                    // +0x00
    Array<CString> names;                   // +0x14
};
struct ScriptVars {
    ScriptNamedValues<float> floats;        // +0x00
    ScriptNamedValues<CString> strings;     // +0x28
    ScriptNamedValues<ScriptObjectRef> objects; // +0x50
    ScriptNamedValues<ScriptGroup> groups;  // +0x78
};
static_assert(sizeof(ScriptVars) == 0xA0);

// A command block.
struct ScriptDecl {
    std::uint8_t unknown0[0x14];
    int id;                                 // +0x14
};
static_assert(sizeof(ScriptDecl) == 0x18);

// A running script instance.
struct ScriptDefinition {
    int index;                              // +0x00
    CString name;                           // +0x04
    std::uint8_t unknown8[0x18];
    ScriptVars vars;                        // +0x20
};
static_assert(sizeof(ScriptDefinition) == 0xC0);

// A waiting run.
struct ScriptRun {
    ScriptDecl* block;                      // +0x00
    int index;                              // +0x04
    int ticks;                              // +0x08
    int sleepStart;                         // +0x0C
    int* killFlag;                          // +0x10
    ScriptVars vars;                        // +0x14
};
static_assert(sizeof(ScriptRun) == 0xB4);

struct ScriptFxSource {
    CEffectSound* object;                   // +0x00
    int id;                                 // +0x04
    std::uint8_t unknown8[0x4];
    Vector3 position;                       // +0x0C
    std::uint8_t unknown18[0x8];
    int idle;                               // +0x20
    float spread;                           // +0x24
    int minDelay;                           // +0x28
    int maxDelay;                           // +0x2C
    int countdown;                          // +0x30
};
static_assert(sizeof(ScriptFxSource) == 0x34);

struct ScriptParticleSource {
    CEffectParticle* object;                // +0x00
    int id;                                 // +0x04
    std::uint8_t unknown8[0x4];
    int moving;                             // +0x0C
    int steps;                              // +0x10
    std::uint8_t unknown14[0xC];
    int lifetime;                           // +0x20
};
static_assert(sizeof(ScriptParticleSource) == 0x24);

struct ScriptPointLight {
    CEffectPointLight* object;              // +0x00
    int id;                                 // +0x04
    std::uint8_t unknown8[0x4];
    int moving;                             // +0x0C
    int steps;                              // +0x10
    std::uint8_t unknown14[0xC];
};
static_assert(sizeof(ScriptPointLight) == 0x20);

struct ScriptLightning {
    CEffectLightning* object;               // +0x00
    int id;                                 // +0x04
};
static_assert(sizeof(ScriptLightning) == 0x8);

// The mission script (script.h).
struct CScript {
    Array<ScriptPointLight> pointLights_;   // +0x00
    List<ScriptParticleSource> particles_;  // +0x14
    List<ScriptFxSource> fxSources_;        // +0x30
    List<ScriptLightning> lightning_;       // +0x4C
    int nextParticleId_;                    // +0x68
    int nextFxId_;                          // +0x6C
    std::uint8_t unknown70[0x30];
    ScriptVars globals_;                    // +0xA0
    List<ScriptDefinition> instances_;      // +0x140
    List<ScriptRun> runs_;                  // +0x15C
    std::uint8_t unknown178[0x30];
    int loaded_;                            // +0x1A8
    int fileCount_;                         // +0x1AC
    std::uint8_t unknown1B0[0x48];
};
static_assert(sizeof(CScript) == 0x1F8);

// A party member's record (unit.h).
struct PartyUnitRecord {
    std::uint8_t unknown0[0x1C];
    std::uint32_t unitId;                   // +0x1C
    std::uint8_t unknown20[0x764];
};
static_assert(sizeof(PartyUnitRecord) == 0x784);

// A party (party.h).
struct CParty : CEngineObject {
    std::uint8_t unknown1C[0x10];
    Array<PartyUnitRecord> members_;        // +0x2C
    AlignedArray<CItem*> items_;            // +0x40
    int money_;                             // +0x50
    NamedFloatTable variables_;             // +0x54
    std::uint8_t unknown68[0x4];
    std::uint32_t entrance_;                // +0x6C
};
static_assert(sizeof(CParty) == 0x70);

// A player's connection, the base of its CPlayerServer or CPlayerClient (player_messages.h).
struct CPlayerConnection : CEngineObject {
    CPlayer* player_;                       // +0x1C
    int state_;                             // +0x20 1 in the world, 3 leaving the zone
};
static_assert(sizeof(CPlayerConnection) == 0x24);

// A player (player_messages.h).
struct CPlayer : CEngineObject {
    std::uint8_t unknown1C[0x4];
    CPlayerConnection* connection_;         // +0x20 its connection (counted)
    int playerId_;                          // +0x24
    HashMap<int, CMapObject*> knownObjects_;// +0x28
    std::uint8_t unknown3C[0x4];
    HashMap<int, CUnit*> units_;            // +0x40
    std::uint8_t unknown54[0x1C];
    CParty party_;                          // +0x70
    List<CItem*> items_;                    // +0xE0
    int partyId_;                           // +0xFC
    int sessionKind100_;                    // +0x100
    std::uint32_t lastScreen_;              // +0x104
    std::uint8_t godMode_;                  // +0x108
    std::uint8_t unknown109[0x7];
    int lastTick_;                          // +0x110
    List<std::uint32_t> recharging_;        // +0x114
    std::uint8_t unknown130[0x4];
    CParty* Party() { return &party_; }
};
static_assert(sizeof(CPlayer) == 0x134);

// The world's players (player_manager.h).
struct CPlayerManager : CEngineObject {
    std::uint8_t unknown1C[0x18];
    List<CEngineObject*> objectList_;       // +0x34
    std::uint8_t unknown50[0x60];
};
static_assert(sizeof(CPlayerManager) == 0xB0);

// What both worlds share (world.h).
struct CWorld : CEngineObject {
    float timeBase_;                        // +0x1C
    float secondsPerTick_;                  // +0x20
    std::uint8_t windDirection_[12];        // +0x24
    std::uint32_t windStrength_;            // +0x30
    std::uint8_t unknown34[0x4];
    CPlayerManager* players_;               // +0x38
    std::uint8_t unknown3C[0x20];
    RefPtr shared5C_;                       // +0x5C
    std::uint8_t unknown60[0x4];
    int tick_;                              // +0x64
    int updateCount_;                       // +0x68
    std::uint8_t paused_;                   // +0x6C
    std::uint8_t unknown6D[0x17];
};
static_assert(sizeof(CWorld) == 0x84);

// The server world (world.h).
struct CWorldServer : CWorld {
    float nightLevel_;                      // +0x84
    std::uint32_t weatherFactor_;           // +0x88
    WorldAlarm alarms_[5];                  // +0x8C
    int climate_;                           // +0xDC
    int cave_;                              // +0xE0
    int weather_;                           // +0xE4
    float weatherStrength_;                 // +0xE8
    Vector<ZoneExit> exits_;                // +0xEC
    std::uint8_t unknown100[0x4];
    ObjectIdMap updateMap_;                 // +0x104
    std::uint8_t unknown11C[0x4];
    CString zoneName_;                      // +0x120
    std::uint8_t unknown124[0x14];
    int gameResult_;                        // +0x138
    std::uint32_t lastRainBoltTick_;        // +0x13C
    std::uint8_t unknown140[0x1C];
    int zoneLoaded_;                        // +0x15C
    int scriptsActive_;                     // +0x160
    Vector3 startPosition_;                 // +0x164
    CAIMap aiMap_;                          // +0x170
    AIEventTable aiEvents_;                 // +0x2838
    CScript commandsA_;                     // +0x12874
    std::uint8_t unknown12A6C[0x1F8];
    CDiplomacy diplomacy_;                  // +0x12C64
    int sessionKind_;                       // +0x12D64
    std::uint8_t unknown12D68[0x8];
    ObjectIdMap objects_;                   // +0x12D70
    ObjectIdMap sentObjects_;               // +0x12D88
    ObjectIdMap mapObjects_;                // +0x12DA0
    ObjectIdMap otherMapObjects_;           // +0x12DB8
    std::uint8_t unknown12DD0[0x18];
    ObjectIdMap timedObjects_;              // +0x12DE8
    std::uint8_t unknown12E00[0x28];
    List<CSpellEffect*> projectiles_;       // +0x12E28
    List<CEngineObject*> counted_;          // +0x12E44
    List<SharedObject> sharedObjects_;      // +0x12E60
    CAIMap* AIMap() { return &aiMap_; }
    AIEventTable* AIEvents() { return &aiEvents_; }
    CScript* MainScript() { return &commandsA_; }
};
static_assert(sizeof(CWorldServer) == 0x12E7C);

// A window rectangle of the frame: the client world's screen list (world.h).
struct ScreenEntry {
    int classId;                            // +0x00
    unsigned objectId;                      // +0x04
    Rect rect;                              // +0x08
};
static_assert(sizeof(ScreenEntry) == 0x18);

// The client world (world.h).
struct CWorldClient : CWorld {
    std::uint8_t unknown84[0x34];
    StlpList<CMapObject*> objects_;         // +0xB8
    List<CEngineObject*> dynamicObjects_;   // +0xBC
    std::uint8_t unknownD8[0x18];
    StlpList<ScreenEntry> screenEntries_;   // +0xF0 the window rectangles of the frame
    std::uint8_t unknownF4[0x8C];
    Terrain* terrain_;                      // +0x180
    std::uint8_t unknown184[0x560];
    int partyDanger_;                       // +0x6E4
    std::uint8_t unknown6E8[0x4];
    int ticksRun_;                          // +0x6EC
    float tickFraction_;                    // +0x6F0
    std::uint8_t unknown6F4[0xF0];
};
static_assert(sizeof(CWorldClient) == 0x7E4);

// A landscape vertex record (terrain_sector.h; packed).
#pragma pack(push, 1)
struct SectorVertex {
    std::uint8_t unknown0[0x15];
    std::uint32_t specular;                 // +0x15
    std::uint8_t unknown19[0xC];
    std::uint16_t waterMaterial;            // +0x25
    std::uint8_t unknown27[0x2D];
};
#pragma pack(pop)
static_assert(sizeof(SectorVertex) == 0x54);

struct SectorPosition {
    float x;                                // +0x00
    float y;                                // +0x04
    float z;                                // +0x08
};
static_assert(sizeof(SectorPosition) == 0xC);

// A cached landscape sector (terrain_sector.h).
struct SectorData {
    std::uint8_t unknown0[0x8];
    int sx;                                 // +0x08
    int sy;                                 // +0x0C
    int hasWater;                           // +0x10
    std::uint8_t unknown14[0x168];
    Vector<SectorPosition> vertices;        // +0x17C
    Array<SectorVertex> records;            // +0x190
    std::uint8_t unknown1A4[0x1F0];
};
static_assert(sizeof(SectorData) == 0x394);

// The landscape (terrain.h).
struct Terrain : CTransformObject {
    std::uint8_t unknown54[0x40];
    std::uint32_t landscapeFlags;           // +0x94
    std::uint8_t unknown98[0x164];
    List<SectorData*> sectorCache;          // +0x1FC
    int frame;                              // +0x218
    std::uint8_t unknown21C[0x6C];
    float wavePhase;                        // +0x288
    std::uint8_t unknown28C[0x870];
};
static_assert(sizeof(Terrain) == 0xAFC);

// The game's ISAAC generator (random.h).
struct IsaacRandom {
    std::uint32_t seeded_;                  // +0x00
    std::uint32_t count_;                   // +0x04
    std::uint32_t results_[256];            // +0x08
    std::uint32_t memory_[256];             // +0x408
    std::uint32_t a_;                       // +0x808
    std::uint32_t b_;                       // +0x80C
    std::uint32_t c_;                       // +0x810
};
static_assert(sizeof(IsaacRandom) == 0x814);

// The AI's Suspection mode (unit_ai.h).
struct SuspectionMode {
    const void* const* vtable;              // +0x00
    std::uint8_t unknown4[0x4];
    Vector3 point;                          // +0x08
    int level;                              // +0x14
    void ClearFields() {
    point = {};
    level = 0;
    }
};
static_assert(sizeof(SuspectionMode) == 0x18);

// The AI's Fear mode (unit_ai.h): its fields from `distance` on.
struct FearMode {
    const void* const* vtable;              // +0x00
    std::uint8_t unknown4[0x4];
    float distance;                         // +0x08
    std::uint8_t unknownC[0x20];
    void ClearFields() { std::memset(&distance, 0, sizeof(FearMode) - offsetof(FearMode, distance)); }
};
static_assert(sizeof(FearMode) == 0x2C);

// A class template (class_templates.h).
struct ClassTemplate {
    std::uint8_t unknown0[0x34];
    std::uint8_t weightsTail[4];            // +0x34
    std::uint8_t unknown38[0x254];
};
static_assert(sizeof(ClassTemplate) == 0x28C);

// A path cell (ai_map.h).
struct PathCell {
    std::int16_t x;                         // +0x00
    std::int16_t y;                         // +0x02
    std::uint16_t flags;                    // +0x04
    std::uint16_t terrainBits;              // +0x06
};
static_assert(sizeof(PathCell) == 0x8);

using PathCells = List<PathCell>;

// A counted network object (core_refcount.h).
struct CRefObject {
    const void* const* vtable;              // +0x00
    std::int16_t refs_;                     // +0x04
    std::uint8_t unknown6[0x1];
    std::uint8_t flags7_;                   // +0x07
    static constexpr std::uint8_t kDetached = 0x80;   // flags7_: shut down by its owner
    // The scalar deleting destructor (slot 0): game.exe's destructor and operator delete.
    void Delete() { reinterpret_cast<void*(__thiscall*)(CRefObject*, unsigned)>(vtable[0])(this, 1); }
};
static_assert(sizeof(CRefObject) == 0x8);

// A network game's chat window (ui_briefing.h).
struct CChatWindow {
    std::uint8_t unknown0[0x148];
    int listed_;                            // +0x148
    std::uint8_t unknown14C[0x24];
};
static_assert(sizeof(CChatWindow) == 0x170);

// The base screen (ui_briefing.h).
struct CBriefingScreen {
    std::uint8_t unknown0[0x280];
    CChatWindow* chat_;                     // +0x280
    std::uint8_t unknown284[0x20];
};
static_assert(sizeof(CBriefingScreen) == 0x2A4);

// An item (item.h).
struct CItem : CEngineObject {
    std::uint8_t unknown1C[0x28];
    CSpell* spell_;                         // +0x44
    std::uint8_t unknown48[0x4];
};
static_assert(sizeof(CItem) == 0x4C);

// A quest item (item.h).
struct CItemQuest : CItem {
    std::uint32_t questItemId_;             // +0x4C
};
static_assert(sizeof(CItemQuest) == 0x50);

// A weapon (item.h).
struct CItemWeapon : CItem {
    std::uint8_t unknown4C[0x10];
};
static_assert(sizeof(CItemWeapon) == 0x5C);

// A unit's client half (unit_client_models.h).
struct CUnitClientSpecific {
    std::uint8_t unknown0[0xCC];
    void* faceTexture;                      // +0xCC
    std::uint8_t unknownD0[0xC];
};
static_assert(sizeof(CUnitClientSpecific) == 0xDC);

// The party manager (party_manager.h).
struct PartyManager {
    std::uint8_t unknown0[0x38];
    std::uint8_t templates[12];             // +0x38
};
static_assert(sizeof(PartyManager) == 0x44);

// A cursor image (ui_cursor.h).
struct CursorImage {
    std::uint8_t unknown0[0x14];
    int hotX;                               // +0x14
    int hotY;                               // +0x18
    std::uint8_t unknown1C[0x4];
};
static_assert(sizeof(CursorImage) == 0x20);

// The mouse cursor (ui_cursor.h).
struct CCursor {
    int mode_;                              // +0x00
    std::uint8_t unknown4[0x8];
    int imageShown_;                        // +0x0C
    CursorImage* image_;                    // +0x10
    std::uint8_t unknown14[0x84];
    void* thread_;                          // +0x98
    std::uint8_t unknown9C[0x1C];
};
static_assert(sizeof(CCursor) == 0xB8);

namespace options {

// The game's options (options.h).
struct GameOptions {
    std::uint8_t unknown0[0xC8];
    int enableWaterWaves_;                  // +0xC8
    std::uint8_t unknownCC[0x13C];
    int showTutorial_;                      // +0x208
    std::uint8_t unknown20C[0x5C];
    CString masterServerName_;              // +0x268
};
static_assert(sizeof(GameOptions) == 0x26C);

} // namespace options

// The game flow (world.h).
struct GameFlow {
    int state_;                             // +0x00
    CString requestTarget_;                 // +0x04
    std::uint8_t unknown8[0x7C];
};
static_assert(sizeof(GameFlow) == 0x84);

// The interface's screen fade (ui.h).
struct CScreenFade {
    std::uint8_t unknown0[0x4];
    float alpha_;                           // +0x04
    std::uint8_t active_;                   // +0x08
    std::uint8_t unknown9[0xB];
};
static_assert(sizeof(CScreenFade) == 0x14);

// A memory stream (core_streams.h).
struct CMemoryStream {
    std::uint8_t unknown0[0x18];
    std::uint8_t* data_;                    // +0x18
    std::uint8_t unknown1C[0x8];
    std::uint32_t size_;                    // +0x24
};
static_assert(sizeof(CMemoryStream) == 0x28);

// A surface holder (texture.h).
struct CDXSurface {
    std::uint8_t unknown0[0x10];
    void* surface;                          // +0x10
    std::uint8_t unknown14[0xC];
};
static_assert(sizeof(CDXSurface) == 0x20);

// An interface surface (ui.h).
struct CUiSurface {
    std::uint8_t unknown0[0x8];
    void* dc_;                              // +0x08
    std::uint8_t unknownC[0x4];
    void* holder_;                          // +0x10
    int width_;                             // +0x14
    int height_;                            // +0x18
    std::uint8_t unknown1C[0x1C];
};
static_assert(sizeof(CUiSurface) == 0x38);

// A window's clickable region (ui.h).
struct CUiRegion {
    std::uint8_t unknown0[0x14];
    Rect bounds_;                           // +0x14
    std::uint8_t unknown24[0x1];
    bool enabled_;                          // +0x25
    std::uint8_t unknown26[0x2];
    int offsetX_;                           // +0x28
    int offsetY_;                           // +0x2C
    std::uint8_t unknown30[0x4];
};
static_assert(sizeof(CUiRegion) == 0x34);

// An interface window (ui.h).
struct CUiWindow {
    std::uint8_t unknown0[0x118];
    AlignedArray<CUiSurface*> surfaces_;    // +0x118
    AlignedArray<CUiRegion*> regions_;      // +0x128
    std::uint8_t unknown138[0x10];
};
static_assert(sizeof(CUiWindow) == 0x148);

namespace console {

// The console (console.h).
struct ConsoleWindow {
    std::uint8_t unknown0[0x4];
    CUiSurface* surface_;                   // +0x04
    std::uint8_t unknown8[0x74];
};
static_assert(sizeof(ConsoleWindow) == 0x7C);

} // namespace console

// The interface manager (ui.h).
struct CInterface3D {
    int width_;                             // +0x00
    int height_;                            // +0x04
    std::uint8_t unknown8[0xC];
    console::ConsoleWindow* console_;       // +0x14
    CScreenFade* fade_;                     // +0x18
    void* root_;                            // +0x1C
    CUiSurface* fpsSurface_;                // +0x20
    std::uint8_t unknown24[0x1C];
    CUiWindow* queuedWindow_;               // +0x40
    bool closeAll_;                         // +0x44
    std::uint8_t unknown45[0xF];
    CUiSurface* backdrop_;                  // +0x54
    CUiSurface* secondBackdrop_;            // +0x58
    std::uint8_t unknown5C[0x8];
    CUiSurface* statusSurface_;             // +0x64
    std::uint8_t unknown68[0x48];
};
static_assert(sizeof(CInterface3D) == 0xB0);

// The save / load window (ui.h).
struct CSaveLoadWindow {
    std::uint8_t unknown0[0x179];
    bool sliding_;                          // +0x179
    std::uint8_t unknown17A[0x1A];
};
static_assert(sizeof(CSaveLoadWindow) == 0x194);

// The options window (ui.h).
struct COptionsWindow {
    std::uint8_t unknown0[0x3F5];
    bool sliding_;                          // +0x3F5
    std::uint8_t unknown3F6[0x22];
};
static_assert(sizeof(COptionsWindow) == 0x418);

struct CHudQuickItems;
struct CHudSpellColumn;

// The battle screen (ui.h).
struct CBattleScreen {
    std::uint8_t unknown0[0x168];
    Vector<CUnit*> party_;                  // +0x168
    int pendingAction_;                     // +0x17C
    int pendingSlot_;                       // +0x180
    int pendingItem_;                       // +0x184
    std::uint8_t unknown188[0x10];
    CHudQuickItems* quickItems_;            // +0x198
    CHudSpellColumn* spellColumn_;          // +0x19C
    std::uint8_t unknown1A0[0x90];
};
static_assert(sizeof(CBattleScreen) == 0x230);

// An entry of the quick item panel (ui.h, CHudQuickItems::Entry).
struct CHudQuickItemsEntry {
    bool own;                               // +0x00
    std::uint8_t unknown1[0x3];
    int id;                                 // +0x04
    CItem* item;                            // +0x08
};
static_assert(sizeof(CHudQuickItemsEntry) == 0xC);

// The quick item panel (ui.h).
struct CHudQuickItems {
    std::uint8_t unknown0[0x150];
    int selected_;                          // +0x150
    CUnit* unit_;                           // +0x154
    Vector<CHudQuickItemsEntry> entries_;   // +0x158
    int regionItem_[8];                     // +0x16C
    std::uint8_t unknown18C[0x14];
    using Entry = CHudQuickItemsEntry;
};
static_assert(sizeof(CHudQuickItems) == 0x1A0);

// The spell column (ui.h).
struct CHudSpellColumn {
    std::uint8_t unknown0[0x14C];
    int count_;                             // +0x14C
    int selected_;                          // +0x150
    CUnit* unit_;                           // +0x154
    std::uint8_t unknown158[0x14];
};
static_assert(sizeof(CHudSpellColumn) == 0x16C);

// The render device (texture.h).
struct CTextureDevice {
    std::uint8_t unknown0[0x8];
    int lastLoadResult_;                    // +0x08
    void* ddraw_;                           // +0x0C
    std::uint8_t unknown10[0x8];
    CDXSurface* renderTarget_;              // +0x18 the render target's holder
    std::uint8_t unknown1C[0x160];
};
static_assert(sizeof(CTextureDevice) == 0x17C);

// The scene renderer's device (model.h).
struct HeldDevice {
    CTextureDevice* device;                 // +0x00
};
static_assert(sizeof(HeldDevice) == 0x4);

// The scene renderer (model.h).
struct CSceneRenderer {
    std::uint8_t unknown0[0x20];
    HeldDevice device_;                     // +0x20
    std::uint8_t unknown24[0x38];
};
static_assert(sizeof(CSceneRenderer) == 0x5C);

// An item strip (ui_screens.h).
struct CUiItemStrip {
    std::uint8_t unknown0[0x1AC];
    AlignedArray<CItem*>* source_;          // +0x1AC all the items (a party's item list)
    std::uint8_t unknown1B0[0x4];
};
static_assert(sizeof(CUiItemStrip) == 0x1B4);

// The camp (ui_camp.h).
struct CCampPages {
    const void* const* vtable;              // +0x00
    std::uint8_t unknown4[0x18C];
    int page_;                              // +0x190
    std::uint8_t unknown194[0x4C];
    CUiItemStrip* itemStrip_;               // +0x1E0
    std::uint8_t unknown1E4[0x7C];
    // Slot 42 (TakeItem: MoveItem 00610AD0): `item` moved by action `mode` (1: put on the hero from the strip).
    void TakeItem(CItem* item, int mode) {
        reinterpret_cast<void(__thiscall*)(CCampPages*, CItem*, int)>(vtable[42])(this, item, mode);
    }
};
static_assert(sizeof(CCampPages) == 0x260);

// A map.txt record (object_tables.h).
struct MapRecord {
    std::uint8_t unknown0[0x28];
    int kind;                               // +0x28 0 game, 1 edge, 2 brief
    std::uint8_t unknown2C[0x30];
};
static_assert(sizeof(MapRecord) == 0x5C);

namespace net {

struct NetAddress {
    std::uint8_t unknown0[0x2];
    std::uint16_t port;                     // +0x02
    std::uint32_t ip;                       // +0x04
    std::uint8_t unknown8[0x8];
};
static_assert(sizeof(NetAddress) == 0x10);

// A server the client found (net_classes.h).
struct ServerEntry {
    std::uint8_t unknown0[0x8];
    NetAddress address_;                    // +0x08
    std::uint8_t unknown18[0x8];
    std::uint8_t foreign_;                  // +0x20
    std::uint8_t unknown21[0x3];
    std::uint32_t state_;                   // +0x24
    StlpString name_;                       // +0x28
    std::uint32_t kind_;                    // +0x34
    StlpString description_;                // +0x38
    std::uint8_t players_;                  // +0x44
    std::uint8_t maxPlayers_;               // +0x45
    std::uint8_t password_;                 // +0x46
    std::uint8_t unknown47[0x1];
};
static_assert(sizeof(ServerEntry) == 0x48);

// The game's server session (net_classes.h).
struct ServerSession {
    std::uint8_t unknown0[0x28];
    StlpList<RefPtr> clients_;              // +0x28 the connections
    std::uint8_t unknown2C[0x25D4];
};
static_assert(sizeof(ServerSession) == 0x2600);

// The game's client session (net_classes.h).
struct ClientSession {
    std::uint8_t unknown0[0x2598];
    std::uint32_t joinState_;               // +0x2598
    std::uint8_t unknown259C[0xC];
    StlpList<RefPtr> servers_;              // +0x25A8
    std::uint8_t unknown25AC[0x2C];
    StlpList<RefPtr> players_;              // +0x25D8
    std::uint8_t unknown25DC[0x14];
    CRefObject* binding_;                   // +0x25F0
    std::uint8_t unknown25F4[0x10C];
};
static_assert(sizeof(ClientSession) == 0x2700);

} // namespace net

// game.exe's objects at their addresses.
inline CWorldServer* ServerWorld() { return &OriginalGameData<CWorldServer>(0x007AFF60); }
inline CWorldClient* ClientWorld() { return &OriginalGameData<CWorldClient>(0x007AF5B8); }
inline const IsaacRandom& Random() { return OriginalGameData<IsaacRandom>(0x007A9D40); }

} // namespace ei::verify::exe
