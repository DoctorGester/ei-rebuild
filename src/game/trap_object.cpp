// Placed traps (trap_object.h) and the magic trap's class slots and deleting destructor.
#include "trap_object.h"

#include "core_streams.h"
#include "magic_trap.h"
#include "sector_map.h"
#include "unit.h"
#include "world.h"

#include <list>
#include <new>
#include <string>
#include "lib/crt.h"
#include "lib/mfc.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
const char* const kTrapName = "CTrapObject";
const char* const kMagicTrapName = "CMagicTrapObject";
// The script the trap runs when set off (x, y, z).
const char* const kTrapScript = "Worldscript( CreateParticleSource( 100, %g, %g, %g, 1, \"Mushroom\"))";
constexpr int kClassTrap = 0x55;
constexpr int kClassMagicTrap = 0x61;
constexpr std::uint32_t kFlag40 = 0x40;
constexpr std::uint32_t kArmed = 0x80000;
constexpr std::uint32_t kArmedFresh = 0x100000;
constexpr float kTriggerRadius = 1.0f;
} // namespace

bool LockKey::AcceptedByTrap() const { return true; }

CTrapObject::CTrapObject() {
    CreateMapObject();
    flags_ &= ~kFlag40;
}

int CTrapObject::CreateTrap() {
    int result = CreateMapObject();
    flags_ &= ~kFlag40;
    return result;
}

int CTrapObject::UpdateTrap() {
    Update();
    if (!(flags_ & kArmed)) return 0;
    if (!WorldContext()->IsServer()) return 0;
    std::list<CUnit*> units{};
    auto* world = ServerWorld();
    world->Sectors()->FindUnitsInRadius(&localPosition_, &units, kTriggerRadius);
    if ((flags_ & kArmedFresh) && units.empty()) {
        flags_ &= ~kArmedFresh;
    } else if (!units.empty()) {
        std::string line;
        float x = localPosition_.x;
        float y = localPosition_.y;
        double z = world->RawSurfaceHeightAt(x, y);
        line = str::Format(kTrapScript, static_cast<double>(x), static_cast<double>(y), z);
        TheMissionScript()->Load(&line, 1);
        flags_ &= ~kArmed;
    }
    units.clear();
    return 0;
}

bool CTrapObject::Disarm(const LockKey* key, int) {
    if (!(flags_ & kArmed) || !key->AcceptedByTrap()) return false;
    flags_ &= ~kArmed;
    return true;
}

int CTrapObject::KeyFits(const LockKey* key) { return key->AcceptedByTrap(); }

void CTrapObject::LoadTrapObject(CStream* stream) {
    LoadMapObject(stream);
    stream->Read(&savedValue_, 4);
}

void CTrapObject::SaveTrapObject(CStream* stream) {
    SaveMapObject(stream);
    std::uint32_t value = savedValue_;
    stream->Write(&value, 4);
}

void CTrapObject::Load(CStream* stream) { LoadTrapObject(stream); }
void CTrapObject::Save(CStream* stream) { SaveTrapObject(stream); }
bool CTrapObject::ApplyScience(const LockKey* key, CEngineObject*) { return Disarm(key, 0); }

bool CTrapObject::Slot29True() { return true; }

int CTrapObject::Slot38One() { return 1; }

const char* CTrapObject::GetClassName() { return kTrapName; }
int CTrapObject::GetClassId() { return kClassTrap; }
const char* MagicTrap::GetClassName() { return kMagicTrapName; }
int MagicTrap::GetClassId() { return kClassMagicTrap; }
