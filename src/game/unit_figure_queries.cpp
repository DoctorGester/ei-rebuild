// Unit queries on the figure (U11): animation entries for a key, the body's extent, the height of
// the ground under a point and the point effects start from. See unit.h and docs/notes/units.md
// ("Thirteenth round (U11)").
#include "unit.h"

#include "ai_map.h"
#include "originals_units.h"
#include "unit_client_models.h"
#include "world.h"

#include "lib/crt.h"

#include "ei/hook.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
constexpr int kTypeOtherUnit = 0x33;            // CEngineObject::type_ (kUnitTypeHero 0x32 .. 0x34: units)
constexpr int kTypeLastUnit = 0x34;
constexpr int kClassUnitServer = 0x3E;          // CUnitServer (combat.h)
constexpr std::uint8_t kMovesOnWater = 2;       // CUnit::movementKind

// Animation keys (docs/notes/combat.md): the low 15 bits are variant bits, the rest the kind;
// 0x38000 are the movement bits (0x30000 mode 0, 0x20000 mode 1, 0x10000 mode 2, 0x8000 mode 4).
constexpr std::uint32_t kKindBits = 0xFFFF8000;
constexpr std::uint32_t kMoveBits = 0x38000;
constexpr std::uint32_t kMove1Bit = 0x20000;
constexpr std::uint32_t kMove2Bit = 0x10000;
constexpr std::uint32_t kMove4Bit = 0x8000;
constexpr std::uint32_t kEntryIndex = 0xFF;

const char* const kNoAnimationEntry = "Can't find animation entry index\n %i\n for \"%s\".\n";   // 0078F918
const char* const kBodyPart = "bd";             // 0078AD38
const char* const kChestPart = "chest";         // 0078F954
const char* const kMosquito = "unmosp";         // 0078F94C
const char* const kRightHand = "rh3";           // 0078F95C

constexpr double kNoExtent = 0.0;               // 0073B858
constexpr float kExtentScale = 1.0f;            // 0073B84C
constexpr float kMosquitoExtentScale = 2.0f;    // 0073CDB8

// A random animation entry of the figure matching `key` (005190C0: the entries whose kind equals
// the key's and whose variant bits include the key's, or exactly none for a key without them, and
// which virtual slot 18 accepts; one picked with rand()), or of the key's kind only (00519380);
// null when none (both RET 4).
__declspec(naked) char* __fastcall RandomAnimation(CUnit*, void*, std::uint32_t /*key*/) EI_ORIGINAL(0x005190C0)
__declspec(naked) char* __fastcall RandomAnimationOfKind(CUnit*, void*, std::uint32_t /*key*/) EI_ORIGINAL(0x00519380)

// `v` turned by the rotation `q` as the original spells it out: 2 (u.v) u + (s^2 - u.u) v +
// 2 s (u x v), with u the vector part and s the scalar part.
Vector3 Rotate(const Quaternion& q, const Vector3& v) {
    float ux = q.x;
    float uy = q.y;
    float uz = q.z;
    float twiceDot = 2.0f * ((v.y * uy + v.z * uz) + v.x * ux);
    Vector3 along = {twiceDot * ux, twiceDot * uy, twiceDot * uz};
    float twiceS = q.w + q.w;
    Vector3 cross = {uy * v.z - uz * v.y, uz * v.x - ux * v.z, ux * v.y - uy * v.x};
    Vector3 turn = {twiceS * cross.x, twiceS * cross.y, twiceS * cross.z};
    float k = q.w * q.w - (ux * ux + uy * uy + uz * uz);
    return {k * v.x + turn.x + along.x, k * v.y + turn.y + along.y, k * v.z + turn.z + along.z};
}
} // namespace

__declspec(naked) bool CUnitClientSpecific::WeaponPoint(Vector3*) EI_ORIGINAL(0x00535770)

char* CUnit::FindAnimation(std::uint32_t key, bool anyOfKind) {
    if (type_ < kUnitTypeHero || type_ > kTypeLastUnit) return nullptr;
    if (char* entry = RandomAnimation(this, nullptr, key)) return entry;
    std::uint32_t kind = key & kKindBits;
    if (char* entry = RandomAnimation(this, nullptr, kind)) return entry;
    if ((kind & kMoveBits) == kMove4Bit) {
        if (char* entry = RandomAnimation(this, nullptr, kind & ~kMoveBits)) return entry;
        if (char* entry = RandomAnimation(this, nullptr, (kind & ~(kMove1Bit | kMove4Bit)) | kMove2Bit)) return entry;
    }
    if (!anyOfKind) return nullptr;
    return RandomAnimationOfKind(this, nullptr, kind);
}
EI_REPLACE_METHOD(0x005301B0, CUnit::FindAnimation);

char* CUnit::AnimationEntry(std::uint32_t key) {
    char* entry = ObjectAnimationEntryOriginal(this, nullptr, key);
    if (!entry) game::DebugLog(kNoAnimationEntry, static_cast<int>(key & kEntryIndex), prototypeName);
    return entry;
}
EI_REPLACE_METHOD(0x00530240, CUnit::AnimationEntry);

double CUnit::BodyExtent() {
    int part = ObjectFindPartIndexOriginal(this, nullptr, kBodyPart);
    if (part < 0) {
        part = ObjectFindPartIndexOriginal(this, nullptr, kChestPart);
        if (part < 0) return kNoExtent;
    }
    float scale = kExtentScale;
    if (type_ == kTypeOtherUnit && std::strcmp(prototypeName, kMosquito) == 0) scale = kMosquitoExtentScale;
    const Vector3& corner = partBoxes[part].boxMax;
    double y = corner.y;
    double x = corner.x;
    return scale * std::sqrt(y * y + x * x);
}
EI_REPLACE_METHOD(0x005304A0, CUnit::BodyExtent);

double CUnit::GroundHeight(float x, float y) {
    auto* world = static_cast<CWorld*>(World());
    if (movementKind != kMovesOnWater) return world->GroundHeightAt(x, y);
    if (controller->GetClassId() == kClassUnitServer) return world->SurfaceHeightAt(x, y) + positionZ;
    return world->SurfaceHeightAt(x, y) + heightOffset;
}
EI_REPLACE_METHOD(0x00530280, CUnit::GroundHeight);

Vector3* CUnit::EffectPoint(Vector3* out) {
    // The original leaves this uninitialised on the server and for a hero without a right hand,
    // and turns whatever the stack holds; zero gives the position there (docs/notes/units.md).
    Vector3 local = {0.0f, 0.0f, 0.0f};
    auto* world = static_cast<CWorld*>(ManagerContext());
    if (!world->IsServer()) {
        if (type_ == kUnitTypeHero) {
            if (CEngineObject* hand = ObjectFindPartOriginal(this, nullptr, kRightHand)) {
                float at[3];
                ObjectPartPositionOriginal(this, nullptr, at, hand);
                out->x = at[0];
                out->y = at[1];
                out->z = at[2];
                return out;
            }
        } else if (!static_cast<CUnitClientSpecific*>(controller)->WeaponPoint(&local)) {
            out->x = positionX + center[0];
            out->y = positionY + center[1];
            out->z = positionZ + center[2];
            return out;
        }
    }
    Vector3 turned = Rotate(orientation, local);
    out->x = turned.x + positionX;
    out->y = turned.y + positionY;
    out->z = turned.z + positionZ;
    return out;
}
EI_REPLACE_METHOD(0x00532860, CUnit::EffectPoint);
