// Placed traps (trap_object.h) and the magic trap's class slots and deleting destructor.
#include "trap_object.h"

#include "core_streams.h"
#include "game_singletons.h"
#include "magic_trap.h"
#include "sector_map.h"
#include "unit.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/str.h"

#include <cstdint>
#include <list>
#include <new>
#include <string>

namespace {
const char* const kTrapName = "CTrapObject";
const char* const kMagicTrapName = "CMagicTrapObject";
// The script the trap runs when set off (x, y, z).
const char* const kTrapScript = "Worldscript( CreateParticleSource( 100, %g, %g, %g, 1, \"Mushroom\"))";
constexpr float kTriggerRadius = 1.0f;
} // namespace

bool LockKey::AcceptedByTrap() const { return true; }

CTrapObject::CTrapObject() {
    CreateMapObject();
    flags_ &= ~MapObjectFlag::NotDrawn;
}

int CTrapObject::CreateTrap() {
    int result = CreateMapObject();
    flags_ &= ~MapObjectFlag::NotDrawn;
    return result;
}

int CTrapObject::UpdateTrap() {
    Update();
    if (!(flags_ & MapObjectFlag::Armed)) return 0;
    if (!WorldContext()->IsServer()) return 0;
    std::list<CUnit*> units{};
    auto* world = ServerWorld();
    world->aiMap_.sectorMap.FindUnitsInRadius(&localPosition_, &units, kTriggerRadius);
    if ((flags_ & MapObjectFlag::ArmedFresh) && units.empty()) {
        flags_ &= ~MapObjectFlag::ArmedFresh;
    } else if (!units.empty()) {
        std::string line;
        float x = localPosition_.x;
        float y = localPosition_.y;
        double z = world->RawSurfaceHeightAt(x, y);
        line = str::Format(kTrapScript, static_cast<double>(x), static_cast<double>(y), z);
        TheMissionScript()->Load(&line, 1);
        flags_ &= ~MapObjectFlag::Armed;
    }
    units.clear();
    return 0;
}

bool CTrapObject::Disarm(const LockKey* key) {
    if (!(flags_ & MapObjectFlag::Armed) || !key->AcceptedByTrap()) return false;
    flags_ &= ~MapObjectFlag::Armed;
    return true;
}

int CTrapObject::KeyFits(const LockKey* key) { return key->AcceptedByTrap(); }

void CTrapObject::LoadTrapObject(CStream* stream) {
    LoadMapObject(stream);
    stream->ReadValue(savedValue_);
}

void CTrapObject::SaveTrapObject(CStream* stream) {
    SaveMapObject(stream);
    std::uint32_t value = savedValue_;
    stream->WriteValue(value);
}

void CTrapObject::Load(CStream* stream) { LoadTrapObject(stream); }
void CTrapObject::Save(CStream* stream) { SaveTrapObject(stream); }
bool CTrapObject::ApplyScience(const LockKey* key, CEngineObject*) { return Disarm(key); }

bool CTrapObject::Slot29True() { return true; }

bool CTrapObject::Slot38One() { return true; }

const char* CTrapObject::ClassName() { return kTrapName; }
ClassId CTrapObject::GetClassId() { return ClassId::TrapObject; }
const char* MagicTrap::ClassName() { return kMagicTrapName; }
ClassId MagicTrap::GetClassId() { return ClassId::MagicTrap; }
