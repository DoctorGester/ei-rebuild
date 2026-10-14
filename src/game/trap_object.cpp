// Placed traps (trap_object.h) and the magic trap's class slots and deleting destructor (A-U6).
#include "trap_object.h"

#include "core_streams.h"
#include "magic_trap.h"
#include "sector_map.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_script.h"

namespace {
void* const kTrapVtable = reinterpret_cast<void*>(0x0073E548);
const char* const kTrapName = reinterpret_cast<const char*>(0x0078F724);        // "CTrapObject"
const char* const kMagicTrapName = reinterpret_cast<const char*>(0x0078F774);   // "CMagicTrapObject"
// 0078F730: the script the trap runs when set off (x, y, z).
const char* const kTrapScript = reinterpret_cast<const char*>(0x0078F730);
void* MissionScript() { return reinterpret_cast<void*>(0x007C27D4); }
constexpr int kClassTrap = 0x55;
constexpr int kClassMagicTrap = 0x61;
constexpr std::uint32_t kFlag40 = 0x40;
constexpr std::uint32_t kArmed = 0x80000;
constexpr std::uint32_t kArmedFresh = 0x100000;
constexpr float kTriggerRadius = 1.0f;   // 0073E174
constexpr int kBlockSize = 10;
} // namespace

BOOL LockKey::AcceptedByTrap() const { return 1; }
EI_REPLACE_METHOD(0x00522AD0, LockKey::AcceptedByTrap);

CTrapObject* CTrapObject::Construct() {
    CMapObject::Construct();
    *reinterpret_cast<void**>(this) = kTrapVtable;
    CreateMapObject();
    flags_ &= ~kFlag40;
    return this;
}
EI_REPLACE_METHOD(0x00529A80, CTrapObject::Construct);

CTrapObject* CTrapObject::DeletingDestruct(int flags) {
    CMapObject::DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00529B30, CTrapObject::DeletingDestruct);

int CTrapObject::CreateTrap() {
    int result = CreateMapObject();
    flags_ &= ~kFlag40;
    return result;
}
EI_REPLACE_METHOD(0x00529B60, CTrapObject::CreateTrap);

int CTrapObject::UpdateTrap() {
    Update();
    if (!(flags_ & kArmed)) return 0;
    if (!static_cast<CWorld*>(ManagerContext())->IsServer()) return 0;
    mfc::CTypedPtrList<CEngineObject> units{mfc::PtrListVtable(), nullptr, nullptr, 0, nullptr, nullptr, kBlockSize};
    auto* world = static_cast<CWorldServer*>(ManagerContext());
    world->Sectors()->FindUnitsInRadius(&position, &units, kTriggerRadius);
    if ((flags_ & kArmedFresh) && units.count == 0) {
        flags_ &= ~kArmedFresh;
    } else if (units.count != 0) {
        mfc::CString line;
        float x = position.x;
        float y = position.y;
        double z = world->RawSurfaceHeightAt(x, y);
        line.Format(kTrapScript, static_cast<double>(x), static_cast<double>(y), z);
        ScriptLoadLine(MissionScript(), nullptr, &line, 1);
        flags_ &= ~kArmed;
    }
    units.RemoveAll();
    return 0;
}
EI_REPLACE_METHOD(0x00529B80, CTrapObject::UpdateTrap);

void CTrapObject::Arm() { flags_ |= kArmed | kArmedFresh; }
EI_REPLACE_METHOD(0x00529D20, CTrapObject::Arm);

bool CTrapObject::Disarm(const LockKey* key, int) {
    if (!(flags_ & kArmed) || !key->AcceptedByTrap()) return false;
    flags_ &= ~kArmed;
    return true;
}
EI_REPLACE_METHOD(0x00529D30, CTrapObject::Disarm);

int CTrapObject::KeyFits(const LockKey* key) { return key->AcceptedByTrap(); }
EI_REPLACE_METHOD(0x00529D60, CTrapObject::KeyFits);

void CTrapObject::LoadTrapObject(CStream* stream) {
    LoadMapObject(stream);
    stream->Read(&savedValue_, 4);
}
EI_REPLACE_METHOD(0x00529D70, CTrapObject::LoadTrapObject);

void CTrapObject::SaveTrapObject(CStream* stream) {
    SaveMapObject(stream);
    std::uint32_t value = savedValue_;
    stream->Write(&value, 4);
}
EI_REPLACE_METHOD(0x00529DA0, CTrapObject::SaveTrapObject);

bool CTrapObject::Slot29True() { return true; }
EI_REPLACE_METHOD(0x00529AE0, CTrapObject::Slot29True);

int CTrapObject::Slot38One() { return 1; }
EI_REPLACE_METHOD(0x00529AF0, CTrapObject::Slot38One);

const char* __fastcall TrapClassName(CMapObject*) { return kTrapName; }
EI_REPLACE(0x00529B00, TrapClassName);
int __fastcall TrapClassId(CMapObject*) { return kClassTrap; }
EI_REPLACE(0x00529B10, TrapClassId);
const char* __fastcall MagicTrapClassName(CMapObject*) { return kMagicTrapName; }
EI_REPLACE(0x00529F50, MagicTrapClassName);
int __fastcall MagicTrapClassId(CMapObject*) { return kClassMagicTrap; }
EI_REPLACE(0x00529F60, MagicTrapClassId);

MagicTrap* MagicTrap::DeletingDestruct(int flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00529F70, MagicTrap::DeletingDestruct);
