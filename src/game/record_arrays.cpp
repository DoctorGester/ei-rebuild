// The CArray<T> instantiations of game records (record_arrays.h), registered at their library
// addresses (ticket S6b). Our code keeps calling them through their addresses (the forwarders in
// scene_loader.cpp, script_copy.cpp, sound_music.cpp, ...).
#include "game/record_arrays.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstring>
#include <new>

namespace recordarrays {

using mfc::impl::ArrayHeader;
using mfc::impl::GlobalMapPointRecords;

// ---- Scene records -------------------------------------------------------------------------------

// 00453240 writes zeros over the mesh's six arrays (+0x1C0..+0x21C) after ConstructBase; the zero
// fill already cleared them.
void InitFigurePart(CFigureMesh* p) {
    p->ConstructBase();
    ei::SetVtable(p, CFigureMesh::kPlainMeshVtableAddress);
}

// ---- Records -------------------------------------------------------------------------------------

void InitTheme(MusicTheme* p) {
    new (&p->name) mfc::CString();
    new (&p->briefing) mfc::CString();
    new (&p->constructor) mfc::CString();
    p->calm.Construct();
    p->combat.Construct();
}
void __fastcall DestroyTheme(MusicTheme* p) {
    p->combat.Destruct();
    p->calm.Destruct();
    p->constructor.~CString();
    p->briefing.~CString();
    p->name.~CString();
}
EI_REWRITTEN(0x006B4D90, DestroyTheme);

void __fastcall DestroyWave(WaveRecord* p) { gamelists::DestroyWave(p); }
EI_REWRITTEN(0x006B90C0, DestroyWave);

mfc::CString* PartNames::CopyAt(mfc::CString* result, int index) const {
    new (result) mfc::CString(data[index]);
    return result;
}
EI_REWRITTEN(0x004698A0, PartNames::CopyAt);

// The global map's links and quests (the zero fill, then both strings empty).
void __fastcall DestroyGlobalMapRecord(Bytes<0x18>* p) { mfc::impl::DestroyStringsAt<Bytes<0x18>, 0x14, 0x10>(p); }
EI_REWRITTEN(0x005FBD80, DestroyGlobalMapRecord);
// The zones (16 bytes): the strings at +0xC, then +8.
void __fastcall DestroyGlobalMapZone(Bytes<0x10>* p) { mfc::impl::DestroyStringsAt<Bytes<0x10>, 0xC, 8>(p); }
EI_REWRITTEN(0x005FBD30, DestroyGlobalMapZone);

// ---- The script's command arrays ------------------------------------------------------------------

namespace {

// The storage of a copy constructor: exactly `n` elements, built by the instantiation's
// ConstructElements; none when `n` is 0.
template <typename Abi>
void AllocateExact(typename Abi::Array* self, int n) {
    using T = typename Abi::Element;
    if (n) {
        self->data = static_cast<T*>(gamecrt::operator_new(static_cast<std::size_t>(n) * sizeof(T)));
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
    auto* cmd = static_cast<SSCCmd*>(gamecrt::operator_new(sizeof(SSCCmd)));
    p->cmd = cmd ? cmd->Construct() : nullptr;
}

// The by-value GetAt wrappers: their bodies (004CE750, 004CCAA0) have the same arguments.
EI_REWRITTEN(0x004CD450, ScriptCommands::CopyAt);
EI_REWRITTEN(0x004C7510, ScriptSubCalls::CopyAt);

// ---- Destructors only the vtables point at (ticket S6c) ------------------------------------------

using mfc::impl::ScalarDelete;

// Scalar deleting destructors (vtable slot 1).
EI_REPLACE(0x004529F0, ScalarDelete<&SceneNodes::Destruct<SceneNodeArrayClass>>);
EI_REPLACE(0x004CB0D0, ScalarDelete<&SubCalls::Destruct<SubCallBaseClass>>);
EI_REPLACE(0x004CCF50, ScalarDelete<&SubCalls::Destruct<SubCallBaseClass>>);
EI_REPLACE(0x00510160, ScalarDelete<&DeltaFields::Destruct<DeltaFieldArrayClass>>);
EI_REPLACE(0x00556BE0, ScalarDelete<&Behaviours::Destruct<BehaviourArrayClass>>);
EI_REPLACE(0x005587E0, ScalarDelete<&Behaviours::Destruct<BehaviourArrayClass>>);
EI_REPLACE(0x006B5980, ScalarDelete<&Themes::Destruct<ThemeArrayClass>>);
EI_REPLACE(0x00454CC0, ScalarDelete<&scene::FaceGroupArrayDestruct>);
EI_REPLACE(0x006C4EC0, ScalarDelete<&TerrainAlignedRecords::Destruct<TerrainAlignedArrayClass>>);
EI_REPLACE(0x006C4E80, ScalarDelete<&SectorVertices::Destruct<SectorVertexArrayClass>>);
EI_REPLACE(0x006BD190, ScalarDelete<&gamelists::Waves::Destruct<WaveListClass>>);

// Serialize (vtable slot 2): afxtempl's with the raw element bytes (mfc_slots.h); loading sizes the
// array through the instantiation's own SetSize, so its elements are built and destroyed as above.
using mfc::impl::SerializeCArray;
EI_REPLACE(0x0044FEF0, SerializeCArray<FigureParts>);
EI_REPLACE(0x00450160, SerializeCArray<SceneMeshes>);
EI_REPLACE(0x004507E0, SerializeCArray<Materials>);
EI_REPLACE(0x00451640, SerializeCArray<SceneNodes>);
EI_REPLACE(0x00454A70, SerializeCArray<FaceGroups>);
EI_REPLACE(0x004C52A0, SerializeCArray<SubCalls>);
EI_REPLACE(0x004C7820, SerializeCArray<Definitions>);
EI_REPLACE(0x0050FC80, SerializeCArray<DeltaFields>);
EI_REPLACE(0x00557960, SerializeCArray<Behaviours>);
EI_REPLACE(0x006B5770, SerializeCArray<Themes>);
EI_REPLACE(0x006C4790, SerializeCArray<SectorVertices>);
EI_REPLACE(0x00571240, SerializeCArray<WaterLevels>);
EI_REPLACE(0x005993A0, SerializeCArray<Sectors>);

// The water levels' element helpers.
void __fastcall DestroyWaterLevel(WaterLevelElement* p) {
    mfc::impl::DestroyArrayAt<WaterLevelElement, 0, mfc::impl::FloatArrayClass>(p);
}
EI_REWRITTEN(0x005755C0, DestroyWaterLevel);

} // namespace recordarrays

// ---- Members of the script command arrays (script.h) ----------------------------------------------

// The original assigns the commands member by member (the float arguments through SetSize 004C4410
// and dword copies); SSCCmd's assignment copies them through the FPU, the same values.
void ScriptCommands::CopyFrom(const ScriptCommands& source) {
    mfc::impl::ArrayAs<recordarrays::Cmds>(*this).SetSize(source.size, -1);
    for (int i = 0; i < source.size; ++i) {
        alignas(SSCCmd) std::uint8_t storage[sizeof(SSCCmd)];
        auto* copy = reinterpret_cast<SSCCmd*>(storage);
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
    Construct(reinterpret_cast<std::uintptr_t>(recordarrays::BindingArrayClass::Vtable()));
    recordarrays::AllocateExact<recordarrays::Bindings>(&mfc::impl::ArrayAs<recordarrays::Bindings>(*this), source.size);
    for (int i = 0; i < source.size; ++i) data[i] = source.data[i];
    return this;
}
EI_REWRITTEN(0x004CCCB0, ScriptBindings::CopyConstruct);

ScriptSubCalls* ScriptSubCalls::CopyConstruct(const ScriptSubCalls& source) {
    Construct(reinterpret_cast<std::uintptr_t>(recordarrays::SubCallArrayClass::Vtable()));
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
    to.Construct(reinterpret_cast<std::uintptr_t>(mfc::impl::WaypointArrayClass::Vtable()));
    int n = from.waypoints.size;
    if (n) {
        to.data = static_cast<mapspec::Waypoint*>(gamecrt::operator_new(static_cast<std::size_t>(n) * sizeof(mapspec::Waypoint)));
        mfc::PlainElements<mapspec::Waypoint>::Construct(to.data, n);   // 0045E130
    }
    to.maxSize = n;
    to.size = n;
    for (int i = 0; i < from.waypoints.size; ++i) std::memcpy(&to.data[i], &from.waypoints.data[i], sizeof(mapspec::Waypoint));
    return result;
}
EI_REWRITTEN(0x0046EAB0, mapspec::Patrols::CopyAt);


// The first ten dwords as they are, the target copy-constructed, then the rest (the flag as a byte).
ZoneExit* ZoneExits::CopyAt(ZoneExit* result, int index) const {
    const ZoneExit& from = data[index];
    std::memcpy(result, &from, offsetof(ZoneExit, target));
    new (&result->target) mfc::CString(from.target);
    result->entrance = from.entrance;
    result->view = from.view;
    result->hasDeployAngle = from.hasDeployAngle;
    result->deployAngle = from.deployAngle;
    return result;
}
EI_REWRITTEN(0x00584EA0, ZoneExits::CopyAt);

void DeltaState::SetFieldCount(int count) {
    recordarrays::DeltaFields::SetSize(reinterpret_cast<recordarrays::DeltaFields::Array*>(this), nullptr, count, -1);
}
