// Clearing parts of a CScript while the world runs: dropping references to deleted world objects
// (after every object removal), and the zone reset when the party enters a zone (instances and runs
// dropped, effects removed, globals zeroed, the WorldScript blocks started again). See script.h and
// docs/notes/script.md.
#include "script.h"
#include "world_effect.h"

#include "lib/static_object.h"
#include "lib/crt.h"
#include "game_lists.h"
#include "world.h"
#include "game_singletons.h"
#include "game_arrays.h"

#include "lib/mfc_array.h"

#include <cstring>
#include <memory>

namespace {

// CList::RemoveAll as inlined: each element destroyed in place, the list emptied, its blocks freed.
template <typename T, typename Destroy>
void RemoveAll(mfc::CList<T>& list, Destroy destroy) {
    for (auto* node = list.head; node; node = node->next) destroy(&node->data);
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    mfc::PlexFreeDataChain(list.blocks);
    list.blocks = nullptr;
}

// The group variables (names, then values) of a removed instance or run.
void DestroyGroupVariables(ScriptVars& vars) {
    ScriptNamesDestruct(&vars.groupNames);
    mfc::impl::DestructArrayOf<gamearrays::GroupArrayClass>(mfc::impl::ArrayAs<gamearrays::Groups>(vars.groups));
}

} // namespace

// Called after every removal of a world object: the runs', the instances' and the global
// variables drop deleted objects (object variables released, group members removed), then the
// evaluation temporaries are freed.
void __fastcall ScriptPurgeDeletedObjects(CScript* script) {
    for (auto* node = script->runs_.head; node;) {
        auto* next = node->next;
        node->data.vars.RemoveDeletedObjects();
        node = next;
    }
    for (auto* node = script->instances_.head; node;) {
        auto* next = node->next;
        node->data.vars.RemoveDeletedObjects();
        node = next;
    }
    script->globals_.RemoveDeletedObjects();
    ScriptFreeTemporaries(script);
}

// Every instance and every run removed from the head of its list and destroyed.
void __fastcall ScriptDropInstancesAndRuns(CScript* script) {
    while (script->instances_.count != 0) {
        lib::InPlace<ScriptDefinition> element;
        script->instances_.PopHead(element.Get());
        auto* instance = element.Get();
        ScriptVars& vars = instance->vars;
        DestroyGroupVariables(vars);
        ScriptObjectsDestruct(NamedValues<ScriptObjectRef>(&vars.objects));
        ScriptStringsDestruct(NamedValues<std::string>(&vars.strings));
        ScriptFloatsDestruct(NamedValues(&vars.floats));
        mfc::impl::DestructHeader<mfc::impl::ScriptIfThenArrayClass>(&instance->ifThen);
        std::destroy_at(&instance->params);
        std::destroy_at(&instance->name);
    }
    while (script->runs_.count != 0) {
        lib::InPlace<ScriptRun> element;
        script->runs_.PopHead(element.Get());
        ScriptVars& vars = element.Get()->vars;
        DestroyGroupVariables(vars);
        ScriptNamesDestruct(&vars.objectNames);
        vars.objects.vtable = mfc::impl::ObjectRefArrayClass::Vtable();
        if (vars.objects.data) {
            gamearrays::ObjectRefs::DestructElements(vars.objects.data, vars.objects.size);
            gamecrt::operator_delete(vars.objects.data);
        }
        ScriptNamesDestruct(&vars.stringNames);
        ScriptNamesDestruct(&vars.strings);
        ScriptNamesDestruct(&vars.floatNames);
        vars.floats.vtable = mfc::impl::ScriptFloatsClass::Vtable();
        if (vars.floats.data) gamecrt::operator_delete(vars.floats.data);
    }
}

// When the party enters a zone: instances and runs dropped, the For depth and the
// temporaries reset, every effect removed (point light slots zeroed; FX source, particle source and
// lightning lists emptied), the global values reset, and every WorldScript block run again.
void __fastcall ScriptResetZone(CScript* script) {
    ScriptDropInstancesAndRuns(script);
    script->forDepth_ = 0;
    ScriptFreeTemporaries(script);

    mfc::CArray<ScriptPointLight>& lights = script->pointLights_;
    for (int i = 0; i < lights.size; ++i) {
        if (CEngineObject* object = lights.data[i].object) {
            TheServerWorld()->RemoveTimedObject(object->id_);
            lights.data[i].object->Release();
        }
    }
    if (lights.size) std::memset(lights.data, 0, static_cast<unsigned>(lights.size) * sizeof(ScriptPointLight));

    RemoveAll(script->fxSources_, [](ScriptFxSource* source) { source->ScriptFxSourceDestruct(); });
    RemoveAll(script->particles_, [](ScriptParticleSource* source) {
        if (CEngineObject* object = source->object) {
            TheServerWorld()->RemoveTimedObject(object->id_);
            source->object->Release();
        }
        source->object = nullptr;
    });
    RemoveAll(script->lightning_, [](ScriptLightning* bolt) { bolt->ScriptLightningDestruct(); });

    script->globals_.ResetValues();
    for (auto* node = script->worldBlocks_.head; node;) {
        auto* next = node->next;
        script->RunWorldScript(&node->data);
        node = next;
    }
}
