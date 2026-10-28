// The CArray<T> instantiations of game records (record_arrays.h).
#include "game/record_arrays.h"

#include "lib/static_object.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>

namespace recordarrays {

// ---- Scene records -------------------------------------------------------------------------------


// ---- The script's command arrays ------------------------------------------------------------------

namespace {

// The storage of a copy constructor: exactly `n` elements, built by the instantiation's
// ConstructElements; none when `n` is 0.
template <typename Abi>
void AllocateExact(typename Abi::Array* self, int n) {
    using T = typename Abi::Element;
    if (n) {
        self->data = gamecrt::operator_new_array<T>(static_cast<std::size_t>(n));
        Abi::ConstructElements(self->data, n);
    }
    self->maxSize = n;
    self->size = n;
}

// Each nested call of `self` (as many as the source has) assigned from a GetAt copy of the source's:
// type and argument, the command by SSCCmd's assignment, the copy's command destroyed and deleted.
void AssignSubCalls(ScriptSubCalls* self, const ScriptSubCalls* source) {
    for (int i = 0; i < source->size; ++i) {
        ScriptSubCall copy;
        source->CopyAt(&copy, i);
        ScriptSubCall& to = self->data[i];
        to.type = copy.type;
        to.argIndex = copy.argIndex;
        to.cmd->ScriptCmdAssign(copy.cmd);
        if (copy.cmd) {
            ScriptCmdDestruct(copy.cmd);
            gamecrt::operator_delete(copy.cmd);
        }
    }
}

} // namespace

void InitSubCall(ScriptSubCall* p) {
    p->type = 0;
    p->argIndex = 0;
    auto* cmd = gamecrt::operator_new_as<SSCCmd>();
    p->cmd = cmd ? cmd->Construct() : nullptr;
}

} // namespace recordarrays

// ---- Members of the script command arrays (script.h) ----------------------------------------------

// The game assigns the commands member by member (the float arguments through SetSize
// and dword copies); SSCCmd's assignment copies them through the FPU, the same values.
void ScriptCommands::CopyFrom(const ScriptCommands& source) {
    mfc::impl::ArrayAs<recordarrays::Cmds>(*this).SetSize(source.size, -1);
    for (int i = 0; i < source.size; ++i) {
        lib::InPlace<SSCCmd> storage;
        auto* copy = storage.Get();
        source.CopyAt(copy, i);
        data[i].ScriptCmdAssign(copy);
        ScriptCmdDestruct(copy);
    }
}

void ScriptSubCalls::CopyFrom(const ScriptSubCalls& source) {
    mfc::impl::ArrayAs<recordarrays::SubCalls>(*this).SetSize(source.size, -1);
    recordarrays::AssignSubCalls(this, &source);
}

ScriptBindings* ScriptBindings::CopyConstruct(const ScriptBindings& source) {
    Construct(recordarrays::BindingArrayClass::Vtable());
    recordarrays::AllocateExact<recordarrays::Bindings>(&mfc::impl::ArrayAs<recordarrays::Bindings>(*this), source.size);
    for (int i = 0; i < source.size; ++i) data[i] = source.data[i];
    return this;
}

ScriptSubCalls* ScriptSubCalls::CopyConstruct(const ScriptSubCalls& source) {
    Construct(recordarrays::SubCallArrayClass::Vtable());
    recordarrays::AllocateExact<recordarrays::SubCalls>(&mfc::impl::ArrayAs<recordarrays::SubCalls>(*this), source.size);
    recordarrays::AssignSubCalls(this, &source);
    return this;
}


