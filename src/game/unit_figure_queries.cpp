// Unit queries on the figure: animation entries for a key, the body's extent, the height of the ground under
// a point and the point effects start from. See unit.h and docs/notes/units.md ("Creation, databases, save").
#include "map_object_figures.h"
#include "unit.h"

#include "ai_map.h"
#include "unit_client_models.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/pc24.h"
#include "lib/str.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
constexpr std::uint8_t kMovesOnWater = 2;       // UnitParams::movementKind

// Animation keys (docs/notes/combat.md): the low 15 bits are variant bits, the rest the kind;
// 0x38000 are the movement bits (0x30000 mode 0, 0x20000 mode 1, 0x10000 mode 2, 0x8000 mode 4).
constexpr std::uint32_t kKindBits = 0xFFFF8000;
constexpr std::uint32_t kMoveBits = 0x38000;
constexpr std::uint32_t kMove1Bit = 0x20000;
constexpr std::uint32_t kMove2Bit = 0x10000;
constexpr std::uint32_t kMove4Bit = 0x8000;
constexpr std::uint32_t kEntryIndex = 0xFF;

const char* const kNoAnimationEntry = "Can't find animation entry index\n %i\n for \"%s\".\n";
const char* const kBodyPart = "bd";
const char* const kChestPart = "chest";
const char* const kMosquito = "unmosp";
const char* const kRightHand = "rh3";

constexpr float kNoExtent = 0.0f;
constexpr float kExtentScale = 1.0f;
constexpr float kMosquitoExtentScale = 2.0f;

// `v` turned by the rotation `q`, spelled out as: 2 (u.v) u + (s^2 - u.u) v + 2 s (u x v), with u the
// vector part and s the scalar part.
Vector3 RotateVector(const Quaternion& q, const Vector3& v) {
    float ux = q.x;
    float uy = q.y;
    float uz = q.z;
    // Precise helpers: /fp:fast regrouped an inlined unrounded dot with the `2 *` into
    // (2xx' + d) + d with d = yy' + zz' (a second rounding, V3c); written out it had become
    // (2yy' + 2zz') + 2xx', which is exact.
    Vector3 u = {ux, uy, uz};
    float twiceDot = 2.0f * DotZYX(v, u);
    Vector3 along = {twiceDot * ux, twiceDot * uy, twiceDot * uz};
    float twiceS = q.w + q.w;
    Vector3 cross = {uy * v.z - uz * v.y, uz * v.x - ux * v.z, ux * v.y - uy * v.x};
    Vector3 turn = {twiceS * cross.x, twiceS * cross.y, twiceS * cross.z};
    float k = q.w * q.w - LengthSquaredXYZ(u);
    return {k * v.x + turn.x + along.x, k * v.y + turn.y + along.y, k * v.z + turn.z + along.z};
}
} // namespace

FigureAnimation* Unit::FindAnimation(std::uint32_t key, bool anyOfKind, AnimationRandom random) {
    if (!IsUnitType(MapType())) return nullptr;
    if (FigureAnimation* entry = RandomAnimation(key, random)) return entry;
    std::uint32_t kind = key & kKindBits;
    if (FigureAnimation* entry = RandomAnimation(kind, random)) return entry;
    if ((kind & kMoveBits) == kMove4Bit) {
        if (FigureAnimation* entry = RandomAnimation(kind & ~kMoveBits, random)) return entry;
        if (FigureAnimation* entry = RandomAnimation((kind & ~(kMove1Bit | kMove4Bit)) | kMove2Bit, random)) return entry;
    }
    if (!anyOfKind) return nullptr;
    return RandomAnimationOfKind(kind, random);
}

FigureAnimation* Unit::AnimationEntry(std::uint32_t key) {
    FigureAnimation* entry = AnimationAt(key);
    if (!entry) game::DebugLog(kNoAnimationEntry, static_cast<int>(key & kEntryIndex), name);
    return entry;
}

float Unit::BodyExtent() {
    int part = FindPartIndex(kBodyPart);
    if (part < 0) {
        part = FindPartIndex(kChestPart);
        if (part < 0) return kNoExtent;
    }
    float extentScale = kExtentScale;
    if (MapType() == MapObjectType::Creature && str::Compare(name, kMosquito) == 0) extentScale = kMosquitoExtentScale;
    const Vector3& corner = partBoxes[part].bounds.boxMax;
    return extentScale * std::sqrt(LengthSquared2DUnrounded(corner.y, corner.x));
}

double Unit::GroundHeight(float x, float y) {
    auto* world = OwningWorld();
    if (params.movementKind != kMovesOnWater) return world->GroundHeightAt(x, y);
    // The world's double plus a float: one rounding.
    if (controller->GetClassId() == ClassId::UnitServer) return pc24::Add(world->SurfaceHeightAt(x, y), localPosition.z);
    return pc24::Add(world->SurfaceHeightAt(x, y), heightOffset);
}


Vector3* Unit::EffectPoint(Vector3* out) {
    // The game leaves this uninitialised on the server and for a hero without a right hand, and turns
    // whatever the stack holds; zero gives the position there (docs/notes/units.md).
    Vector3 local = {0.0f, 0.0f, 0.0f};
    auto* world = WorldContext();
    if (!world->IsServer()) {
        if (MapType() == MapObjectType::Hero) {
            if (FigureMesh* hand = FindPartByPrefix(kRightHand)) {
                Vector3 at;
                PartWorldPosition(&at.x, hand);
                out->x = at.x;
                out->y = at.y;
                out->z = at.z;
                return out;
            }
        } else if (!ClientController()->WeaponPoint(&local)) {
            out->x = localPosition.x + bounds.center.x;
            out->y = localPosition.y + bounds.center.y;
            out->z = localPosition.z + bounds.center.z;
            return out;
        }
    }
    Vector3 turned = RotateVector(localRotation, local);
    out->x = turned.x + localPosition.x;
    out->y = turned.y + localPosition.y;
    out->z = turned.z + localPosition.z;
    return out;
}
