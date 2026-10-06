#pragma once

// The 3D object base under CMapObject (vtable 0073DDD4, constructor 00515BD0, 0x170 bytes): the
// object's figure (a model resource shared through the figure manager), its parts (scene nodes,
// one per figure piece) and their bounding box, and the scale the figure was built for. Names
// provisional; see docs/notes/map_objects.md ("Figures").

#include "engine_object.h"
#include "maths.h"

#include "lib/containers.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class CObject3D;
class CTransformObject;   // core_transform.h

// A growable pointer array whose storage is 32-byte aligned (the block from operator new is
// kept at data[-1]). Registered on the template: SetAtGrow 00519E80 and SetSize 00519D70
// (map_object_figures.cpp) and their copies 0051A010 / 0051A190, 00663960-00663D30 and
// 00664490-00664710; Add 00519A10 (object3d_figure_setup.cpp).
using AlignedPtrArray = lib::AlignedArray<void*>;

struct FigurePart;
struct FootprintPart;   // ai_map.h: a figure part's box (0x38 bytes)

// What animates one figure part (class id 0x300B, made by the part's manager on first use and
// kept counted at FigurePart +0x50). Only the fields the animation state sets.
struct FigurePartAnimator : CEngineObject {
    const struct FigureAnimation* animation;    // +0x1C the animation it plays (null: none)
    FigurePart* part;               // +0x20 the part it animates
    std::uint8_t unknown_24[0x48 - 0x24];
    int unknown_48;                 // +0x48 cleared when the animation changes
};
static_assert(offsetof(FigurePartAnimator, part) == 0x20);
static_assert(offsetof(FigurePartAnimator, unknown_48) == 0x48);

// A part of an object's figure (a scene node; names provisional). Only what the figure code
// reads: its position relative to the object, the piece's offset and box in the figure.
struct FigurePart : CEngineObject {
    float position[3];              // +0x01C
    std::uint8_t unknown_028[0x2C - 0x28];
    float alpha;                    // +0x02C (a unit's fade-in, CUnitClientSpecific +0x78)
    std::uint8_t unknown_030[0x40 - 0x30];
    float tint[3];                  // +0x040 a colour the client half sets (stunned: red, 0, 0)
    FigurePartAnimator* previousAnimator;   // +0x04C the animator before an animation change
    FigurePartAnimator* animator;   // +0x050 counted; made on first use (Animator())
    std::uint8_t unknown_054[0x74 - 0x54];
    float pivot[3];                 // +0x074 copied to the object's part box (FootprintPart::pivot)
    std::uint8_t unknown_080[0x158 - 0x80];
    std::uint32_t piece158;         // +0x158 from the piece record's +0x20 (00517240)
    std::uint32_t piece15C;         // +0x15C from its +0x1C
    std::uint8_t unknown_160[0x188 - 0x160];
    // +0x188..+0x1C0: the part's box, laid out as a FootprintPart (ai_map.h); copied whole from
    // the piece record (FigurePieceRecord::boxFlags on).
    std::uint32_t boxFlags;         // +0x188 4 loaded, 0x400 "BASE", 0x90 "CROWN" / "EMPTY", 0x800 "EMPTY"
    float offset[3];                // +0x18C the piece's place in the figure
    float unknown_198;              // +0x198
    float boxMin[3];                // +0x19C
    float boxMax[3];                // +0x1A8
    float boxPivot[3];              // +0x1B4 the part's position when it was loaded
};
static_assert(offsetof(FigurePart, position) == 0x1C);
static_assert(offsetof(FigurePart, tint) == 0x40);
static_assert(offsetof(FigurePart, animator) == 0x50);
static_assert(offsetof(FigurePart, offset) == 0x18C);
static_assert(offsetof(FigurePart, boxMin) == 0x19C);
static_assert(offsetof(FigurePart, boxMax) == 0x1A8);
static_assert(offsetof(FigurePart, piece158) == 0x158);
static_assert(offsetof(FigurePart, boxFlags) == 0x188);
static_assert(offsetof(FigurePart, boxPivot) == 0x1B4);

// A node of the figure's piece map (an STLport map<string, piece> at figure + 0x14: the key at
// +0x10, the piece from +0x1C); offsets are from the node. Only these fields reach the parts.
struct FigurePieceRecord {
    std::uint8_t unknown_00[0x1C];
    std::uint32_t unknown_1C;       // +0x1C -> part +0x15C (00517240)
    std::uint32_t unknown_20;       // +0x20 -> part +0x158
    std::uint32_t boxFlags;         // +0x24 -> part +0x188 (0x38 bytes copied from here)
    float offset[3];                // +0x28 -> part +0x18C
    float unknown_34;               // +0x34 -> part +0x198
    float boxMin[3];                // +0x38 -> part +0x19C
    float boxMax[3];                // +0x44 -> part +0x1A8
    float pivot[3];                 // +0x50 -> part +0x1B4
};
static_assert(offsetof(FigurePieceRecord, boxMax) == 0x44);

// One animation of a figure's animation database (0x58 bytes).
struct FigureAnimation {
    char name[0x10];                // +0x00 (the figure manager's 005ACCF0 takes the entry as its name)
    std::uint8_t index;             // +0x10 its index in the database (a unit keeps it at +0x254 / +0x255)
    std::uint8_t unknown_11[3];
    std::uint32_t key;              // +0x14 animation key bits (docs/notes/combat.md): kind 0x3C0000, number 0x3FC00000
    std::uint32_t flags;            // +0x18 bit 0: a bow's arrow shows from frame arrowFrom to arrowTo
    std::uint32_t chance;           // +0x1C percent (005190C0 drops entries below a random percent)
    std::uint32_t frameCount;       // +0x20 the animation's length in frames
    std::uint8_t unknown_24[0x28 - 0x24];
    std::uint32_t arrowFrom;        // +0x28
    std::uint32_t arrowTo;          // +0x2C
    std::uint32_t stepFrames[4];    // +0x30 the footsteps' frames + 1 (0 ends the list)
    std::uint32_t moment;           // +0x40 the action's moment (frame)
    int soundFrame;                 // +0x44 the frame of the action's sound (0: none)
    std::uint32_t sounds[4];        // +0x48 sound numbers to pick from (0 ends the list)
};
static_assert(offsetof(FigureAnimation, arrowFrom) == 0x28);
static_assert(offsetof(FigureAnimation, soundFrame) == 0x44);
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
    // 19 / 20: client 0051B7E0 (CObject3DClientSpecific::Draw) / 0051CE60 (DrawShadow); server 0051AC60 / 0051AC70.
    virtual int DrawParts(void* context, int* dirty);
    virtual int DrawPartsShadow(void* context);
    virtual void Slot21();
    virtual void Slot22();
    virtual void Slot23();
    virtual void Slot24();
    virtual void Slot25(int);                       // 25: client 0051C9D0 (empty)
    virtual void SetLit(int lit);                   // 26: client 0051C9E0 (0051C9F0): the object's light is on / off

    CObject3D* object;              // +0x1C the object it belongs to
};

class CObject3D;

// The figure's animation state (CObject3D + 0x98; 0050D0E0 adds a part, 0050D350 removes one,
// 0050D3A0 lets go of every part, 0050D7C0 rescales).
class CFigureAnimationState : public CEngineObject {
public:
    // map_object_animation_state.cpp (A-U2).
    // 0050CA50. Plays `animation` from its start: each part keeps its animator as the previous
    // one (making it first, Animator()), the figure manager (the world's slot 15) is asked to set
    // the animation on this state by the owner's model name (005ACCF0); when it does not, each
    // part's animator takes it (AnimatePart). Then SetTime(0) and `current` = `animation`.
    void Play(FigureAnimation* animation);
    // 0050CBA0. Queues `animation` after the one playing (the queue's AddTail). Returns the node.
    void* Queue(FigureAnimation* animation);
    // 0050D600. The frame reached becomes `frame` (`changed` set when it differs): with nothing
    // playing the next queued animation starts at 0; while the frame lies beyond the animation's
    // frame count the count is taken off, the callback (+0x64) runs, and the next queued animation
    // plays, or else the animation holds its last frame unless its key's top bits are 10 (loops).
    // When the animation changed, the figure manager or the parts take it as in Play.
    void SetTime(float frame);
    // 0050D5E0. SetTime(time + `frames`).
    void Advance(float frames);

    // Each part's animator plays `animation` (0050CA50 / 0050D600 inline it): the part's manager
    // is told (005ACA60) and the animator's +0x48 cleared.
    static void AnimatePart(FigurePart* part, const FigureAnimation* animation);

    CObject3D* owner;               // +0x1C
    std::uint8_t unknown_20[4];
    // +0x24 the parts the animation moves (MFC CPtrList; 0050D0E0 adds, 0050D350 removes).
    mfc::CTypedPtrList<FigurePart> parts;
    FigureAnimation* current;       // +0x40 the animation playing, or null
    // +0x44 the animations queued after it (MFC CPtrList; the client update keeps fewer than 3).
    mfc::CTypedPtrList<FigureAnimation> queue;
    float time;                     // +0x60 the frame reached
    // +0x64 called (with the owner in ECX) each time an animation's end is passed, or null.
    void(__fastcall* onEnd)(CObject3D* owner);
    std::uint8_t unknown_68[0x74 - 0x68];
    std::uint8_t changed;           // +0x74 set when the frame or the animation changed
};
static_assert(offsetof(CFigureAnimationState, parts) == 0x24);
static_assert(offsetof(CFigureAnimationState, current) == 0x40);
static_assert(offsetof(CFigureAnimationState, queue) == 0x44);
static_assert(offsetof(CFigureAnimationState, time) == 0x60);
static_assert(offsetof(CFigureAnimationState, onEnd) == 0x64);
static_assert(offsetof(CFigureAnimationState, changed) == 0x74);

// The figure an object holds at +0xF0 (counted; slot 0 deletes; class not identified). Only what
// CObject3D::LoadPart uses.
class CFigureInstance {
public:
    virtual void DeletingDestructor(unsigned flags);   // 0
    virtual void Slot1();
    virtual void Slot2();
    virtual void Slot3();
    virtual bool HasPiece(const void* name);   // 4: the piece map (+0x14) holds `name` (an stlp::string)

    int refCount;                   // +0x04
    std::uint8_t unknown_08[0x14 - 0x08];
    std::uint8_t pieces[0x10];      // +0x14 STLport map<string, FigurePieceRecord> (find 005197B0)
};
static_assert(offsetof(CFigureInstance, pieces) == 0x14);

// The figure cache (the world's slot 23): builds the figures objects hold at +0xF0. Only the slots
// the 3D object calls; the scale is passed as float bits.
class CFigureCache {
public:
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    // 3: the figure of model `name` (an stlp::string) made of the pieces `pieceNames` (an STLport
    // vector<string>), at the scale; referenced by the caller.
    virtual CFigureInstance* BuildFigure(const void* name, const void* pieceNames, std::uint32_t x, std::uint32_t y,
                                         std::uint32_t z, int flag);
    // 4: the whole figure of model `name` at the scale.
    virtual CFigureInstance* BuildWholeFigure(const void* name, std::uint32_t x, std::uint32_t y, std::uint32_t z,
                                              int flag);
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
    // 00516B50 (object3d_figure_setup.cpp, A-U4; RET 8). Every part (parts, then pending parts)
    // whose name starts with `prefix` (strstr at the name's start) appended to `out`.
    void CollectPartsByPrefix(const char* prefix, AlignedPtrArray* out);
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
    // object3d_animations.cpp (A-U4).
    // 005190C0 (RET 4). A random animation for `key`: the database's entries whose key has the
    // same bits above 0x7FFF and all of key's low 15 bits (none when key has none) and that the
    // object's slot 18 accepts (collected with 0051A010); one is returned as it is, several are
    // narrowed to those whose chance (+0x1C) is at least a rand() percent and one of them is
    // picked with rand() (the pick made before narrowing when none is left). Null without a
    // database or a match.
    FigureAnimation* RandomAnimation(std::uint32_t key);
    // 00519380 (RET 4). As RandomAnimation for the entries whose key's bits above 0x7FFF equal
    // key's (grown with 0051A190), one picked with rand(); no chance narrowing.
    FigureAnimation* RandomAnimationOfKind(std::uint32_t key);
    // 00518D30. For units (class id 0x50) with an animation database whose length is neither 0
    // nor NaN: (start + (end - start) * time) / length, else 1. In ST0, unrounded.
    double AnimationFactor(float time);
    // object3d_figure_setup.cpp (A-U4).
    // 005179E0. Sets the figure up for the model `model` (RET 4): the animation state (+0x98) is
    // rescaled (0050D7C0), given the model (0050D320), plays nothing and applies it; one part box
    // (+0x15C, CArray of FootprintPart, grown like MFC's SetSize) and one part name (+0x110) per
    // animated part: the figure manager's bounds of the part (005AC010 with the model and the
    // scale), the part's pivot, flags 4 (| 0x400 "BASE", 0x90 "CROWN", 0x890 "EMPTY" name
    // prefixes); then the state releases its parts (0050C990). The bounds (+0x124..+0x14C) span
    // every box's pivot + min / max + offset (heroes, type_ 0x32: x and y -0.5..0.5); without an
    // animation state the box stays empty (FLT_MAX). Returns nothing.
    void SetUpAnimatedFigure(const char* model);
    // 00517240 (RET 0xC). A part object for the figure part `partName` (the figure manager's
    // 005AC2B0 with the object's model name, `model`, the scale, `arg` and flags_ & 0x1000; none:
    // "Can't load figure" logged, null). With a figure (+0xF0) the part must be one of its pieces
    // (else null, the part kept unreleased) and takes the piece record's box (0x38 bytes at
    // +0x188) and +0x158 / +0x15C. Name prefixes "BASE" / "CROWN" / "EMPTY" add box flags 0x400 /
    // 0x90 / 0x890. A new part joins the parts (referenced; owner, animation state and side half
    // told as AddPart does); the box pivot is the part's position and flag 4 is set. Returns it.
    FigurePart* LoadPart(const char* partName, void* model, std::uint32_t arg);
    // 00517110 (RET 0xC). The object's figure becomes `model` (+0xA4) and each name of the
    // CStringArray `partNames` is loaded by LoadPart (with the cached model; failures logged).
    // Without a figure manager: FatalError. Returns 1.
    int LoadParts(const char* model, const void* partList, std::uint32_t arg);
    // 00518100 (plain RET; the world's 00566030 when a unit becomes the controlled one). With the
    // figure loaded (flags_ 0x80): each part's world transform is updated from the object's
    // (part slot 15) and its world box grows the bounds (0059FDF0); the centre (+0x128) is the
    // box centre relative to the position, below / above the box minus the centre, the radius
    // the length of `below`; bounds flags 2 and 1 set.
    void UpdatePartBounds();
    // 00516DA0 (RET 0xC). Builds the figure of `model` (+0xA4) for the part names `partList` (a
    // CStringArray): the animation state is rescaled and given the model; with no names and an
    // animation state the names are the state's parts (0050D920) and the figure the cache's whole
    // figure (slot 4), otherwise the cache's figure of those pieces (slot 3); then LoadParts, an
    // animated unit (class 0x50 / 0x54) plays nothing, the bounds are recomputed and flags_ 0x80
    // is set unless both `arg` and the figure are 0 (then cleared). Returns 1.
    int MakeFigure(const char* model, void* partList, std::uint32_t arg);
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

    // object3d_slots.cpp (A-U2).
    // 00516370. Virtual slot 14 (CTransformObject::CombineRotation's place; the same body as
    // 0040CB80): worldOrientation = parent * orientation.
    void CombineRotation3D(const Quaternion* parent);
    // 005161E0. Virtual slot 20: takes `other`'s parts (the array copied, no references taken)
    // and its transform (CTransformObject::CopyTransform 00401490).
    void CopyFigure(const CObject3D* other);
    // 00516D50. Virtual slot 19: a side half (type 0x46) replaces the attached one, which is
    // detached (+0x1C cleared) and released; the new one points back here and gains a reference.
    // Other objects (and null) are ignored.
    void AttachSpecific(CEngineObject* half);
    // 00515E90. Virtual slot 5: every effect source is taken out of the list, detached (004849F0
    // with null) and released.
    void RemoveAllEffectSources();
    // 00516460 / 00516420 / 00516490. Virtual slots 22 / 23 / 24: the side half draws the figure
    // (its slot 19; slot 23 first empties dirtyRect and has it filled) / the shadow (its slot 20).
    // 1 without a side half, else the half's result.
    int DrawFigure(void* context);
    int DrawCollectDirty(void* context);
    int DrawFigureShadow(void* context);

    // The side-specific half (+0x94).
    CObject3DSpecific* Specific() const { return static_cast<CObject3DSpecific*>(attached); }
    // The object as the CTransformObject it starts with (core_transform.h).
    CTransformObject* Transform() { return reinterpret_cast<CTransformObject*>(this); }
    const CTransformObject* Transform() const { return reinterpret_cast<const CTransformObject*>(this); }

    float position[3];              // +0x01C
    std::uint8_t unknown_028[0x34 - 0x28];
    Quaternion orientation;         // +0x034
    Quaternion worldOrientation;    // +0x044 the parent's rotation times the orientation (slot 14)
    std::uint8_t unknown_054[0x94 - 0x54];
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
    int dirtyRect[4];               // +0x100 RECT: the screen area the last DrawCollectDirty covered
    void* partNameArrayVtable;      // +0x110 \ CStringArray of the part names
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
    FootprintPart* partBoxes;       // +0x160 | (ai_map.h)
    int partBoxCount;               // +0x164 |
    int partBoxCapacity;            // +0x168 |
    int partBoxGrowBy;              // +0x16C /
};
static_assert(offsetof(CObject3D, orientation) == 0x34);
static_assert(offsetof(CObject3D, worldOrientation) == 0x44);
static_assert(offsetof(CObject3D, dirtyRect) == 0x100);
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
