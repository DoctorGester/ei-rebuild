// Small methods of CUnit (A-U12): the class name and id, the motion given its speeds, the player
// test, twice the height above the centre and a worn armour piece. See unit.h.
#include "unit.h"

#include "ai_map.h"      // AIPath
#include "ai_motion.h"   // UnitMotion

#include "ei/hook.h"

namespace {
constexpr std::uintptr_t kUnitClassName = std::uintptr_t(0x0078F910);   // "CUnit"
constexpr int kUnitClassId = 0x50;
} // namespace

// 0052E280 / 0052E290 (slots 11 / 12 of the unit's vtable 0073EA8C).
const char* __fastcall UnitClassName(CUnit*) { return reinterpret_cast<const char*>(kUnitClassName); }
EI_REPLACE(0x0052E280, UnitClassName);
int __fastcall UnitClassId(CUnit*) { return kUnitClassId; }
EI_REPLACE(0x0052E290, UnitClassId);

// 0052E230: the constructor of the motion's segment part (+0x50; nothing to construct). Returns
// it. Three bytes (too short to hook).
void* __fastcall MotionSegmentConstruct(void* self) { return self; }
EI_REPLACE(0x0052E230, MotionSegmentConstruct);

void CUnit::SetMotionSpeeds(float speed, float turnRate) {
    AIPath* path = Controller()->Path();
    path->ForgetObject(id_);
    UnitMotion* unitMotion = Motion();
    unitMotion->segment = -1;
    unitMotion->speedScale = speed;
    unitMotion->baseTurnRate = turnRate;
    unitMotion->Build(path);
}
EI_REPLACE_METHOD(0x0052EA40, CUnit::SetMotionSpeeds);

int CUnit::HasPlayer() const { return player != nullptr; }
EI_REPLACE_METHOD(0x005301A0, CUnit::HasPlayer);

double CUnit::DoubleHeight() const { return above.z + above.z; }
EI_REPLACE_METHOD(0x00530560, CUnit::DoubleHeight);

CEngineObject* CUnit::WornItem(int slot) const { return armor[slot]; }
EI_REPLACE_METHOD(0x00530830, CUnit::WornItem);
