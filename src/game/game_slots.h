#pragma once

// The scalar deleting destructors (and the destructors they call) of the MFC collections whose
// elements the game destroys through its own code (ticket S6a; the library-only ones are
// lib/mfc_slots.h's). Registered in game_slots.cpp; checked by tests/verify/verify_mfc_slots.cpp.

#include "lib/mfc_slots.h"
#include "game/game_arrays.h"
#include "game/game_lists.h"
#include "game/script.h"

struct PlatformSet;   // game/world.h

namespace gameslots {

// The script's #define list (004D6340).
struct DefineListClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D4C0); }
};

// A script group's destructor body as a free function (004C2290).
void __fastcall GroupDestruct(ScriptGroup* group);

// 00561840: CBridgeManager's slot 3 (Destroy): the face list emptied (Clear's body without its 1).
void __fastcall BridgeManagerDestroy(PlatformSet* set);

// ---- Elements -------------------------------------------------------------------------------------

// A unit memory's 8-byte records: an object reference (released through slot 4, then cleared) and
// a value.
struct ObjectAndValue {
    ScriptObjectRef ref;
    int value;
};
static_assert(sizeof(ObjectAndValue) == 8);
inline void ReleaseObjectAndValue(ObjectAndValue* p) { gamearrays::ReleaseAndClear(&p->ref); }

// A map object client's 16-byte effect records: an engine object at +8 (released, left).
struct EffectObjectRecord {
    std::uint32_t head[2];
    CEngineObject* object;   // +0x08
    std::uint32_t tail;
};
static_assert(sizeof(EffectObjectRecord) == 0x10);
inline void ReleaseEffectObject(EffectObjectRecord* p) { gamearrays::ReleaseOnly(&p->object); }

inline void DestroyCommand(SSCCmd* p) { ScriptCmdDestruct(p); }
inline void DestroyDecl(ScriptDecl* p) { ScriptDeclDestruct(p); }

using mfc::impl::ArrayAbi;
using mfc::impl::EachElements;
using mfc::impl::ListAbi;

using ObjectValues = ArrayAbi<ObjectAndValue, EachElements<ObjectAndValue, nullptr, ReleaseObjectAndValue>>;
using EffectObjects = ArrayAbi<EffectObjectRecord, EachElements<EffectObjectRecord, nullptr, ReleaseEffectObject>>;
using Definitions = ArrayAbi<ScriptDefinition, EachElements<ScriptDefinition, nullptr, gamelists::DestroyDefinition>>;
using Commands = ArrayAbi<SSCCmd, EachElements<SSCCmd, nullptr, DestroyCommand>>;
using DeclList = ListAbi<ScriptDecl, EachElements<ScriptDecl, nullptr, DestroyDecl>>;
using GroupList = ListAbi<ScriptGroup, EachElements<ScriptGroup, nullptr, gamearrays::DestroyGroup>>;

// ---- The instantiations' vtables (game data) -------------------------------------------------------

struct ItemObjectListClass {   // a list of engine objects released through slot 4 (00427600)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073BE74); }
};
struct ClientObjectList2Class {   // the same in the client maps (00571F10)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F61C); }
};
struct ObjectRefListClass {   // CList<ScriptObjectRef>
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D448); }
};
struct VictimListClass {   // a magic trap's victims (counted references)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073E730); }
};
struct PointLightArrayClass {   // the script's point lights
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D3A0); }
};
struct DefinitionArrayClass {   // CArray<ScriptDefinition>
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D3B8); }
};
struct MemoryObjectArrayClass {   // a unit memory's object records
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F058); }
};
struct ClientObjectPtrArrayClass {   // the client maps' engine object pointers
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F53C); }
};
struct EffectObjectArrayClass {   // a map object client's effect records
    static void* Vtable() { return reinterpret_cast<void*>(0x0073DC48); }
};
struct RefListAClass {   // counted pointer lists of the world server (00598F40)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FA4C); }
};
struct RefListBClass {   // (00599D60)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F9D0); }
};
struct ParticleSourceListClass {   // the script's particle sources
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D388); }
};
struct FxSourceListClass {   // the script's FX sources
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D37C); }
};
struct LightningListClass {   // the script's lightnings
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D370); }
};
struct DeclListClass {   // CList<ScriptDecl>
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D364); }
};
struct DefinitionListClass {   // CList<ScriptDefinition>
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D34C); }
};
struct RunListClass {   // CList<ScriptRun>
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D340); }
};
struct GroupListClass {   // CList<ScriptGroup>
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D43C); }
};
struct ArmourSpellListClass {   // the armour spells (12-byte records holding an engine object)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F010); }
};
struct CommandArrayClass {   // CArray<SSCCmd> (a command block)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D3D0); }
};

} // namespace gameslots
