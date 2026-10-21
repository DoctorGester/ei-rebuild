#pragma once

#include "engine_object.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>

// An engine object placed in the world: local position and rotation relative to its parent, and
// the world transform derived from them. Base of the camera, the scene's figures, units and other
// placed objects (abstract: slots 11 and 12 are pure). Name provisional; vtable 0073BA38, 18 slots.
// Slots 0-12 are CEngineObject's (CopyFrom 00402D40, Release 00455980, empty Load/Save).
// Slots 13-17 are defined (core_transform.cpp), for a derived class whose vtable is the compiler's (C19).
class CTransformObject : public CEngineObject {
public:
    CTransformObject* DeleteScalar(unsigned flags);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BA38);

    // 13: 00401490. Copies the local and world transform; returns this.
    virtual CTransformObject* CopyTransform(const CTransformObject& other);
    CTransformObject* CopyTransformImpl(const CTransformObject& other);   // body of the slot above (registered by member pointer)
    // 14: 0040CB80. worldRotation = parent * localRotation. Overridden by 00516370 in units.
    virtual void CombineRotation(const Quaternion& parent);
    void CombineRotationImpl(const Quaternion& parent);   // body of the slot above (registered by member pointer)
    // 15: 0040CC30. Recomputes the world transform from the parent's rotation and position.
    virtual void UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition);
    void UpdateWorldImpl(const Quaternion& parentRotation, const Vector3& parentPosition);   // body of the slot above (registered by member pointer)
    // 16: 00401510. Writes the local transform to an MFC CFile (slot 14, Write).
    virtual void SaveTransform(void* file);
    void SaveTransformImpl(void* file);   // body of the slot above (registered by member pointer)
    // 17: 00401540. Reads the local transform from an MFC CFile (slot 13, Read).
    virtual void LoadTransform(void* file);
    void LoadTransformImpl(void* file);   // body of the slot above (registered by member pointer)

    // 00453400. Constructor body: base fields, identity rotations (positions left as they are),
    // vtable 0073BA38; returns this.
    CTransformObject* ConstructBase();

    // Rotation helpers (core_transform_math.cpp).
    void WorldMatrix(float* m) const;                    // 00401570: 4x4 world matrix
    void TiltTowards(const Vector3& target);             // 00401680: down axis a tenth towards target
    void TurnTowards(const Vector3& target);             // 00401940: forward axis onto target
    void RollTowards(const Vector3& dir);                // 00401B50: down axis towards dir about forward
    void Rotate(const Vector3& axis, float angle);       // 00401E50: localRotation = turn * localRotation
    void TurnUpTowards(const Vector3& dir, int reduced); // 00401F00: up axis towards dir (0.4 of it if reduced)
    void SetHeading(const float* dir);                   // 00402220: turn about +Z by atan2(dir[1], dir[0])

    Vector3 localPosition_;     // +0x1C
    Vector3 worldPosition_;     // +0x28
    Quaternion localRotation_;  // +0x34
    Quaternion worldRotation_;  // +0x44
};
static_assert(offsetof(CTransformObject, localPosition_) == 0x1C);
static_assert(offsetof(CTransformObject, localRotation_) == 0x34);
static_assert(sizeof(CTransformObject) == 0x54);
