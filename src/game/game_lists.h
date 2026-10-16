#pragma once

// The CList<T> instantiations whose elements the game destroys through its own code (ticket S7):
// lib/mfc_list.h's templates over the game's element types, the element destructors ours (called
// directly). Registered in game_lists.cpp; checked in game by verify_mfc_list.cpp until S21d.

#include "lib/mfc_list.h"
#include "lib/stlp.h"
#include "game/core_refcount.h"
#include "game/game_arrays.h"
#include "game/sound.h"
#include "game/world.h"

void __fastcall RefPtrRelease(RefPtr* self);                  // refptr_copies.cpp
void __fastcall ObjectHolderRelease(CEngineObject** self);    // refptr_copies.cpp
void __fastcall ObjectHolderAssign(CEngineObject** self, void*, CEngineObject* object);

namespace gamelists {

using mfc::impl::Bytes;
using mfc::impl::EachElements;
using mfc::impl::ListAbi;

// Counted pointers released by their plain release (RefPtrRelease) or by the owner's (Destroy).
inline void ReleaseRef(RefPtr* p) { RefPtrRelease(p); }
inline void DestroyRef(RefPtr* p) { p->Destroy(); }
// 12-byte records whose first dword holds an engine object (released through slot 4).
inline void ReleaseHolder(Bytes<12>* p) { ObjectHolderRelease(reinterpret_cast<CEngineObject**>(p)); }
// The world's shared objects: the object array at +0x0C destroyed (00594950, 0059A100 inlined).
inline void DestroySharedObject(SharedObject* p) {
    gamearrays::ObjectPtrs::Destruct<gamearrays::ObjectPtrArrayBaseClass>(
        reinterpret_cast<gamearrays::ObjectPtrs::Array*>(&p->objects));
}
// The sound manager's wave records: the name an empty string with room for 8 bytes (006BE240, after
// the zero fill); its buffer freed (006B90C0).
inline stlp::string* WaveName(WaveRecord* p) { return reinterpret_cast<stlp::string*>(p->name); }
inline void InitWave(WaveRecord* p) { stlp::StringElement::Default(WaveName(p)); }
inline void DestroyWave(WaveRecord* p) { stlp::StringElement::Destroy(WaveName(p)); }

using EngineObjects = ListAbi<CEngineObject*, EachElements<CEngineObject*, nullptr, gamearrays::ReleaseOnly>>;
using ObjectRefs = ListAbi<ScriptObjectRef, EachElements<ScriptObjectRef, nullptr, gamearrays::ReleaseAndClear>>;
using Refs = ListAbi<RefPtr, EachElements<RefPtr, nullptr, ReleaseRef>>;
using OwnedRefs = ListAbi<RefPtr, EachElements<RefPtr, nullptr, DestroyRef>>;
using Holders = ListAbi<Bytes<12>, EachElements<Bytes<12>, nullptr, ReleaseHolder>>;
using SharedObjects = ListAbi<SharedObject, EachElements<SharedObject, nullptr, DestroySharedObject>>;
using Waves = ListAbi<WaveRecord, EachElements<WaveRecord, InitWave, DestroyWave>>;

// The script's effects (each leaves the world's timed objects and is released; the sound's name
// destroyed after), its tokens (the text destroyed), its definitions and runs.
inline void DestroyParticleSource(ScriptParticleSource* p) { p->ScriptParticleSourceDestruct(); }
inline void DestroyFxSource(ScriptFxSource* p) { p->ScriptFxSourceDestruct(); }
inline void DestroyLightning(ScriptLightning* p) { p->ScriptLightningDestruct(); }
inline void DestroyToken(ScriptToken* p) { p->text.~CString(); }
inline void DestroyDefinition(ScriptDefinition* p) { ScriptDefinitionDestruct(p); }
inline void DestroyRun(ScriptRun* p) { ScriptRunDestruct(p); }
// A command block (CScript +0x70 / +0x178): 004C54A0, the body of ScriptDeclDestruct.
inline void DestroyDecl(ScriptDecl* p) { ScriptDeclDestruct(p); }
// After the zero fill (ConstructElements 004CEB80 / 004CECB0): a definition's name and parameters
// empty, its if-then array (vtable 0073D430) and variables constructed, index 0; a run's variables.
void InitDefinition(ScriptDefinition* p);
void InitRun(ScriptRun* p);
using DefinitionElements = EachElements<ScriptDefinition, InitDefinition, DestroyDefinition>;
using RunElements = EachElements<ScriptRun, InitRun, DestroyRun>;

using ParticleSources = ListAbi<ScriptParticleSource, EachElements<ScriptParticleSource, nullptr, DestroyParticleSource>>;
using FxSources = ListAbi<ScriptFxSource, EachElements<ScriptFxSource, nullptr, DestroyFxSource>>;
using Lightnings = ListAbi<ScriptLightning, EachElements<ScriptLightning, nullptr, DestroyLightning>>;
using Tokens = ListAbi<ScriptToken, EachElements<ScriptToken, nullptr, DestroyToken>>;
using Definitions = ListAbi<ScriptDefinition, DefinitionElements>;
using Runs = ListAbi<ScriptRun, RunElements>;
using Decls = ListAbi<ScriptDecl, EachElements<ScriptDecl, nullptr, DestroyDecl>>;
using Groups = ListAbi<ScriptGroup, EachElements<ScriptGroup, gamearrays::InitGroup, gamearrays::DestroyGroup>>;

// The script lists' members with the game's copy semantics (ticket S7a). A RemoveHead / RemoveTail
// copy-constructs the element into `out` (RET 4, returns out) before the node is freed (the
// original copies into a temporary first, then into `out`: the same values). An AddHead / AddTail
// constructs the new node's element, then assigns `value` (RET 4, returns the node).
// 004C69B0: the command blocks (the commands through GetAt copies assigned into a new array, the id).
ScriptDecl* __fastcall DeclRemoveHead(Decls::List* self, void*, ScriptDecl* out);
// 004C7A10: the instances (index, name, parameters, the if-then pairs, the variables' copy constructor).
ScriptDefinition* __fastcall InstanceRemoveHead(Definitions::List* self, void*, ScriptDefinition* out);
// 004C82B0: the runs (the first five fields, the variables a value-and-names pair at a time).
ScriptRun* __fastcall RunRemoveHead(Runs::List* self, void*, ScriptRun* out);
// 004C7C90: an instance at the head (ScriptDefinition::operator=).
Definitions::Node* __fastcall InstanceAddHead(Definitions::List* self, void*, const ScriptDefinition* value);
// 004C9210: a temporary reference at the head (the new slot released, then shares the object).
ObjectRefs::Node* __fastcall ObjectRefAddHead(ObjectRefs::List* self, void*, const ScriptObjectRef* value);
// 004C9100: the last temporary reference (the object keeps one reference for `out`).
ScriptObjectRef* __fastcall ObjectRefRemoveTail(ObjectRefs::List* self, void*, ScriptObjectRef* out);
// 004C8CE0 / 004C8D80: a temporary group at the head / tail (an empty group, then ObjectRefCopy).
Groups::Node* __fastcall GroupAddHead(Groups::List* self, void*, const ScriptGroup* value);
Groups::Node* __fastcall GroupAddTail(Groups::List* self, void*, const ScriptGroup* value);
// 004C8AF0: the last temporary group (copied as GroupGetAt copies one).
ScriptGroup* __fastcall GroupRemoveTail(Groups::List* self, void*, ScriptGroup* out);

// 00599FA0 (RET 4): AddTail of a counted engine object (the node's slot zeroed, then assigned:
// the object gains a reference); returns the node.
EngineObjects::Node* __fastcall CountedAddTail(EngineObjects::List* self, void*, CEngineObject* object);

// The lists' vtables.
struct OwnedRefListClass {   // a map object controller's counted pointer list (005575F0)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F028); }
};
struct TokenListClass {   // the script parser's tokens (004CA260)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D424); }
};
struct SharedObjectListClass {   // the world server's shared objects (0059A3C0)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FA10); }
};

// CScript's lists (their destructors, S13b: 004C92C0, 004C8E20, 004C7560, 004C8520, 004C7E30,
// 004C6760, 004C6410, 004C6120).
struct TempObjectListClass {   // +0x1CC
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D448); }
};
struct TempGroupListClass {   // +0x1B0
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D43C); }
};
struct DeclListClass {   // +0x70 and +0x178
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D364); }
};
struct RunListClass {   // +0x15C
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D340); }
};
struct InstanceListClass {   // +0x140
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D34C); }
};
struct LightningListClass {   // +0x4C
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D370); }
};
struct FxSourceListClass {   // +0x30
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D37C); }
};
struct ParticleSourceListClass {   // +0x14
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D388); }
};

} // namespace gamelists
