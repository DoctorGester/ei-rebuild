#include "object.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "figure_mesh.h"
#include "scene_loader.h"
#include "unit.h"

// --- CHoover ------------------------------------------------------------------------------------

CHoover* CHoover::Construct() {
    ClearFields();
    ei::SetVtable(this, Vtable());
    return this;
}
EI_REWRITTEN(0x00461C10, CHoover::Construct);

void CHoover::Destruct() {
    ei::SetVtable(this, Vtable());
    DestructBase();
}
EI_REWRITTEN(0x00461C80, CHoover::Destruct);

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
