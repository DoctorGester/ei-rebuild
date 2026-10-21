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

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_script.h"
#include "unit_ai.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

using namespace scriptarray;

// The instantiations' ConstructElements: bindings zeroed (004CCA50); each nested call zeroed with a
// new SSCCmd (004CD130); each definition with its arrays' vtables (004CEB80); looks zeroed (004CDE10).
using DefinitionArray = mfc::impl::ArrayAbi<ScriptDefinition, gamelists::DefinitionElements>;

// The element destructors the inlined SetSize runs when the array empties or shrinks (neither
// happens in Add or SetAtGrow, whose new size is above the old one).
void DestroySubCalls(char* first, int count) {
    for (auto* call = reinterpret_cast<ScriptSubCall*>(first); count > 0; --count, ++call) ScriptSubCallDestruct(call);
}
void DestroyDefinitions(char* first, int count) {
    for (auto* definition = reinterpret_cast<ScriptDefinition*>(first); count > 0; --count, ++definition)
        ScriptDefinitionDestruct(definition);
}

const ElementKind kBindings{sizeof(ScriptBinding), ConstructAs<mfc::impl::Plain12>, nullptr, nullptr};
const ElementKind kSubCalls{sizeof(ScriptSubCall), ConstructAs<recordarrays::SubCalls>, DestroySubCalls, DestroySubCalls};
const ElementKind kDefinitions{sizeof(ScriptDefinition), ConstructAs<DefinitionArray>, DestroyDefinitions, DestroyDefinitions};
const ElementKind kLooks{sizeof(AILook), ConstructAs<mfc::impl::Plain16>, nullptr, nullptr};

// Grows `array` by one element and returns the old size (the new element's index).
template <typename T>
int Append(mfc::CArray<T>* array, const ElementKind& kind) {
    int index = array->size;
    InlineSetSize(Raw(array), index + 1, kind);
    return index;
}

} // namespace

int ScriptBindings::Add(const ScriptBinding& binding) {
    int index = Append(this, kBindings);
    data[index] = binding;
    return index;
}
EI_REPLACE_METHOD(0x004D5B50, ScriptBindings::Add);

int ScriptSubCalls::Add(const ScriptSubCall& call) {
    int index = Append(this, kSubCalls);
    ScriptSubCall& element = data[index];
    element.type = call.type;
    element.argIndex = call.argIndex;
    element.cmd->ScriptCmdAssign(call.cmd);
    return index;
}
EI_REPLACE_METHOD(0x004D5CB0, ScriptSubCalls::Add);

int ScriptCommands::Add(const SSCCmd& cmd) {
    int index = Append(this, kCmds);
    data[index].ScriptCmdAssign(&cmd);
    return index;
}
EI_REPLACE_METHOD(0x004D5ED0, ScriptCommands::Add);

void ScriptCommands::SetAtGrow(int index, const SSCCmd& cmd) {
    if (index >= size) InlineSetSize(Raw(this), index + 1, kCmds);
    data[index].ScriptCmdAssign(&cmd);
}
EI_REPLACE_METHOD(0x004D65E0, ScriptCommands::SetAtGrow);

int ScriptDefinitions::Add(const ScriptDefinition& definition) {
    int index = Append(this, kDefinitions);
    data[index].ScriptDefinitionAssign(&definition);   // inlined in the original
    return index;
}
EI_REPLACE_METHOD(0x004D6100, ScriptDefinitions::Add);

void __fastcall ScriptSubCallDestruct(ScriptSubCall* call) {
    SSCCmd* cmd = call->cmd;
    if (cmd) {
        ScriptCmdDestruct(cmd);
        gamecrt::operator_delete(cmd);
    }
}
EI_REPLACE(0x004C5480, ScriptSubCallDestruct);

template <typename T>
int ScriptNamedValues<T>::FindName(const char* name) const {
    for (int i = 0; i < names.size; ++i) {
        if (gamecrt::mbscmp(static_cast<const char*>(names.data[i]),
                            name) == 0)
            return i;
    }
    return -1;
}
template int ScriptNamedValues<float>::FindName(const char*) const;
template int ScriptNamedValues<mfc::CString>::FindName(const char*) const;
template int ScriptNamedValues<ScriptObjectRef>::FindName(const char*) const;
template int ScriptNamedValues<ScriptGroup>::FindName(const char*) const;
EI_REPLACE_METHOD(0x004D5A50, ScriptNamedValues<float>::FindName);
EI_REPLACE_METHOD(0x004D5A90, ScriptNamedValues<mfc::CString>::FindName);
EI_REPLACE_METHOD(0x004D5AD0, ScriptNamedValues<ScriptObjectRef>::FindName);
EI_REPLACE_METHOD(0x004D5B10, ScriptNamedValues<ScriptGroup>::FindName);

ScriptCommands* ScriptCommands::ConstructEmpty() {
    data = nullptr;
    growBy = 0;
    maxSize = 0;
    size = 0;
    vtable = scriptvtable::kCmds;
    return this;
}
EI_REPLACE_METHOD(0x004D5A10, ScriptCommands::ConstructEmpty);

ScriptIfThens* ScriptIfThens::ConstructEmpty() {
    data = nullptr;
    growBy = 0;
    maxSize = 0;
    size = 0;
    vtable = scriptvtable::kIfThen;
    return this;
}
EI_REPLACE_METHOD(0x004D5A30, ScriptIfThens::ConstructEmpty);

void ScriptStrings::SetAtGrow(int index, const mfc::CString& text) {
    if (index >= size) InlineSetSize(Raw(this), index + 1, kStrings);
    data[index] = text;
}
EI_REPLACE_METHOD(0x004CE5F0, ScriptStrings::SetAtGrow);

void ScriptStrings::CopyTo(int index, mfc::CString* out) const {
    mfc::CString copy(data[index]);   // the by-value GetAt (004C2680)
    *out = copy;
}
EI_REPLACE_METHOD(0x004C2F80, ScriptStrings::CopyTo);

void AILooks::SetAtGrow(int index, const AILook& look) {
    if (index >= size) InlineSetSize(Raw(this), index + 1, kLooks);
    data[index] = look;
}
EI_REPLACE_METHOD(0x004CDCB0, AILooks::SetAtGrow);
