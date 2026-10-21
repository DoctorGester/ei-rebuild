#pragma once

// The 3D object base under CMapObject (vtable 0073DDD4, constructor 00515BD0, 0x170 bytes): the
// object's figure (a model resource shared through the figure manager), its parts (scene nodes,
// one per figure piece) and their bounding box, and the scale the figure was built for. Names
// provisional; see docs/notes/map_objects.md ("Figures").

#include "engine_object.h"
#include "figure_mesh.h"            // CFigureMesh (an object's figure parts)
#include "map_object_animation.h"   // AnimationTrack (a bone's animator)
#include "maths.h"

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/nan_audit.h"

#include <cstddef>
#include <cstdint>

class CObject3D;
class CLinkMesh;          // link_figure.h: an object's figure (+0xF0)
class CTransformObject;   // core_transform.h
struct tagRECT;           // windows.h

// A growable pointer array whose storage is 32-byte aligned (the block from operator new is
// kept at data[-1]). Registered on the template: SetAtGrow 00519E80 and SetSize 00519D70
// (map_object_figures.cpp) and their copies 0051A010 / 0051A190, 00663960-00663D30 and
// 00664490-00664710; Add 00519A10 (object3d_figure_setup.cpp).
using AlignedPtrArray = lib::AlignedArray<void*>;

class CFigureMesh;      // figure_mesh.h: a part of an object's figure
struct FootprintPart;   // ai_map.h: a figure part's box (0x38 bytes)

// One animation of a figure's animation database (0x58 bytes).
struct FigureAnimation {
    char name[0x10];                // +0x00 (the figure manager's 005ACCF0 takes the entry as its name)
    std::uint8_t index;             // +0x10 its index in the database (a unit keeps it at +0x254 / +0x255)
    std::uint8_t unused_11[3];
    std::uint32_t key;              // +0x14 animation key bits (docs/notes/combat.md): kind 0x3C0000, number 0x3FC00000
    std::uint32_t flags;            // +0x18 bit 0: a bow's arrow shows from frame arrowFrom to arrowTo
    std::uint32_t chance;           // +0x1C percent (005190C0 drops entries below a random percent)
    std::uint32_t frameCount;       // +0x20 the animation's length in frames
    // +0x24 the distance one play of the animation moves the unit (0: the speed comes from the
    // unit instead; ClientPlaceOnPath, unit_client_path.cpp).
    float stride;
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
    std::uint8_t unused_00[4];
    std::uint32_t count;            // +0x04 animations
    std::uint8_t unused_08[0x20 - 0x08];
    float start;                    // +0x20 \ the animation time factor maps t to
    float length;                   // +0x24 | (start + (end - start) * t) / length
    float end;                      // +0x28 /
    FigureAnimation animations[1];  // +0x2C `count` of them

    // The database `animation` is an entry of (entries know their index).
    static const AnimationDatabase* Of(const FigureAnimation* animation) {
        const FigureAnimation* first = animation - animation->index;
        return reinterpret_cast<const AnimationDatabase*>(reinterpret_cast<const char*>(first) -
                                                          offsetof(AnimationDatabase, animations));
    }
    // (start + (end - start) * t) / length, unrounded; 1 for a length of 0 (00518D30, 0050C230).
    double TimeFactor(float t) const {
        EI_NAN_AUDIT(length);
        if (length == 0.0f) return 1.0;
        return ((static_cast<double>(end) - start) * t + start) / length;
    }
};
static_assert(offsetof(AnimationDatabase, start) == 0x20);
static_assert(offsetof(AnimationDatabase, animations) == 0x2C);

class CBaseManager;   // base_manager.h

// The side-specific half of a 3D object (CObject3D + 0x94): CObject3DClientSpecific (class id
// 0x37, vtable 0073DE90; map_object_shadow.h) on the client, CObject3DServer (0x38, 0073DFBC,
// stubs) on the server. Only the slots the rewritten code calls are named; the classes
// themselves do not derive from this declaration (their rewritten slots are registered by
// address), the 3D object reaches its half through CObject3D::Specific().
class CObject3DSpecific;
// 0051F240 (object3d_halves.cpp). The server half's constructor (the engine object's fields inline): no
// object. Returns the half.
CObject3DSpecific* __fastcall Object3DServerConstruct(CObject3DSpecific* self);
class CObject3DSpecific : public CEngineObject {
public:
    // 0051F2E0 (slot 0 of the server half, RET 4; object3d_halves.cpp). The destructor, then freed when
    // bit 0 of `flags` is set. Returns this.
    CObject3DSpecific* DeleteServerHalf(unsigned flags);
    void IgnoreArgument(int);
    int ReturnZero(void*);
    int DrawNothing(void* /*context*/, int* /*dirty*/);
    virtual int OnPartAdded(CFigureMesh* part);      // 13: client 0051B050, server 0051F270 (0)
    // 14: (name) client 0051B210: the part named (slot 16) taken out of the drawn part sets (+0x20,
    // +0x24) and released; returns it or null. Server 0051F290. Slots declared `void Name()` keep
    // their place in the order only (docs/notes/map_objects.md ("Virtual slot names")).
    virtual CFigureMesh* RemoveFromPartSets(const char* name);
    virtual int OnPartRemoved(CFigureMesh* part);    // 15: client 0051B160, server 0051F280 (0)
    virtual CFigureMesh* FindPart(const char* name); // 16: client 0051B2E0 (a part by name, 005169A0); server 0051F2B0 (0)
    virtual void FindPartById();                    // 17: client 0051B2D0 (a part by id, 00516920)
    // 18: the figure's extra scale: client +0xF0..+0xF8 (0051AD20), server 1, 1, 1 (0051AC30).
    virtual void GetScale(float* x, float* y, float* z);
    // 19 / 20: client 0051B7E0 (CObject3DClientSpecific::Draw) / 0051CE60 (DrawShadow); server 0051AC60 / 0051AC70.
    virtual int DrawParts(void* context, int* dirty);
    virtual int DrawPartsShadow(void* context);
    // 21: client 0051CB30 (after LoadFigures): the world's materials (slot 33) to the parts, then the
    // skin texture (+0xE0 / +0xE4, 0051C250). Server 0051AC80.
    virtual void ApplyMaterials();
    // 22-24 (screen picking): client 0051C130 the point in the object's dirtyRect (flags_ 0x20) /
    // 0051C1D0 a rect meeting a drawn part's screen rect / 0051C160 the point in one. Server
    // 0051AC90 / 0051ACB0 / 0051ACA0.
    // 22: whether the screen point (x, y) is in the object's dirtyRect (PickAt).
    virtual int BoundsContainPoint(const long* point);
    // 23 (RET 4): whether the screen rectangle (a RECT) meets a drawn part's rectangle.
    virtual int PartsIntersectRect(const long* rect);
    // 24: whether the point is in the rectangle (+0x94 + 0x20) of a part with flag 0x40 (PickAt2).
    virtual int PartsContainPoint(const long* point);
    virtual void Unused25(int);                     // 25: client 0051C9D0, server 0051ACC0 (empty)
    virtual void SetLit(int lit);                   // 26: client 0051C9E0 (0051C9F0): the object's light is on / off

    CObject3D* object;              // +0x1C the object it belongs to
};

class CObject3D;

// A bone of a figure's skeleton (class id 0x300C, made by 0050CC60 from the skeleton links; names
// provisional). The animation state's bone list holds them, its +0x20 the root. Only the fields
// the pose code uses.
struct FigureBone : CEngineObject {
    mfc::CString name;                      // +0x1C the piece's name (0050CC60 links children by it)
    mfc::CString name2;                     // +0x20 its parent's name (empty for the root)
    mfc::CString name3;                     // +0x24 the figure's model name (LinkBone)
    FigureBone* parent;                     // +0x28 null for the root
    mfc::CTypedPtrList<FigureBone> children;    // +0x2C
    CFigureMesh* mesh;                      // +0x48 the part the bone places, or null
    AnimationTrack* pose;                   // +0x4C the animator whose values place the bone
    AnimationTrack* animator;               // +0x50 counted, made on first use; Play copies it to +0x4C
    CEngineObject* counted54;               // +0x54 counted (released by DestroyBone)
    CEngineObject* counted58;               // +0x58 counted
    Vector3 offset;                         // +0x5C the place relative to the parent
    float morphS;                           // +0x68 the figure's morph coefficients (0050D7C0)
    float morphT;                           // +0x6C
    float morphU;                           // +0x70 (also the time factor's t, 0050C230)
    Vector3 position;                       // +0x74 in figure space
    Quaternion rotation;                    // +0x80 in figure space
    lib::AlignedArray<Matrix4> matrices;    // +0x90 (emptied without freeing; freed by the destructor)

    // figure_bone_lifetime.cpp (A-U9; vtable 0073DA0C, 0xA0 bytes, made by the figure factory
    // 005AAB57):
    // 0050AF40. The constructor: engine object fields, empty names and child list (blocks of 10),
    // the vtable, then CreateBone's reset. Returns this.
    FigureBone* Construct();
    // 0050B370 (slot 2, EAX 1). Reset: names "", no parent, the child list emptied (its blocks
    // freed), mesh, pose, animator and +0x54 / +0x58 forgotten, the default position and rotation
    // (007AF110 / 007AF100, the animation tracks' defaults), no matrices (the block kept), morph 0.
    int CreateBone();
    // 0050B510 (slot 3). Releases the mesh (forgotten first), the animator, +0x54 and +0x58 (the
    // pose is only forgotten), then CreateBone's reset.
    void DestroyBone();
    // 0050B1D0. The destructor body: the vtable, DestroyBone, the matrices' block freed, the list's
    // and the names' destructors, the engine object's.
    void DestructBody();
    // 0050B1B0 (slot 0, RET 4). DestructBody, then operator delete with flags bit 0. Returns this.
    FigureBone* DeleteBone(unsigned flags);

    // 0050C230 (RET 4; figure_bones.cpp, A-U9). Places this bone and, recursively, its children: a
    // child turns its offset by the parent's rotation ((w^2 - u.u) o + 2w (u x o) + 2 (u.o) u, the
    // scaled terms rounded) onto the parent's position and takes rotation = pose * parent (Hamilton); the root
    // takes the pose's position (times the animation's time factor at morphU while one plays)
    // minus `origin`, and its rotation; a root without a pose keeps both. A bone with a mesh gives
    // it the position and rotation and the pose's morphed vertices as offsets (005A0B10).
    void Place(const Vector3* origin);
};
static_assert(offsetof(FigureBone, parent) == 0x28 && offsetof(FigureBone, children) == 0x2C);
static_assert(offsetof(FigureBone, mesh) == 0x48 && offsetof(FigureBone, offset) == 0x5C);
static_assert(offsetof(FigureBone, position) == 0x74 && offsetof(FigureBone, rotation) == 0x80);
static_assert(offsetof(FigureBone, counted54) == 0x54 && offsetof(FigureBone, matrices) == 0x90);
static_assert(sizeof(FigureBone) == 0xA0);

// A part of a figure model (the model 005AA5F0 finds: +0 mesh, +4 bones, ... ; the animations
// part comes from 005A9EF0). Only the slot the animation state calls is named.
class __declspec(novtable) CFigureModelPart {
public:
    virtual void Unknown0();
    virtual void Unknown1();
    virtual void Unknown2();
    virtual void Unknown3();
    virtual void Unknown4();
    // 5: the data of entry `name` (its size to *size when not null), or null.
    virtual void* ReadEntry(const char* name, int* size);
};

// The figure's animation state (class id 0x300D, vtable 0073DA4C, 0x78 bytes; CObject3D + 0x98): the
// skeleton's bones, the animation playing and the queue behind it, the frame reached.
class CFigureAnimationState : public CEngineObject {
public:
    // map_object_animation_state.cpp (A-U2).
    // 0050CA50. Plays `animation` from its start: each bone keeps its animator as the one placing
    // it (making it first), the figure manager (the world's slot 15) is asked to set the animation
    // on this state by the owner's model name (005ACCF0); when it does not, each bone's animator
    // takes it (AnimateBone). Then SetTime(0) and `current` = `animation`.
    void Play(FigureAnimation* animation);
    // 0050CBA0. Queues `animation` after the one playing (the queue's AddTail). Returns the node.
    void* Queue(FigureAnimation* animation);
    // 0050D600. The frame reached becomes `frame` (`changed` set when it differs): with nothing
    // playing the next queued animation starts at 0; while the frame lies beyond the animation's
    // frame count the count is taken off, the callback (+0x64) runs, and the next queued animation
    // plays, or else the animation holds its last frame unless its key's top bits are 10 (loops).
    // When the animation changed, the figure manager or the bones take it as in Play.
    void SetTime(float frame);
    // 0050D5E0. SetTime(time + `frames`).
    void Advance(float frames);

    // Each bone's animator plays `animation` (0050CA50 / 0050D600 inline it): the bone's manager
    // is told (005ACA60) and the animator's time cleared.
    static void AnimateBone(FigureBone* bone, const FigureAnimation* animation);

    // map_object_animation_bones.cpp (A-U10).
    // 0050C5F0. The constructor: engine object fields, empty lists (blocks of 10), the vtable, then
    // Reset. Returns this.
    CFigureAnimationState* Construct();
    // 0050C730. The destructor body: every bone released, the lists emptied, Reset's fields, the
    // lists' destructors, the engine object's.
    void DestructBody();
    // 0050C710 (slot 0, RET 4). DestructBody, then operator delete with flags bit 0. Returns this.
    CFigureAnimationState* DeleteState(unsigned flags);
    // 0050C860 (slot 2). Reset with `changed` set first. Returns 1.
    int CreateState();
    // 0050C8E0 (slot 3). ReleaseBones, then Reset (the root only forgotten).
    void DestroyState();
    // 0050C990. Every bone released, the list emptied.
    void ReleaseBones();
    // 0050C9E0. No owner, root, callback, bones (not released), animation, queue; frame and morph
    // 0, `changed` set.
    void Reset();
    // 0050CC20 (RET 4). The first bone named `boneName` (_mbscmp), or null.
    FigureBone* FindBone(const char* boneName);
    // 0050CC60 (RET 0xC). A new bone (the figure manager's class 0x300C) named `child` with parent
    // `parentName` joins through LinkBone. Returns 1.
    int LinkPiece(void* model, const char* child, const char* parentName);
    // 0050CD70 (RET 8). `bone` becomes a child of the first bone named as its parent (name2)
    // unless that one has a child of its name; it joins the list (referenced), takes the model
    // name (name3) and morph, and its manager loads its key frames from `model` (005ACB80).
    // `changed` set. Returns 1.
    int LinkBone(void* model, FigureBone* bone);
    // 0050D0E0 (RET 4). The bone named as `part` (its slot 7) carries it as its mesh (referenced,
    // the old one released): the part takes the bone's position and rotation. 0 when no bone has
    // the name, else 1. `changed` set.
    int AttachMesh(CEngineObject* part);
    // 0050D2E0. The root becomes the first bone without a parent name. 1, or 0 when none.
    int FindRoot();
    // 0050D320 (RET 4). The figure manager links the skeleton of `model` into this state
    // (005AC880). `changed` set. Returns its result.
    int SetModel(const char* model);
    // 0050D350 (RET 4). The first bone carrying `part` lets it go (released). 1, or 0 when none.
    int DetachMesh(CEngineObject* part);
    // 0050D3A0. Every bone lets its mesh go (released). `changed` set. Returns 1.
    int DetachMeshes();
    // 0050D3E0. When `changed`: cleared, and the root (if any) places the skeleton (0050C230) at
    // the owner's time factor (00518D30 at its scale z) times its default scale.
    void PlaceIfChanged();
    // 0050D450. When `changed`: each bone's placing animator is evaluated at the frame reached
    // (0050ACD0), then PlaceIfChanged.
    void UpdatePose();
    // 0050D5A0. The queue emptied, Play(null), `changed` set.
    void Stop();
    // 0050D7C0 (RET 0xC). The morph becomes (s, t, u) on every bone, whose manager reloads its key
    // frames from the owner's model (005ACB80), and on the state (`changed` cleared); then the
    // root places the skeleton as PlaceIfChanged does.
    void Rescale(float s, float t, float u);
    // 0050D920 (RET 4). Every bone's name appended to `out` (SetAtGrow at its size).
    void CollectBoneNames(mfc::CStringArray* out);
    // 0050D950 (the figure manager's 005ACCF0 without an animation). Every bone's placing track
    // forgets its animation and key frames (DestroyTrack's reset, the bone kept). `changed` set.
    // Returns 1.
    int ClearBoneTracks();
    // 0050DA10 (RET 8; 005ACCF0 with an animation). Every bone's placing track loads the entry of
    // `part` named as the bone (slot 5; 0050AAF0), or without one is cleared as above, and takes
    // `animation`. `changed` set. Returns 1.
    int LoadBoneTracks(CFigureModelPart* part, const FigureAnimation* animation);

    CObject3D* owner;               // +0x1C
    FigureBone* root;               // +0x20 the skeleton's root bone (FindRoot)
    // +0x24 the skeleton's bones (MFC CPtrList, vtable 0073DA40; LinkBone adds).
    mfc::CTypedPtrList<FigureBone> bones;
    FigureAnimation* current;       // +0x40 the animation playing, or null
    // +0x44 the animations queued after it (MFC CPtrList, vtable 0073DA80; the client update
    // keeps fewer than 3).
    mfc::CTypedPtrList<FigureAnimation> queue;
    float time;                     // +0x60 the frame reached
    // +0x64 called (with the owner in ECX) each time an animation's end is passed, or null.
    void(__fastcall* onEnd)(CObject3D* owner);
    float morphS;                   // +0x68 the figure's morph coefficients (Rescale; bones copy them)
    float morphT;                   // +0x6C
    float morphU;                   // +0x70
    std::uint8_t changed;           // +0x74 set when the frame, the animation or the bones changed
};
static_assert(offsetof(CFigureAnimationState, bones) == 0x24);
static_assert(offsetof(CFigureAnimationState, current) == 0x40);
static_assert(offsetof(CFigureAnimationState, queue) == 0x44);
static_assert(offsetof(CFigureAnimationState, time) == 0x60);
static_assert(offsetof(CFigureAnimationState, onEnd) == 0x64);
static_assert(offsetof(CFigureAnimationState, morphS) == 0x68);
static_assert(offsetof(CFigureAnimationState, changed) == 0x74);
static_assert(sizeof(CFigureAnimationState) == 0x78);

// A counted reference: the pointer slot is the `this` of 00519580 (the 3D object's figure, a
// CLinkMesh) and of its byte-identical copies 0051E570 (the client half's wounds material) and
// 0051E4F0 (that material's helper preset), both called by 0051C3D0. T's count is at +0x04 and its
// slot 0 deletes it; object3d_lifetime.cpp defines Assign for those three types.
template <typename T>
struct CountedRef {
    // object = `value`: the new one gains a reference, the old one loses one and is deleted (its
    // slot 0 with 1) when that was the last.
    void Assign(T* value);

    T* object;
};

// A counted pointer slot viewed as the reference the game's copies of Assign take as `this`
// (CObject3D::FigureRef for the figure; the wounds material and its preset in 0051C3D0 / 0051AF70).
template <typename T>
inline CountedRef<T>& AsCountedRef(T*& slot) { return *reinterpret_cast<CountedRef<T>*>(&slot); }

class CObject3D : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073DDD4);

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
    int AddPart(CFigureMesh* part);

    // map_object_parts.cpp (A-U).
    // 00516A80. The first part (of the parts, then of the pending parts) whose name starts with
    // `prefix` (strstr at the name's start; case-sensitive), or null.
    CFigureMesh* FindPartByPrefix(const char* prefix);
    // 005169A0. The first part (parts, then pending parts) named exactly `partName`, or null.
    CFigureMesh* FindPart(const char* partName);
    // 00516920 (RET 4; object3d_specific_slots.cpp, A-U11). The first part (parts, then pending
    // parts) whose engine object type_ is `type`, or null.
    CFigureMesh* FindPartOfType(int type);
    // 00516B50 (object3d_figure_setup.cpp, A-U4; RET 8). Every part (parts, then pending parts)
    // whose name starts with `prefix` (strstr at the name's start) appended to `out`.
    void CollectPartsByPrefix(const char* prefix, AlignedPtrArray* out);
    // 005167E0. Takes `part` out of the parts (it forgets its owner, 0059F3D0) or else out of the
    // pending parts, tells the animation state (+0x98, 0050D350) and releases it. The side half
    // (+0x94, slot 15) hears of it first, found or not. Returns 1 when it was there, else 0.
    int RemovePart(CFigureMesh* part);
    // 005168F0. FindPart(`partName`), removed by RemovePart. Returns the part (released; the pointer
    // may be dangling) or null.
    CFigureMesh* RemovePartNamed(const char* partName);
    // 005188B0. `part`'s position (+0x1C) turned by the orientation (+0x34, w first), scaled by
    // the side half's GetScale and moved to the object's position. Returns `out`.
    float* PartWorldPosition(float* out, const CFigureMesh* part);

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
    CFigureMesh* LoadPart(const char* partName, void* model, std::uint32_t arg);
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
    // 00564AF0 (world_client.cpp; the client's AddObject walks a platform object's boxes with it).
    // The box (CFigureMesh::bounds_) of part `index` of the parts followed by the pending parts.
    FootprintPart* PartBox(int index);

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

    // object3d_small_methods.cpp (A-U12).
    // 00515D50 (slot 0, RET 4). DestructBody, then operator delete with flags bit 0. Returns this.
    CObject3D* DeleteObject3D(unsigned flags);
    // 005161A0 (RET 4). `half` (of any type, or null) replaces the attached half: the old one is
    // detached (+0x1C cleared) and released, the new one points back here and gains a reference.
    void SetSpecific(CObject3DSpecific* half);
    // 00517910 / 00517950 (RET 4). Every part's flags_ gains / loses the bits of `mask`.
    void SetPartFlags(std::uint32_t mask);
    void ClearPartFlags(std::uint32_t mask);
    // 00519500 / 00519530 (RET 4). Every part's flatColour / flatSpecular becomes `value`.
    void SetPartsFlatColour(std::uint32_t value);
    void SetPartsFlatSpecular(std::uint32_t value);
    // 005187D0 (RET 4). `state` (or null) replaces the animation state: the old one loses its
    // owner and is released, the new one gains a reference and this owner.
    void SetAnimationState(CFigureAnimationState* state);
    // 005187B0 (RET 8). The animation state (if any) plays `animation`; `unused` is not read.
    void PlayAnimation(FigureAnimation* animation, int unused);
    // 00518840 (RET 8). The same with the arguments the other way round. Returns 1.
    int PlayAnimationSecond(int unused, FigureAnimation* animation);
    // 00518820 (RET 4). The animation state's bone named as `part` carries it (AttachMesh); 0
    // without a state.
    int AttachPartMesh(CEngineObject* part);
    // 00518860. The animation state (if any) goes to frame 0 and places the skeleton.
    void RestartPose();

    // dirtyRect as the RECT it is (windows.h).
    tagRECT* DirtyRect() { return reinterpret_cast<tagRECT*>(dirtyRect); }

    // The side-specific half (+0x94).
    CObject3DSpecific* Specific() const { return static_cast<CObject3DSpecific*>(attached); }
    // The figure (+0xF0) as the counted reference 00519580 assigns.
    CountedRef<CLinkMesh>& FigureRef() { return *reinterpret_cast<CountedRef<CLinkMesh>*>(&figure); }
    // The object as the CTransformObject it starts with (core_transform.h).
    CTransformObject* Transform() { return reinterpret_cast<CTransformObject*>(this); }
    const CTransformObject* Transform() const { return reinterpret_cast<const CTransformObject*>(this); }

    Vector3 position;               // +0x01C
    Vector3 viewPosition;           // +0x028 the position in view space (CWorldClient::CollectVisible)
    Quaternion orientation;         // +0x034
    Quaternion worldOrientation;    // +0x044 the parent's rotation times the orientation (slot 14)
    Matrix4 worldMatrix;            // +0x054 object -> world: the rotation, the position as column 3
    CEngineObject* attached;        // +0x094 the object slot 19 accepts (type 0x46)
    CFigureAnimationState* animation;   // +0x098 the figure's animation state
    std::uint32_t spare09C;         // +0x09C cleared by Create3D / Destroy3D; nothing reads it
    AnimationDatabase* animations;  // +0x0A0 loaded on first use (00518CA0)
    mfc::CString name;              // +0x0A4 the figure's name
    // +0x0A8 the particle sources attached to the object (MFC CPtrList, vtable 0073DE4C, block
    // size 10).
    mfc::CTypedPtrList<CEngineObject> effectSources;
    Vector3 scale;                  // +0x0C4
    AlignedPtrArray parts;          // +0x0D0 CFigureMesh*
    AlignedPtrArray pendingParts;   // +0x0E0 CFigureMesh*, built by LoadFigures
    // +0x0F0 the figure (counted; slot 0 deletes), made by the figure cache: the parts' piece
    // headers (LoadPart, SetFigureScale) and the meshes the parts draw and shadow through.
    CLinkMesh* figure;
    Vector3 defaultScale;           // +0x0F4 from 007AF1D0 (constructor, Create3D)
    int dirtyRect[4];               // +0x100 RECT: the screen area the last DrawCollectDirty covered
    mfc::CStringArray partNames;    // +0x110 the figure's part names (00517990), in the order of the
                                    // part boxes (CUnit::partBoxes, +0x160)
    std::uint32_t boundsFlags;      // +0x124 1 and 2: the bounds are valid
    Vector3 center;                 // +0x128
    float radius;                   // +0x134
    Vector3 below;                  // +0x138 box minimum - centre
    Vector3 above;                  // +0x144 box maximum - centre
    std::uint8_t unused_150[0x158 - 0x150];    // cleared with the bounds
    // +0x158 cleared with the bounds; a part's piece record sets it (LoadPart). The AI adds it to a
    // unit's eye height (005CA410).
    float eyeHeight;
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
static_assert(offsetof(CObject3D, partNames) == 0x110);
static_assert(offsetof(CObject3D, scale) == 0xC4);
static_assert(offsetof(CObject3D, parts) == 0xD0);
static_assert(offsetof(CObject3D, pendingParts) == 0xE0);
static_assert(offsetof(CObject3D, figure) == 0xF0);
static_assert(offsetof(CObject3D, boundsFlags) == 0x124);
static_assert(offsetof(CObject3D, center) == 0x128);
static_assert(offsetof(CObject3D, radius) == 0x134);
static_assert(offsetof(CObject3D, below) == 0x138);
static_assert(offsetof(CObject3D, above) == 0x144);
static_assert(offsetof(CObject3D, eyeHeight) == 0x158);
static_assert(sizeof(CObject3D) == 0x170);
