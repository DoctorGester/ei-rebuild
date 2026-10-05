#pragma once

// The 3D object base under CMapObject (vtable 0073DDD4, constructor 00515BD0, 0x170 bytes): the
// object's figure (a model resource shared through the figure manager), its parts (scene nodes,
// one per figure piece) and their bounding box, and the scale the figure was built for. Names
// provisional; see docs/notes/map_objects.md ("Figures").

#include "engine_object.h"
#include "maths.h"

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

// A growable pointer array whose storage is 32-byte aligned (the block from operator new is
// kept at data[-1]): data, size, capacity, grow-by (0: an eighth of the size, 4..1024).
struct AlignedPtrArray {
    // 00519E80. data[index] = value, growing the array to index + 1 first when needed.
    void SetAtGrow(int index, void* value);
    // 00519D70. Resizes to `size` (0 frees the storage); `growBy` replaces the grow-by unless -1.
    // New elements are not cleared.
    void SetSize(int size, int growBy);

    void** data;
    int size;
    int capacity;
    int growBy;
};
static_assert(sizeof(AlignedPtrArray) == 0x10);

// A part of an object's figure (a scene node; names provisional). Only what the figure code
// reads: its position relative to the object, the piece's offset and box in the figure.
struct FigurePart : CEngineObject {
    float position[3];              // +0x01C
    std::uint8_t unknown_028[0x18C - 0x28];
    float offset[3];                // +0x18C the piece's place in the figure
    float unknown_198;              // +0x198
    float boxMin[3];                // +0x19C
    float boxMax[3];                // +0x1A8
};
static_assert(offsetof(FigurePart, position) == 0x1C);
static_assert(offsetof(FigurePart, offset) == 0x18C);
static_assert(offsetof(FigurePart, boxMin) == 0x19C);
static_assert(offsetof(FigurePart, boxMax) == 0x1A8);

// A node of the figure's piece map (an STLport map<string, piece> at figure + 0x14: the key at
// +0x10, the piece from +0x1C); offsets are from the node. Only these fields reach the parts.
struct FigurePieceRecord {
    std::uint8_t unknown_00[0x28];
    float offset[3];                // +0x28 -> part +0x18C
    float unknown_34;               // +0x34 -> part +0x198
    float boxMin[3];                // +0x38 -> part +0x19C
    float boxMax[3];                // +0x44 -> part +0x1A8
};
static_assert(offsetof(FigurePieceRecord, boxMax) == 0x44);

// One animation of a figure's animation database (0x58 bytes).
struct FigureAnimation {
    std::uint8_t unknown_00[0x14];
    std::uint32_t key;              // +0x14 animation key bits (docs/notes/combat.md): kind 0x3C0000, number 0x3FC00000
    std::uint8_t unknown_18[0x58 - 0x18];
};
static_assert(sizeof(FigureAnimation) == 0x58);

// A figure's animation database: the data of "<figure name>.adb" as the object's manager
// returns it (CBaseManager::GetFile), kept at CObject3D + 0xA0.
struct AnimationDatabase {
    std::uint8_t unknown_00[4];
    std::uint32_t count;            // +0x04 animations
    std::uint8_t unknown_08[0x20 - 0x08];
    float start;                    // +0x20 \ the animation time factor maps t to
    float length;                   // +0x24 | (start + (end - start) * t) / length
    float end;                      // +0x28 /
    FigureAnimation animations[1];  // +0x2C `count` of them
};
static_assert(offsetof(AnimationDatabase, start) == 0x20);
static_assert(offsetof(AnimationDatabase, animations) == 0x2C);

// The side-specific half of a 3D object (CObject3D + 0x94): CObject3DClientSpecific (class id
// 0x37, vtable 0073DE90; map_object_shadow.h) on the client, CObject3DServer (0x38, 0073DFBC,
// stubs) on the server. Only the slots the rewritten code calls are named; the classes
// themselves do not derive from this declaration (their rewritten slots are registered by
// address), the 3D object reaches its half through CObject3D::Specific().
class CObject3DSpecific : public CEngineObject {
public:
    virtual int OnPartAdded(FigurePart* part);      // 13: client 0051B050, server 0051F270 (0)
    virtual void Slot14();                          // 14
    virtual int OnPartRemoved(FigurePart* part);    // 15: client 0051B160, server 0051F280 (0)
    virtual void Slot16();                          // 16: client 0051B2E0 (a part by name, 005169A0)
    virtual void Slot17();                          // 17: client 0051B2D0 (a part by id, 00516920)
    // 18: the figure's extra scale: client +0xF0..+0xF8 (0051AD20), server 1, 1, 1 (0051AC30).
    virtual void GetScale(float* x, float* y, float* z);
};

class CObject3D;

// The figure's animation state (CObject3D + 0x98; 0050D0E0 adds a part, 0050D350 removes one,
// 0050D3A0 lets go of every part, 0050D7C0 rescales). Only its back pointer is named.
class CFigureAnimationState : public CEngineObject {
public:
    CObject3D* owner;               // +0x1C
};

// A counted reference: the pointer slot is the `this` of 00519580.
struct CountedRef {
    // 00519580 (object3d_lifetime.cpp). object = `value`: the new object gains a reference, the
    // old one loses one and is deleted (its slot 0 with 1) when that was the last.
    void Assign(CEngineObject* value);

    CEngineObject* object;
};

class CObject3D : public CEngineObject {
public:
    // map_object_lifetime.cpp.
    // 00515BD0. The constructor: the engine object and transform bases (vtables 0073B8E0,
    // 0073BA38), identity orientations, empty name, part list (vtable 0073DE4C), arrays and
    // string array, the bounds cleared, then the vtable 0073DDD4 and the default figure scale
    // from 007AF1D0. Returns this.
    CObject3D* Construct();
    // 00515D70. The destructor body: the parts released (00516080), the frame array, string
    // array, figure (counted), part arrays, list blocks and name freed, then the transform and
    // engine object bases.
    void DestructBody();

    // 00518310. The bounding box of the parts (+0xD0, only those within 1e9 squared of the
    // object) and of the pending parts (+0xE0, whose positions are reset to 0) into the bounds:
    // centre +0x128, half extents below (+0x138) and above (+0x144) it, radius +0x134; sets bits
    // 1 and 2 of +0x124. Nothing changes without parts. Returns what the original leaves in EAX
    // (the centre's z bits, 0 without parts).
    std::uint32_t ComputeBounds();

    // 00517700. Sets the scale (+0xC4) and, when it changed, rebuilds the figure at that scale
    // through the figure manager, copies each part's piece record from it, rescales the
    // animation (+0x98, 0050D7C0) and recomputes the bounds. Returns what the original leaves in
    // EAX on the rebuild path (0 when the scale did not change).
    std::uintptr_t SetFigureScale(const float* newScale);

    // 00517500. Virtual slot 26: loads the figure named by +0xA4 and builds the pending parts
    // (+0xE0) at the current scale; each part not yet in +0xD0 joins it (referenced), learns its
    // owner and is announced to the animation (+0x98) and the attached object (+0x94, slot 13).
    // Non-units mark every part and themselves (flags_ 8 / 0x200). Then +0x94's slot 21 and
    // flags_ 0x80. Returns 1 (also when already loaded).
    int LoadFigures();

    // 005164B0. Adds `part` to the parts unless it is there: referenced, told its owner, announced
    // to the animation and to the attached object (whose slot 13 result is returned). Returns 1
    // otherwise.
    int AddPart(FigurePart* part);

    // map_object_parts.cpp (A-U).
    // 00516A80. The first part (of the parts, then of the pending parts) whose name starts with
    // `prefix` (strstr at the name's start; case-sensitive), or null.
    FigurePart* FindPartByPrefix(const char* prefix);
    // 005169A0. The first part (parts, then pending parts) named exactly `partName`, or null.
    FigurePart* FindPart(const char* partName);
    // 005167E0. Takes `part` out of the parts (it forgets its owner, 0059F3D0) or else out of the
    // pending parts, tells the animation state (+0x98, 0050D350) and releases it. The side half
    // (+0x94, slot 15) hears of it first, found or not. Returns 1 when it was there, else 0.
    int RemovePart(FigurePart* part);
    // 005168F0. FindPart(`partName`), removed by RemovePart. Returns the part (released; the pointer
    // may be dangling) or null.
    FigurePart* RemovePartNamed(const char* partName);
    // 005188B0. `part`'s position (+0x1C) turned by the orientation (+0x34, w first), scaled by
    // the side half's GetScale and moved to the object's position. Returns `out`.
    float* PartWorldPosition(float* out, const FigurePart* part);

    // 00518CA0. The animation database (+0xA0), loaded on first use through the owner's GetFile
    // ("<name>.adb"; nothing without an owner). Null when there is none.
    AnimationDatabase* Animations();
    // 00518F80. Animation `key & 0xFF` of the database, or null (no database, or beyond its count).
    FigureAnimation* AnimationAt(std::uint32_t key);
    // 00518FC0. The first animation of kind 0x40000 whose number (key bits 22-29) is `number`, or
    // null (the hero preview asks for 0x21, 00616E20; the interface's models 005FC3B0).
    FigureAnimation* FindSkillAnimation(int number);
    // 00518D30. For units (class id 0x50) with an animation database whose length is neither 0
    // nor NaN: (start + (end - start) * time) / length, else 1. In ST0, unrounded.
    double AnimationFactor(float time);
    // 00517990. The index of the part name (+0x114) equal to `partName` (_mbscmp), or -1.
    int FindPartIndex(const char* partName);

    // object3d_lifetime.cpp (A-U).
    // 00515F70. Virtual slot 2 (Create; another name so it is no override): position 0, the
    // identity orientation (007AF1C0), no side half (dropped without a release), flags 0, no
    // animation database, scale 0, the figure released (deleted at its last reference), the
    // default scale (007AF1D0) at +0xF4, the bounds cleared and the part boxes freed. Returns 1.
    int Create3D();
    // 00516080. Virtual slot 3 (Destroy): the figure released (00519580), every effect source
    // removed (00518AE0), the animation state lets go of its parts (0050D3A0), is detached and
    // released, the parts lose a reference without a release and the array is freed, the pending
    // parts are released and freed, the side half released; +0x9C, the database and flags 0.
    void Destroy3D();
    // 00518A00. Adds `source` (a particle source; it calls this from its Attach 004849F0) to the
    // effect sources unless it is there: referenced and attached to this object.
    void AddEffectSource(CEngineObject* source);
    // 00518AE0. Takes `source` out of the effect sources (its node freed; the last one frees the
    // list's blocks), detaches it (004849F0 with null) and releases it. Nothing when not there.
    void RemoveEffectSource(CEngineObject* source);

    // The side-specific half (+0x94).
    CObject3DSpecific* Specific() const { return static_cast<CObject3DSpecific*>(attached); }

    float position[3];              // +0x01C
    std::uint8_t unknown_028[0x34 - 0x28];
    Quaternion orientation;         // +0x034
    std::uint8_t unknown_044[0x94 - 0x44];
    CEngineObject* attached;        // +0x094 the object slot 19 accepts (type 0x46)
    CFigureAnimationState* animation;   // +0x098 the figure's animation state
    std::uint32_t unknown_09C;      // +0x09C cleared by Create3D / Destroy3D
    AnimationDatabase* animations;  // +0x0A0 loaded on first use (00518CA0)
    const char* name;               // +0x0A4 CString: the figure's name
    // +0x0A8 the particle sources attached to the object (MFC CPtrList, vtable 0073DE4C, block
    // size 10).
    mfc::CTypedPtrList<CEngineObject> effectSources;
    float scale[3];                 // +0x0C4
    AlignedPtrArray parts;          // +0x0D0 FigurePart*
    AlignedPtrArray pendingParts;   // +0x0E0 FigurePart*, built by LoadFigures
    CEngineObject* figure;          // +0x0F0 counted reference (slot 0 deletes)
    float defaultScale[3];          // +0x0F4 from 007AF1D0 (constructor, Create3D)
    std::uint8_t unknown_100[0x114 - 0x100];
    char** partNames;               // +0x114 the figure's part names (00517990), in the order of
    int partNameCount;              // +0x118 the part boxes (CUnit::partBoxes, +0x160)
    std::uint8_t unknown_11C[0x124 - 0x11C];
    std::uint32_t boundsFlags;      // +0x124 1 and 2: the bounds are valid
    float center[3];                // +0x128
    float radius;                   // +0x134
    float below[3];                 // +0x138 box minimum - centre
    float above[3];                 // +0x144 box maximum - centre
    std::uint8_t unknown_150[0x15C - 0x150];   // cleared with the bounds
    void* partBoxArrayVtable;       // +0x15C \ CArray of the part boxes (vtable 0073DE40)
    void* partBoxes;                // +0x160 |
    int partBoxCount;               // +0x164 |
    int partBoxCapacity;            // +0x168 |
    int partBoxGrowBy;              // +0x16C /
};
static_assert(offsetof(CObject3D, orientation) == 0x34);
static_assert(offsetof(CObject3D, attached) == 0x94);
static_assert(offsetof(CObject3D, effectSources) == 0xA8);
static_assert(offsetof(CObject3D, defaultScale) == 0xF4);
static_assert(offsetof(CObject3D, partBoxes) == 0x160);
static_assert(offsetof(CObject3D, animations) == 0xA0);
static_assert(offsetof(CObject3D, name) == 0xA4);
static_assert(offsetof(CObject3D, partNames) == 0x114);
static_assert(offsetof(CObject3D, scale) == 0xC4);
static_assert(offsetof(CObject3D, parts) == 0xD0);
static_assert(offsetof(CObject3D, pendingParts) == 0xE0);
static_assert(offsetof(CObject3D, figure) == 0xF0);
static_assert(offsetof(CObject3D, boundsFlags) == 0x124);
static_assert(offsetof(CObject3D, center) == 0x128);
static_assert(offsetof(CObject3D, radius) == 0x134);
static_assert(offsetof(CObject3D, below) == 0x138);
static_assert(offsetof(CObject3D, above) == 0x144);
static_assert(sizeof(CObject3D) == 0x170);
