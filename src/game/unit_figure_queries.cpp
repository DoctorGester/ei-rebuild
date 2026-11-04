// Unit queries on the figure: animation entries for a key, the body's extent, the height of the ground under
// a point and the point effects start from. See unit.h and docs/notes/units.md ("Creation, databases, save").
#include "ai_map.h"
#include "map_object_figures.h"
#include "unit.h"
#include "unit_client_models.h"
#include "world.h"

#include "lib/crt.h"
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
    return extentScale * Length2D(XY(partBoxes[part].bounds.boxMax));
}

float Unit::GroundHeight(float x, float y) {
    auto* world = OwningWorld();
    if (params.movementKind != kMovesOnWater) return world->GroundHeightAt(x, y);
    if (controller->GetClassId() == ClassId::UnitServer) return world->SurfaceHeightAt(x, y) + localPosition.z;
    return world->SurfaceHeightAt(x, y) + heightOffset;
}


Vector3 Unit::EffectPoint() {
    // The game leaves this uninitialised on the server and for a hero without a right hand, and turns
    // whatever the stack holds; zero gives the position there (docs/notes/units.md).
    Vector3 local = {0.0f, 0.0f, 0.0f};
    auto* world = WorldContext();
    if (!world->IsServer()) {
        if (MapType() == MapObjectType::Hero) {
            if (FigureMesh* hand = FindPartByPrefix(kRightHand)) {
                Vector3 at;
                at = PartWorldPosition(hand);
                return at;
            }
        } else if (std::optional<Vector3> weapon = ClientController()->WeaponPoint()) {
            local = *weapon;
        } else {
            return localPosition + bounds.center;
        }
    }
    Vector3 turned = localRotation.Rotate(local);
    return turned + localPosition;
}
