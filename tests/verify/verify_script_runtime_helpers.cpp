// Checks for script_runtime_helpers.cpp. Scenarios: script_exec (AddUnitToParty / CopyItems name
// parties; the AI map wrappers move and place units; the console prints floats with #...),
// script_copy / world_load (definitions, groups and command blocks destroyed at reset and load).
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/console.h"
#include "../../src/game/party_records.h"
#include "../../src/game/script.h"

#include <cstring>

// 004BAEC0: MOV EAX,FS:[0] (6 bytes). The original on a copy of the name first; party and the
// shortened name compared.
static void* (__fastcall* s_partyForScript)(void*, mfc::CString*);
static void* __fastcall PartyForScriptVerify(void* player, mfc::CString* name) {
    mfc::CString copy(*name);
    void* expected = s_partyForScript(player, &copy);
    void* actual = PartyForScript(player, name);
    ei::verify::Check("PartyForScript", expected, actual, "%p");
    ei::verify::Check("PartyForScript name", 0, std::strcmp(copy, *name), "%d");
    return actual;
}
EI_VERIFY(0x004BAEC0, PartyForScriptVerify, 6, s_partyForScript);

// 004B5EF0 / 004BF7E0 / 004D6340 (ours only): the base vtable written; the list emptied.
static void (__fastcall* s_groupBody)(ScriptGroup*);
static void __fastcall GroupBodyVerify(ScriptGroup* group) {
    group->DestructBody();
    ei::verify::Check("ScriptGroup::DestructBody vtable", std::uintptr_t(0x0073D11C),
                      reinterpret_cast<std::uintptr_t>(group->vtable), "%08X");
}
EI_VERIFY(0x004B5EF0, GroupBodyVerify, 0, s_groupBody);

static void (__fastcall* s_declDestruct)(ScriptDecl*);
static void __fastcall DeclDestructVerify(ScriptDecl* block) {
    ScriptDeclDestruct(block);
    ei::verify::Check("ScriptDeclDestruct vtable", std::uintptr_t(0x0073D3D0),
                      reinterpret_cast<std::uintptr_t>(block->commands.vtable), "%08X");
}
EI_VERIFY(0x004BF7E0, DeclDestructVerify, 0, s_declDestruct);

static void (__fastcall* s_defineListDestruct)(ScriptDefineList*);
static void __fastcall DefineListDestructVerify(ScriptDefineList* list) {
    ScriptDefinesDestruct(list);
    ei::verify::Check("ScriptDefinesDestruct", true,
                      !list->head && !list->tail && !list->count && !list->free && !list->blocks, "emptied");
}
EI_VERIFY(0x004D6340, DefineListDestructVerify, 0, s_defineListDestruct);

// 004BA090: MOV DL,[ESP+4]; MOV AL,[ECX+2] (7 bytes). The original first, the flag put back, then
// ours; result and flag compared.
static std::uint8_t (__fastcall* s_setRefreshUnits)(CAIMap*, void*, int);
static std::uint8_t __fastcall SetRefreshUnitsVerify(CAIMap* map, void*, int on) {
    auto& flag = reinterpret_cast<std::uint8_t&>(map->refreshUnits);
    std::uint8_t before = flag;
    std::uint8_t expected = s_setRefreshUnits(map, nullptr, on);
    std::uint8_t expectedFlag = flag;
    flag = before;
    std::uint8_t actual = map->SetRefreshUnits(on);
    ei::verify::Check("SetRefreshUnits", expected, actual, "%u");
    ei::verify::Check("SetRefreshUnits flag", expectedFlag, flag, "%u");
    return actual;
}
EI_VERIFY(0x004BA090, SetRefreshUnitsVerify, 7, s_setRefreshUnits);

// 004BA0A0 / 004BA0C0 / 004BA0E0 / 004BA100 (ours only): they pass on to the sector map (its own
// wrappers check it); the results are booleans.
static bool (__fastcall* s_addToSectors)(CAIMap*, void*, CEngineObject*);
static bool __fastcall AddToSectorsVerify(CAIMap* map, void*, CEngineObject* object) {
    bool r = map->AddToSectors(object);
    ei::verify::Check("AddToSectors", true, r == 0 || r == 1, "bool");
    return r;
}
EI_VERIFY(0x004BA0A0, AddToSectorsVerify, 0, s_addToSectors);

static bool (__fastcall* s_removeFromSectors)(CAIMap*, void*, CEngineObject*);
static bool __fastcall RemoveFromSectorsVerify(CAIMap* map, void*, CEngineObject* object) {
    bool r = map->RemoveFromSectors(object);
    ei::verify::Check("RemoveFromSectors", true, r == 0 || r == 1, "bool");
    return r;
}
EI_VERIFY(0x004BA0C0, RemoveFromSectorsVerify, 0, s_removeFromSectors);

static bool (__fastcall* s_moveInSectors)(CAIMap*, void*, CEngineObject*, const Vector3*);
static bool __fastcall MoveInSectorsVerify(CAIMap* map, void*, CEngineObject* object, const Vector3* position) {
    bool r = map->MoveInSectors(object, position);
    ei::verify::Check("MoveInSectors", true, r == 0 || r == 1, "bool");
    return r;
}
EI_VERIFY(0x004BA0E0, MoveInSectorsVerify, 0, s_moveInSectors);

static void (__fastcall* s_findUnits)(CAIMap*, void*, const Vector3*, void*, float);
static void __fastcall FindUnitsVerify(CAIMap* map, void*, const Vector3* position, void* result, float radius) {
    map->FindUnitsInRadius(position, result, radius);
}
EI_VERIFY(0x004BA100, FindUnitsVerify, 0, s_findUnits);

// 004BA9D0 (ours only): returns the stream.
static console::GameOStream* (__fastcall* s_putFloat)(console::GameOStream*, void*, float);
static console::GameOStream* __fastcall PutFloatVerify(console::GameOStream* stream, void*, float f) {
    console::GameOStream* r = &stream->PutFloatArgument(f);
    ei::verify::Check("PutFloatArgument", stream, r, "%p");
    return r;
}
EI_VERIFY(0x004BA9D0, PutFloatVerify, 0, s_putFloat);
