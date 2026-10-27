// The CArray<T> instantiations of game records (record_arrays.h), registered at their library addresses.
#include "game/record_arrays.h"

#include "ei/hook.h"
#include "lib/static_object.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>

namespace recordarrays {

using mfc::impl::ArrayHeader;

// ---- Scene records -------------------------------------------------------------------------------

EI_REWRITTEN(0x0044FCD0, FigureParts::SetSize);
EI_REWRITTEN(0x00453240, FigureParts::ConstructElements);
EI_REWRITTEN(0x00453550, Materials::SetSize);
EI_REWRITTEN(0x00454E40, Materials::ConstructElements);
EI_REWRITTEN(0x004545C0, SceneMeshes::ConstructElements);
EI_REWRITTEN(0x00454F80, SceneNodes::ConstructElements);
EI_REWRITTEN(0x00477100, SceneNodes::DestructElements);
EI_REWRITTEN(0x00474DB0, FaceGroups::SetSize);
EI_REWRITTEN(0x00476F20, FaceGroups::DestructElements);
EI_REWRITTEN(0x00454A50, ArrayHeader<FaceGroupArrayClass>::Construct);

EI_REWRITTEN(0x0050FA30, DeltaFields::SetSize);
EI_REWRITTEN(0x005101A0, DeltaFields::ConstructElements);
void InitTheme(MusicTheme* p) {
    std::construct_at(&p->name);
    std::construct_at(&p->briefing);
    std::construct_at(&p->constructor);
    p->calm.Construct();
    p->combat.Construct();
}
void __fastcall DestroyTheme(MusicTheme* p) {
    p->combat.Destruct();
    p->calm.Destruct();
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
EI_REWRITTEN(0x006B59A0, Themes::SetSize);
EI_REWRITTEN(0x006B5BC0, Themes::ConstructElements);
EI_REWRITTEN(0x006B4D90, DestroyTheme);
EI_REWRITTEN(0x006B56C0, Themes::Destruct<ThemeArrayClass>);

void __fastcall DestroyWave(WaveRecord* p) { gamelists::DestroyWave(p); }
EI_REWRITTEN(0x006BE240, Waves::ConstructElements);
EI_REWRITTEN(0x006BE210, Waves::DestructElements);
EI_REWRITTEN(0x006B90C0, DestroyWave);

EI_REWRITTEN(0x00557890, Behaviours::Destruct<BehaviourArrayClass>);
EI_REWRITTEN(0x00558E10, Behaviours::ConstructElements);
mfc::CString* PartNames::CopyAt(mfc::CString* result, int index) const {
    std::construct_at(result, data[index]);
    return result;
}
EI_REWRITTEN(0x004698A0, PartNames::CopyAt);
EI_REWRITTEN(0x004698C0, PartNames::CopyAt);

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

EI_REWRITTEN(0x004CD2B0, Cmds::SetSize);
EI_REWRITTEN(0x004CE920, Cmds::ConstructElements);
EI_REWRITTEN(0x004CD470, Cmds::DestructElements);
EI_REWRITTEN(0x004C4FD0, SubCalls::SetSize);
EI_REWRITTEN(0x004CD130, SubCalls::ConstructElements);
EI_REWRITTEN(0x004CD0F0, SubCalls::DestructElements);
EI_REWRITTEN(0x004CD770, Definitions::SetSize);
EI_REWRITTEN(0x004CD900, Definitions::DestructElements);
EI_REWRITTEN(0x004CEC80, Runs::DestructElements);
EI_REWRITTEN(0x004CD740, Decls::DestructElements);
// The by-value GetAt wrappers: their bodies have the same arguments.
EI_REWRITTEN(0x004CD450, ScriptCommands::CopyAt);
EI_REWRITTEN(0x004C7510, ScriptSubCalls::CopyAt);

// ---- Destructors only the vtables point at -----------------------------------------------

using mfc::impl::ScalarDeleting;

EI_REWRITTEN(0x004514B0, SceneNodes::Destruct<SceneNodeArrayClass>);
EI_REWRITTEN(0x004C5180, SubCalls::Destruct<SubCallBaseClass>);
EI_REWRITTEN(0x004CCF70, SubCalls::Destruct<SubCallBaseClass>);
EI_REWRITTEN(0x0050FBC0, DeltaFields::Destruct<DeltaFieldArrayClass>);
EI_REWRITTEN(0x00545BA0, Behaviours::Destruct<BehaviourArrayClass>);
EI_REWRITTEN(0x006C4C40, TerrainAlignedRecords::Destruct<TerrainAlignedArrayClass>);
EI_REWRITTEN(0x006C4690, SectorVertices::Destruct<SectorVertexArrayClass>);
// A fake unit's arrays (the tags are record_arrays.h's; mfc_slots.h has the element policies).
EI_REWRITTEN(0x0044FE80, FigureParts::Destruct<FakeUnitPartArrayClass>);
EI_REWRITTEN(0x004500F0, SceneMeshes::Destruct<FakeUnitSceneRecordArrayClass>);
EI_REWRITTEN(0x00450770, Materials::Destruct<FakeUnitRecord9CArrayClass>);
EI_REWRITTEN(0x00453210, FigureParts::DestructElements);
EI_REWRITTEN(0x00476F50, SceneMeshes::DestructElements);
// Scalar deleting destructors (vtable slot 1).
EI_REWRITTEN(0x004529F0, ScalarDeleting<&SceneNodes::Destruct<SceneNodeArrayClass>>::Delete);
EI_REWRITTEN(0x004CB0D0, ScalarDeleting<&SubCalls::Destruct<SubCallBaseClass>>::Delete);
EI_REWRITTEN(0x004CCF50, ScalarDeleting<&SubCalls::Destruct<SubCallBaseClass>>::Delete);
EI_REWRITTEN(0x00510160, ScalarDeleting<&DeltaFields::Destruct<DeltaFieldArrayClass>>::Delete);
EI_REWRITTEN(0x00556BE0, ScalarDeleting<&Behaviours::Destruct<BehaviourArrayClass>>::Delete);
EI_REWRITTEN(0x005587E0, ScalarDeleting<&Behaviours::Destruct<BehaviourArrayClass>>::Delete);
EI_REWRITTEN(0x006B5980, ScalarDeleting<&Themes::Destruct<ThemeArrayClass>>::Delete);
EI_REWRITTEN(0x00454CC0, ScalarDeleting<&scene::FaceGroupArrayDestruct>::Delete);
EI_REWRITTEN(0x006C4EC0, ScalarDeleting<&TerrainAlignedRecords::Destruct<TerrainAlignedArrayClass>>::Delete);
EI_REWRITTEN(0x006C4E80, ScalarDeleting<&SectorVertices::Destruct<SectorVertexArrayClass>>::Delete);
EI_REWRITTEN(0x00452890, ScalarDeleting<&FigureParts::Destruct<FakeUnitPartArrayClass>>::Delete);
EI_REWRITTEN(0x004528B0, ScalarDeleting<&SceneMeshes::Destruct<FakeUnitSceneRecordArrayClass>>::Delete);
EI_REWRITTEN(0x004528F0, ScalarDeleting<&Materials::Destruct<FakeUnitRecord9CArrayClass>>::Delete);

// Serialize (vtable slot 2): afxtempl's with the raw element bytes (mfc_slots.h); loading sizes the
// array through the instantiation's own SetSize, so its elements are built and destroyed as above.
EI_REWRITTEN(0x0044FEF0, FigureParts::Serialize);
EI_REWRITTEN(0x00450160, SceneMeshes::Serialize);
EI_REWRITTEN(0x004507E0, Materials::Serialize);
EI_REWRITTEN(0x00451640, SceneNodes::Serialize);
EI_REWRITTEN(0x00454A70, FaceGroups::Serialize);
EI_REWRITTEN(0x004C52A0, SubCalls::Serialize);
EI_REWRITTEN(0x004C7820, Definitions::Serialize);
EI_REWRITTEN(0x0050FC80, DeltaFields::Serialize);
EI_REWRITTEN(0x00557960, Behaviours::Serialize);
EI_REWRITTEN(0x006B5770, Themes::Serialize);
EI_REWRITTEN(0x006C4790, SectorVertices::Serialize);
EI_REWRITTEN(0x00571240, WaterLevels::Serialize);
EI_REWRITTEN(0x005993A0, Sectors::Serialize);

// The water levels' element helpers.
EI_REWRITTEN(0x00575690, WaterLevels::ConstructElements);
void __fastcall DestroyWaterLevel(WaterLevelElement* p) {
    mfc::impl::DestroyArrayAt<WaterLevelElement, 0, mfc::impl::FloatArrayClass>(p);
}
EI_REWRITTEN(0x005755C0, DestroyWaterLevel);
EI_REWRITTEN(0x005711E0, WaterLevels::Destruct<mfc::impl::ClientMapArrayClass>);

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
EI_REWRITTEN(0x004CC0C0, ScriptCommands::CopyFrom);

void ScriptSubCalls::CopyFrom(const ScriptSubCalls& source) {
    mfc::impl::ArrayAs<recordarrays::SubCalls>(*this).SetSize(source.size, -1);
    recordarrays::AssignSubCalls(this, &source);
}
EI_REWRITTEN(0x004C6D10, ScriptSubCalls::CopyFrom);

ScriptBindings* ScriptBindings::CopyConstruct(const ScriptBindings& source) {
    Construct(recordarrays::BindingArrayClass::Vtable());
    recordarrays::AllocateExact<recordarrays::Bindings>(&mfc::impl::ArrayAs<recordarrays::Bindings>(*this), source.size);
    for (int i = 0; i < source.size; ++i) data[i] = source.data[i];
    return this;
}
EI_REWRITTEN(0x004CCCB0, ScriptBindings::CopyConstruct);

ScriptSubCalls* ScriptSubCalls::CopyConstruct(const ScriptSubCalls& source) {
    Construct(recordarrays::SubCallArrayClass::Vtable());
    recordarrays::AllocateExact<recordarrays::SubCalls>(&mfc::impl::ArrayAs<recordarrays::SubCalls>(*this), source.size);
    recordarrays::AssignSubCalls(this, &source);
    return this;
}
EI_REWRITTEN(0x004CCD90, ScriptSubCalls::CopyConstruct);

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
EI_REWRITTEN(0x0046EAB0, mapspec::Patrols::CopyAt);
EI_REWRITTEN(0x0046EFD0, mapspec::Patrols::CopyAt);


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
EI_REWRITTEN(0x00584EA0, ZoneExits::CopyAt);
EI_REWRITTEN(0x00587DD0, ZoneExits::CopyAt);

void DeltaState::SetFieldCount(int count) {
    fields_.SetSize(count, -1);
}

// The lib templates over tags the game's headers define: the registrations that need the complete tag.
namespace mfc::impl {

EI_REWRITTEN(0x005711C0, ArrayHeader<ClientMapArrayClass>::Construct);

} // namespace mfc::impl
