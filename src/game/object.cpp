#include "object.h"

#include "ei/hook.h"
#include "figure_mesh.h"
#include "scene_loader.h"
#include "unit.h"


namespace {
const char* const kHooverName = "CHoover";
constexpr int kClassHoover = 0x1F;
} // namespace

CHoover::CHoover() { ClearFields(); }
EI_REWRITTEN_SPECIAL(0x00461C10, CHoover::CHoover);

CHoover::~CHoover() {}
EI_REWRITTEN_SPECIAL(0x00461C80, CHoover::~CHoover);
EI_REWRITTEN_SPECIAL(0x00461C60, CHoover::~CHoover);

int CHoover::Create() { return 1; }
EI_REWRITTEN(0x00461C90, CHoover::Create);
const char* CHoover::GetClassName() { return kHooverName; }
EI_REWRITTEN(0x00461C40, CHoover::GetClassName);
int CHoover::GetClassId() { return kClassHoover; }
EI_REWRITTEN(0x00461C50, CHoover::GetClassId);

// --- Unit and figure getters --------------------------------------------------------------------

const char* __fastcall UnitGetName(const CUnit* unit) { return unit->scriptName.GetString(); }
EI_REWRITTEN(0x00446820, UnitGetName);

int __fastcall UnitGetFlag278(const CUnit* unit) { return static_cast<int>(unit->obstacle); }
EI_REWRITTEN(0x00446890, UnitGetFlag278);

int __fastcall UnitGetFlag274(const CUnit* unit) { return unit->dead; }
EI_REWRITTEN(0x00446880, UnitGetFlag274);

float __fastcall UnitGetValue328(const CUnit* unit) { return unit->unitRadius; }
EI_REWRITTEN(0x004468A0, UnitGetValue328);

const char* __fastcall FigureGetName(const CEngineObject* figure) {
    return static_cast<const CFigureMesh*>(figure)->name_.GetString();
}
EI_REWRITTEN(0x00453440, FigureGetName);

const char* __fastcall FakeFigureGetName(const CEngineObject* figure) {
    return static_cast<const scene::SceneMesh*>(figure)->name.GetString();
}
EI_REWRITTEN(0x004547C0, FakeFigureGetName);
