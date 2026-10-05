#include "object.h"

#include "ei/hook.h"
#include "ei/vcall.h"

// --- Forwarders (not rewritten) ----------------------------------------------------------------

__declspec(naked) void __fastcall UnitSetHostileMask(CUnit*, void*, std::uint32_t) EI_ORIGINAL(0x0052E970)
__declspec(naked) void __fastcall UnitAddHostileMask(CUnit*, void*, std::uint32_t) EI_ORIGINAL(0x0052E940)

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

// --- CMemoryStream ------------------------------------------------------------------------------

void CMemoryStream::Reset() {
    ok_ = 1;
    data_ = nullptr;
    unknown_1C = 0;
    position_ = 0;
    size_ = 0;
    growBy_ = 0x80;
    unknown_14 = -1;
}
EI_REPLACE_METHOD(0x00468C20, CMemoryStream::Reset);
