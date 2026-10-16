#include "object.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_units.h"
#include "unit.h"

// --- Forwarders (not rewritten) ----------------------------------------------------------------

void __fastcall UnitSetHostileMask(CUnit* a0, void*, std::uint32_t a2) { static_cast<CUnit*>((void*)a0)->SetHostileMask(a2); }

// --- CHoover ------------------------------------------------------------------------------------

CHoover* CHoover::Construct() {
    ClearFields();
    ei::SetVtable(this, kVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x00461C10, CHoover::Construct);

void CHoover::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    DestructBase();
}
EI_REPLACE_METHOD(0x00461C80, CHoover::Destruct);

// --- Unit and figure getters --------------------------------------------------------------------

const char* __fastcall UnitGetName(const CUnit* unit) { return FieldAt<const char*>(unit, unitfield::kName); }
EI_REPLACE(0x00446820, UnitGetName);

int __fastcall UnitGetFlag278(const CUnit* unit) { return FieldAt<int>(unit, unitfield::kFlag278); }
EI_REPLACE(0x00446890, UnitGetFlag278);

int __fastcall UnitGetFlag274(const CUnit* unit) { return FieldAt<int>(unit, unitfield::kFlag274); }
EI_REPLACE(0x00446880, UnitGetFlag274);

float __fastcall UnitGetValue328(const CUnit* unit) { return FieldAt<float>(unit, unitfield::kValue328); }
EI_REPLACE(0x004468A0, UnitGetValue328);

const char* __fastcall FigureGetName(const CEngineObject* figure) { return FieldAt<const char*>(figure, 0x120); }
EI_REPLACE(0x00453440, FigureGetName);

const char* __fastcall FakeFigureGetName(const CEngineObject* figure) { return FieldAt<const char*>(figure, 0x94); }
EI_REPLACE(0x004547C0, FakeFigureGetName);
