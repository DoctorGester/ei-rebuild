// The scalar deleting destructors the exe's vtables point at and the destructors they call, for the
// instantiations whose elements need nothing of the game (mfc_slots.h, ticket S6a); the game-element
// ones are in src/game/game_slots.cpp. Each deleting destructor calls its class's destructor directly
// (the original calls it by address), the destructors are the S6 / S7 templates. Checked by
// tests/verify/verify_mfc_slots.cpp.
#include "mfc_slots.h"

#include "crt.h"
#include "ei/hook.h"

namespace mfc::impl {

void __fastcall ExceptionDestruct(libcrt::Exception* self) { self->Destruct(); }

// ---- Destructors ----------------------------------------------------------------------------------

// Arrays: the vtable, the storage freed.
EI_REPLACE(0x00452870, ArrayHeader<Vector3ArrayClass>::Destruct);
EI_REPLACE(0x004509E0, ArrayHeader<NodeItemsAClass>::Destruct);
EI_REPLACE(0x00450B90, ArrayHeader<NodeItemsBClass>::Destruct);
EI_REPLACE(0x00452990, ArrayHeader<BoneLinkArrayClass>::Destruct);
EI_REPLACE(0x004529D0, ArrayHeader<NodeItemsCClass>::Destruct);
EI_REPLACE(0x00451830, ArrayHeader<TextureLinkArrayClass>::Destruct);
EI_REPLACE(0x00451EC0, ArrayHeader<MeshCornerArrayClass>::Destruct);
EI_REPLACE(0x0045E020, ArrayHeader<ArrayBaseClass>::Destruct);
EI_REPLACE(0x00468D80, ArrayHeader<SpriteArrayClass>::Destruct);
EI_REPLACE(0x0046E910, ArrayHeader<TrapCircleArrayClass>::Destruct);
EI_REPLACE(0x0046E950, ArrayHeader<TrapPointArrayClass>::Destruct);
EI_REPLACE(0x0046EF40, ArrayHeader<TrapCircleArrayClass>::Destruct);
EI_REPLACE(0x0046EF80, ArrayHeader<TrapPointArrayClass>::Destruct);
EI_REPLACE(0x004C1820, ArrayHeader<ScriptAreaClass>::Destruct);
EI_REPLACE(0x004BC980, ArrayHeader<ScriptFloatsClass>::Destruct);
EI_REPLACE(0x004BE410, ArrayHeader<ScriptIfThenArrayClass>::Destruct);
EI_REPLACE(0x004CAF90, ArrayHeader<ScriptAreaClass>::Destruct);
EI_REPLACE(0x004C4840, ArrayHeader<ArgObjectsBaseClass>::Destruct);
EI_REPLACE(0x004C4B30, ArrayHeader<ArgGroupsBaseClass>::Destruct);
EI_REPLACE(0x004C4E20, ArrayHeader<BindingsBaseClass>::Destruct);
EI_REPLACE(0x004CB710, ArrayHeader<PartySlotArrayClass>::Destruct);
EI_REPLACE(0x004BF760, ArrayHeader<FloatArrayClass>::Destruct);
EI_REPLACE(0x004BF780, ArrayHeader<ArgObjectsBaseClass>::Destruct);
EI_REPLACE(0x004BF7A0, ArrayHeader<ArgGroupsBaseClass>::Destruct);
EI_REPLACE(0x004BF7C0, ArrayHeader<BindingsBaseClass>::Destruct);
EI_REPLACE(0x004CDF00, ArrayHeader<AiLookArrayClass>::Destruct);
EI_REPLACE(0x0050FFB0, ArrayHeader<EffectArrayClass>::Destruct);
EI_REPLACE(0x00511E00, ArrayHeader<EffectSoundArrayClass>::Destruct);
EI_REPLACE(0x00519B30, ArrayHeader<FrameArrayClass>::Destruct);
EI_REPLACE(0x0052E240, ArrayHeader<MotionPointArrayClass>::Destruct);
EI_REPLACE(0x0052E260, ArrayHeader<MotionWaypointBaseClass>::Destruct);
EI_REPLACE(0x00533540, ArrayHeader<MotionPointArrayClass>::Destruct);
EI_REPLACE(0x00533580, ArrayHeader<MotionWaypointBaseClass>::Destruct);
EI_REPLACE(0x005331C0, ArrayHeader<CarriedArrayClass>::Destruct);
EI_REPLACE(0x00533370, ArrayHeader<IdArrayClass>::Destruct);
EI_REPLACE(0x005396C0, ArrayHeader<SkinLayerArrayClass>::Destruct);
EI_REPLACE(0x00556C20, ArrayHeader<AnimationStepsClass>::Destruct);
EI_REPLACE(0x005604B0, ArrayHeader<CountdownArrayClass>::Destruct);
EI_REPLACE(0x0059B0B0, ArrayHeader<PartyUnitArrayClass>::Destruct);
EI_REPLACE(0x0059E0A0, ArrayHeader<SectorOccluderArrayClass>::Destruct);
EI_REPLACE(0x005C1A90, ArrayHeader<SpiralCellArrayClass>::Destruct);
EI_REPLACE(0x005C5850, ArrayHeader<SmoothRowArrayClass>::Destruct);
EI_REPLACE(0x005C5890, ArrayHeader<SmoothEdgeArrayClass>::Destruct);
EI_REPLACE(0x005C58D0, ArrayHeader<SmoothCopyArrayClass>::Destruct);
EI_REPLACE(0x005C5910, ArrayHeader<SmoothStepArrayClass>::Destruct);
EI_REPLACE(0x005EBBE0, ArrayHeader<UnitPtrArrayClass>::Destruct);
EI_REPLACE(0x0060A870, ArrayHeader<ObjectiveExitArrayClass>::Destruct);
EI_REPLACE(0x00613A30, ArrayHeader<ItemStripArrayClass>::Destruct);
EI_REPLACE(0x0064B1C0, ArrayHeader<QuickEntryArrayClass>::Destruct);
EI_REPLACE(0x0065DC70, ArrayHeader<TokenPtrArrayClass>::Destruct);
EI_REPLACE(0x0065DE20, ArrayHeader<LinePtrArrayClass>::Destruct);
EI_REPLACE(0x006CD6D0, ArrayHeader<TerrainArrayAClass>::Destruct);
EI_REPLACE(0x006CD880, ArrayHeader<TerrainArrayBClass>::Destruct);
EI_REPLACE(0x006CDA20, ArrayHeader<TerrainArrayCClass>::Destruct);

// Lists: the vtable, RemoveAll.
EI_REPLACE(0x00498CA0, PtrList::Destruct<ClientListClass>);
EI_REPLACE(0x0050DBC0, PtrList::Destruct<BoneListClass>);
EI_REPLACE(0x0050DE30, PtrList::Destruct<AnimationStateListClass>);
EI_REPLACE(0x00519870, PtrList::Destruct<Object3DListClass>);
EI_REPLACE(0x00570510, PtrList::Destruct<ClientObjectListClass>);
EI_REPLACE(0x00570ED0, PtrList::Destruct<ClientList2Class>);
EI_REPLACE(0x00571600, PtrList::Destruct<ClientList3Class>);
EI_REPLACE(0x0059B260, PtrList::Destruct<ExperienceListClass>);
EI_REPLACE(0x005D99E0, PtrList::Destruct<AttentionListClass>);
EI_REPLACE(0x00665FF0, PtrList::Destruct<PlayerListClass>);
EI_REPLACE(0x00694160, PtrList::Destruct<TextureListClass>);
EI_REPLACE(0x006BC570, PtrList::Destruct<SoundListClass>);

// Maps: the vtable, RemoveAll.
EI_REPLACE(0x00451C40, Map32::Destruct<FigureRegistryMapClass>);
EI_REPLACE(0x00452090, Map32::Destruct<CornerMapAClass>);
EI_REPLACE(0x00452330, Map32::Destruct<CornerMapBClass>);
EI_REPLACE(0x004525D0, Map32::Destruct<CornerMapCClass>);

// Arrays whose elements own something: each destroyed (DestructElements inlined), the storage freed.
// CObjects through slot 1, C++ objects through slot 0 (flags 0).
EI_REPLACE(0x00450500, FakeUnitBoneArrays::Destruct<FakeUnitBoneArraysClass>);
EI_REPLACE(0x006C49D0, ByteArrays::Destruct<TerrainByteArraysClass>);
EI_REPLACE(0x004763C0, SceneObjects::Destruct<SceneObjectArrayClass>);
EI_REPLACE(0x0044FE80, FigureParts::Destruct<FakeUnitPartArrayClass>);
EI_REPLACE(0x004500F0, SceneRecords::Destruct<FakeUnitSceneRecordArrayClass>);
EI_REPLACE(0x00450770, FakeUnitRecords9C::Destruct<FakeUnitRecord9CArrayClass>);
// Aligned blocks.
EI_REPLACE(0x004515F0, AlignedAt4Records::Destruct<AlignedAt4ArrayClass>);
EI_REPLACE(0x0050A1C0, AlignedAt8Records::Destruct<AlignedAt8ArrayClass>);
// CStrings.
EI_REPLACE(0x00451A00, TextureNames::Destruct<TextureNameArrayClass>);
EI_REPLACE(0x00460490, ExitTargets::Destruct<ExitArrayClass>);
EI_REPLACE(0x00460A80, ExitTargets::Destruct<ExitArrayClass>);
EI_REPLACE(0x00556B70, MemoryRecords::Destruct<MemoryRecordArrayClass>);
EI_REPLACE(0x00556FE0, MemoryRecords::Destruct<MemoryRecordArrayClass>);
EI_REPLACE(0x005FB2F0, GlobalMapPoints::Destruct<GlobalMapPointArrayClass>);
EI_REPLACE(0x005FB560, GlobalMapPoints::Destruct<GlobalMapPathArrayClass>);
EI_REPLACE(0x005FB090, GlobalMapZones::Destruct<GlobalMapZoneArrayClass>);
// Elements holding an array: patrols (the waypoints), allods (six CStrings and the ids), script
// areas.
EI_REPLACE(0x0045D8B0, OwnedPatrols::Destruct<PatrolArrayClass>);
EI_REPLACE(0x0045E090, OwnedPatrols::Destruct<PatrolArrayClass>);
EI_REPLACE(0x0045FBD0, OwnedAllods::Destruct<AllodArrayClass>);
EI_REPLACE(0x004BE430, ScriptAreas::Destruct<ScriptAreaArrayClass>);

// The exception classes' own destructors (a jump to std::exception's; their throw information's).
EI_REPLACE(0x0040C4A0, ExceptionDestruct);
EI_REPLACE(0x0040C4D0, ExceptionDestruct);
EI_REPLACE(0x0040C500, ExceptionDestruct);
EI_REPLACE(0x0041F760, ExceptionDestruct);

// ---- Scalar deleting destructors (vtable slot 1; the exception classes' slot 0) --------------------

// Over the destructors of arrays whose elements own something.
EI_REPLACE(0x004528D0, ScalarDelete<&FakeUnitBoneArrays::Destruct<FakeUnitBoneArraysClass>>);
EI_REPLACE(0x006C4EA0, ScalarDelete<&ByteArrays::Destruct<TerrainByteArraysClass>>);
EI_REPLACE(0x00476D70, ScalarDelete<&SceneObjects::Destruct<SceneObjectArrayClass>>);
EI_REPLACE(0x00452890, ScalarDelete<&FigureParts::Destruct<FakeUnitPartArrayClass>>);
EI_REPLACE(0x004528B0, ScalarDelete<&SceneRecords::Destruct<FakeUnitSceneRecordArrayClass>>);
EI_REPLACE(0x004528F0, ScalarDelete<&FakeUnitRecords9C::Destruct<FakeUnitRecord9CArrayClass>>);
EI_REPLACE(0x00452950, ScalarDelete<&AlignedAt4Records::Destruct<AlignedAt4ArrayClass>>);
EI_REPLACE(0x0050A410, ScalarDelete<&AlignedAt8Records::Destruct<AlignedAt8ArrayClass>>);
EI_REPLACE(0x00452A30, ScalarDelete<&TextureNames::Destruct<TextureNameArrayClass>>);
EI_REPLACE(0x00460470, ScalarDelete<&ExitTargets::Destruct<ExitArrayClass>>);
EI_REPLACE(0x00460A60, ScalarDelete<&ExitTargets::Destruct<ExitArrayClass>>);
EI_REPLACE(0x00556B50, ScalarDelete<&MemoryRecords::Destruct<MemoryRecordArrayClass>>);
EI_REPLACE(0x00558720, ScalarDelete<&MemoryRecords::Destruct<MemoryRecordArrayClass>>);
EI_REPLACE(0x004CB790, ScalarDelete<&NamedRecords::Destruct<NamedRecordArrayClass>>);
EI_REPLACE(0x005FB870, ScalarDelete<&GlobalMapPoints::Destruct<GlobalMapPointArrayClass>>);
EI_REPLACE(0x005FB890, ScalarDelete<&GlobalMapPoints::Destruct<GlobalMapPathArrayClass>>);
EI_REPLACE(0x005FB850, ScalarDelete<&GlobalMapZones::Destruct<GlobalMapZoneArrayClass>>);
EI_REPLACE(0x0045D890, ScalarDelete<&OwnedPatrols::Destruct<PatrolArrayClass>>);
EI_REPLACE(0x0045E070, ScalarDelete<&OwnedPatrols::Destruct<PatrolArrayClass>>);
EI_REPLACE(0x004600F0, ScalarDelete<&OwnedAllods::Destruct<AllodArrayClass>>);
EI_REPLACE(0x004C1A60, ScalarDelete<&ScriptAreas::Destruct<ScriptAreaArrayClass>>);
EI_REPLACE(0x004CB630, ScalarDelete<&ScriptAreas::Destruct<ScriptAreaArrayClass>>);

// Over the destructors of the arrays, lists and maps whose elements need nothing.

EI_REPLACE(0x00405540, ScalarDelete<&PtrList::Destruct<TempListClass>>);   // 00405130
EI_REPLACE(0x0040C480, ScalarDelete<&ExceptionDestruct>);   // 0040C4A0
EI_REPLACE(0x0040C4B0, ScalarDelete<&ExceptionDestruct>);   // 0040C4D0
EI_REPLACE(0x0040C4E0, ScalarDelete<&ExceptionDestruct>);   // 0040C500
EI_REPLACE(0x0041F740, ScalarDelete<&ExceptionDestruct>);   // 0041F760
EI_REPLACE(0x00429180, ScalarDelete<&PtrList::Destruct<ItemListClass>>);   // 00427830
EI_REPLACE(0x00452850, ScalarDelete<&ArrayHeader<Vector3ArrayClass>::Destruct>);   // 00452870
EI_REPLACE(0x00452910, ScalarDelete<&ArrayHeader<NodeItemsAClass>::Destruct>);   // 004509E0
EI_REPLACE(0x00452930, ScalarDelete<&ArrayHeader<NodeItemsBClass>::Destruct>);   // 00450B90
EI_REPLACE(0x00452970, ScalarDelete<&ArrayHeader<BoneLinkArrayClass>::Destruct>);   // 00452990
EI_REPLACE(0x004529B0, ScalarDelete<&ArrayHeader<NodeItemsCClass>::Destruct>);   // 004529D0
EI_REPLACE(0x00452A10, ScalarDelete<&ArrayHeader<TextureLinkArrayClass>::Destruct>);   // 00451830
EI_REPLACE(0x00452A50, ScalarDelete<&Map32::Destruct<FigureRegistryMapClass>>);   // 00451C40
EI_REPLACE(0x00452A70, ScalarDelete<&ArrayHeader<MeshCornerArrayClass>::Destruct>);   // 00451EC0
EI_REPLACE(0x00452A90, ScalarDelete<&Map32::Destruct<CornerMapAClass>>);   // 00452090
EI_REPLACE(0x00452AB0, ScalarDelete<&Map32::Destruct<CornerMapBClass>>);   // 00452330
EI_REPLACE(0x00452AD0, ScalarDelete<&Map32::Destruct<CornerMapCClass>>);   // 004525D0
EI_REPLACE(0x0045E000, ScalarDelete<&ArrayHeader<ArrayBaseClass>::Destruct>);   // 0045E020
EI_REPLACE(0x0045E230, ScalarDelete<&ArrayHeader<ArrayBaseClass>::Destruct>);   // 0045E250
EI_REPLACE(0x004600B0, ScalarDelete<&ArrayHeader<IntArrayClass>::Destruct>);   // 004600D0
EI_REPLACE(0x00468F50, ScalarDelete<&ArrayHeader<SpriteArrayClass>::Destruct>);   // 00468D80
EI_REPLACE(0x0046E8F0, ScalarDelete<&ArrayHeader<TrapCircleArrayClass>::Destruct>);   // 0046E910
EI_REPLACE(0x0046E930, ScalarDelete<&ArrayHeader<TrapPointArrayClass>::Destruct>);   // 0046E950
EI_REPLACE(0x0046EF20, ScalarDelete<&ArrayHeader<TrapCircleArrayClass>::Destruct>);   // 0046EF40
EI_REPLACE(0x0046EF60, ScalarDelete<&ArrayHeader<TrapPointArrayClass>::Destruct>);   // 0046EF80
EI_REPLACE(0x00476C90, ScalarDelete<&ArrayHeader<FilePointArrayClass>::Destruct>);   // 00474F30
EI_REPLACE(0x00476CB0, ScalarDelete<&ArrayHeader<FileUvArrayClass>::Destruct>);   // 00475110
EI_REPLACE(0x00476CD0, ScalarDelete<&ArrayHeader<FileFaceArrayClass>::Destruct>);   // 004752F0
EI_REPLACE(0x00476CF0, ScalarDelete<&ArrayHeader<RotKeyArrayClass>::Destruct>);   // 00475680
EI_REPLACE(0x00476D10, ScalarDelete<&ArrayHeader<PosKeyArrayClass>::Destruct>);   // 004759E0
EI_REPLACE(0x00476D30, ScalarDelete<&ArrayHeader<MorphHeadArrayClass>::Destruct>);   // 00475D40
EI_REPLACE(0x00476D50, ScalarDelete<&Map32::Destruct<DwordMapClass>>);   // 00476140
EI_REPLACE(0x00476D90, ScalarDelete<&ArrayHeader<IndexArrayClass>::Destruct>);   // 00476780
EI_REPLACE(0x00476DB0, ScalarDelete<&ArrayHeader<NormalArrayClass>::Destruct>);   // 00476AB0
EI_REPLACE(0x00499020, ScalarDelete<&PtrList::Destruct<ClientListClass>>);   // 00498CA0
EI_REPLACE(0x00499040, ScalarDelete<&ArrayHeader<Array0498Class>::Destruct>);   // 00498E80
EI_REPLACE(0x004C1800, ScalarDelete<&ArrayHeader<ScriptAreaClass>::Destruct>);   // 004C1820
EI_REPLACE(0x004C1880, ScalarDelete<&ArrayHeader<ScriptFloatsClass>::Destruct>);   // 004BC980
EI_REPLACE(0x004C1A00, ScalarDelete<&ArrayHeader<ScriptIfThenArrayClass>::Destruct>);   // 004BE410
EI_REPLACE(0x004CAF70, ScalarDelete<&ArrayHeader<ScriptAreaClass>::Destruct>);   // 004CAF90
EI_REPLACE(0x004CAFF0, ScalarDelete<&ArrayHeader<ScriptFloatsClass>::Destruct>);   // 004C29C0
EI_REPLACE(0x004CB030, ScalarDelete<&ArrayHeader<FloatArrayClass>::Destruct>);   // 004CB050
EI_REPLACE(0x004CB070, ScalarDelete<&ArrayHeader<ArgObjectsBaseClass>::Destruct>);   // 004C4840
EI_REPLACE(0x004CB090, ScalarDelete<&ArrayHeader<ArgGroupsBaseClass>::Destruct>);   // 004C4B30
EI_REPLACE(0x004CB0B0, ScalarDelete<&ArrayHeader<BindingsBaseClass>::Destruct>);   // 004C4E20
EI_REPLACE(0x004CB110, ScalarDelete<&ArrayHeader<ScriptIfThenArrayClass>::Destruct>);   // 004C5AF0
EI_REPLACE(0x004CB6F0, ScalarDelete<&ArrayHeader<PartySlotArrayClass>::Destruct>);   // 004CB710
EI_REPLACE(0x004CB730, ScalarDelete<&PtrList::Destruct<PtrListClass>>);   // 004C96F0
EI_REPLACE(0x004CB750, ScalarDelete<&ArrayHeader<AiLookArrayClass>::Destruct>);   // 004CB770
EI_REPLACE(0x004CCED0, ScalarDelete<&ArrayHeader<FloatArrayClass>::Destruct>);   // 004BF760
EI_REPLACE(0x004CCEF0, ScalarDelete<&ArrayHeader<ArgObjectsBaseClass>::Destruct>);   // 004BF780
EI_REPLACE(0x004CCF10, ScalarDelete<&ArrayHeader<ArgGroupsBaseClass>::Destruct>);   // 004BF7A0
EI_REPLACE(0x004CCF30, ScalarDelete<&ArrayHeader<BindingsBaseClass>::Destruct>);   // 004BF7C0
EI_REPLACE(0x004CDF20, ScalarDelete<&ArrayHeader<AiLookArrayClass>::Destruct>);   // 004CDF00
EI_REPLACE(0x0050DFB0, ScalarDelete<&PtrList::Destruct<BoneListClass>>);   // 0050DBC0
EI_REPLACE(0x0050DFD0, ScalarDelete<&PtrList::Destruct<AnimationStateListClass>>);   // 0050DE30
EI_REPLACE(0x00510180, ScalarDelete<&ArrayHeader<EffectArrayClass>::Destruct>);   // 0050FFB0
EI_REPLACE(0x00512010, ScalarDelete<&ArrayHeader<EffectSoundArrayClass>::Destruct>);   // 00511E00
EI_REPLACE(0x00519D30, ScalarDelete<&PtrList::Destruct<Object3DListClass>>);   // 00519870
EI_REPLACE(0x00519D50, ScalarDelete<&ArrayHeader<FrameArrayClass>::Destruct>);   // 00519B30
EI_REPLACE(0x00532E30, ScalarDelete<&ArrayHeader<MotionPointArrayClass>::Destruct>);   // 0052E240
EI_REPLACE(0x00532E50, ScalarDelete<&ArrayHeader<MotionWaypointBaseClass>::Destruct>);   // 0052E260
EI_REPLACE(0x00533520, ScalarDelete<&ArrayHeader<MotionPointArrayClass>::Destruct>);   // 00533540
EI_REPLACE(0x00533560, ScalarDelete<&ArrayHeader<MotionWaypointBaseClass>::Destruct>);   // 00533580
EI_REPLACE(0x005335A0, ScalarDelete<&ArrayHeader<CarriedArrayClass>::Destruct>);   // 005331C0
EI_REPLACE(0x005335C0, ScalarDelete<&ArrayHeader<IdArrayClass>::Destruct>);   // 00533370
EI_REPLACE(0x00539870, ScalarDelete<&PtrList::Destruct<CellListClass>>);   // 00539890
EI_REPLACE(0x005398F0, ScalarDelete<&ArrayHeader<SkinLayerArrayClass>::Destruct>);   // 005396C0
EI_REPLACE(0x00556C00, ScalarDelete<&ArrayHeader<AnimationStepsClass>::Destruct>);   // 00556C20
EI_REPLACE(0x00558740, ScalarDelete<&ArrayHeader<PointArrayClass>::Destruct>);   // 00557250
EI_REPLACE(0x00558760, ScalarDelete<&ArrayHeader<AttackOptionArrayClass>::Destruct>);   // 00557410
EI_REPLACE(0x00558800, ScalarDelete<&ArrayHeader<AnimationStepsClass>::Destruct>);   // 00557E70
EI_REPLACE(0x00558880, ScalarDelete<&PtrList::Destruct<LootListClass>>);   // 00558470
EI_REPLACE(0x00560490, ScalarDelete<&ArrayHeader<CountdownArrayClass>::Destruct>);   // 005604B0
EI_REPLACE(0x00571E90, ScalarDelete<&PtrList::Destruct<ClientObjectListClass>>);   // 00570510
EI_REPLACE(0x00571EB0, ScalarDelete<&PtrList::Destruct<ClientList2Class>>);   // 00570ED0
EI_REPLACE(0x00571FA0, ScalarDelete<&PtrList::Destruct<ClientList3Class>>);   // 00571600
EI_REPLACE(0x0059B410, ScalarDelete<&ArrayHeader<CellRowArrayClass>::Destruct>);   // 00599180
EI_REPLACE(0x0059B450, ScalarDelete<&PtrList::Destruct<NodeListClass>>);   // 005995E0
EI_REPLACE(0x0059B470, ScalarDelete<&ArrayHeader<AreaListClass>::Destruct>);   // 005997A0
EI_REPLACE(0x0059B540, ScalarDelete<&ArrayHeader<PartyUnitArrayClass>::Destruct>);   // 0059B0B0
EI_REPLACE(0x0059B560, ScalarDelete<&PtrList::Destruct<ExperienceListClass>>);   // 0059B260
EI_REPLACE(0x0059E240, ScalarDelete<&ArrayHeader<SectorOccluderArrayClass>::Destruct>);   // 0059E0A0
EI_REPLACE(0x005C1C30, ScalarDelete<&ArrayHeader<SectorCellArrayClass>::Destruct>);   // 005C18E0
EI_REPLACE(0x005C1C50, ScalarDelete<&ArrayHeader<SpiralCellArrayClass>::Destruct>);   // 005C1A90
EI_REPLACE(0x005C5830, ScalarDelete<&ArrayHeader<SmoothRowArrayClass>::Destruct>);   // 005C5850
EI_REPLACE(0x005C5870, ScalarDelete<&ArrayHeader<SmoothEdgeArrayClass>::Destruct>);   // 005C5890
EI_REPLACE(0x005C58B0, ScalarDelete<&ArrayHeader<SmoothCopyArrayClass>::Destruct>);   // 005C58D0
EI_REPLACE(0x005C58F0, ScalarDelete<&ArrayHeader<SmoothStepArrayClass>::Destruct>);   // 005C5910
EI_REPLACE(0x005CDD40, ScalarDelete<&PtrList::Destruct<StaticListClass>>);   // 005CDBA0
EI_REPLACE(0x005D9D20, ScalarDelete<&PtrList::Destruct<AttentionListClass>>);   // 005D99E0
EI_REPLACE(0x005EBD90, ScalarDelete<&ArrayHeader<UnitPtrArrayClass>::Destruct>);   // 005EBBE0
EI_REPLACE(0x0060ABC0, ScalarDelete<&ArrayHeader<ObjectiveExitArrayClass>::Destruct>);   // 0060A870
EI_REPLACE(0x00613A10, ScalarDelete<&ArrayHeader<ItemStripArrayClass>::Destruct>);   // 00613A30
EI_REPLACE(0x0064B630, ScalarDelete<&ArrayHeader<QuickEntryArrayClass>::Destruct>);   // 0064B1C0
EI_REPLACE(0x0065DFD0, ScalarDelete<&ArrayHeader<TokenPtrArrayClass>::Destruct>);   // 0065DC70
EI_REPLACE(0x0065DFF0, ScalarDelete<&ArrayHeader<LinePtrArrayClass>::Destruct>);   // 0065DE20
EI_REPLACE(0x00666170, ScalarDelete<&PtrList::Destruct<PlayerListClass>>);   // 00665FF0
EI_REPLACE(0x006942E0, ScalarDelete<&PtrList::Destruct<TextureListClass>>);   // 00694160
EI_REPLACE(0x006BD170, ScalarDelete<&PtrList::Destruct<SoundListClass>>);   // 006BC570
EI_REPLACE(0x006CDBD0, ScalarDelete<&ArrayHeader<TerrainArrayAClass>::Destruct>);   // 006CD6D0
EI_REPLACE(0x006CDBF0, ScalarDelete<&ArrayHeader<TerrainArrayBClass>::Destruct>);   // 006CD880
EI_REPLACE(0x006CDC10, ScalarDelete<&ArrayHeader<TerrainArrayCClass>::Destruct>);   // 006CDA20
EI_REPLACE(0x004CAFD0, ScalarDelete<&Strings::Destruct<StringArrayClass>>);   // 004C26A0

// ---- Serialize (vtable slot 2) -------------------------------------------------------------------

// Arrays of plain elements (zero filled when SetSize builds them; the comment names the vtable).
EI_REPLACE(0x0044F650, SerializeCArray<Plain12>);   // Vector3ArrayClass
EI_REPLACE(0x00450A00, SerializeCArray<Plain20>);   // NodeItemsAClass
EI_REPLACE(0x00450BB0, SerializeCArray<Plain24>);   // NodeItemsBClass
EI_REPLACE(0x004511F0, SerializeCArray<Plain4>);   // NodeItemsCClass
EI_REPLACE(0x00451850, SerializeCArray<Plain8>);   // TextureLinkArrayClass
EI_REPLACE(0x00451EE0, SerializeCArray<Plain16>);   // MeshCornerArrayClass
EI_REPLACE(0x0045D950, SerializeCArray<Plain20>);   // ArrayBaseClass
EI_REPLACE(0x0045FA40, SerializeCArray<Plain4>);   // IntArrayClass
EI_REPLACE(0x00468DA0, SerializeCArray<Plain8>);   // SpriteArrayClass
EI_REPLACE(0x0046EC10, SerializeCArray<Plain12>);   // TrapCircleArrayClass
EI_REPLACE(0x0046EDA0, SerializeCArray<Plain8>);   // TrapPointArrayClass
EI_REPLACE(0x00474F50, SerializeCArray<Plain12>);   // FilePointArrayClass
EI_REPLACE(0x00475130, SerializeCArray<Plain12>);   // FileUvArrayClass
EI_REPLACE(0x00475310, SerializeCArray<Plain28>);   // FileFaceArrayClass
EI_REPLACE(0x004756A0, SerializeCArray<Plain20>);   // RotKeyArrayClass
EI_REPLACE(0x00475A00, SerializeCArray<Plain24>);   // PosKeyArrayClass
EI_REPLACE(0x00475D60, SerializeCArray<Plain8>);   // MorphHeadArrayClass
EI_REPLACE(0x004767A0, SerializeCArray<Plain4>);   // IndexArrayClass
EI_REPLACE(0x00476AD0, SerializeCArray<Plain20>);   // NormalArrayClass
EI_REPLACE(0x00498EA0, SerializeCArray<Plain8>);   // Array0498Class
EI_REPLACE(0x004C1E70, SerializeCArray<Plain20>);   // ScriptAreaClass
EI_REPLACE(0x004C29E0, SerializeCArray<Plain4>);   // ScriptFloatsClass
EI_REPLACE(0x004C4560, SerializeCArray<Plain4>);   // FloatArrayClass
EI_REPLACE(0x004C4860, SerializeCArray<Plain4>);   // ArgObjectsBaseClass
EI_REPLACE(0x004C4B50, SerializeCArray<Plain4>);   // ArgGroupsBaseClass
EI_REPLACE(0x004C4E40, SerializeCArray<Plain12>);   // BindingsBaseClass
EI_REPLACE(0x004C5B10, SerializeCArray<Plain8>);   // ScriptIfThenArrayClass
EI_REPLACE(0x004C9B10, SerializeCArray<Plain16>);   // AiLookArrayClass
EI_REPLACE(0x0050FFD0, SerializeCArray<Plain20>);   // EffectArrayClass
EI_REPLACE(0x00532E70, SerializeCArray<Plain28>);   // MotionPointArrayClass
EI_REPLACE(0x00533030, SerializeCArray<Plain12>);   // MotionWaypointBaseClass
EI_REPLACE(0x005331E0, SerializeCArray<Plain4>);   // CarriedArrayClass
EI_REPLACE(0x00533390, SerializeCArray<Plain4>);   // IdArrayClass
EI_REPLACE(0x00557270, SerializeCArray<Plain8>);   // PointArrayClass
EI_REPLACE(0x00557430, SerializeCArray<Plain40>);   // AttackOptionArrayClass
EI_REPLACE(0x00560200, SerializeCArray<Plain16>);   // CountdownArrayClass
EI_REPLACE(0x005991A0, SerializeCArray<Plain4>);   // CellRowArrayClass
EI_REPLACE(0x005997C0, SerializeCArray<Plain8>);   // AreaListClass
EI_REPLACE(0x0059B0D0, SerializeCArray<Plain4>);   // PartyUnitArrayClass
EI_REPLACE(0x005C1900, SerializeCArray<Plain4>);   // SectorCellArrayClass
EI_REPLACE(0x005C4D80, SerializeCArray<Plain8>);   // SmoothRowArrayClass
EI_REPLACE(0x005C5070, SerializeCArray<Plain20>);   // SmoothEdgeArrayClass
EI_REPLACE(0x005C5360, SerializeCArray<Plain8>);   // SmoothCopyArrayClass
EI_REPLACE(0x005C5670, SerializeCArray<Plain36>);   // SmoothStepArrayClass
EI_REPLACE(0x005EBC00, SerializeCArray<Plain4>);   // UnitPtrArrayClass
EI_REPLACE(0x0060A890, SerializeCArray<Plain28>);   // ObjectiveExitArrayClass
EI_REPLACE(0x00613690, SerializeCArray<Plain4>);   // ItemStripArrayClass
EI_REPLACE(0x0064B1E0, SerializeCArray<Plain12>);   // QuickEntryArrayClass
EI_REPLACE(0x0065DC90, SerializeCArray<Plain4>);   // TokenPtrArrayClass
EI_REPLACE(0x0065DE40, SerializeCArray<Plain4>);   // LinePtrArrayClass
EI_REPLACE(0x006CD6F0, SerializeCArray<Plain4>);   // TerrainArrayAClass
EI_REPLACE(0x006CDA40, SerializeCArray<Plain4>);   // TerrainArrayCClass

// Lists of plain elements.
EI_REPLACE(0x00405190, SerializeCList<PtrList>);   // TempListClass
EI_REPLACE(0x00427890, SerializeCList<PtrList>);   // ItemListClass
EI_REPLACE(0x00498D00, SerializeCList<PtrList>);   // ClientListClass
EI_REPLACE(0x004C9750, SerializeCList<PtrList>);   // PtrListClass
EI_REPLACE(0x0050DC20, SerializeCList<PtrList>);   // BoneListClass
EI_REPLACE(0x0050DE90, SerializeCList<PtrList>);   // AnimationStateListClass
EI_REPLACE(0x005198D0, SerializeCList<PtrList>);   // Object3DListClass
EI_REPLACE(0x00539440, SerializeCList<List8>);   // CellListClass
EI_REPLACE(0x005584D0, SerializeCList<PtrList>);   // LootListClass
EI_REPLACE(0x00570570, SerializeCList<PtrList>);   // ClientObjectListClass
EI_REPLACE(0x00570F30, SerializeCList<PtrList>);   // ClientList2Class
EI_REPLACE(0x00571660, SerializeCList<PtrList>);   // ClientList3Class
EI_REPLACE(0x00599640, SerializeCList<List1C>);   // NodeListClass
EI_REPLACE(0x0059B2C0, SerializeCList<List8>);   // ExperienceListClass
EI_REPLACE(0x00666050, SerializeCList<PtrList>);   // PlayerListClass
EI_REPLACE(0x006941C0, SerializeCList<PtrList>);   // TextureListClass
EI_REPLACE(0x006BC5D0, SerializeCList<PtrList>);   // SoundListClass

// CMap<DWORD, DWORD> and the corner maps (64-bit keys).
EI_REPLACE(0x00451CC0, SerializeCMap<Map32>);   // FigureRegistryMapClass
EI_REPLACE(0x00452110, SerializeCMap<Map64x64>);   // CornerMapAClass
EI_REPLACE(0x004523B0, SerializeCMap<Map64x32>);   // CornerMapBClass
EI_REPLACE(0x00452650, SerializeCMap<Map32>);   // CornerMapCClass
EI_REPLACE(0x004761C0, SerializeCMap<Map32>);   // DwordMapClass

// Arrays whose elements SetSize builds (zeroed, then their fields) and nothing destroys.
EI_REPLACE(0x00451060, SerializeCArray<KeyFrames>);   // vtable 0073C12C
EI_REPLACE(0x004C9510, SerializeCArray<Placements>);   // PartySlotArrayClass
EI_REPLACE(0x00511E20, SerializeCArray<SoundRecords>);   // EffectSoundArrayClass
EI_REPLACE(0x005396E0, SerializeCArray<PathRecords>);   // SkinLayerArrayClass
EI_REPLACE(0x00557E90, SerializeCArray<AnimationSteps>);   // AnimationStepsClass
EI_REPLACE(0x0059E0C0, SerializeCArray<Plain8>);   // SectorOccluderArrayClass
EI_REPLACE(0x005C1AB0, SerializeCArray<Plain8>);   // SpiralCellArrayClass
EI_REPLACE(0x006CD8A0, SerializeCArray<Plain16>);   // TerrainArrayBClass
EI_REPLACE(0x00519B50, SerializeCArray<Plain56>);   // FrameArrayClass

// Arrays whose elements SetSize builds and destroys (a shrinking load destroys the ones past the count).
EI_REPLACE(0x0045DC60, SerializeCArray<OwnedPatrols>);   // PatrolArrayClass
EI_REPLACE(0x0045FCB0, SerializeCArray<OwnedAllods>);   // AllodArrayClass
EI_REPLACE(0x004C88E0, SerializeCArray<ScriptAreas>);   // ScriptAreaArrayClass
EI_REPLACE(0x006C4A40, SerializeCArray<ByteArrays>);   // TerrainByteArraysClass
EI_REPLACE(0x005D9610, SerializeCArray<AiLookRecords>);   // vtable 0073FF74
EI_REPLACE(0x00450D40, SerializeCArray<AlignedAt4Records>);   // AlignedAt4ArrayClass
EI_REPLACE(0x0050A210, SerializeCArray<AlignedAt8Records>);   // AlignedAt8ArrayClass
EI_REPLACE(0x00450570, SerializeCArray<OwnedBoneLinkArrays>);   // FakeUnitBoneArraysClass
EI_REPLACE(0x00476430, SerializeCArray<OwnedDwordMaps>);   // SceneObjectArrayClass
EI_REPLACE(0x006C4C90, SerializeCArray<AlignedArrays>);   // vtable 007441B4
EI_REPLACE(0x00451A70, SerializeCArray<NamedStrings>);   // TextureNameArrayClass

// Records with CStrings.
EI_REPLACE(0x005FB120, SerializeCArray<GlobalMapZoneRecords>);    // GlobalMapZoneArrayClass
EI_REPLACE(0x005FB380, SerializeCArray<GlobalMapPointRecords>);   // GlobalMapPointArrayClass
EI_REPLACE(0x005FB5F0, SerializeCArray<GlobalMapPointRecords>);   // GlobalMapPathArrayClass
EI_REPLACE(0x00557050, SerializeCArray<MemoryCallRecords>);       // MemoryRecordArrayClass

// More lists of plain elements (the AI's, the client maps' platforms) and AIAttention's records.
EI_REPLACE(0x0056FEA0, SerializeCList<List4C>);          // PlatformListClass
EI_REPLACE(0x005B1870, SerializeCList<List12>);          // BlockCostListClass
EI_REPLACE(0x005BDF40, SerializeCList<List12>);          // WaypointListClass
EI_REPLACE(0x005B1A00, SerializeCList<List8>);           // AiMapListClass
EI_REPLACE(0x005CDC00, SerializeCList<List30>);          // StaticListClass
EI_REPLACE(0x005D9A40, SerializeCList<AttentionList>);   // AttentionListClass

} // namespace mfc::impl
