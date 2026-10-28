// Small pieces of the script runtime that the rewritten script code reaches through
// the game's code (the party a script names is party_script.cpp): the group / command block /
// define list destructors, the AI map's sector map wrappers
// and the console's float output. Names are provisional.
#include "ai_map.h"
#include "console.h"
#include "script.h"
#include "sector_map.h"

#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "game/game_slots.h"

void ScriptGroup::DestructBody() {
    ei::SetVtable(this, mfc::impl::ObjectRefArrayClass::Vtable());   // CArray<ScriptObjectRef>'s base vtable
    if (!data) return;
    for (int i = 0; i < size; ++i) {
        if (data[i].object) data[i].object->Release();
        data[i].object = nullptr;
    }
    gamecrt::operator_delete(data);
}

void ScriptDeclDestruct(ScriptDecl* block) {
    mfc::CArray<SSCCmd>& commands = block->commands;
    ei::SetVtable(&commands, gameslots::CommandArrayClass::Vtable());   // CArray<SSCCmd>
    if (!commands.data) return;
    for (int i = 0; i < commands.size; ++i) ScriptCmdDestruct(&commands.data[i]);
    gamecrt::operator_delete(commands.data);
}

void ScriptDefinesDestruct(ScriptDefineList* defines) {
    ei::SetVtable(defines, gameslots::DefineListClass::Vtable());   // CList<ScriptDefine>
    defines->RemoveAll();
}

// ---- The AI map's sector map ------------------------------------------------------------------------

namespace {
CSectorMap* Sectors(CAIMap* map) { return &map->sectorMap; }
} // namespace

std::uint8_t CAIMap::SetRefreshUnits(int on) {
    std::uint8_t& flag = refreshUnits;
    std::uint8_t old = flag;
    flag = static_cast<std::uint8_t>(on);
    return old;
}

bool CAIMap::AddToSectors(CMapObject* object) { return Sectors(this)->Add(object); }

bool CAIMap::RemoveFromSectors(CMapObject* object) { return Sectors(this)->Remove(object); }

bool CAIMap::MoveInSectors(CMapObject* object, const Vector3* position) {
    return Sectors(this)->Move(object, position);
}

void CAIMap::FindUnitsInRadius(const Vector3* position, mfc::CTypedPtrList<CUnit>* result, float radius) {
    Sectors(this)->FindUnitsInRadius(position, result, radius);
}

// ---- Console output -------------------------------------------------------------------------------

console::GameOStream& console::GameOStream::PutFloatArgument(float f) {
    floatUsed = 1;
    return *this << static_cast<double>(f);
}
