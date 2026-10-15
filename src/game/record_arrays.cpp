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

EI_REPLACE(0x0044FCD0, FigureParts::SetSize);
EI_REPLACE(0x00453240, FigureParts::ConstructElements);
EI_REPLACE(0x00453550, Materials::SetSize);
EI_REPLACE(0x00454E40, Materials::ConstructElements);
EI_REPLACE(0x004545C0, SceneMeshes::ConstructElements);
EI_REPLACE(0x00454F80, SceneNodes::ConstructElements);
EI_REPLACE(0x00477100, SceneNodes::DestructElements);
EI_REPLACE(0x00474DB0, FaceGroups::SetSize);
EI_REPLACE(0x00476F20, FaceGroups::DestructElements);
EI_REPLACE(0x00454A50, ArrayHeader<FaceGroupArrayClass>::Construct);

// ---- Records -------------------------------------------------------------------------------------

EI_REPLACE(0x0050FA30, DeltaFields::SetSize);
EI_REPLACE(0x005101A0, DeltaFields::ConstructElements);

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
EI_REPLACE(0x006B59A0, Themes::SetSize);
EI_REPLACE(0x006B5BC0, Themes::ConstructElements);
EI_REPLACE(0x006B4D90, DestroyTheme);
EI_REPLACE(0x006B56C0, Themes::Destruct<ThemeArrayClass>);

void __fastcall DestroyWave(WaveRecord* p) { gamelists::DestroyWave(p); }
EI_REPLACE(0x006BE240, Waves::ConstructElements);
EI_REPLACE(0x006BE210, Waves::DestructElements);
EI_REPLACE(0x006B90C0, DestroyWave);

EI_REPLACE(0x00557890, Behaviours::Destruct<BehaviourArrayClass>);
EI_REPLACE(0x00558E10, Behaviours::ConstructElements);

// The position and the waypoints are copied as the dwords they are.
mapspec::Patrol* __fastcall PatrolGetAt(const mfc::CArray<mapspec::Patrol>* self, void*, mapspec::Patrol* result,
                                        int index) {
    const mapspec::Patrol& from = self->data[index];
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
EI_REPLACE(0x0046EAB0, PatrolGetAt);
EI_REPLACE(0x0046EFD0, PatrolGetAt);

// The first ten dwords as they are, the target copy-constructed, then the rest (the flag as a byte).
ZoneExit* __fastcall ExitGetAt(const mfc::CArray<ZoneExit>* self, void*, ZoneExit* result, int index) {
    const ZoneExit& from = self->data[index];
    std::memcpy(result, &from, offsetof(ZoneExit, target));
    new (static_cast<void*>(&result->target)) mfc::CString(*reinterpret_cast<const mfc::CString*>(&from.target));
    result->entrance = from.entrance;
    result->view = from.view;
    result->hasDeployAngle = from.hasDeployAngle;
    result->deployAngle = from.deployAngle;
    return result;
}
EI_REPLACE(0x00584EA0, ExitGetAt);
EI_REPLACE(0x00587DD0, ExitGetAt);

// The global map's links and quests (the zero fill, then both strings empty).
void __fastcall DestroyGlobalMapRecord(Bytes<0x18>* p) { mfc::impl::DestroyStringsAt<Bytes<0x18>, 0x14, 0x10>(p); }
EI_REPLACE(0x00631E70, GlobalMapPointRecords::SetSize);
EI_REPLACE(0x00631FF0, GlobalMapPointRecords::SetSize);
EI_REPLACE(0x005FBFB0, GlobalMapPointRecords::ConstructElements);
EI_REPLACE(0x005FC010, GlobalMapPointRecords::ConstructElements);
EI_REPLACE(0x005FBD80, DestroyGlobalMapRecord);
EI_REPLACE(0x005FBDD0, DestroyGlobalMapRecord);

// ---- The script's command arrays ------------------------------------------------------------------

namespace {

// The arrays as the GetAt bodies (script_copy.cpp) take them.
const mfc::CArray<SSCCmd>* Plain(const Cmds::Array* a) { return reinterpret_cast<const mfc::CArray<SSCCmd>*>(a); }
const mfc::CArray<ScriptSubCall>* Plain(const SubCalls::Array* a) {
    return reinterpret_cast<const mfc::CArray<ScriptSubCall>*>(a);
}

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
void AssignSubCalls(SubCalls::Array* self, const SubCalls::Array* source) {
    for (int i = 0; i < source->size; ++i) {
        ScriptSubCall copy;
        ScriptSubCallCopyAt(Plain(source), nullptr, &copy, i);
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

// The original assigns the commands member by member (the float arguments through SetSize 004C4410
// and dword copies); SSCCmd's assignment copies them through the FPU, the same values.
void __fastcall CmdArrayCopy(Cmds::Array* self, void*, const Cmds::Array* source) {
    self->SetSize(source->size, -1);
    for (int i = 0; i < source->size; ++i) {
        alignas(SSCCmd) std::uint8_t storage[sizeof(SSCCmd)];
        auto* copy = reinterpret_cast<SSCCmd*>(storage);
        ScriptCmdCopyAt(Plain(source), nullptr, copy, i);
        self->data[i].ScriptCmdAssign(copy);
        ScriptCmdDestruct(copy);
    }
}

void __fastcall SubCallArrayCopy(SubCalls::Array* self, void*, const SubCalls::Array* source) {
    self->SetSize(source->size, -1);
    AssignSubCalls(self, source);
}

Bindings::Array* __fastcall BindingsCopyConstruct(Bindings::Array* self, void*, const Bindings::Array* source) {
    self->Construct(reinterpret_cast<std::uintptr_t>(BindingArrayClass::Vtable()));
    AllocateExact<Bindings>(self, source->size);
    for (int i = 0; i < source->size; ++i) self->data[i] = source->data[i];
    return self;
}

SubCalls::Array* __fastcall SubCallsCopyConstruct(SubCalls::Array* self, void*, const SubCalls::Array* source) {
    self->Construct(reinterpret_cast<std::uintptr_t>(SubCallArrayClass::Vtable()));
    AllocateExact<SubCalls>(self, source->size);
    AssignSubCalls(self, source);
    return self;
}

EI_REPLACE(0x004CD2B0, Cmds::SetSize);
EI_REPLACE(0x004CE920, Cmds::ConstructElements);
EI_REPLACE(0x004CD470, Cmds::DestructElements);
EI_REPLACE(0x004C4FD0, SubCalls::SetSize);
EI_REPLACE(0x004CD130, SubCalls::ConstructElements);
EI_REPLACE(0x004CD0F0, SubCalls::DestructElements);
EI_REPLACE(0x004CD770, Definitions::SetSize);
EI_REPLACE(0x004CD900, Definitions::DestructElements);
EI_REPLACE(0x004CEC80, Runs::DestructElements);
EI_REPLACE(0x004CD740, Decls::DestructElements);
EI_REPLACE(0x004CC0C0, CmdArrayCopy);
EI_REPLACE(0x004C6D10, SubCallArrayCopy);
EI_REPLACE(0x004CCCB0, BindingsCopyConstruct);
EI_REPLACE(0x004CCD90, SubCallsCopyConstruct);
// The by-value GetAt wrappers: their bodies (004CE750, 004CCAA0) have the same arguments.
EI_REPLACE(0x004CD450, ScriptCmdCopyAt);
EI_REPLACE(0x004C7510, ScriptSubCallCopyAt);

// ---- Destructors only the vtables point at (ticket S6c) ------------------------------------------

using mfc::impl::ScalarDelete;

// The array destructors: each element's destructor inlined (the nested calls' commands destroyed and
// deleted; 00545BA0 is 00557890's copy).
EI_REPLACE(0x004514B0, SceneNodes::Destruct<SceneNodeArrayClass>);
EI_REPLACE(0x004C5180, SubCalls::Destruct<SubCallBaseClass>);
EI_REPLACE(0x004CCF70, SubCalls::Destruct<SubCallBaseClass>);
EI_REPLACE(0x0050FBC0, DeltaFields::Destruct<DeltaFieldArrayClass>);
EI_REPLACE(0x00545BA0, Behaviours::Destruct<BehaviourArrayClass>);
EI_REPLACE(0x006C4C40, TerrainAlignedRecords::Destruct<TerrainAlignedArrayClass>);
EI_REPLACE(0x006C4690, SectorVertices::Destruct<SectorVertexArrayClass>);
EI_REPLACE(0x006BC870, gamelists::Waves::Destruct<WaveListClass>);

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
EI_REPLACE(0x00575690, WaterLevels::ConstructElements);
void __fastcall DestroyWaterLevel(WaterLevelElement* p) {
    mfc::impl::DestroyArrayAt<WaterLevelElement, 0, mfc::impl::FloatArrayClass>(p);
}
EI_REPLACE(0x005755C0, DestroyWaterLevel);

} // namespace recordarrays
