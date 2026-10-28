#pragma once

#include "engine_object.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>

// An engine object placed in the world: local position and rotation relative to its parent, and
// the world transform derived from them. Base of the camera, the scene's figures, units and other
// placed objects (abstract: slots 2, 3, 11 and 12 are pure). Name provisional; 18 slots.
// Slots 0-12 are CEngineObject's (CopyFrom, Release, empty Load/Save); 13-17 are defined in
// The game's out-of-line constructor body (the base's fields and identity rotations) is
// what the derived classes' constructors do; its destructor (deleting too) is CEngineObject's.
namespace lib {
class File;
}

class CTransformObject : public CEngineObject {
public:
    virtual CTransformObject* CopyTransform(const CTransformObject& other);
    // WorldRotation = parent * localRotation. Overridden in units.
    virtual void CombineRotation(const Quaternion& parent);
    // Recomputes the world transform from the parent's rotation and position.
    virtual void UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition);
    virtual void SaveTransform(lib::File* file);
    virtual void LoadTransform(lib::File* file);

    void WorldMatrix(float* m) const;                    // 4x4 world matrix
    void TiltTowards(const Vector3& target);             // down axis a tenth towards target
    void TurnTowards(const Vector3& target);             // forward axis onto target
    void RollTowards(const Vector3& dir);                // down axis towards dir about forward
    void Rotate(const Vector3& axis, float angle);       // localRotation = turn * localRotation
    void TurnUpTowards(const Vector3& dir, int reduced); // up axis towards dir (0.4 of it if reduced)
    void SetHeading(const float* dir);                   // turn about +Z by atan2(dir[1], dir[0])

    Vector3 localPosition_;
    Vector3 worldPosition_;
    Quaternion localRotation_;
    Quaternion worldRotation_;
};
static_assert(offsetof(CTransformObject, localPosition_) == 0x1C);
static_assert(offsetof(CTransformObject, localRotation_) == 0x34);
static_assert(sizeof(CTransformObject) == 0x54);
