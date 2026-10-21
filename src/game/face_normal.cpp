// A mesh face's plane (A-S4): 0049AEC0, called for every face of the fake unit scene's meshes
// (scene_normals.cpp). Plain float maths (rule 7); the values the original stores to floats are
// rounded to float.
#include "scene_loader.h"

#include "lib/vecmath.h"
#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"

#include <cmath>

namespace scene {

namespace {

// The normal a degenerate face gets: zero (a static initialiser's stores).
constexpr Vector3 kDegenerateNormal = /*007A9C98*/ {0.0f, 0.0f, 0.0f};

constexpr double kMinimumSquare = 1e-10;

} // namespace

void MeshFace::ComputeNormal(const PointBuffer* positions) {
    const auto* points = reinterpret_cast<const Vector3*>(positions->data);
    const Vector3& origin = points[vertex[1]];
    const Vector3& p2 = points[vertex[2]];
    const Vector3& p0 = points[vertex[0]];

    double ax = static_cast<double>(p2.x) - origin.x;
    double ay = static_cast<double>(p2.y) - origin.y;
    float az = static_cast<float>(static_cast<double>(p2.z) - origin.z);   // kept as a float
    double lengthA = LengthSquaredUnrounded<Sum3::ZYX, double>(ax, ay, az);
    if (lengthA > kMinimumSquare) {
        double inverse = 1.0 / std::sqrt(lengthA);
        ax *= inverse;
        ay *= inverse;
        az = static_cast<float>(az * inverse);
    }

    double bx = static_cast<double>(p0.x) - origin.x;
    double by = static_cast<double>(p0.y) - origin.y;
    double bz = static_cast<double>(p0.z) - origin.z;
    double lengthB = LengthSquaredUnrounded<Sum3::ZYX>(bx, by, bz);
    if (lengthB > kMinimumSquare) {
        double inverse = 1.0 / std::sqrt(lengthB);
        bx *= inverse;
        by *= inverse;
        bz *= inverse;
    }

    normal.x = static_cast<float>(bz * ay - by * az);
    normal.y = static_cast<float>(az * bx - bz * ax);
    normal.z = static_cast<float>(by * ax - ay * bx);

    const double x = normal.x;
    const double y = normal.y;
    const double z = normal.z;
    const double length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(x, y, z));
    EI_NAN_AUDIT(length);
    if (length == 0.0) {   // FCOM 0; C3
        normal = kDegenerateNormal;
    } else {
        double inverse = 1.0 / length;
        normal.x = static_cast<float>(inverse * normal.x);
        normal.y = static_cast<float>(inverse * normal.y);
        normal.z = static_cast<float>(inverse * normal.z);
    }
    const Vector3& at = points[vertex[1]];
    distance = static_cast<float>(DotZYX(at, normal));
}
EI_REPLACE_METHOD(0x0049AEC0, MeshFace::ComputeNormal);

} // namespace scene
