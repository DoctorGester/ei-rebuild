#pragma once

#include "engine_object.h"
#include "maths.h"

#include <cstddef>
#include <cstdint>

// An engine object placed in the world: local position and rotation relative to its parent, and
// the world transform derived from them. Base of the camera, the scene's figures, units and other
// placed objects (abstract: slots 11 and 12 are pure). Name provisional; vtable 0073BA38, 18 slots.
// Slots 0-12 are CEngineObject's (CopyFrom 00402D40, Release 00455980, empty Load/Save).
class CTransformObject : public CEngineObject {
public:
    // 13: 00401490. Copies the local and world transform; returns this.
    virtual CTransformObject* CopyTransform(const CTransformObject& other);
    // 14: 0040CB80. worldRotation = parent * localRotation. Overridden by 00516370 in units.
    virtual void CombineRotation(const Quaternion& parent);
    // 15: 0040CC30. Recomputes the world transform from the parent's rotation and position.
    virtual void UpdateWorld(const Quaternion& parentRotation, const Vector3& parentPosition);
    // 16: 00401510. Writes the local transform to an MFC CFile (slot 14, Write).
    virtual void SaveTransform(void* file);
    // 17: 00401540. Reads the local transform from an MFC CFile (slot 13, Read).
    virtual void LoadTransform(void* file);

    // 00453400 (object.cpp). Constructor body: base fields, identity rotations (positions left as
    // they are), vtable 0073BA38; returns this.
    CTransformObject* ConstructBase();

    Vector3 localPosition_;     // +0x1C
    Vector3 worldPosition_;     // +0x28
    Quaternion localRotation_;  // +0x34
    Quaternion worldRotation_;  // +0x44
};
static_assert(offsetof(CTransformObject, localPosition_) == 0x1C);
static_assert(offsetof(CTransformObject, localRotation_) == 0x34);
static_assert(sizeof(CTransformObject) == 0x54);

// The camera ("CCamera", class id 0x43, vtable 0073B9E0). Only the members used so far.
class CCamera : public CTransformObject {
public:
    // 0040E810. Where the camera's view axis (rotated Z) meets the ground plane z = 0; the camera
    // position itself when the axis is (nearly) horizontal or points away from the plane.
    void GroundPoint(Vector3* out) const;

    // 0040F360 / 0040F3C0. Set a target rate; the current rate jumps up to it when larger in
    // magnitude. Names provisional (driven by mouse rotation in 005EAD50).
    void SetTurnRate(float rate);
    void SetTiltRate(float rate);
    // 0040F420.
    void SetUnknownD0(std::uint32_t value);

    // 0040D3B0 / 0040D420. Follows the object with this id (-1: none); starting to follow resets
    // the two offset vectors to the zero vector.
    void SetTarget(const CEngineObject* target);
    void SetTargetId(int id);

    std::uint8_t unknown_54[0x98 - 0x54];
    Vector3 offset98_;          // +0x98
    Vector3 offsetA4_;          // +0xA4
    std::uint32_t unknown_B0;
    float turnRate_;            // +0xB4
    float turnTarget_;          // +0xB8
    std::uint32_t unknown_BC;
    float tiltRate_;            // +0xC0
    float tiltTarget_;          // +0xC4
    std::uint8_t unknown_C8[0xD0 - 0xC8];
    std::uint32_t unknownD0_;   // +0xD0
    std::uint8_t unknown_D4[0x11C - 0xD4];
    int targetId_;              // +0x11C
};
static_assert(offsetof(CCamera, offset98_) == 0x98);
static_assert(offsetof(CCamera, turnRate_) == 0xB4);
static_assert(offsetof(CCamera, tiltTarget_) == 0xC4);
static_assert(offsetof(CCamera, unknownD0_) == 0xD0);
static_assert(offsetof(CCamera, targetId_) == 0x11C);
