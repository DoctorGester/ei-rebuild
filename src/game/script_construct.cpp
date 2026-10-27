// The CScript constructor and CScript::GetError. See script.h and docs/notes/script.md.
// The constructor runs for the world's two scripts only, from the world's constructor.
#include "script.h"

#include "world_glue.h"

#include "ei/hook.h"
#include "game_lists.h"
#include "game/game_slots.h"
#include "game/record_arrays.h"

#include <cstring>
#include <memory>
#include <new>

namespace {


template <typename T>
void ConstructArray(mfc::CArray<T>* array, void* vtable) {
    array->vtable = vtable;
    array->data = nullptr;
    array->growBy = 0;
    array->maxSize = 0;
    array->size = 0;
}

// A CList constructed empty with the script lists' block size.
template <typename T>
void ConstructList(mfc::CList<T>* list, void* vtable) {
    list->vtable = vtable;
    list->count = 0;
    list->free = nullptr;
    list->tail = nullptr;
    list->head = nullptr;
    list->blocks = nullptr;
    list->blockSize = 10;
}

} // namespace

CScript* CScript::Construct() {
    ConstructArray(&pointLights_, gamearrays::PointLightArrayClass::Vtable());
    ConstructList(&particles_, gameslots::ParticleSourceListClass::Vtable());
    ConstructList(&fxSources_, gameslots::FxSourceListClass::Vtable());
    ConstructList(&lightning_, gameslots::LightningListClass::Vtable());
    ConstructList(&blocks_, gameslots::DeclListClass::Vtable());
    ConstructArray(&definitions_, recordarrays::DefinitionArrayClass::Vtable());
    ScriptFloatsConstruct(NamedValues(&globals_.floats));
    ScriptStringsConstruct(NamedValues(&globals_.strings));
    ScriptObjectsConstruct(NamedValues(&globals_.objects));
    ScriptGroupsConstruct(NamedValues(&globals_.groups));
    ConstructList(&instances_, gameslots::DefinitionListClass::Vtable());
    ConstructList(&runs_, gameslots::RunListClass::Vtable());
    ConstructList(&worldBlocks_, gameslots::DeclListClass::Vtable());
    ConstructArray(&areas_, mfc::impl::ScriptAreaArrayClass::Vtable());
    ConstructList(&tempGroups_, gameslots::GroupListClass::Vtable());
    ConstructList(&tempObjects_, gameslots::ObjectRefListClass::Vtable());
    std::construct_at(&errorText_);

    // 256 point light slots, zeroed (again) after the array made them.
    pointLights_.SetSize(0x100, -1);
    std::memset(pointLights_.data, 0, static_cast<std::size_t>(pointLights_.size) * sizeof(ScriptPointLight));

    // The evaluator's temporaries start with one empty group and one null reference.
    ScriptGroup group;
    ConstructArray(static_cast<mfc::CArray<ScriptObjectRef>*>(&group), mfc::impl::ScriptGroupClass::Vtable());
    // An empty group and a null reference at the temporary lists' heads.
    tempGroups_.PushHead(&group);
    static_cast<ScriptGroup*>(&group)->DestructBody();
    ScriptObjectRef none{nullptr};
    tempObjects_.PushHead(&none);
    none.Reset();

    ScriptReset(this);
    return this;
}
EI_REWRITTEN(0x004BDE40, CScript::Construct);

ScriptErrorInfo* CScript::GetError(ScriptErrorInfo* out) {
    std::construct_at(&out->text, errorText_);
    out->code = errorCode_;
    out->line = errorLine_;
    return out;
}
EI_REWRITTEN(0x00456360, CScript::GetError);
