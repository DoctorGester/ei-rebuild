// The 3D object base under CMapObject: loading the figure's parts, rescaling them and their
// bounding box. See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "figure_manager.h"
#include "figure_mesh.h"
#include "map_object_figures.h"
#include "link_figure.h"   // LinkHeaderMap: the figure's pieces
#include "world.h"   // CWorld: the figure manager and cache

#include "ei/x87.h"
#include "lib/bytes.h"
#include "lib/nan_audit.h"

#include <concepts>
#include <bit>
#include <cstring>

// The bounds must keep the original's order of operations, its float stores and its NaN behaviour: values it
// keeps on the FPU stack are double locals, values it stores are floats.

namespace {
constexpr std::uint32_t kBoundsValid = 1 | 2;

constexpr std::uint32_t kFloatMax = 0x7F7FFFFF;      // FLT_MAX / -FLT_MAX: the empty box
constexpr std::uint32_t kFloatLowest = 0xFF7FFFFF;
constexpr float kNearSquared = 1e9f;                 // parts farther away are skipped

std::uint32_t Bits(float v) { return std::bit_cast<std::uint32_t>(v); }
float Float(std::uint32_t bits) { return std::bit_cast<float>(bits); }
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float Float(T) = delete;

// Animation state: rescale to x, y, z.
void AnimationSetScale(void* state, float x, float y, float z) {
    static_cast<FigureAnimationState*>(state)->Rescale(x, y, z);
}

// The original's branch-free selects: the stored candidate `v` replaces the bound when the comparison sets C0
// (it is beyond the bound, or unordered).
std::uint32_t KeepLower(std::uint32_t bound, float v) { return v >= Float(bound) ? bound : Bits(v); }
std::uint32_t KeepHigher(std::uint32_t bound, float v) { return Float(bound) >= v ? bound : Bits(v); }

// a + b + c in that order, stored to a float.
float Sum3(float a, float b, float c) { return static_cast<float>((static_cast<double>(a) + b) + c); }

// The box being grown, as float bits (the original selects bits, never loading them twice).
struct BoxBits {
    std::uint32_t low[3] = {kFloatMax, kFloatMax, kFloatMax};
    std::uint32_t high[3] = {kFloatLowest, kFloatLowest, kFloatLowest};
};

// (x * x + y * y) + z * z of the part's position, unrounded.
double DistanceSquared(const FigureMesh* part) { return LengthSquaredUnrounded<Sum3::XYZ, double>(part->localPosition); }

// A placed part: its piece's box moved by its position and the piece's offset.
void AddPlacedPart(BoxBits& box, const FigureMesh* p) {
    box.low[0] = KeepLower(box.low[0], Sum3(p->box.bounds.boxMin.x, p->localPosition.x, p->box.bounds.center.x));
    box.high[0] = KeepHigher(box.high[0], Sum3(p->box.bounds.boxMax.x, p->localPosition.x, p->box.bounds.center.x));
    box.low[1] = KeepLower(box.low[1], Sum3(p->box.bounds.boxMin.y, p->localPosition.y, p->box.bounds.center.y));
    box.high[1] = KeepHigher(box.high[1], Sum3(p->box.bounds.boxMax.y, p->localPosition.y, p->box.bounds.center.y));
    box.low[2] = KeepLower(box.low[2], Sum3(p->box.bounds.boxMin.z, p->localPosition.z, p->box.bounds.center.z));
    box.high[2] = KeepHigher(box.high[2], Sum3(p->box.bounds.boxMax.z, p->localPosition.z, p->box.bounds.center.z));
}

} // namespace

void CObject3D::ComputeBounds() {
    BoxBits box;
    for (int i = 0; i < std::ssize(parts); ++i) {
        FigureMesh* part = parts[i];
        if (DistanceSquared(part) > kNearSquared) continue;   // NaN distances count as near
        AddPlacedPart(box, part);
    }
    if (parts.empty()) return;

    float lowX = Float(box.low[0]), lowY = Float(box.low[1]), lowZ = Float(box.low[2]);
    float highX = Float(box.high[0]), highY = Float(box.high[1]), highZ = Float(box.high[2]);
    double sumX = static_cast<double>(lowX) + highX;                  // kept on the stack
    float sumY = static_cast<float>(static_cast<double>(lowY) + highY);
    float sumZ = static_cast<float>(static_cast<double>(lowZ) + highZ);
    float centerX = static_cast<float>(sumX * 0.5f);
    float centerY = static_cast<float>(static_cast<double>(sumY) * 0.5f);
    float centerZ = static_cast<float>(static_cast<double>(sumZ) * 0.5f);
    float aboveX = static_cast<float>(static_cast<double>(highX) - centerX);
    float aboveY = static_cast<float>(static_cast<double>(highY) - centerY);
    float aboveZ = static_cast<float>(static_cast<double>(highZ) - centerZ);
    double belowX = static_cast<double>(lowX) - centerX;             // kept on the stack (radius)
    float belowXf = static_cast<float>(belowX);
    float belowY = static_cast<float>(static_cast<double>(lowY) - centerY);
    float belowZ = static_cast<float>(static_cast<double>(lowZ) - centerZ);
    bounds.boxMin.x = belowXf;
    bounds.boxMin.y = belowY;
    bounds.boxMin.z = belowZ;
    double length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(belowX, belowY, belowZ));
    bounds.boxMax.x = aboveX;
    bounds.boxMax.y = aboveY;
    bounds.boxMax.z = aboveZ;
    bounds.flags |= BoundsFlag::Box;
    bounds.flags |= BoundsFlag::Sphere;
    bounds.radius = static_cast<float>(length);
    bounds.center.x = centerX;
    bounds.center.y = centerY;
    bounds.center.z = centerZ;
}

void CObject3D::SetFigureScale(const float* newScale) {
    EI_NAN_AUDIT(scale.x, scale.y, scale.z, newScale[0], newScale[1], newScale[2]);
    if (scale.x == newScale[0] && scale.y == newScale[1] && scale.z == newScale[2])
        return;
    lib::LoadBytes(scale, newScale);
    if (LinkMesh* current = figure) {
        LinkFigureLoader* loader = WorldContext()->FigureLoader();
        LinkMesh* rebuilt = loader->Remorph(current, scale.x, scale.y, scale.z);
        figure.Assign(rebuilt);
        for (int i = 0; i < std::ssize(parts); ++i) {
            FigureMesh* part = parts[i];
            const char* pieceName = part->GetName();
            std::map<std::string, LinkPartHeader>::const_iterator piece;
            {
                std::string key(pieceName);
                piece = figure->headers.find(key);
            }
            part = parts[i];
            const ClipBounds& fitted = piece->second.box.bounds;
            part->box.bounds.center = fitted.center;
            part->box.bounds.boxMin = fitted.boxMin;
            part->box.bounds.boxMax = fitted.boxMax;
            part->box.bounds.radius = fitted.radius;
        }
    }
    if (animation) AnimationSetScale(animation, scale.x, scale.y, scale.z);
    ComputeBounds();
}

void CObject3D::LoadFigures() {
    if (flags & MapObjectFlag::FiguresLoaded) return;
    {
        std::string key(name);
        FigureManagerOf(this)->FindModel(&key);
    }
    if (GetClassId() != ClassId::Unit) {
        for (int i = 0; i < std::ssize(parts); ++i) parts[i]->flags |= FigureMeshFlag::Stationary;
        flags |= MapObjectFlag::Stationary;
    }
    attached->ApplyMaterials();
    flags |= MapObjectFlag::FiguresLoaded;
}
