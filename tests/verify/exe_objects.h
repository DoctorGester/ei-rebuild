#pragma once

// game.exe's objects at game.exe's own offsets (ticket H30b), as the vector recorders read them in game.exe's runs
// (tests/vectors/record_*.cpp, not built since H40) and the script replay's views of them (tests/vectors/
// script_original.h). The harness itself no longer reads game.exe (H40). Our classes (src/game) may reorder and retype
// their members freely: nothing here includes a header of src/game or src/lib.
//
// A view has our class's name, the original's size and the members the harness reads, under our names (the
// digest's reading code compiled against it until H40); the rest is padding. Containers are game.exe's MFC 4.2
// and STLport 4 layouts. The only code called is game.exe's own (EngineObject's slots 11 and 12). Offsets are game.exe
// 1.06's (they were our classes' when H30b split the two; docs/notes/cleanup-tooling.md, "The harness in game.exe").


#include <cstddef>
#include <cstdint>
#include <cstring>

// A view's size or offset as game.exe's: asserted in the 32-bit build. An x64 build (build.cmd verify-x64) compiles the
// views for the shared harness but never reads game.exe (32-bit), so its pointers and offsets differ there.
#if defined(_M_IX86)
#define EI_EXE_LAYOUT(...) static_assert(__VA_ARGS__)
#else
#define EI_EXE_LAYOUT(...) static_assert(true)
#endif

namespace ei::verify::exe {

struct Vector3 {
    float x, y, z;
};
EI_EXE_LAYOUT(sizeof(Vector3) == 0xC);

struct Quaternion {
    float w, x, y, z;
};
EI_EXE_LAYOUT(sizeof(Quaternion) == 0x10);

// A Windows RECT.
struct Rect {
    int left, top, right, bottom;
};
EI_EXE_LAYOUT(sizeof(Rect) == 0x10);

// MFC's CString: a pointer to the text (its header before it).
struct CString {
    const char* text;
    operator const char*() const { return text; }
};
EI_EXE_LAYOUT(sizeof(CString) == 4);

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
EI_EXE_LAYOUT(sizeof(StlpString) == 0xC);

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
EI_EXE_LAYOUT(sizeof(Array<int>) == 0x14);

// A CArray<T> the rebuild holds as a std::vector: the same bytes, read through the vector's API (the shared readers
// name both sides alike).
template <class T>
struct Vector {
    const void* vtable;
    T* elements;
    int count;
    int maxSize;
    int growBy;
    std::size_t size() const { return static_cast<std::size_t>(count); }
    bool empty() const { return count == 0; }
    const T* data() const { return elements; }
    const T* begin() const { return elements; }
    const T* end() const { return elements + count; }
    const T& operator[](std::size_t i) const { return elements[i]; }
};
EI_EXE_LAYOUT(sizeof(Vector<int>) == 0x14);

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
EI_EXE_LAYOUT(sizeof(List<int>) == 0x1C);

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
EI_EXE_LAYOUT(sizeof(StlpList<int>) == 4);

// The game's aligned pointer array (data, count, capacity, grow-by).
template <class T>
struct AlignedArray {
    T* data;
    int size;
    int capacity;
    int growBy;
};
EI_EXE_LAYOUT(sizeof(AlignedArray<int>) == 0x10);

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
        const Node& operator*() const { return *node; }
        const Node* operator->() const { return node; }
        const_iterator& operator++() {
            node = node->next ? node->next : table->After(table->start + table->BucketOf(node) + 1);
            return *this;
        }
        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.node == b.node; }

    private:
        friend struct HashTable;
        const_iterator(const HashTable* table, const Node* node) : table(table), node(node) {}
        const HashTable* table;
        const Node* node;
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
EI_EXE_LAYOUT(sizeof(HashMap<int, int>) == 0x14);

// The world's maps of objects by id: the map and the highest id put in.
struct MapObject;
struct ObjectIdMap {
    HashMap<unsigned, MapObject*> map;
    int maxId;
};
EI_EXE_LAYOUT(sizeof(ObjectIdMap) == 0x18);

// A party's named floats: a hashtable of {CString, float}.
using NamedFloatTable = HashMap<CString, float>;
EI_EXE_LAYOUT(sizeof(NamedFloatTable) == 0x14);

// An element of the network sessions' lists: a counted pointer.
struct RefPtr {
    void* ptr;
};

// Objects the harness only points at or counts.
struct AIHeardCall;
struct AILook;
struct AIMode;
struct AIReport;
struct Item;
struct Player;
struct MotionPoint;
struct MotionWaypoint;
struct SharedObject;
struct Terrain;
struct ZoneExit;

struct ScriptObjectRef;
// A script's group of objects: a CArray of object references.
struct ScriptGroup : Vector<ScriptObjectRef> {};

// EngineObject (engine_object.h): every world object's base. Slot 11 ClassName, slot 12 GetClassId.
struct EngineObject {
    const void* const* vtable;              // +0x00
    int refCount;                           // +0x04
    std::uint32_t flags;                    // +0x08
    int id;                                 // +0x0C
    int type;                               // +0x10
    std::uint8_t unknown14[0x4];
    std::uint32_t prototypeId;              // +0x18
    const char* ClassName() { return CallSlot<const char*>(11); }
    int GetClassId() { return CallSlot<int>(12); }
    template <class R> R CallSlot(int slot) { return reinterpret_cast<R(__thiscall*)(EngineObject*)>(vtable[slot])(this); }
};
EI_EXE_LAYOUT(sizeof(EngineObject) == 0x1C);

// A placed object: position and orientation.
struct TransformObject : EngineObject {
    Vector3 localPosition;                  // +0x1C
    Vector3 worldPosition;                  // +0x28
    Quaternion localRotation;               // +0x34
    std::uint8_t unknown44[0x10];
};
EI_EXE_LAYOUT(sizeof(TransformObject) == 0x54);

// The camera (camera.h, 0079B2A8): its focus, distance and heading; the camera trace reads them (harness_camera.cpp).
struct Camera : TransformObject {
    std::uint8_t unknown54[0x8C];
    Vector3 focus;                          // +0xE0
    float focusHeight;                      // +0xEC
    float distance;                         // +0xF0
    Quaternion viewRotation;                // +0xF4
    std::uint8_t unknown104[0x200];
};
EI_EXE_LAYOUT(sizeof(Camera) == 0x304 && offsetof(Camera, focus) == 0xE0 && offsetof(Camera, viewRotation) == 0xF4);

// A texture (texture.h).
struct Texture : EngineObject {
    CString name;                           // +0x1C
    std::uint8_t unknown20[0x48];
};
EI_EXE_LAYOUT(sizeof(Texture) == 0x68);

// A model's part set (map_object_shadow.h).
struct TextureParts {
    Texture* texture;                      // +0x00
    std::uint8_t unknown4[0x10];
};
EI_EXE_LAYOUT(sizeof(TextureParts) == 0x14);

// A 3D object's drawing half (map_object_shadow.h).
struct Object3DClientSpecific : EngineObject {
    std::uint8_t unknown1C[0x4];
    TextureParts* partSets[2];              // +0x20
    std::uint8_t unknown28[0xD4];
};
EI_EXE_LAYOUT(sizeof(Object3DClientSpecific) == 0xFC);

// A figure (link_figure.h).
struct LinkMesh {
    std::uint8_t unknown0[0x8];
    StlpString name;                        // +0x08
    std::uint8_t unknown14[0x1C];
    float morphWeights[3];                  // +0x30
    std::uint8_t unknown3C[0x74];
};
EI_EXE_LAYOUT(sizeof(LinkMesh) == 0xB0);

// A figure in the world: its model's name and scale.
struct Object3D : TransformObject {
    std::uint8_t unknown54[0x40];
    EngineObject* attached;                // +0x94
    std::uint8_t unknown98[0xC];
    CString name;                           // +0xA4
    std::uint8_t unknownA8[0x1C];
    Vector3 scale;                          // +0xC4
    std::uint8_t unknownD0[0x20];
    LinkMesh* figure;                      // +0xF0
    std::uint8_t unknownF4[0x7C];
    Object3DClientSpecific* ClientSpecific() const { return static_cast<Object3DClientSpecific*>(attached); }
};
EI_EXE_LAYOUT(sizeof(Object3D) == 0x170);

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
EI_EXE_LAYOUT(sizeof(MapEffect) == 0x14);

// A map object's vitals as the client sees them.
struct UnitVitalsPacked {
    std::uint8_t unknown0[0x2];
    std::int16_t hitPoints;                 // +0x02
    std::int16_t maxHitPoints;              // +0x04
    std::uint16_t mana;                     // +0x06
    std::uint16_t maxMana;                  // +0x08
    std::uint8_t unknownA[0xE];
};
EI_EXE_LAYOUT(sizeof(UnitVitalsPacked) == 0x18);

// A map object (map_object.h): its server or client half, effects and sector cell.
struct MapObject : Object3D {
    EngineObject* companion;               // +0x170
    int updateCountdown;                    // +0x174
    float animationSpeed;                   // +0x178
    UnitVitalsPacked packedVitals;          // +0x17C
    Vector<MapEffect> effects;              // +0x194 (map_object.h's effects)
    std::uint8_t unknown1A8[0x10];
    int sectorCellX;                        // +0x1B8
    int sectorCellY;                        // +0x1BC
    std::uint8_t unknown1C0[0x78];
};
EI_EXE_LAYOUT(sizeof(MapObject) == 0x238);

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
EI_EXE_LAYOUT(sizeof(UnitVitals) == 0x58);

// A map object's server half (class 0x39).
struct MapObjectServer : EngineObject {
    std::uint8_t unknown1C[0x4];
    UnitVitals vitals;                      // +0x20
    std::uint8_t unknown78[0x1C];
};
EI_EXE_LAYOUT(sizeof(MapObjectServer) == 0x94);

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
EI_EXE_LAYOUT(sizeof(UnitMotion) == 0xA4);

// A unit (unit.h, class 0x50).
struct Unit : MapObject {
    CString scriptName;                     // +0x238
    std::uint8_t unknown23C[0x4];
    EngineObject* controller;              // +0x240
    std::uint32_t relationsA;               // +0x244
    std::uint32_t relationsB;               // +0x248
    Player* player;                        // +0x24C
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
    std::uint8_t unknown328[0x4];
    float standingHeight;                   // +0x32C
    std::uint8_t unknown330[0x108];
    int fightAtWill;                        // +0x438
    std::uint32_t altPassCosts;             // +0x43C
    std::uint8_t unknown440[0x9C];
};
EI_EXE_LAYOUT(sizeof(Unit) == 0x4DC && offsetof(Unit, motion) == 0x27C);

// A unit controller's order (combat.h).
struct UnitAction {
    int kind;                               // +0x00
    std::uint8_t unknown4[0x4];
    MapObject* target;                     // +0x08
    std::uint8_t unknownC[0x18];
};
EI_EXE_LAYOUT(sizeof(UnitAction) == 0x24);

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
EI_EXE_LAYOUT(sizeof(UnitStats) == 0x704);

// The AI's move target.
struct AIMoveOrder {
    Vector3 point;                          // +0x00
    Vector<AILook> looks;                   // +0x0C
};
EI_EXE_LAYOUT(sizeof(AIMoveOrder) == 0x20);

// What the AI remembers (unit_ai.h).
struct AIMemory {
    std::uint8_t unknown0[0x10];
    float noticedX;                         // +0x10
    float noticedY;                         // +0x14
    int noticedLevel;                       // +0x18
    int noticedDecay;                        // +0x1C
    std::uint8_t unknown20[0x4];
    List<Unit*> nearby;                    // +0x24
    List<Unit*> visible;                   // +0x40
    std::uint8_t unknown5C[0x1C];
    List<Unit*> corpses;                   // +0x78
    Vector<AIReport> reports;               // +0x94
    Vector<AIHeardCall> calls;              // +0xA8
    std::uint8_t seesEnemy;                 // +0xBC
    std::uint8_t unknownBD[0x3];
};
EI_EXE_LAYOUT(sizeof(AIMemory) == 0xC0);

// A unit controller's AI (unit_ai.h).
struct AIBrain {
    std::uint8_t unknown0[0x4];
    int action;                             // +0x04
    AIMoveOrder move;                       // +0x08
    int actionTime;                         // +0x28
    MapObject* target;                     // +0x2C
    MapObject* seen;                       // +0x30
    std::uint8_t alerted;                   // +0x34
    std::int8_t alarm;                      // +0x35
    std::int8_t alarmSlot;                  // +0x36
    std::uint8_t unknown37[0x1];
    float callRadius;                       // +0x38
    int alertMode;                          // +0x3C
    int alarmCountdown;                     // +0x40
    float nearRange;                            // +0x44
    float farRange;                            // +0x48
    float decay;                            // +0x4C
    std::uint8_t unknown50[0x18];
    int standCooldown;                      // +0x68
    int followTimer;                        // +0x6C
    List<AIMode*> modes;                    // +0x70
    AIMode* current;                        // +0x8C
    std::uint8_t unknown90[0x40];
    AIMemory memory;                        // +0xD0
};
EI_EXE_LAYOUT(sizeof(AIBrain) == 0x190);

// A unit's server controller (combat.h, class 0x3E).
struct UnitServer : EngineObject {
    std::uint8_t unknown1C[0x148];
    int state;                              // +0x164
    // A movement record (combat.h's MoveModeState).
    struct MoveMode {
        int mode;
        std::uint8_t onGuard;
        std::uint8_t wasMode3;
    };
    MoveMode moveMode;                      // +0x168
    MoveMode moveMode2;                     // +0x170
    std::uint8_t unknown178[0x14];
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
};
EI_EXE_LAYOUT(sizeof(UnitServer) == 0xB20);
EI_EXE_LAYOUT(offsetof(UnitServer, moveMode2) == 0x170 && offsetof(UnitServer, orderStopped) == 0x18C);

// A timed effect (world_effect.h).
struct Effect : TransformObject {
    std::uint8_t unknown54[0x40];
    int startTick;                          // +0x94
    Vector3 velocity;                       // +0x98
    int moving;                             // +0xA4
    std::uint8_t unknownA8[0xA4];
};
EI_EXE_LAYOUT(sizeof(Effect) == 0x14C);

struct EffectSound : Effect {
    std::uint8_t unknown14C[0x100];
    float volume;                           // +0x24C
    float radius;                           // +0x250
    int looping;                            // +0x254
    std::uint8_t unknown258[0xC];
};
EI_EXE_LAYOUT(sizeof(EffectSound) == 0x264);

struct EffectParticle : Effect {
    std::uint8_t unknown14C[0xC];
    int type;                               // +0x158
    float size;                             // +0x15C
    std::uint8_t unknown160[0x8];
    int attachedTo;                         // +0x168
    Vector3 target;                         // +0x16C
    std::uint8_t unknown178[0x4];
    int unitId;                             // +0x17C
};
EI_EXE_LAYOUT(sizeof(EffectParticle) == 0x180);

struct EffectPointLight : Effect {
    Vector3 color;                          // +0x14C
    float range;                            // +0x158
    std::uint8_t unknown15C[0x8];
};
EI_EXE_LAYOUT(sizeof(EffectPointLight) == 0x164);

struct EffectLightning : Effect {
    Vector3 from;                           // +0x14C
    Vector3 to;                             // +0x158
    std::uint8_t unknown164[0x8];
    int toObject;                           // +0x16C
    int fromObject;                         // +0x170
    std::uint8_t unknown174[0x4];
};
EI_EXE_LAYOUT(sizeof(EffectLightning) == 0x178);

struct EffectMissile : Effect {
    int arrived;                            // +0x14C
    int targetId;                           // +0x150
    std::uint8_t unknown154[0x4];
};
EI_EXE_LAYOUT(sizeof(EffectMissile) == 0x158);

// A spell (spell.h).
struct Spell : EngineObject {
    std::uint8_t prototypeIndex;            // +0x1C
    std::uint8_t unknown1D[0x3];
    std::uint32_t duration;                 // +0x20
    float damage;                           // +0x24
    float radius;                           // +0x28
    std::uint8_t unknown2C[0x10];
    std::uint32_t targetFlags;              // +0x3C
    std::uint8_t unknown40[0x1C];
};
EI_EXE_LAYOUT(sizeof(Spell) == 0x5C);

// A spell on its way or lasting (spell.h): the server world's missiles.
struct SpellEffect {
    const void* const* vtable;              // +0x00
    std::uint8_t unknown4[0x8];
    std::uint32_t flags;                    // +0x0C
    Vector3 position;                       // +0x10
    MapObject* target;                     // +0x1C
    Unit* caster;                          // +0x20
    Vector3 targetPoint;                    // +0x24
    int state;                              // +0x30
    Spell spell;                           // +0x34
    Effect* visual;                        // +0x90
    Effect* sound;                         // +0x94
    EffectPointLight* light;               // +0x98
    std::uint8_t footprintStamped;          // +0x9C
    std::uint8_t unknown9D[0x3];
    int timer;                              // +0xA0
    std::uint8_t unknownA4[0x4];
    const Spell* HeldSpell() const { return &spell; }
};
EI_EXE_LAYOUT(sizeof(SpellEffect) == 0xA8);

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
EI_EXE_LAYOUT(sizeof(AIEvent) == 0x30);

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
EI_EXE_LAYOUT(sizeof(AIEventTable) == 0x1003C);

// The server world's AI map (ai_map.h).
struct AIMap {
    std::uint8_t unknown0[0x2694];
    int footprintTick;                      // +0x2694
    std::uint8_t unknown2698[0x30];
    AIEventTable events;                    // +0x26C8
};
EI_EXE_LAYOUT(sizeof(AIMap) == 0x12704 && offsetof(AIMap, footprintTick) == 0x2694 && offsetof(AIMap, events) == 0x26C8);

struct WorldAlarm {
    std::uint8_t active;                    // +0x00
    std::uint8_t unknown1[0x3];
    int time;                               // +0x04
    float x;                                // +0x08
    float y;                                // +0x0C
};
EI_EXE_LAYOUT(sizeof(WorldAlarm) == 0x10);

struct Diplomacy {
    std::uint32_t allied[32];               // +0x00
    std::uint32_t hostile[32];              // +0x80
};
EI_EXE_LAYOUT(sizeof(Diplomacy) == 0x100);

struct ScriptObjectRef {
    EngineObject* object;                  // +0x00
};
EI_EXE_LAYOUT(sizeof(ScriptObjectRef) == 0x4);

// A script's variables: values and names of the four kinds (script.h).
template <typename Value>
struct ScriptNamedValues {
    Vector<Value> values;                   // +0x00
    Vector<CString> names;                  // +0x14
};
struct ScriptVars {
    ScriptNamedValues<float> floats;        // +0x00
    ScriptNamedValues<CString> strings;     // +0x28
    ScriptNamedValues<ScriptObjectRef> objects; // +0x50
    ScriptNamedValues<ScriptGroup> groups;  // +0x78
};
EI_EXE_LAYOUT(sizeof(ScriptVars) == 0xA0);

// A command block.
struct ScriptDecl {
    std::uint8_t unknown0[0x14];
    int id;                                 // +0x14
};
EI_EXE_LAYOUT(sizeof(ScriptDecl) == 0x18);

// A running script instance.
struct ScriptDefinition {
    int index;                              // +0x00
    CString name;                           // +0x04
    std::uint8_t unknown8[0x18];
    ScriptVars vars;                        // +0x20
};
EI_EXE_LAYOUT(sizeof(ScriptDefinition) == 0xC0);

// A waiting run.
struct ScriptRun {
    ScriptDecl* block;                      // +0x00
    int index;                              // +0x04
    int ticks;                              // +0x08
    int sleepStart;                         // +0x0C
    int* killFlag;                          // +0x10
    ScriptVars vars;                        // +0x14
};
EI_EXE_LAYOUT(sizeof(ScriptRun) == 0xB4);

struct ScriptFxSource {
    EffectSound* object;                   // +0x00
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
EI_EXE_LAYOUT(sizeof(ScriptFxSource) == 0x34);

struct ScriptParticleSource {
    EffectParticle* object;                // +0x00
    int id;                                 // +0x04
    std::uint8_t unknown8[0x4];
    int moving;                             // +0x0C
    int steps;                              // +0x10
    std::uint8_t unknown14[0xC];
    int lifetime;                           // +0x20
};
EI_EXE_LAYOUT(sizeof(ScriptParticleSource) == 0x24);

struct ScriptPointLight {
    EffectPointLight* object;              // +0x00
    int id;                                 // +0x04
    std::uint8_t unknown8[0x4];
    int moving;                             // +0x0C
    int steps;                              // +0x10
    std::uint8_t unknown14[0xC];
};
EI_EXE_LAYOUT(sizeof(ScriptPointLight) == 0x20);

struct ScriptLightning {
    EffectLightning* object;               // +0x00
    int id;                                 // +0x04
};
EI_EXE_LAYOUT(sizeof(ScriptLightning) == 0x8);

// The mission script (script.h).
struct Script {
    Vector<ScriptPointLight> pointLights;   // +0x00
    List<ScriptParticleSource> particles;   // +0x14
    List<ScriptFxSource> fxSources;         // +0x30
    List<ScriptLightning> lightning;        // +0x4C
    int nextParticleId;                     // +0x68
    int nextFxId;                           // +0x6C
    std::uint8_t unknown70[0x30];
    ScriptVars globals;                     // +0xA0
    List<ScriptDefinition> instances;       // +0x140
    List<ScriptRun> runs;                   // +0x15C
    std::uint8_t unknown178[0x30];
    int loaded;                             // +0x1A8
    int fileCount;                          // +0x1AC
    std::uint8_t unknown1B0[0x48];
};
EI_EXE_LAYOUT(sizeof(Script) == 0x1F8);

// A party member's record (unit.h).
struct PartyUnitRecord {
    std::uint8_t unknown0[0x1C];
    std::uint32_t unitId;                   // +0x1C
    std::uint8_t unknown20[0x764];
};
EI_EXE_LAYOUT(sizeof(PartyUnitRecord) == 0x784);

// A party (party.h).
struct Party : EngineObject {
    std::uint8_t unknown1C[0x10];
    Vector<PartyUnitRecord> members;        // +0x2C
    AlignedArray<Item*> items;             // +0x40
    int money;                              // +0x50
    NamedFloatTable variables;              // +0x54
    std::uint8_t unknown68[0x4];
    std::uint32_t entrance;                 // +0x6C
};
EI_EXE_LAYOUT(sizeof(Party) == 0x70);

// A player's connection, the base of its PlayerServer or PlayerClient (player_messages.h).
struct PlayerConnection : EngineObject {
    Player* player;                        // +0x1C
    int state;                              // +0x20 1 in the world, 3 leaving the zone
};
EI_EXE_LAYOUT(sizeof(PlayerConnection) == 0x24);

// A player (player_messages.h).
struct Player : EngineObject {
    std::uint8_t unknown1C[0x4];
    PlayerConnection* connection;          // +0x20 its connection (counted)
    int playerId;                           // +0x24
    HashMap<int, MapObject*> knownObjects;// +0x28
    std::uint8_t unknown3C[0x4];
    HashMap<int, Unit*> units;             // +0x40
    std::uint8_t unknown54[0x1C];
    Party party;                           // +0x70
    List<Item*> items;                     // +0xE0
    int partyId;                            // +0xFC
    int sessionKind;                     // +0x100
    std::uint32_t lastScreen;               // +0x104
    std::uint8_t godMode;                   // +0x108
    std::uint8_t unknown109[0x7];
    int lastTick;                           // +0x110
    List<std::uint32_t> recharging;         // +0x114
    std::uint8_t unknown130[0x4];
    Party* GetParty() { return &party; }
};
EI_EXE_LAYOUT(sizeof(Player) == 0x134);

// The world's players (player_manager.h).
struct PlayerManager : EngineObject {
    std::uint8_t unknown1C[0x18];
    List<EngineObject*> objectList;        // +0x34
    std::uint8_t unknown50[0x60];
};
EI_EXE_LAYOUT(sizeof(PlayerManager) == 0xB0);

// What both worlds share (world.h).
struct World : EngineObject {
    float timeBase;                         // +0x1C
    float secondsPerTick;                   // +0x20
    std::uint8_t windDirection[12];         // +0x24
    std::uint32_t windStrength;             // +0x30
    std::uint8_t unknown34[0x4];
    PlayerManager* players;                // +0x38
    std::uint8_t unknown3C[0x20];
    RefPtr worldHandle;                        // +0x5C
    std::uint8_t unknown60[0x4];
    int tick;                               // +0x64
    int updateCount;                        // +0x68
    std::uint8_t paused;                    // +0x6C
    std::uint8_t unknown6D[0x17];
};
EI_EXE_LAYOUT(sizeof(World) == 0x84);

// The server world (world.h).
struct WorldServer : World {
    float nightLevel;                       // +0x84
    std::uint32_t weatherFactor;            // +0x88
    WorldAlarm alarms[5];                   // +0x8C
    int climate;                            // +0xDC
    int cave;                               // +0xE0
    int weather;                            // +0xE4
    float weatherStrength;                  // +0xE8
    Vector<ZoneExit> exits;                 // +0xEC
    std::uint8_t unknown100[0x4];
    ObjectIdMap updateMap;                  // +0x104
    std::uint8_t unknown11C[0x4];
    CString zoneName;                       // +0x120
    std::uint8_t unknown124[0x14];
    int gameResult;                         // +0x138
    std::uint32_t lastRainBoltTick;         // +0x13C
    std::uint8_t unknown140[0x1C];
    int zoneLoaded;                         // +0x15C
    int scriptsActive;                      // +0x160
    Vector3 startPosition;                  // +0x164
    AIMap aiMap;                           // +0x170
    Script commandsA;                      // +0x12874
    std::uint8_t unknown12A6C[0x1F8];
    Diplomacy diplomacy;                   // +0x12C64
    int sessionKind;                        // +0x12D64
    std::uint8_t unknown12D68[0x8];
    ObjectIdMap objects;                    // +0x12D70
    ObjectIdMap sentObjects;                // +0x12D88
    ObjectIdMap mapObjects;                 // +0x12DA0
    ObjectIdMap otherMapObjects;            // +0x12DB8
    std::uint8_t unknown12DD0[0x18];
    ObjectIdMap timedObjects;               // +0x12DE8
    std::uint8_t unknown12E00[0x28];
    List<SpellEffect*> projectiles;        // +0x12E28
    List<EngineObject*> counted;           // +0x12E44
    List<SharedObject> sharedObjects;       // +0x12E60
    Script* MainScript() { return &commandsA; }
};
EI_EXE_LAYOUT(sizeof(WorldServer) == 0x12E7C && offsetof(WorldServer, aiMap) == 0x170 && offsetof(WorldServer, commandsA) == 0x12874);

// A window rectangle of the frame: the client world's screen list (world.h).
struct ScreenEntry {
    int classId;                            // +0x00
    unsigned objectId;                      // +0x04
    Rect rect;                              // +0x08
};
EI_EXE_LAYOUT(sizeof(ScreenEntry) == 0x18);

// The client world (world.h).
struct WorldClient : World {
    std::uint8_t unknown84[0x34];
    StlpList<MapObject*> objects;          // +0xB8
    List<EngineObject*> dynamicObjects;    // +0xBC
    std::uint8_t unknownD8[0x18];
    StlpList<ScreenEntry> screenEntries;    // +0xF0 the window rectangles of the frame
    std::uint8_t unknownF4[0x8C];
    Terrain* terrain;                       // +0x180
    std::uint8_t unknown184[0x560];
    int partyDanger;                        // +0x6E4
    std::uint8_t unknown6E8[0x4];
    int ticksRun;                           // +0x6EC
    float tickFraction;                     // +0x6F0
    std::uint8_t unknown6F4[0xF0];
};
EI_EXE_LAYOUT(sizeof(WorldClient) == 0x7E4);

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
EI_EXE_LAYOUT(sizeof(SectorVertex) == 0x54);

struct SectorPosition {
    float x;                                // +0x00
    float y;                                // +0x04
    float z;                                // +0x08
};
EI_EXE_LAYOUT(sizeof(SectorPosition) == 0xC);

// A cached landscape sector (terrain_sector.h).
struct SectorData {
    std::uint8_t unknown0[0x4];
    std::uint32_t built;                    // +0x04 SectorStage bits
    int sx;                                 // +0x08
    int sy;                                 // +0x0C
    int hasWater;                           // +0x10
    std::uint8_t unknown14[0x168];
    Vector<SectorPosition> vertices;        // +0x17C
    Vector<SectorVertex> records;           // +0x190
    std::uint8_t unknown1A4[0x1F0];
};
EI_EXE_LAYOUT(sizeof(SectorData) == 0x394);

// The landscape (terrain.h).
struct Terrain : TransformObject {
    std::uint8_t unknown54[0x40];
    std::uint32_t landscapeFlags;           // +0x94
    std::uint8_t unknown98[0x164];
    List<SectorData*> sectorCache;          // +0x1FC
    int frame;                              // +0x218
    std::uint8_t unknown21C[0x6C];
    float wavePhase;                        // +0x288
    std::uint8_t unknown28C[0x870];
};
EI_EXE_LAYOUT(sizeof(Terrain) == 0xAFC);

// The game's ISAAC generator (random.h).
struct IsaacRandom {
    std::uint32_t seeded;                   // +0x00
    std::uint32_t count;                    // +0x04
    std::uint32_t results[256];             // +0x08
    std::uint32_t memory[256];              // +0x408
    std::uint32_t aa;                       // +0x808
    std::uint32_t bb;                       // +0x80C
    std::uint32_t cc;                       // +0x810
};
EI_EXE_LAYOUT(sizeof(IsaacRandom) == 0x814);

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
EI_EXE_LAYOUT(sizeof(SuspectionMode) == 0x18);

// The AI's Fear mode (unit_ai.h): its fields from `distance` on.
struct FearMode {
    const void* const* vtable;              // +0x00
    std::uint8_t unknown4[0x4];
    float distance;                         // +0x08
    std::uint8_t unknownC[0x20];
    void ClearFields() { std::memset(&distance, 0, sizeof(FearMode) - offsetof(FearMode, distance)); }
};
EI_EXE_LAYOUT(sizeof(FearMode) == 0x2C);

// A class template (class_templates.h).
struct ClassTemplate {
    std::uint8_t unknown0[0x34];
    std::uint8_t weightsTail[4];            // +0x34
    std::uint8_t unknown38[0x254];
};
EI_EXE_LAYOUT(sizeof(ClassTemplate) == 0x28C);

// A path cell (ai_map.h).
struct PathCell {
    std::int16_t x;                         // +0x00
    std::int16_t y;                         // +0x02
    std::uint16_t flags;                    // +0x04
    std::uint16_t terrainBits;              // +0x06
};
EI_EXE_LAYOUT(sizeof(PathCell) == 0x8);

using PathCells = List<PathCell>;

// A counted network object (core_refcount.h).
struct RefObject {
    const void* const* vtable;              // +0x00
    std::int16_t refs;                      // +0x04
    std::uint8_t unknown6[0x1];
    std::uint8_t flags7;                    // +0x07
    static constexpr std::uint8_t kDetached = 0x80;   // flags7: shut down by its owner
    // The scalar deleting destructor (slot 0): game.exe's destructor and operator delete.
    void Delete() { reinterpret_cast<void*(__thiscall*)(RefObject*, unsigned)>(vtable[0])(this, 1); }
};
EI_EXE_LAYOUT(sizeof(RefObject) == 0x8);

// A network game's chat window (ui_briefing.h).
struct ChatWindow {
    std::uint8_t unknown0[0x148];
    int listed;                             // +0x148
    std::uint8_t unknown14C[0x24];
};
EI_EXE_LAYOUT(sizeof(ChatWindow) == 0x170);

// The base screen (ui_briefing.h).
struct BriefingScreen {
    std::uint8_t unknown0[0x280];
    ChatWindow* chat;                      // +0x280
    std::uint8_t unknown284[0x20];
};
EI_EXE_LAYOUT(sizeof(BriefingScreen) == 0x2A4);

// An item (item.h).
struct Item : EngineObject {
    std::uint8_t unknown1C[0x28];
    Spell* spell;                          // +0x44
    std::uint8_t unknown48[0x4];
};
EI_EXE_LAYOUT(sizeof(Item) == 0x4C);

// A quest item (item.h).
struct ItemQuest : Item {
    std::uint32_t questItemId;              // +0x4C
};
EI_EXE_LAYOUT(sizeof(ItemQuest) == 0x50);

// A weapon (item.h).
struct ItemWeapon : Item {
    std::uint8_t unknown4C[0x10];
};
EI_EXE_LAYOUT(sizeof(ItemWeapon) == 0x5C);

// A unit's client half (unit_client_models.h).
struct UnitClientSpecific {
    std::uint8_t unknown0[0xCC];
    void* faceTexture;                      // +0xCC
    std::uint8_t unknownD0[0xC];
};
EI_EXE_LAYOUT(sizeof(UnitClientSpecific) == 0xDC);

// The party manager (party_manager.h).
struct PartyManager {
    std::uint8_t unknown0[0x38];
    std::uint8_t templates[12];             // +0x38
};
EI_EXE_LAYOUT(sizeof(PartyManager) == 0x44);

// A cursor image (ui_cursor.h).
struct CursorImage {
    std::uint8_t unknown0[0x14];
    int hotX;                               // +0x14
    int hotY;                               // +0x18
    std::uint8_t unknown1C[0x4];
};
EI_EXE_LAYOUT(sizeof(CursorImage) == 0x20);

// The mouse cursor (ui_cursor.h).
struct Cursor {
    int mode;                               // +0x00
    std::uint8_t unknown4[0x8];
    int imageShown;                         // +0x0C
    CursorImage* image;                     // +0x10
    std::uint8_t unknown14[0x84];
    void* thread;                           // +0x98
    std::uint8_t unknown9C[0x1C];
};
EI_EXE_LAYOUT(sizeof(Cursor) == 0xB8);

namespace options {

// The game's options (options.h).
struct GameOptions {
    std::uint8_t unknown0[0xC8];
    int enableWaterWaves;                   // +0xC8
    std::uint8_t unknownCC[0x13C];
    int showTutorial;                       // +0x208
    std::uint8_t unknown20C[0x5C];
    CString masterServerName;               // +0x268
};
EI_EXE_LAYOUT(sizeof(GameOptions) == 0x26C);

} // namespace options

// The game flow (world.h).
struct GameFlow {
    int state;                              // +0x00
    CString requestTarget;                  // +0x04
    std::uint8_t unknown8[0x7C];
};
EI_EXE_LAYOUT(sizeof(GameFlow) == 0x84);

// The interface's screen fade (ui.h).
struct ScreenFade {
    std::uint8_t unknown0[0x4];
    float alpha;                            // +0x04
    std::uint8_t active;                    // +0x08
    std::uint8_t unknown9[0xB];
};
EI_EXE_LAYOUT(sizeof(ScreenFade) == 0x14);

// A memory stream (core_streams.h).
struct MemoryStream {
    std::uint8_t unknown0[0x18];
    std::uint8_t* data;                     // +0x18
    std::uint8_t unknown1C[0x8];
    std::uint32_t size;                     // +0x24
};
EI_EXE_LAYOUT(sizeof(MemoryStream) == 0x28);

// A surface holder (texture.h).
struct DXSurface {
    std::uint8_t unknown0[0x10];
    void* surface;                          // +0x10
    std::uint8_t unknown14[0xC];
};
EI_EXE_LAYOUT(sizeof(DXSurface) == 0x20);

// An interface surface (ui.h).
struct UiSurface {
    std::uint8_t unknown0[0x8];
    void* dc;                               // +0x08
    std::uint8_t unknownC[0x4];
    void* holder;                           // +0x10
    int width;                              // +0x14
    int height;                             // +0x18
    std::uint8_t unknown1C[0x1C];
};
EI_EXE_LAYOUT(sizeof(UiSurface) == 0x38);

// A window's clickable region (ui.h).
struct UiRegion {
    std::uint8_t unknown0[0x14];
    Rect bounds;                            // +0x14
    std::uint8_t unknown24[0x1];
    bool enabled;                           // +0x25
    std::uint8_t unknown26[0x2];
    int offsetX;                            // +0x28
    int offsetY;                            // +0x2C
    std::uint8_t unknown30[0x4];
};
EI_EXE_LAYOUT(sizeof(UiRegion) == 0x34);

// An interface window (ui.h).
struct UiWindow {
    std::uint8_t unknown0[0x118];
    AlignedArray<UiSurface*> surfaces;     // +0x118
    AlignedArray<UiRegion*> regions;       // +0x128
    std::uint8_t unknown138[0x10];
};
EI_EXE_LAYOUT(sizeof(UiWindow) == 0x148);

namespace console {

// The console (console.h).
struct ConsoleWindow {
    std::uint8_t unknown0[0x4];
    UiSurface* surface;                    // +0x04
    std::uint8_t unknown8[0x74];
};
EI_EXE_LAYOUT(sizeof(ConsoleWindow) == 0x7C);

} // namespace console

// The interface manager (ui.h).
struct Interface3D {
    int width;                              // +0x00
    int height;                             // +0x04
    std::uint8_t unknown8[0xC];
    console::ConsoleWindow* console;        // +0x14
    ScreenFade* fade;                      // +0x18
    void* root;                             // +0x1C
    UiSurface* fpsSurface;                 // +0x20
    std::uint8_t unknown24[0x1C];
    UiWindow* queuedWindow;                // +0x40
    bool closeAll;                          // +0x44
    std::uint8_t unknown45[0xF];
    UiSurface* backdrop;                   // +0x54
    UiSurface* secondBackdrop;             // +0x58
    std::uint8_t unknown5C[0x8];
    UiSurface* statusSurface;              // +0x64
    std::uint8_t unknown68[0x48];
};
EI_EXE_LAYOUT(sizeof(Interface3D) == 0xB0);

// The save / load window (ui.h).
struct SaveLoadWindow {
    std::uint8_t unknown0[0x179];
    bool sliding;                           // +0x179
    std::uint8_t unknown17A[0x1A];
};
EI_EXE_LAYOUT(sizeof(SaveLoadWindow) == 0x194);

// The options window (ui.h).
struct OptionsWindow {
    std::uint8_t unknown0[0x3F5];
    bool sliding;                           // +0x3F5
    std::uint8_t unknown3F6[0x22];
};
EI_EXE_LAYOUT(sizeof(OptionsWindow) == 0x418);

struct HudQuickItems;
struct HudSpellColumn;

// The battle screen (ui.h).
struct BattleScreen {
    std::uint8_t unknown0[0x168];
    Vector<Unit*> party;                   // +0x168
    int pendingAction;                      // +0x17C
    int pendingSlot;                        // +0x180
    int pendingItem;                        // +0x184
    std::uint8_t unknown188[0x10];
    HudQuickItems* quickItems;             // +0x198
    HudSpellColumn* spellColumn;           // +0x19C
    std::uint8_t unknown1A0[0x90];
};
EI_EXE_LAYOUT(sizeof(BattleScreen) == 0x230);

// An entry of the quick item panel (ui.h, HudQuickItems::Entry).
struct HudQuickItemsEntry {
    bool own;                               // +0x00
    std::uint8_t unknown1[0x3];
    int id;                                 // +0x04
    Item* item;                            // +0x08
};
EI_EXE_LAYOUT(sizeof(HudQuickItemsEntry) == 0xC);

// The quick item panel (ui.h).
struct HudQuickItems {
    std::uint8_t unknown0[0x150];
    int selected;                           // +0x150
    Unit* unit;                            // +0x154
    Vector<HudQuickItemsEntry> entries;    // +0x158
    int regionItem[8];                      // +0x16C
    std::uint8_t unknown18C[0x14];
    using Entry = HudQuickItemsEntry;
};
EI_EXE_LAYOUT(sizeof(HudQuickItems) == 0x1A0);

// The spell column (ui.h).
struct HudSpellColumn {
    std::uint8_t unknown0[0x14C];
    int count;                              // +0x14C
    int selected;                           // +0x150
    Unit* unit;                            // +0x154
    std::uint8_t unknown158[0x14];
};
EI_EXE_LAYOUT(sizeof(HudSpellColumn) == 0x16C);

// The render device (texture.h).
struct TextureDevice {
    std::uint8_t unknown0[0x8];
    int lastLoadResult;                     // +0x08
    void* ddraw;                            // +0x0C
    std::uint8_t unknown10[0x8];
    DXSurface* renderTarget;               // +0x18 the render target's holder
    std::uint8_t unknown1C[0x160];
};
EI_EXE_LAYOUT(sizeof(TextureDevice) == 0x17C);

// The scene renderer's device (model.h).
struct HeldDevice {
    TextureDevice* device;                 // +0x00
};
EI_EXE_LAYOUT(sizeof(HeldDevice) == 0x4);

// The scene renderer (model.h).
struct SceneRenderer {
    std::uint8_t unknown0[0x20];
    HeldDevice device;                      // +0x20
    std::uint8_t unknown24[0x38];
};
EI_EXE_LAYOUT(sizeof(SceneRenderer) == 0x5C);

// An item strip (ui_screens.h).
struct UiItemStrip {
    std::uint8_t unknown0[0x1AC];
    AlignedArray<Item*>* source;           // +0x1AC all the items (a party's item list)
    std::uint8_t unknown1B0[0x4];
};
EI_EXE_LAYOUT(sizeof(UiItemStrip) == 0x1B4);

// The camp (ui_camp.h).
struct CampPages {
    const void* const* vtable;              // +0x00
    std::uint8_t unknown4[0x18C];
    int page;                               // +0x190
    std::uint8_t unknown194[0x4C];
    UiItemStrip* itemStrip;                // +0x1E0
    std::uint8_t unknown1E4[0x7C];
    // Slot 42 (TakeItem: MoveItem 00610AD0): `item` moved by action `mode` (1: put on the hero from the strip).
    void TakeItem(Item* item, int mode) {
        reinterpret_cast<void(__thiscall*)(CampPages*, Item*, int)>(vtable[42])(this, item, mode);
    }
};
EI_EXE_LAYOUT(sizeof(CampPages) == 0x260);

// A map.txt record (object_tables.h).
struct MapRecord {
    std::uint8_t unknown0[0x28];
    int kind;                               // +0x28 0 game, 1 edge, 2 brief
    std::uint8_t unknown2C[0x30];
};
EI_EXE_LAYOUT(sizeof(MapRecord) == 0x5C);

namespace net {

struct NetAddress {
    std::uint8_t unknown0[0x2];
    std::uint16_t port;                     // +0x02
    std::uint32_t ip;                       // +0x04
    std::uint8_t unknown8[0x8];
};
EI_EXE_LAYOUT(sizeof(NetAddress) == 0x10);

// A server the client found (net_classes.h).
struct ServerEntry {
    std::uint8_t unknown0[0x8];
    NetAddress address;                     // +0x08
    std::uint8_t unknown18[0x8];
    std::uint8_t foreign;                   // +0x20
    std::uint8_t unknown21[0x3];
    std::uint32_t state;                    // +0x24
    StlpString name;                        // +0x28
    std::uint32_t kind;                     // +0x34
    StlpString description;                 // +0x38
    std::uint8_t players;                   // +0x44
    std::uint8_t maxPlayers;                // +0x45
    std::uint8_t password;                  // +0x46
    std::uint8_t unknown47[0x1];
};
EI_EXE_LAYOUT(sizeof(ServerEntry) == 0x48);

// The game's server session (net_classes.h).
struct ServerSession {
    std::uint8_t unknown0[0x28];
    StlpList<RefPtr> clients;               // +0x28 the connections
    std::uint8_t unknown2C[0x25D4];
};
EI_EXE_LAYOUT(sizeof(ServerSession) == 0x2600);

// The game's client session (net_classes.h).
struct ClientSession {
    std::uint8_t unknown0[0x2598];
    std::uint32_t joinState;                // +0x2598
    std::uint8_t unknown259C[0xC];
    StlpList<RefPtr> servers;               // +0x25A8
    std::uint8_t unknown25AC[0x2C];
    StlpList<RefPtr> players;               // +0x25D8
    std::uint8_t unknown25DC[0x14];
    RefObject* binding;                    // +0x25F0
    std::uint8_t unknown25F4[0x10C];
};
EI_EXE_LAYOUT(sizeof(ClientSession) == 0x2700);

} // namespace net


} // namespace ei::verify::exe
