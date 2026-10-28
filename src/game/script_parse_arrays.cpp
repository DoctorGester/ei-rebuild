// The CArray members the script loader calls out of line (MFC template code the game compiled for its
// own element types): Add for bindings, nested calls, commands and definitions, SetAtGrow for
// commands, the nested call's destructor body, the per-type variable name lookups and two array
// constructors; also the parameter names' and the look queue's SetAtGrow and a string GetAt copy.
// Each Add / SetAtGrow inlines CArray::SetSize(n, -1) (scriptarray::InlineSetSize with the
// instantiation's element helpers) and the element's assignment operator. See script.h.
#include "script.h"
#include "script_array.h"
#include "game_lists.h"
#include "record_arrays.h"
#include "game_arrays.h"

#include "lib/crt.h"

#include "unit_ai.h"

#include <string>

namespace {

using namespace scriptarray;

// The instantiations' ConstructElements: bindings zeroed; each nested call zeroed with a
// new SSCCmd; each definition with its arrays' vtables; looks zeroed.

// The element destructors the inlined SetSize runs when the array empties or shrinks (neither
// happens in Add or SetAtGrow, whose new size is above the old one).
constexpr RangeFn DestroySubCalls = DestroyEach<ScriptSubCall, ScriptSubCallDestruct>;
constexpr RangeFn DestroyDefinitions = DestroyEach<ScriptDefinition, ScriptDefinitionDestruct>;

const ElementKind kBindings{ConstructAs<mfc::impl::Plain12>, nullptr, nullptr};
const ElementKind kSubCalls{ConstructAs<recordarrays::SubCalls>, DestroySubCalls, DestroySubCalls};
const ElementKind kDefinitions{ConstructAs<mfc::impl::ArrayAbi<ScriptDefinition, gamelists::DefinitionElements>>, DestroyDefinitions, DestroyDefinitions};
const ElementKind kLooks{ConstructAs<mfc::impl::Plain16>, nullptr, nullptr};

// Grows `array` by one element and returns the old size (the new element's index).
template <typename T>
int Append(mfc::CArray<T>* array, const ElementKind& kind) {
    int index = array->size;
    InlineSetSize(array, index + 1, kind);
    return index;
}

} // namespace

int ScriptBindings::Add(const ScriptBinding& binding) {
    int index = Append(this, kBindings);
    data[index] = binding;
    return index;
}

int ScriptSubCalls::Add(const ScriptSubCall& call) {
    int index = Append(this, kSubCalls);
    ScriptSubCall& element = data[index];
    element.type = call.type;
    element.argIndex = call.argIndex;
    element.cmd->ScriptCmdAssign(call.cmd);
    return index;
}

int ScriptCommands::Add(const SSCCmd& cmd) {
    int index = Append(this, kCmds);
    data[index].ScriptCmdAssign(&cmd);
    return index;
}

void ScriptCommands::SetAtGrow(int index, const SSCCmd& cmd) {
    if (index >= size) InlineSetSize(this, index + 1, kCmds);
    data[index].ScriptCmdAssign(&cmd);
}

int ScriptDefinitions::Add(const ScriptDefinition& definition) {
    int index = Append(this, kDefinitions);
    data[index].ScriptDefinitionAssign(&definition);
    return index;
}

void __fastcall ScriptSubCallDestruct(ScriptSubCall* call) {
    SSCCmd* cmd = call->cmd;
    if (cmd) {
        ScriptCmdDestruct(cmd);
        gamecrt::operator_delete(cmd);
    }
}

template <typename T>
int ScriptNamedValues<T>::FindName(const char* name) const {
    for (int i = 0; i < names.size; ++i) {
        if (gamecrt::mbscmp(names.data[i].c_str(), name) == 0)
            return i;
    }
    return -1;
}
template int ScriptNamedValues<float>::FindName(const char*) const;
template int ScriptNamedValues<std::string>::FindName(const char*) const;
template int ScriptNamedValues<ScriptObjectRef>::FindName(const char*) const;
template int ScriptNamedValues<ScriptGroup>::FindName(const char*) const;

ScriptCommands* ScriptCommands::ConstructEmpty() {
    data = nullptr;
    growBy = 0;
    maxSize = 0;
    size = 0;
    vtable = recordarrays::SubCallArrayClass::Vtable();   // the loader arrays
    return this;
}

ScriptIfThens* ScriptIfThens::ConstructEmpty() {
    data = nullptr;
    growBy = 0;
    maxSize = 0;
    size = 0;
    vtable = mfc::impl::ScriptIfThenPairsClass::Vtable();
    return this;
}

void ScriptStrings::SetAtGrow(int index, const std::string& text) {
    if (index >= size) SetSize(index + 1, -1);
    data[index] = text;
}

void ScriptStrings::CopyTo(int index, std::string* out) const {
    std::string copy = data[index];   // the by-value GetAt
    *out = copy;
}

void AILooks::SetAtGrow(int index, const AILook& look) {
    if (index >= size) InlineSetSize(this, index + 1, kLooks);
    data[index] = look;
}
