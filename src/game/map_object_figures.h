#pragma once

// The 3D object base under CMapObject (vtable 0073DDD4, constructor 00515BD0, 0x170 bytes): the
// object's figure (a model resource shared through the figure manager), its parts (scene nodes,
// one per figure piece) and their bounding box, and the scale the figure was built for. Names
// provisional; see docs/notes/map_objects.md ("Figures").

#include "engine_object.h"

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

class CObject3D : public CEngineObject {
public:
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

    float position[3];              // +0x01C
    std::uint8_t unknown_028[0x94 - 0x28];
    CEngineObject* attached;        // +0x094 the object slot 19 accepts (type 0x46)
    void* animation;                // +0x098 the figure's animation state (0050D7C0 rescales it)
    std::uint8_t unknown_09C[0xA4 - 0x9C];
    const char* name;               // +0x0A4 CString: the figure's name
    std::uint8_t unknown_0A8[0xC4 - 0xA8];
    float scale[3];                 // +0x0C4
    AlignedPtrArray parts;          // +0x0D0 FigurePart*
    AlignedPtrArray pendingParts;   // +0x0E0 FigurePart*, built by LoadFigures
    CEngineObject* figure;          // +0x0F0 counted reference (slot 0 deletes)
    std::uint8_t unknown_0F4[0x124 - 0xF4];
    std::uint32_t boundsFlags;      // +0x124 1 and 2: the bounds are valid
    float center[3];                // +0x128
    float radius;                   // +0x134
    float below[3];                 // +0x138 box minimum - centre
    float above[3];                 // +0x144 box maximum - centre
    std::uint8_t unknown_150[0x170 - 0x150];
};
static_assert(offsetof(CObject3D, attached) == 0x94);
static_assert(offsetof(CObject3D, name) == 0xA4);
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
