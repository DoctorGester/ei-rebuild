// Small methods of CUnit: the class name and id, the motion given its speeds, the player test, twice the
// height above the centre and a worn armour piece. See unit.h.
#include "unit.h"

#include "ai_map.h"      // AIPath
#include "ai_motion.h"   // UnitMotion

#include "ei/hook.h"

#undef GetClassName

void CUnit::SetMotionSpeeds(float speed, float turnRate) {
    PathOrder* path = Controller()->Path();
    path->ForgetObject(id_);
    UnitMotion* unitMotion = Motion();
    unitMotion->segment = -1;
    unitMotion->speedScale = speed;
    unitMotion->baseTurnRate = turnRate;
    unitMotion->Build(path);
}
EI_REWRITTEN(0x0052EA40, CUnit::SetMotionSpeeds);

int CUnit::HasPlayer() const { return player != nullptr; }
EI_REWRITTEN(0x005301A0, CUnit::HasPlayer);

double CUnit::DoubleHeight() const { return bounds.boxMax.z + bounds.boxMax.z; }
EI_REWRITTEN(0x00530560, CUnit::DoubleHeight);

CItemArmor* CUnit::WornItem(int slot) const { return armor[slot]; }
EI_REWRITTEN(0x00530830, CUnit::WornItem);
