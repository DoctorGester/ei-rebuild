// Small methods of CUnit: the class name and id, the motion given its speeds, the player test, twice the
// height above the centre and a worn armour piece. See unit.h.
#include "ai_map.h"      // AIPath
#include "ai_motion.h"   // UnitMotion
#include "unit.h"

void CUnit::SetMotionSpeeds(float speed, float turnRate) {
    PathOrder* path = Controller()->Path();
    path->ForgetObject(id_);
    UnitMotion* unitMotion = &motion;
    unitMotion->segment = -1;
    unitMotion->speedScale = speed;
    unitMotion->baseTurnRate = turnRate;
    unitMotion->Build(path);
}

int CUnit::HasPlayer() const { return player != nullptr; }

double CUnit::DoubleHeight() const { return bounds.boxMax.z + bounds.boxMax.z; }

CItemArmor* CUnit::WornItem(int slot) const { return armor[slot]; }
