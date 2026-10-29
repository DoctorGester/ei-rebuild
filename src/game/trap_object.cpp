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

TrapObject::TrapObject() {
    CreateMapObject();
    flags &= ~MapObjectFlag::NotDrawn;
}

void TrapObject::Create() {
    CreateMapObject();
    flags &= ~MapObjectFlag::NotDrawn;
}

bool TrapObject::RunTick() {
    Update();
    if (!(flags & MapObjectFlag::Armed)) return false;
    if (!WorldContext()->IsServer()) return false;
    auto* world = ServerWorld();
    std::list<CUnit*> units = world->aiMap.sectorMap.FindUnitsInRadius(localPosition, kTriggerRadius);
    if ((flags & MapObjectFlag::ArmedFresh) && units.empty()) {
        flags &= ~MapObjectFlag::ArmedFresh;
    } else if (!units.empty()) {
        std::string line;
        float x = localPosition.x;
        float y = localPosition.y;
        double z = world->RawSurfaceHeightAt(x, y);
        line = str::Format(kTrapScript, static_cast<double>(x), static_cast<double>(y), z);
        TheMissionScript()->Load(&line, 1);
        flags &= ~MapObjectFlag::Armed;
    }
    units.clear();
    return false;
}

bool TrapObject::Disarm(const LockKey* key) {
    if (!(flags & MapObjectFlag::Armed) || !key->AcceptedByTrap()) return false;
    flags &= ~MapObjectFlag::Armed;
    return true;
}

int TrapObject::KeyFits(const LockKey* key) { return key->AcceptedByTrap(); }

void TrapObject::LoadTrapObject(ByteStream* stream) {
    LoadMapObject(stream);
    stream->ReadValue(savedValue);
}

void TrapObject::SaveTrapObject(ByteStream* stream) {
    SaveMapObject(stream);
    std::uint32_t value = savedValue;
    stream->WriteValue(value);
}

void TrapObject::Load(ByteStream* stream) { LoadTrapObject(stream); }
void TrapObject::Save(ByteStream* stream) { SaveTrapObject(stream); }
bool TrapObject::ApplyScience(const LockKey* key, EngineObject*) { return Disarm(key); }

const char* TrapObject::ClassName() { return kTrapName; }
ClassId TrapObject::GetClassId() { return ClassId::TrapObject; }
const char* MagicTrap::ClassName() { return kMagicTrapName; }
ClassId MagicTrap::GetClassId() { return ClassId::MagicTrap; }
