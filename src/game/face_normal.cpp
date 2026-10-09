// A mesh face's plane (A-S4): 0049AEC0, called for every face of the fake unit scene's meshes
// (scene_normals.cpp). Plain float maths (rule 7); the values the original stores to floats are
// rounded through a volatile store.
#include "scene_loader.h"

#include "lib/vecmath.h"
#include "ei/hook.h"
#include "ei/x87.h"

#include <cmath>
#include <cstring>

namespace scene {

namespace {

float StoreFloat(double value) {
    volatile float stored = static_cast<float>(value);
    return stored;
}

// The normal used for a degenerate face (007A9C98).
const Vector3& DegenerateNormal() { return *reinterpret_cast<const Vector3*>(0x007A9C98); }

constexpr double kMinimumSquare = 1e-10;

} // namespace

void __fastcall ComputeFaceNormal(MeshFace* face, void*, const PointBuffer* positions) {
    const auto* points = reinterpret_cast<const Vector3*>(positions->data);
    const Vector3& origin = points[face->vertex[1]];
    const Vector3& p2 = points[face->vertex[2]];
    const Vector3& p0 = points[face->vertex[0]];

    double ax = static_cast<double>(p2.x) - origin.x;
    double ay = static_cast<double>(p2.y) - origin.y;
    float az = StoreFloat(static_cast<double>(p2.z) - origin.z);   // kept as a float
    double lengthA = static_cast<double>(az) * az + ay * ay + ax * ax;
    if (lengthA > kMinimumSquare) {
        double inverse = 1.0 / std::sqrt(lengthA);
        ax *= inverse;
        ay *= inverse;
        az = StoreFloat(az * inverse);
    }

    double bx = static_cast<double>(p0.x) - origin.x;
    double by = static_cast<double>(p0.y) - origin.y;
    double bz = static_cast<double>(p0.z) - origin.z;
    double lengthB = bz * bz + by * by + bx * bx;
    if (lengthB > kMinimumSquare) {
        double inverse = 1.0 / std::sqrt(lengthB);
        bx *= inverse;
        by *= inverse;
        bz *= inverse;
    }

    face->normal.x = StoreFloat(bz * ay - by * az);
    face->normal.y = StoreFloat(az * bx - bz * ax);
    face->normal.z = StoreFloat(by * ax - ay * bx);

    const double x = face->normal.x;
    const double y = face->normal.y;
    const double z = face->normal.z;
    const double length = std::sqrt(x * x + y * y + z * z);
    if (length == 0.0 || x87::IsNaN(static_cast<float>(length))) {   // FCOM 0; C3: equal or unordered
        std::memcpy(&face->normal, &DegenerateNormal(), sizeof(Vector3));
    } else {
        double inverse = 1.0 / length;
        face->normal.x = StoreFloat(inverse * face->normal.x);
        face->normal.y = StoreFloat(inverse * face->normal.y);
        face->normal.z = StoreFloat(inverse * face->normal.z);
    }
    const Vector3& at = points[face->vertex[1]];
    face->distance = StoreFloat(DotZYX(at, face->normal));
}
EI_REPLACE(0x0049AEC0, ComputeFaceNormal);

} // namespace scene
