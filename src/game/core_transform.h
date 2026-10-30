#pragma once

#include "engine_object.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>

// An engine object placed in the world: local position and rotation relative to its parent, and
// the world transform derived from them. Base of the camera, the scene's figures, units and other
// placed objects (abstract: some virtuals are pure). Name provisional.
// The virtuals up to the class id are EngineObject's (CopyFrom, Release, empty Load/Save); the rest are defined in
// The game's out-of-line constructor body (the base's fields and identity rotations) is
// what the derived classes' constructors do; its destructor (deleting too) is EngineObject's.
namespace platform {
class File;
}

class TransformObject : public EngineObject {
public:
    virtual TransformObject* CopyTransform(const TransformObject& other);
    // WorldRotation = parent * localRotation. Overridden in units.
    virtual void CombineRotation(const Quaternion& parent);
    // Recomputes the world transform from the parent's rotation and position.
    virtual void UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition);
    virtual void SaveTransform(platform::File* file);
    virtual void LoadTransform(platform::File* file);

    Matrix4 WorldMatrix() const;                         // 4x4 world matrix
    void TiltTowards(const Vector3& target);             // down axis a tenth towards target
    void TurnTowards(const Vector3& target);             // forward axis onto target
    void RollTowards(const Vector3& dir);                // down axis towards dir about forward
    void Rotate(const Vector3& axis, float angle);       // localRotation = turn * localRotation
    void TurnUpTowards(const Vector3& dir, int reduced); // up axis towards dir (0.4 of it if reduced)
    void SetHeading(const float* dir);                   // turn about +Z by atan2(dir[1], dir[0])

    Vector3 localPosition;
    Vector3 worldPosition;
    Quaternion localRotation;
    Quaternion worldRotation;
};
