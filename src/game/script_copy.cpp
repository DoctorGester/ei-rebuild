// Assignment operators of the script types, member by member
// through the CArray instantiations' Copy / SetSize: SSCCmd, ScriptVars,
// ScriptRun, ScriptDecl and the FX source list element. Each
// returns `this`. Also the copy constructors (ScriptVars, names,
// object / group values) and the by-value GetAt bodies of SSCCmd
// and ScriptSubCall. See script.h.
#include "script.h"
#include "game_arrays.h"
#include "record_arrays.h"

#include "lib/crt.h"

#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "ei/x87.h"

#include <memory>

namespace {

// CArray members: SetSize (int, growBy) and Copy (const CArray&).

// The float array resized (the instantiation's SetSize), then each value copied (the
// source's size re-read).
void CopyFloats(mfc::CArray<float>* to, const mfc::CArray<float>* from) {
    to->SetSize(from->size, -1);
    for (int i = 0; i < from->size; ++i) std::memcpy(&to->data[i], &from->data[i], sizeof(float));
}

// A plain array resized (object, group arguments, bindings), then each
// element copied as raw words.
template <typename T>
void CopyPlain(mfc::CArray<T>* to, const mfc::CArray<T>* from) {
    to->SetSize(from->size, -1);
    for (int i = 0; i < from->size; ++i) to->data[i] = from->data[i];
}

// ScriptVars::operator= as inlined into ScriptRun::operator= and out of line.
void AssignVars(ScriptVars* to, const ScriptVars* from) {
    CopyFloats(&to->floats, &from->floats);
    to->floatNames.CopyFrom(from->floatNames);
    to->strings.CopyFrom(from->strings);
    to->stringNames.CopyFrom(from->stringNames);
    to->objects.CopyFrom(from->objects);
    to->objectNames.CopyFrom(from->objectNames);
    to->groups.CopyFrom(from->groups);
    to->groupNames.CopyFrom(from->groupNames);
}

} // namespace

ScriptVars* ScriptVars::ScriptVarsAssign(const ScriptVars* other) {
    AssignVars(this, other);
    return this;
}

ScriptRun* ScriptRun::ScriptRunAssign(const ScriptRun* other) {
    block = other->block;
    index = other->index;
    ticks = other->ticks;
    sleepStart = other->sleepStart;
    killFlag = other->killFlag;
    AssignVars(&vars, &other->vars);
    return this;
}

SSCCmd* SSCCmd::ScriptCmdAssign(const SSCCmd* other) {
    id = other->id;
    CopyFloats(&floatArgs, &other->floatArgs);
    stringArgs.CopyFrom(other->stringArgs);
    CopyPlain(&objectArgs, &other->objectArgs);
    CopyPlain(&groupArgs, &other->groupArgs);
    CopyPlain(&localBindings, &other->localBindings);
    CopyPlain(&globalBindings, &other->globalBindings);
    subCalls.CopyFrom(other->subCalls);   // nested commands deep-copied
    calledScript = other->calledScript;
    return this;
}

ScriptDecl* ScriptDecl::ScriptDeclAssign(const ScriptDecl* other) {
    commands.CopyFrom(other->commands);
    id = other->id;
    return this;
}

// The FX source element: the first two words, the name assigned, the rest copied as words.
ScriptFxSource* ScriptFxSource::ScriptFxSourceAssign(const ScriptFxSource* other) {
    object = other->object;
    id = other->id;
    name = other->name;
    std::memcpy(static_cast<void*>(&position), &other->position, sizeof(ScriptFxSource) - offsetof(ScriptFxSource, position));
    return this;
}

// ---- Copy constructors ---------------------------------------------------------------------------
// Member by member: each array's
// base constructor (all fields 0, base vtable) then the derived vtable, the instantiation's
// SetSize to the source's size and an element loop (floats through the FPU, strings and references
// through GetAt temporaries). Two are the bodies of CArray::GetAt instantiations that return an
// element by value (SSCCmd, ScriptSubCall; on the source array, the
// result's address and the index on the stack). The others are reached from the CList RemoveHead
// instantiations of the CScript reset.

namespace {

template <typename T>
void InitArray(mfc::CArray<T>* a, void* vtable) {
    a->data = nullptr;
    a->size = 0;
    a->maxSize = 0;
    a->growBy = 0;
    a->vtable = vtable;
}

// A string array constructed empty, resized, each element assigned.
void CopyConstructStrings(mfc::CArray<std::string>* to, const mfc::CArray<std::string>* from) {
    InitArray(to, gamearrays::ScriptStringValuesClass::Vtable());
    to->SetSize(from->size, -1);
    for (int i = 0; i < from->size; ++i) to->data[i] = from->data[i];
}

// SSCCmd's copy constructor (inlined into both GetAt bodies).
void CopyConstructCmd(SSCCmd* to, const SSCCmd* from) {
    to->id = from->id;
    InitArray(&to->floatArgs, mfc::impl::ArgFloatArrayClass::Vtable());
    CopyFloats(&to->floatArgs, &from->floatArgs);
    CopyConstructStrings(&to->stringArgs, &from->stringArgs);
    InitArray(&to->objectArgs, mfc::impl::ArgObjectsClass::Vtable());
    CopyPlain(&to->objectArgs, &from->objectArgs);
    InitArray(&to->groupArgs, mfc::impl::ArgGroupsClass::Vtable());
    CopyPlain(&to->groupArgs, &from->groupArgs);
    to->localBindings.CopyConstruct(from->localBindings);
    to->globalBindings.CopyConstruct(from->globalBindings);
    to->subCalls.CopyConstruct(from->subCalls);
    to->calledScript = from->calledScript;
}

void Release(CEngineObject* object) {
    if (object) object->Release();
}

} // namespace

SSCCmd* ScriptCommands::CopyAt(SSCCmd* out, int index) const {
    CopyConstructCmd(out, &data[index]);
    return out;
}

// The nested call's command is deep-copied into a new one; when operator new fails, the copy's
// command is null.
ScriptSubCall* ScriptSubCalls::CopyAt(ScriptSubCall* out, int index) const {
    const ScriptSubCall* from = &data[index];
    out->type = from->type;
    out->argIndex = from->argIndex;
    auto* cmd = gamecrt::operator_new_as<SSCCmd>();
    if (!cmd) {
        out->cmd = nullptr;
        return out;
    }
    CopyConstructCmd(cmd, from->cmd);
    out->cmd = cmd;
    return out;
}

// The by-value GetAt, constructed at `out`.
std::string* ScriptStrings::CopyAt(std::string* out, int index) const {
    return std::construct_at(out, data[index]);
}

void ScriptStrings::CopyFrom(const ScriptStrings& source) {
    SetSize(source.size, -1);
    for (int i = 0; i < source.size; ++i) data[i] = source.data[i];
}

// A name array: storage for exactly the source's size (the inlined SetSize from empty), then each
// name assigned.
ScriptStrings* ScriptStrings::CopyConstruct(const ScriptStrings* other) {
    InitArray(this, gamearrays::ScriptStringValuesClass::Vtable());
    int n = other->size;
    if (n == 0) {
        maxSize = 0;
        size = 0;
    } else {
        data = gamecrt::operator_new_array<std::string>(static_cast<std::size_t>(n));
        mfc::PlainElements<std::string>::Construct(data, n);
        maxSize = n;
        size = n;
    }
    for (int i = 0; i < other->size; ++i) data[i] = other->data[i];
    return this;
}

// Object values: each slot drops its (null) object and takes the GetAt copy's object with a
// reference of its own; the copy is released.
template <> ScriptNamedValues<ScriptObjectRef>* ScriptNamedValues<ScriptObjectRef>::CopyConstruct(const ScriptNamedValues<ScriptObjectRef>* other) {
    InitArray(&values, mfc::impl::ScriptGroupClass::Vtable());
    mfc::impl::ArrayAs<gamearrays::ObjectRefs>(values).SetSize(other->values.size, -1);
    for (int i = 0; i < other->values.size; ++i) {
        ScriptObjectRef value;
        other->values.CopyAt(&value, i);
        ScriptObjectRef* slot = &values.data[i];
        Release(slot->object);
        slot->object = nullptr;
        slot->object = value.object;
        if (value.object) ++value.object->refCount_;
        Release(value.object);
    }
    CopyConstructStrings(&names, &other->names);
    return this;
}

// Group values: each group sized to the GetAt copy's and its members copied in place; the copy's
// destructor inlined (each reference destroyed, the array freed).
template <> ScriptNamedValues<ScriptGroup>* ScriptNamedValues<ScriptGroup>::CopyConstruct(const ScriptNamedValues<ScriptGroup>* other) {
    InitArray(&values, gamearrays::ScriptGroupValuesClass::Vtable());
    mfc::impl::ArrayAs<gamearrays::Groups>(values).SetSize(other->values.size, -1);
    for (int i = 0; i < other->values.size; ++i) {
        ScriptGroup copy;
        other->values.CopyAt(&copy, i);
        ScriptGroup* group = &values.data[i];
        group->SetSize(copy.size, -1);
        for (int k = 0; k < copy.size; ++k) {
            ScriptObjectRef member;
            copy.CopyAt(&member, k);
            group->data[k].Reset();
            group->data[k].Construct(member);
            member.Reset();
        }
        copy.vtable = mfc::impl::ObjectRefArrayClass::Vtable();
        if (copy.data) {
            for (int k = 0; k < copy.size; ++k) copy.data[k].DestroyElement(0);
            gamecrt::operator_delete(copy.data);
        }
    }
    CopyConstructStrings(&names, &other->names);
    return this;
}

// ScriptVars' copy constructor: names, string values (through GetAt), objects through the
// reference helpers, groups assigned from GetAt copies.
ScriptVars* ScriptVars::ScriptVarsCopyConstruct(const ScriptVars* other) {
    InitArray(&floats, mfc::impl::ScriptFloatValuesClass::Vtable());
    CopyFloats(&floats, &other->floats);
    floatNames.CopyConstruct(&other->floatNames);
    CopyConstructStrings(&strings, &other->strings);
    stringNames.CopyConstruct(&other->stringNames);

    InitArray(&objects, mfc::impl::ScriptGroupClass::Vtable());
    mfc::impl::ArrayAs<gamearrays::ObjectRefs>(objects).SetSize(other->objects.size, -1);
    for (int i = 0; i < other->objects.size; ++i) {
        ScriptObjectRef value;
        other->objects.CopyAt(&value, i);
        ScriptObjectRef* slot = &objects.data[i];
        slot->Reset();
        slot->Construct(value);
        value.Reset();
    }
    objectNames.CopyConstruct(&other->objectNames);

    InitArray(&groups, gamearrays::ScriptGroupValuesClass::Vtable());
    mfc::impl::ArrayAs<gamearrays::Groups>(groups).SetSize(other->groups.size, -1);
    for (int i = 0; i < other->groups.size; ++i) {
        ScriptGroup copy;
        other->groups.CopyAt(&copy, i);
        groups.data[i].CopyFrom(copy);
        copy.DestructBody();
    }
    groupNames.CopyConstruct(&other->groupNames);
    return this;
}

// ScriptVars' default constructor: each array's base constructor
// (all fields 0, base vtable) then its derived vtable, in member order; returns the set.
ScriptVars* ScriptVars::ScriptVarsConstruct() {
    InitArray(&floats, mfc::impl::ScriptFloatValuesClass::Vtable());
    InitArray(&floatNames, gamearrays::ScriptStringValuesClass::Vtable());
    InitArray(&strings, gamearrays::ScriptStringValuesClass::Vtable());
    InitArray(&stringNames, gamearrays::ScriptStringValuesClass::Vtable());
    InitArray(&objects, mfc::impl::ScriptGroupClass::Vtable());
    InitArray(&objectNames, gamearrays::ScriptStringValuesClass::Vtable());
    InitArray(&groups, gamearrays::ScriptGroupValuesClass::Vtable());
    InitArray(&groupNames, gamearrays::ScriptStringValuesClass::Vtable());
    return this;
}

// Float values: the values through their SetSize and the FPU, then the names as
// ScriptNamesCopyConstruct builds them.
template <> ScriptNamedValues<float>* ScriptNamedValues<float>::CopyConstruct(const ScriptNamedValues<float>* other) {
    InitArray(&values, mfc::impl::ScriptFloatValuesClass::Vtable());
    CopyFloats(&values, &other->values);
    names.CopyConstruct(&other->names);
    return this;
}

// String values: values and names each through GetAt copies.
template <> ScriptNamedValues<std::string>* ScriptNamedValues<std::string>::CopyConstruct(const ScriptNamedValues<std::string>* other) {
    CopyConstructStrings(&values, &other->values);
    CopyConstructStrings(&names, &other->names);
    return this;
}

// ScriptDefinition::operator=: the pairs read straight from the source (no GetAt), the variables as
// ScriptVars::operator=.
ScriptDefinition* ScriptDefinition::ScriptDefinitionAssign(const ScriptDefinition* other) {
    index = other->index;
    name = other->name;
    params = other->params;
    ifThen.SetSize(other->ifThen.size, -1);
    for (int i = 0; i < other->ifThen.size; ++i) ifThen.data[i] = other->ifThen.data[i];
    AssignVars(&vars, &other->vars);
    return this;
}
