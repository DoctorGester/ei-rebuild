// Placed traps (trap_object.h) and the magic trap's class slots and deleting destructor.
#include "trap_object.h"

#include "core_streams.h"
#include "magic_trap.h"
#include "sector_map.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include <new>
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "game_data.h"
#include "game_singletons.h"

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
constexpr int kBlockSize = 10;
} // namespace

BOOL LockKey::AcceptedByTrap() const { return 1; }
EI_REWRITTEN(0x00522AD0, LockKey::AcceptedByTrap);

CTrapObject::CTrapObject() {
    CreateMapObject();
    flags_ &= ~kFlag40;
}
EI_REWRITTEN_SPECIAL(0x00529A80, CTrapObject::CTrapObject);
EI_REWRITTEN_SPECIAL(0x00529B30, CTrapObject::~CTrapObject);   // deleting

int CTrapObject::CreateTrap() {
    int result = CreateMapObject();
    flags_ &= ~kFlag40;
    return result;
}
EI_REWRITTEN(0x00529B60, CTrapObject::CreateTrap);

int CTrapObject::UpdateTrap() {
    Update();
    if (!(flags_ & kArmed)) return 0;
    if (!WorldContext()->IsServer()) return 0;
    mfc::CTypedPtrList<CUnit> units{mfc::impl::PtrListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, kBlockSize};
    auto* world = ServerWorld();
    world->Sectors()->FindUnitsInRadius(&localPosition_, &units, kTriggerRadius);
    if ((flags_ & kArmedFresh) && units.count == 0) {
        flags_ &= ~kArmedFresh;
    } else if (units.count != 0) {
        mfc::CString line;
        float x = localPosition_.x;
        float y = localPosition_.y;
        double z = world->RawSurfaceHeightAt(x, y);
        line.Format(kTrapScript, static_cast<double>(x), static_cast<double>(y), z);
        TheMissionScript()->Load(&line, 1);
        flags_ &= ~kArmed;
    }
    units.RemoveAll();
    return 0;
}
EI_REWRITTEN(0x00529B80, CTrapObject::UpdateTrap);

bool CTrapObject::Disarm(const LockKey* key, int) {
    if (!(flags_ & kArmed) || !key->AcceptedByTrap()) return false;
    flags_ &= ~kArmed;
    return true;
}
EI_REWRITTEN(0x00529D30, CTrapObject::Disarm);

int CTrapObject::KeyFits(const LockKey* key) { return key->AcceptedByTrap(); }
EI_REWRITTEN(0x00529D60, CTrapObject::KeyFits);

void CTrapObject::LoadTrapObject(CStream* stream) {
    LoadMapObject(stream);
    stream->Read(&savedValue_, 4);
}
EI_REWRITTEN(0x00529D70, CTrapObject::LoadTrapObject);

void CTrapObject::SaveTrapObject(CStream* stream) {
    SaveMapObject(stream);
    std::uint32_t value = savedValue_;
    stream->Write(&value, 4);
}
EI_REWRITTEN(0x00529DA0, CTrapObject::SaveTrapObject);

void CTrapObject::Load(CStream* stream) { LoadTrapObject(stream); }
void CTrapObject::Save(CStream* stream) { SaveTrapObject(stream); }
bool CTrapObject::ApplyScience(const LockKey* key, CEngineObject*) { return Disarm(key, 0); }

bool CTrapObject::Slot29True() { return true; }
EI_REWRITTEN(0x00529AE0, CTrapObject::Slot29True);

int CTrapObject::Slot38One() { return 1; }
EI_REWRITTEN(0x00529AF0, CTrapObject::Slot38One);

const char* CTrapObject::GetClassName() { return kTrapName; }
int CTrapObject::GetClassId() { return kClassTrap; }
const char* MagicTrap::GetClassName() { return kMagicTrapName; }
int MagicTrap::GetClassId() { return kClassMagicTrap; }

EI_REWRITTEN_SPECIAL(0x00529F70, MagicTrap::~MagicTrap);   // deleting
