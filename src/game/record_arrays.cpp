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

void InitTheme(MusicTheme* p) {
    std::construct_at(&p->name);
    std::construct_at(&p->briefing);
    std::construct_at(&p->constructor);
    std::construct_at(&p->calm);
    std::construct_at(&p->combat);
}
void __fastcall DestroyTheme(MusicTheme* p) {
    std::destroy_at(&p->combat);
    std::destroy_at(&p->calm);
    std::destroy_at(&p->constructor);
    std::destroy_at(&p->briefing);
    std::destroy_at(&p->name);
}
void ThemeElements::Relocate(MusicTheme* to, MusicTheme* from, int count) {
    auto move = [](MusicTheme* target, MusicTheme* source) {
        std::memcpy(static_cast<void*>(target), source, sizeof(MusicTheme));
        std::construct_at(&target->name, std::move(source->name));
        std::construct_at(&target->briefing, std::move(source->briefing));
        std::construct_at(&target->constructor, std::move(source->constructor));
        std::construct_at(&target->calm, std::move(source->calm));
        std::construct_at(&target->combat, std::move(source->combat));
        std::destroy_at(&source->combat);
        std::destroy_at(&source->calm);
        std::destroy_at(&source->constructor);
        std::destroy_at(&source->briefing);
        std::destroy_at(&source->name);
    };
    if (to < from) {
        for (int i = 0; i < count; ++i) move(to + i, from + i);
    } else {
        for (int i = count; i-- > 0;) move(to + i, from + i);
    }
}

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

// The position and the waypoints are copied as the dwords they are.
mapspec::Patrol* mapspec::Patrols::CopyAt(Patrol* result, int index) const {
    const mapspec::Patrol& from = data[index];
    std::memcpy(&result->position, &from.position, sizeof(from.position));
    mfc::CArray<mapspec::Waypoint>& to = result->waypoints;
    to.Construct(mfc::impl::WaypointArrayClass::Vtable());
    int n = from.waypoints.size;
    if (n) {
        to.data = gamecrt::operator_new_array<mapspec::Waypoint>(static_cast<std::size_t>(n));
        mfc::PlainElements<mapspec::Waypoint>::Construct(to.data, n);
    }
    to.maxSize = n;
    to.size = n;
    for (int i = 0; i < from.waypoints.size; ++i) std::memcpy(&to.data[i], &from.waypoints.data[i], sizeof(mapspec::Waypoint));
    return result;
}

// The first ten dwords as they are, the target copy-constructed, then the rest (the flag as a byte).
ZoneExit* ZoneExits::CopyAt(ZoneExit* result, int index) const {
    const ZoneExit& from = data[index];
    std::memcpy(result, &from, offsetof(ZoneExit, target));
    std::construct_at(&result->target, from.target);
    result->entrance = from.entrance;
    result->view = from.view;
    result->hasDeployAngle = from.hasDeployAngle;
    result->deployAngle = from.deployAngle;
    return result;
}

void DeltaState::SetFieldCount(int count) {
    fields_.SetSize(count, -1);
}
