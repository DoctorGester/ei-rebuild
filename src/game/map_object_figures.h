#pragma once

// The 3D object base under CMapObject: the object's figure (a model resource shared through the figure
// manager), its parts (scene nodes, one per figure piece) and their bounding box, and the scale the figure
// was built for. See docs/notes/map_objects.md ("Figures").

#include "core_refcount.h"   // CountedRef
#include "core_transform.h"
#include "engine_object.h"
#include "figure_mesh.h"            // CFigureMesh (an object's figure parts)
#include "map_object_animation.h"   // AnimationTrack (a bone's animator)
#include "maths.h"

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/nan_audit.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CObject3D;

// The figures' default scale (a static initialiser: zeros; CObject3D's constructor and Create3D).
inline constexpr Vector3 kDefaultFigureScale = /*007AF1D0*/ {};
class CLinkMesh;          // link_figure.h: an object's figure
class CTransformObject;
struct tagRECT;

// A growable pointer array whose storage is 32-byte aligned (the block from operator new is kept at data[-1]).
// Registered on the template (SetAtGrow and SetSize in map_object_figures.cpp, Add in
// object3d_figure_setup.cpp).
using AlignedPtrArray = lib::AlignedArray<void*>;
// An object's figure parts (CObject3D::parts / pendingParts).
using FigurePartArray = lib::AlignedArray<CFigureMesh*>;

class CFigureMesh;      // figure_mesh.h: a part of an object's figure
struct FigureModel;     // model.h: a cached model
struct FootprintPart;   // ai_map.h: a figure part's box

// One animation of a figure's animation database.
struct FigureAnimation {
    char name[0x10];                // (the figure manager takes the entry as an animation's name)
    std::uint8_t index;             // its index in the database (a unit keeps it)
    std::uint8_t unused_11[3];
    std::uint32_t key;              // animation key bits (docs/notes/combat.md): kind 0x3C0000, number 0x3FC00000
    std::uint32_t flags;            // bit 0: a bow's arrow shows from frame arrowFrom to arrowTo
    std::uint32_t chance;           // percent (a random pick drops entries below a random percent)
    std::uint32_t frameCount;       // the animation's length in frames
    // +0x24 the distance one play of the animation moves the unit (0: the speed comes from the
    // unit instead; ClientPlaceOnPath, unit_client_path.cpp).
    float stride;
    std::uint32_t arrowFrom;
    std::uint32_t arrowTo;
    std::uint32_t stepFrames[4];    // the footsteps' frames + 1 (0 ends the list)
    std::uint32_t moment;           // the action's moment (frame)
    int soundFrame;                 // the frame of the action's sound (0: none)
    std::uint32_t sounds[4];        // sound numbers to pick from (0 ends the list)
};
static_assert(offsetof(FigureAnimation, arrowFrom) == 0x28);
static_assert(offsetof(FigureAnimation, soundFrame) == 0x44);
static_assert(sizeof(FigureAnimation) == 0x58);

// A figure's animation database: the data of "<figure name>.adb" as the object's manager
// returns it (CBaseManager::GetFile), kept at CObject3D + 0xA0.
struct AnimationDatabase {
    std::uint8_t unused_00[4];
    std::uint32_t count;            // animations
    std::uint8_t unused_08[0x20 - 0x08];
    float start;                    // \ the animation time factor maps t to
    float length;                   // | (start + (end - start) * t) / length
    float end;                      // /
    FigureAnimation animations[1];  // `count` of them

    // The database `animation` is an entry of (entries know their index).
    static const AnimationDatabase* Of(const FigureAnimation* animation) {
        const FigureAnimation* first = animation - animation->index;
        return reinterpret_cast<const AnimationDatabase*>(reinterpret_cast<const char*>(first) -
                                                          offsetof(AnimationDatabase, animations));
    }
    // (start + (end - start) * t) / length, unrounded; 1 for a length of 0.
    double TimeFactor(float t) const {
        EI_NAN_AUDIT(length);
        if (length == 0.0f) return 1.0;
        return ((static_cast<double>(end) - start) * t + start) / length;
    }
};
static_assert(offsetof(AnimationDatabase, start) == 0x20);
static_assert(offsetof(AnimationDatabase, animations) == 0x2C);

class CBaseManager;

// The side-specific half of a 3D object (CObject3D's attached half): CObject3DClientSpecific (class id 0x37;
// map_object_shadow.h) on the client, CObject3DServer (0x38, stubs; below) on the server. The 3D object
// reaches its half through CObject3D::Specific().
class CObject3DSpecific;
class CObject3DClientSpecific;
// The base half's LoadFigures-done virtual: nothing.
void __fastcall SpecificBaseNothing(CObject3DSpecific* self);
class CObject3DSpecific : public CEngineObject {
public:
    CObject3DSpecific() {
        ClearFields();
        object = nullptr;
    }
    ~CObject3DSpecific() override;
    void IgnoreArgument(int);

    virtual int OnPartAdded(CFigureMesh* part) = 0;   // server: 0
    // (name): the client takes the part named (FindPart) out of the drawn part sets and releases it; returns it
    // or null. The server returns null.
    virtual CFigureMesh* RemoveFromPartSets(const char* name) = 0;
    virtual int OnPartRemoved(CFigureMesh* part) = 0;      // server: 0
    virtual CFigureMesh* FindPart(const char* name) = 0;   // a part by name (client); server: null
    virtual CFigureMesh* FindPartOfType(int type) = 0;     // a part by type (client); server: null
    // The figure's extra scale: client +0xF0..+0xF8, base 1, 1, 1.
    virtual void GetScale(float* x, float* y, float* z);
    // Draw / DrawShadow: the client's (CObject3DClientSpecific::Draw, DrawShadow); the base returns 0.
    virtual int DrawParts(void* context, int* dirty);
    virtual int DrawPartsShadow(void* context);
    // After LoadFigures: the client gives the parts the world's materials, then the skin texture. The base does
    // nothing.
    virtual void ApplyMaterials() {}
    // Screen picking: the client tests the point in the object's dirtyRect (flags_ 0x20) / a rect meeting a drawn
    // part's screen rect / the point in one. The server's first: whether the screen point (x, y) is in the
    // object's dirtyRect (PickAt).
    virtual int BoundsContainPoint(const long* point);
    // Whether the screen rectangle (a RECT) meets a drawn part's rectangle.
    virtual int PartsIntersectRect(const long* rect);
    // Whether the point is in the rectangle (+0x94 + 0x20) of a part with flag 0x40 (PickAt2).
    virtual int PartsContainPoint(const long* point);
    virtual void Unused25(int) {}                   // empty in the base and the client
    virtual void SetLit(int) {}                     // the object's light is on / off (client); base: nothing

    CObject3D* object;              // the object it belongs to
};

// The server's half: no parts, nothing drawn.
class CObject3DServer : public CObject3DSpecific {
public:
    CObject3DServer();             // The factory allocates it zeroed.
    ~CObject3DServer() override;   // the base half's
    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;
    int GetClassId() override;
    int OnPartAdded(CFigureMesh* part) override;
    CFigureMesh* RemoveFromPartSets(const char* name) override;
    int OnPartRemoved(CFigureMesh* part) override;
    CFigureMesh* FindPart(const char* name) override;
    CFigureMesh* FindPartOfType(int type) override;
};

class CObject3D;

// A bone of a figure's skeleton (made from the skeleton links). The animation state's bone list holds them,
// one of its members the root. Only the fields the pose code uses.
struct FigureBone : CEngineObject {
    mfc::CString name;                      // the piece's name (children are linked by it)
    mfc::CString name2;                     // its parent's name (empty for the root)
    mfc::CString name3;                     // the figure's model name (LinkBone)
    FigureBone* parent;                     // null for the root
    mfc::CTypedPtrList<FigureBone> children;
    CFigureMesh* mesh;                      // the part the bone places, or null
    AnimationTrack* pose;                   // the animator whose values place the bone
    AnimationTrack* animator;               // counted, made on first use; Play copies it to +0x4C
    CEngineObject* counted54;               // counted (released by DestroyBone)
    CEngineObject* counted58;               // counted
    Vector3 offset;                         // the place relative to the parent
    float morphS;                           // the figure's morph coefficients
    float morphT;
    float morphU;                           // (also the time factor's t)
    Vector3 position;                       // in figure space
    Quaternion rotation;                    // in figure space
    lib::AlignedArray<Matrix4> matrices;    // (emptied without freeing; freed by the destructor)

    FigureBone();
    ~FigureBone() override;

    int Create() override;
    void Destroy() override;
    const char* GetName() override;
    const char* GetClassName() override;
    int GetClassId() override;

    int CreateBone();
    void DestroyBone();

    // Places this bone and, recursively, its children: a child turns its offset by the parent's
    // rotation ((w^2 - u.u) o + 2w (u x o) + 2 (u.o) u, the scaled terms rounded) onto the parent's position and
    // takes rotation = pose * parent (Hamilton); the root takes the pose's position (times the animation's time
    // factor at morphU while one plays) minus `origin`, and its rotation; a root without a pose keeps both. A bone
    // with a mesh gives it the position and rotation and the pose's morphed vertices as offsets.
    void Place(const Vector3* origin);
};
static_assert(offsetof(FigureBone, parent) == 0x28 && offsetof(FigureBone, children) == 0x2C);
static_assert(offsetof(FigureBone, mesh) == 0x48 && offsetof(FigureBone, offset) == 0x5C);
static_assert(offsetof(FigureBone, position) == 0x74 && offsetof(FigureBone, rotation) == 0x80);
static_assert(offsetof(FigureBone, counted54) == 0x54 && offsetof(FigureBone, matrices) == 0x90);
static_assert(sizeof(FigureBone) == 0xA0);

// A part of a figure model (the model the figure manager finds: mesh, bones, ...; the animations part comes
// from FigureModel::FindAnimation). Only the virtual the animation state calls is named.
class __declspec(novtable) CFigureModelPart {
public:
    virtual void Unknown0();
    virtual void Unknown1();
    virtual void Unknown2();
    virtual void Unknown3();
    virtual void Unknown4();
    // The data of entry `name` (its size to *size when not null), or null.
    virtual void* ReadEntry(const char* name, int* size);
};

// The figure's animation state (CObject3D's animation member): the skeleton's bones, the animation playing
// and the queue behind it, the frame reached.
class CFigureAnimationState : public CEngineObject {
public:
    // Plays `animation` from its start: each bone keeps its animator as the one
    // placing it (making it first), the figure manager (the world's) is asked to set the animation on this state
    // by the owner's model name; when it does not, each bone's animator takes it (AnimateBone). Then SetTime(0)
    // and `current` = `animation`.
    void Play(FigureAnimation* animation);
    // Queues `animation` after the one playing (the queue's AddTail). Returns the node.
    void* Queue(FigureAnimation* animation);
    // The frame reached becomes `frame` (`changed` set when it differs): with nothing
    // playing the next queued animation starts at 0; while the frame lies beyond the animation's
    // frame count the count is taken off, the callback runs, and the next queued animation
    // plays, or else the animation holds its last frame unless its key's top bits are 10 (loops).
    // When the animation changed, the figure manager or the bones take it as in Play.
    void SetTime(float frame);
    // SetTime(time + `frames`).
    void Advance(float frames);

    // Each bone's animator plays `animation`: the bone's manager is told and the animator's time cleared.
    static void AnimateBone(FigureBone* bone, const FigureAnimation* animation);

    CFigureAnimationState();
    ~CFigureAnimationState() override;

    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;
    int GetClassId() override;

    int CreateState();
    void DestroyState();
    void ReleaseBones();
    void Reset();
    // The first bone named `boneName` (_mbscmp), or null.
    FigureBone* FindBone(const char* boneName);
    // A new bone (the figure manager's class 0x300C) named `child` with parent
    // `parentName` joins through LinkBone. Returns 1.
    int LinkPiece(FigureModel* model, const char* child, const char* parentName);
    // `bone` becomes a child of the first bone named as its parent (name2)
    // unless that one has a child of its name; it joins the list (referenced), takes the model
    // name (name3) and morph, and its manager loads its key frames from `model`.
    // `changed` set. Returns 1.
    int LinkBone(FigureModel* model, FigureBone* bone);
    // The bone named as `part` carries it as its mesh (referenced, the old one released): the part takes the
    // bone's position and rotation. 0 when no bone has the name, else 1. `changed` set.
    int AttachMesh(CEngineObject* part);
    // The root becomes the first bone without a parent name. 1, or 0 when none.
    int FindRoot();
    // The figure manager links the skeleton of `model` into this state
    //. `changed` set. Returns its result.
    int SetModel(const char* model);
    // The first bone carrying `part` lets it go (released). 1, or 0 when none.
    int DetachMesh(CEngineObject* part);
    // Every bone lets its mesh go (released). `changed` set. Returns 1.
    int DetachMeshes();
    // When `changed`: cleared, and the root (if any) places the skeleton at the owner's time factor (at its scale
    // z) times its default scale.
    void PlaceIfChanged();
    // When `changed`: each bone's placing animator is evaluated at the frame reached
    // then PlaceIfChanged.
    void UpdatePose();
    // The queue emptied, Play(null), `changed` set.
    void Stop();
    // The morph becomes (s, t, u) on every bone, whose manager reloads its key
    // frames from the owner's model, and on the state (`changed` cleared); then the
    // root places the skeleton as PlaceIfChanged does.
    void Rescale(float s, float t, float u);
    // Every bone's name appended to `out` (SetAtGrow at its size).
    void CollectBoneNames(mfc::CStringArray* out);
    // The figure manager's SetAnimation without an animation. Every bone's placing track forgets its animation and
    // key frames (DestroyTrack's reset, the bone kept). `changed` set. Returns 1.
    int ClearBoneTracks();
    // The figure manager's SetAnimation with an animation. Every bone's placing track loads the entry of `part`
    // named as the bone (LoadKeyFrames), or without one is cleared as above, and takes `animation`. `changed` set.
    // Returns 1.
    int LoadBoneTracks(CFigureModelPart* part, const FigureAnimation* animation);

    CObject3D* owner;
    FigureBone* root;               // the skeleton's root bone (FindRoot)
    // +0x24 the skeleton's bones (MFC CPtrList, BoneListClass; LinkBone adds).
    mfc::CTypedPtrList<FigureBone> bones;
    FigureAnimation* current;       // the animation playing, or null
    // +0x44 the animations queued after it (MFC CPtrList, AnimationStateListClass; the client update
    // keeps fewer than 3).
    mfc::CTypedPtrList<FigureAnimation> queue;
    float time;                     // the frame reached
    // +0x64 called (with the owner in ECX) each time an animation's end is passed, or null.
    void(__fastcall* onEnd)(CObject3D* owner);
    float morphS;                   // the figure's morph coefficients (Rescale; bones copy them)
    float morphT;
    float morphU;
    std::uint8_t changed;           // set when the frame, the animation or the bones changed
};
static_assert(offsetof(CFigureAnimationState, bones) == 0x24);
static_assert(offsetof(CFigureAnimationState, current) == 0x40);
static_assert(offsetof(CFigureAnimationState, queue) == 0x44);
static_assert(offsetof(CFigureAnimationState, time) == 0x60);
static_assert(offsetof(CFigureAnimationState, onEnd) == 0x64);
static_assert(offsetof(CFigureAnimationState, morphS) == 0x68);
static_assert(offsetof(CFigureAnimationState, changed) == 0x74);
static_assert(sizeof(CFigureAnimationState) == 0x78);

// A counted pointer slot of a shared type viewed as the reference the game's copies of Assign take as `this`
// (CDeviceMaterial::preset, the wounds material's preset).
template <typename T>
inline CountedRef<T>& AsCountedRef(T*& slot) { return *reinterpret_cast<CountedRef<T>*>(&slot); }

// The virtuals of the 3D object (abstract: class name and id are pure; the ones below are defined,
// object3d_slots.cpp). The CTransformObject ones come first: the object starts with its fields (local
// position, world position, which CWorldClient's CollectVisible sets to the position in view space, local
// rotation, world rotation).
class CObject3D : public CTransformObject {
public:
    ~CObject3D() override;
    int Create() override { return Create3D(); }
    void Destroy() override { Destroy3D(); }
    void ClearState() override { RemoveAllEffectSources(); }
    // Two virtuals are CTransformObject's. WorldRotation_ = parent * localRotation_.
    void CombineRotation(const Quaternion& parent) override;
    // The two file virtuals hand the file to CTransformObject's.
    void SaveTransform(mfc::CFileVirtuals* file) override;
    void LoadTransform(mfc::CFileVirtuals* file) override;
    // Whether the object may play the animation entry (FigureAnimation); Any. CUnit overrides it (AcceptsTarget:
    // its flags for heroes).
    virtual bool AcceptsAnimation(const void* animation);
    // AttachSpecific (no result: 0). CMapObject and units override it.
    virtual std::uintptr_t AttachObject(CEngineObject* object);
    // Takes `other`'s parts (the array copied, no references taken) and its transform
    // (CTransformObject::CopyTransform).
    virtual void CopyFigure(const CObject3D* other);
    // Whether the object is invisible: false here; map objects override it.
    virtual bool InvisibleSlot();
    // Draw the figure / draw it and collect its screen rectangle / draw its shadow with the scene context (the
    // side half's draw virtuals, the second first emptying dirtyRect to have it filled; 1 without a half, else
    // the half's result; object3d_slots.cpp).
    virtual int DrawFigure(void* context);
    virtual int DrawCollectDirty(void* context);
    virtual int DrawFigureShadow(void* context);
    // Empty; units override it.
    virtual void AdvanceTime(float time);
    // Loads the figure named by the model name and builds the pending
    // parts at the current scale; each part not yet in the parts joins it (referenced), learns its owner and is
    // announced to the animation and the attached object (OnPartAdded). Non-units mark every part and themselves
    // (flags_ 8 / 0x200). Then the half's LoadFigures-done virtual and flags_ 0x80. Returns 1 (also when already
    // loaded).
    virtual std::uintptr_t LoadFigures();

    // The bounding box of the parts (only those within 1e9 squared of the object) and of the pending parts (whose
    // positions are reset to 0) into the bounds: centre, half extents below and above it, radius; sets two flag
    // bits. Nothing changes without parts. Returns what the original leaves in EAX (the centre's z bits, 0
    // without parts).
    std::uint32_t ComputeBounds();

    // Sets the scale and, when it changed, rebuilds the figure at that scale through the figure manager, copies
    // each part's piece record from it, rescales the animation and recomputes the bounds. Returns what the
    // original leaves in EAX on the rebuild path (0 when the scale did not change).
    std::uintptr_t SetFigureScale(const float* newScale);

    // Adds `part` to the parts unless it is there: referenced, told its owner, announced to the animation and to
    // the attached object (whose OnPartAdded result is returned). Returns 1 otherwise.
    int AddPart(CFigureMesh* part);

    // The first part (of the parts, then of the pending parts) whose name starts with
    // `prefix` (strstr at the name's start; case-sensitive), or null.
    CFigureMesh* FindPartByPrefix(const char* prefix);
    // The first part (parts, then pending parts) named exactly `partName`, or null.
    CFigureMesh* FindPart(const char* partName);
    // The first part (parts, then pending parts) whose engine object type_ is
    // `type`, or null.
    CFigureMesh* FindPartOfType(int type);
    // Every part (parts, then pending parts) whose name starts with `prefix` (strstr at
    // the name's start) appended to `out`.
    void CollectPartsByPrefix(const char* prefix, FigurePartArray* out);
    // Takes `part` out of the parts (it forgets its owner) or else out of the pending parts, tells the animation
    // state and releases it. The side half hears of it first, found or not. Returns 1 when it was there, else 0.
    int RemovePart(CFigureMesh* part);
    // FindPart(`partName`), removed by RemovePart. Returns the part (released; the pointer
    // may be dangling) or null.
    CFigureMesh* RemovePartNamed(const char* partName);
    // `part`'s position turned by the orientation (+0x34, w first), scaled by
    // the side half's GetScale and moved to the object's position. Returns `out`.
    float* PartWorldPosition(float* out, const CFigureMesh* part);

    // The animation database, loaded on first use through the owner's GetFile
    // ("<name>.adb"; nothing without an owner). Null when there is none.
    AnimationDatabase* Animations();
    // Animation `key & 0xFF` of the database, or null (no database, or beyond its count).
    FigureAnimation* AnimationAt(std::uint32_t key);
    // The first animation of kind 0x40000 whose number (key bits 22-29) is `number`, or null (the hero preview
    // asks for 0x21; so do the interface's models).
    FigureAnimation* FindSkillAnimation(int number);
    // A random animation for `key`: the database's entries whose key has the same bits
    // above 0x7FFF and all of key's low 15 bits (none when key has none) and that the object's may-play virtual
    // accepts; one is returned as it is, several are narrowed to those whose chance is at least a rand() percent
    // and one of them is picked with rand() (the pick made before narrowing when none is left). Null without a
    // database or a match.
    FigureAnimation* RandomAnimation(std::uint32_t key);
    // As RandomAnimation for the entries whose key's bits above 0x7FFF equal key's, one picked with rand(); no
    // chance narrowing.
    FigureAnimation* RandomAnimationOfKind(std::uint32_t key);
    // For units (class id 0x50) with an animation database whose length is neither 0
    // nor NaN: (start + (end - start) * time) / length, else 1. In ST0, unrounded.
    double AnimationFactor(float time);
    // Sets the figure up for the model `model`: the animation state is rescaled, given
    // the model, plays nothing and applies it; one part box (a CArray of FootprintPart, grown like MFC's SetSize)
    // and one part name per animated part: the figure manager's bounds of the part (with the model and the
    // scale), the part's pivot, flags 4 (| 0x400 "BASE", 0x90 "CROWN", 0x890 "EMPTY" name prefixes); then the
    // state releases its parts. The bounds span every box's pivot + min / max + offset (heroes, type_ 0x32: x and
    // y -0.5..0.5); without an animation state the box stays empty (FLT_MAX). Returns nothing.
    void SetUpAnimatedFigure(const char* model);
    // A part object for the figure part `partName` (the figure manager's LoadPart with the object's model name,
    // `model`, the scale, `arg` and flags_ & 0x1000; none: "Can't load figure" logged, null). With a figure the
    // part must be one of its pieces (else null, the part kept unreleased) and takes the piece record's box (0x38
    // bytes) and two trailing dwords. Name prefixes "BASE" / "CROWN" / "EMPTY" add box flags 0x400 / 0x90 / 0x890.
    // A new part joins the parts (referenced; owner, animation state and side half told as AddPart does); the box
    // pivot is the part's position and flag 4 is set. Returns it.
    CFigureMesh* LoadPart(const char* partName, FigureModel* model, std::uint32_t arg);
    // The object's figure becomes `model` and each name of the
    // CStringArray `partNames` is loaded by LoadPart (with the cached model; failures logged).
    // Without a figure manager: FatalError. Returns 1.
    int LoadParts(const char* model, const void* partList, std::uint32_t arg);
    // The world client calls it when a unit becomes the controlled one. With the figure loaded (flags_ 0x80): each
    // part's world transform is updated from the object's and its world box grows the bounds; the centre is the
    // box centre relative to the position, below / above the box minus the centre, the radius the length of
    // `below`; bounds flags 2 and 1 set.
    void UpdatePartBounds();
    // Builds the figure of the model for the part names `partList` (a CStringArray): the animation state is
    // rescaled and given the model; with no names and an animation state the names are the state's parts and the
    // figure the cache's whole figure, otherwise the cache's figure of those pieces; then LoadParts, an animated
    // unit (class 0x50 / 0x54) plays nothing, the bounds are recomputed and flags_ 0x80 is set unless both `arg`
    // and the figure are 0 (then cleared). Returns 1.
    int MakeFigure(const char* model, void* partList, std::uint32_t arg);
    // The index of the part name equal to `partName` (_mbscmp), or -1.
    int FindPartIndex(const char* partName);
    // The box
    // (CFigureMesh::bounds_) of part `index` of the parts followed by the pending parts.
    FootprintPart* PartBox(int index);

    // Create (another name so it is no override): position 0, the identity orientation, no
    // side half (dropped without a release), flags 0, no animation database, scale 0, the figure released (deleted
    // at its last reference), the default scale (kDefaultFigureScale), the bounds cleared and the part boxes
    // freed. Returns 1.
    int Create3D();
    // Destroy: the figure released, every effect source removed, the animation state lets go of its parts, is
    // detached and released, the parts lose a reference without a release and the array is freed, the pending
    // parts are released and freed, the side half released; a member, the database and flags 0.
    void Destroy3D();
    // Adds `source` (a particle source; it calls this from its Attach) to the effect sources unless it is there:
    // referenced and attached to this object.
    void AddEffectSource(CEngineObject* source);
    // Takes `source` out of the effect sources (its node freed; the last one frees the list's blocks), detaches it
    // and releases it. Nothing when not there.
    void RemoveEffectSource(CEngineObject* source);

    // AttachSpecific: a side half (type 0x46) replaces the attached one, which is detached
    // and released; the new one points back here and gains a reference. Other objects (and null) are ignored.
    void AttachSpecific(CEngineObject* half);
    // The release-effects virtual: every effect source is taken out of the list, detached and released.
    void RemoveAllEffectSources();

    // `half` (of any type, or null) replaces the attached half: the old one is
    // detached (+0x1C cleared) and released, the new one points back here and gains a reference.
    void SetSpecific(CObject3DSpecific* half);
    // Every part's flags_ gains / loses the bits of `mask`.
    void SetPartFlags(std::uint32_t mask);
    void ClearPartFlags(std::uint32_t mask);
    // Every part's flatColour / flatSpecular becomes `value`.
    void SetPartsFlatColour(std::uint32_t value);
    void SetPartsFlatSpecular(std::uint32_t value);
    // `state` (or null) replaces the animation state: the old one loses its
    // owner and is released, the new one gains a reference and this owner.
    void SetAnimationState(CFigureAnimationState* state);
    // The animation state (if any) plays `animation`; `unused` is not read.
    void PlayAnimation(FigureAnimation* animation, int unused);
    // The same with the arguments the other way round. Returns 1.
    int PlayAnimationSecond(int unused, FigureAnimation* animation);
    // The animation state's bone named as `part` carries it (AttachMesh); 0
    // without a state.
    int AttachPartMesh(CEngineObject* part);
    // The animation state (if any) goes to frame 0 and places the skeleton.
    void RestartPose();

    // dirtyRect as the RECT it is (windows.h).
    tagRECT* DirtyRect() { return reinterpret_cast<tagRECT*>(dirtyRect); }

    // The fields the constructor sets past the engine object's and the name, and the destructor's work but for the
    // name.
    void SetUpObject3D();
    void TearDownObject3D();

    // The side-specific half.
    CObject3DSpecific* Specific() const { return attached; }
    // The same half on the client: the CObject3DClientSpecific (defined in map_object_shadow.h).
    CObject3DClientSpecific* ClientSpecific() const;
    // The object as the CTransformObject it starts with (core_transform.h).

    Matrix4 worldMatrix;            // object -> world: the rotation, the position as column 3
    CObject3DSpecific* attached;    // the side half: AttachSpecific accepts (type 0x46)
    CFigureAnimationState* animation;   // the figure's animation state
    std::uint32_t spare09C;         // cleared by Create3D / Destroy3D; nothing reads it
    AnimationDatabase* animations;  // loaded on first use
    mfc::CString name;              // the figure's name
    // +0x0A8 the particle sources attached to the object (MFC CPtrList, Object3DListClass, block
    // size 10).
    mfc::CTypedPtrList<CEngineObject> effectSources;
    Vector3 scale;
    FigurePartArray parts;
    FigurePartArray pendingParts;   // built by LoadFigures
    // the figure (counted; deleted at its last reference), made by the figure cache: the parts' piece headers
    // (LoadPart, SetFigureScale) and the meshes the parts draw and shadow through.
    CountedRef<CLinkMesh> figure;
    Vector3 defaultScale;           // kDefaultFigureScale (constructor, Create3D)
    int dirtyRect[4];               // RECT: the screen area the last DrawCollectDirty covered
    mfc::CStringArray partNames;    // the figure's part names, in the order of the
                                    // part boxes (CUnit::partBoxes, +0x160)
    std::uint32_t boundsFlags;      // 1 and 2: the bounds are valid
    Vector3 center;
    float radius;
    Vector3 below;                  // box minimum - centre
    Vector3 above;                  // box maximum - centre
    std::uint8_t unused_150[0x158 - 0x150];    // cleared with the bounds
    // +0x158 cleared with the bounds; a part's piece record sets it (LoadPart). The AI adds it to a
    // unit's eye height.
    float eyeHeight;
    void* partBoxArrayVtable;       // CArray of the part boxes
    FootprintPart* partBoxes;       // | (ai_map.h)
    int partBoxCount;               // |
    int partBoxCapacity;            // |
    int partBoxGrowBy;              // /

protected:
    CObject3D();
};
static_assert(offsetof(CObject3D, worldMatrix) == 0x54);
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
