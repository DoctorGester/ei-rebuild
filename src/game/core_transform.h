#pragma once

#include "engine_object.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>

// An engine object placed in the world: local position and rotation relative to its parent, and
// the world transform derived from them. Base of the camera, the scene's figures, units and other
// placed objects (abstract: slots 2, 3, 11 and 12 are pure). Name provisional; 18 slots (game.exe's 0073BA38).
// Slots 0-12 are CEngineObject's (CopyFrom 00402D40, Release 00455980, empty Load/Save); 13-17 are defined in
// core_transform.cpp. game.exe's out-of-line constructor body 00453400 (the base's fields and identity rotations) is
// what the derived classes' constructors do; its destructor 0040CB70 (deleting 0040CB50) is CEngineObject's.
namespace mfc {
class CFileVirtuals;   // lib/mfc.h
}

class CTransformObject : public CEngineObject {
public:
    // Copies the local and world transform; returns this.
    virtual CTransformObject* CopyTransform(const CTransformObject& other);
    // WorldRotation = parent * localRotation. Overridden by 00516370 in units.
    virtual void CombineRotation(const Quaternion& parent);
    // Recomputes the world transform from the parent's rotation and position.
    virtual void UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition);
    // Writes the local transform to an MFC CFile (slot 14, Write).
    virtual void SaveTransform(mfc::CFileVirtuals* file);
    // Reads the local transform from an MFC CFile (slot 13, Read).
    virtual void LoadTransform(mfc::CFileVirtuals* file);

    // Rotation helpers (core_transform_math.cpp).
    void WorldMatrix(float* m) const;                    // 4x4 world matrix
    void TiltTowards(const Vector3& target);             // down axis a tenth towards target
    void TurnTowards(const Vector3& target);             // forward axis onto target
    void RollTowards(const Vector3& dir);                // down axis towards dir about forward
    void Rotate(const Vector3& axis, float angle);       // localRotation = turn * localRotation
    void TurnUpTowards(const Vector3& dir, int reduced); // up axis towards dir (0.4 of it if reduced)
    void SetHeading(const float* dir);                   // turn about +Z by atan2(dir[1], dir[0])

    Vector3 localPosition_;     // +0x1C
    Vector3 worldPosition_;     // +0x28
    Quaternion localRotation_;  // +0x34
    Quaternion worldRotation_;  // +0x44
};
static_assert(offsetof(CTransformObject, localPosition_) == 0x1C);
static_assert(offsetof(CTransformObject, localRotation_) == 0x34);
static_assert(sizeof(CTransformObject) == 0x54);
