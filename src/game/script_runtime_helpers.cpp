// Small pieces of the script runtime the rewritten script code still called through their
// original addresses (A-S4; the party a script names, 004BAEC0, is party_script.cpp): the group / command block /
// define list destructors (004B5EF0, 004BF7E0, 004D6340), the AI map's sector map wrappers
// (004BA090-004BA100) and the console's float output (004BA9D0). Names are provisional.
#include "ai_map.h"
#include "console.h"
#include "script.h"
#include "sector_map.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ai.h"
#include "originals_library.h"

namespace {

void SetVtable(void* object, std::uintptr_t vtable) { ei::SetVtable(object, ei::GamePointer<const void>(vtable)); }


} // namespace

void ScriptGroup::DestructBody() {
    SetVtable(this, 0x0073D11C);   // CArray<ScriptObjectRef>'s base vtable
    if (!data) return;
    for (int i = 0; i < size; ++i) {
        if (data[i].object) data[i].object->Release();
        data[i].object = nullptr;
    }
    gamecrt::operator_delete(data);
}
EI_REPLACE_METHOD(0x004B5EF0, ScriptGroup::DestructBody);

void __fastcall ScriptDeclDestruct(ScriptDecl* block) {
    mfc::CArray<SSCCmd>& commands = block->commands;
    SetVtable(&commands, 0x0073D3D0);   // CArray<SSCCmd>
    if (!commands.data) return;
    for (int i = 0; i < commands.size; ++i) ScriptCmdDestruct(&commands.data[i]);
    gamecrt::operator_delete(commands.data);
}
EI_REPLACE(0x004BF7E0, ScriptDeclDestruct);

void __fastcall ScriptDefinesDestruct(ScriptDefineList* defines) {
    SetVtable(defines, 0x0073D4C0);   // CList<ScriptDefine>
    defines->RemoveAll();
}
EI_REPLACE(0x004D6340, ScriptDefinesDestruct);

// ---- The AI map's sector map ------------------------------------------------------------------------

namespace {
CSectorMap* Sectors(CAIMap* map) { return &map->sectorMap; }
} // namespace

std::uint8_t CAIMap::SetRefreshUnits(int on) {
    auto& flag = reinterpret_cast<std::uint8_t&>(refreshUnits);
    std::uint8_t old = flag;
    flag = static_cast<std::uint8_t>(on);
    return old;
}
EI_REPLACE_METHOD(0x004BA090, CAIMap::SetRefreshUnits);

bool CAIMap::AddToSectors(CEngineObject* object) { return Sectors(this)->Add(object); }
EI_REPLACE_METHOD(0x004BA0A0, CAIMap::AddToSectors);

bool CAIMap::RemoveFromSectors(CEngineObject* object) { return Sectors(this)->Remove(object); }
EI_REPLACE_METHOD(0x004BA0C0, CAIMap::RemoveFromSectors);

bool CAIMap::MoveInSectors(CEngineObject* object, const Vector3* position) {
    return SectorMapMove(Sectors(this), nullptr, object, position);
}
EI_REPLACE_METHOD(0x004BA0E0, CAIMap::MoveInSectors);

void CAIMap::FindUnitsInRadius(const Vector3* position, mfc::CTypedPtrList<CUnit>* result, float radius) {
    Sectors(this)->FindUnitsInRadius(position, result, radius);
}
EI_REPLACE_METHOD(0x004BA100, CAIMap::FindUnitsInRadius);

// ---- Console output -------------------------------------------------------------------------------

console::GameOStream& console::GameOStream::PutFloatArgument(float f) {
    floatUsed_ = 1;
    return *this << static_cast<double>(f);
}
EI_REPLACE_METHOD(0x004BA9D0, console::GameOStream::PutFloatArgument);
