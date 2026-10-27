// The scalar deleting destructors the exe's vtables point at and the destructors they call, for the
// instantiations whose elements need nothing of the game (mfc_slots.h); the game-element
// ones are in src/game/game_slots.cpp. Each deleting destructor calls its class's destructor directly
// (by address), the destructors are templates.
#include "mfc_slots.h"

#include "crt.h"
#include "ei/hook.h"

namespace mfc::impl {

void __fastcall ExceptionDestruct(libcrt::Exception* self) { self->Destruct(); }

void __stdcall SerializeStrings(CArchive* ar, CString* strings, int count) {
    if (!ar->IsLoading())
        for (; count--; ++strings) StoreText(*ar, *strings);
    else
        for (; count--; ++strings) LoadText(*ar, *strings);
}

// ---- Destructors ----------------------------------------------------------------------------------

// Arrays: the vtable, the storage freed.
EI_REWRITTEN(0x00452870, ArrayHeader<Vector3ArrayClass>::Destruct);
EI_REWRITTEN(0x004509E0, ArrayHeader<NodeItemsAClass>::Destruct);
EI_REWRITTEN(0x00450B90, ArrayHeader<NodeItemsBClass>::Destruct);
EI_REWRITTEN(0x00452990, ArrayHeader<BoneLinkArrayClass>::Destruct);
EI_REWRITTEN(0x004529D0, ArrayHeader<NodeItemsCClass>::Destruct);
EI_REWRITTEN(0x00451830, ArrayHeader<TextureLinkArrayClass>::Destruct);
EI_REWRITTEN(0x00451EC0, ArrayHeader<MeshCornerArrayClass>::Destruct);
EI_REWRITTEN(0x0045E020, ArrayHeader<ArrayBaseClass>::Destruct);
EI_REWRITTEN(0x00468D80, ArrayHeader<SpriteArrayClass>::Destruct);
EI_REWRITTEN(0x0045D8B0, OwnedPatrols::Destruct<PatrolArrayClass>);
EI_REWRITTEN(0x0045E090, OwnedPatrols::Destruct<PatrolArrayClass>);
EI_REWRITTEN(0x004BE430, ScriptAreas::Destruct<ScriptAreaArrayClass>);
EI_REWRITTEN(0x004515F0, AlignedAt4Records::Destruct<AlignedAt4ArrayClass>);
EI_REWRITTEN(0x0050A1C0, AlignedAt8Records::Destruct<AlignedAt8ArrayClass>);
EI_REWRITTEN(0x00450500, FakeUnitBoneArrays::Destruct<FakeUnitBoneArraysClass>);
EI_REWRITTEN(0x006C49D0, ByteArrays::Destruct<TerrainByteArraysClass>);
EI_REWRITTEN(0x004763C0, SceneObjects::Destruct<SceneObjectArrayClass>);
EI_REWRITTEN(0x00451C40, Map32::Destruct<FigureRegistryMapClass>);
EI_REWRITTEN(0x00452090, Map32::Destruct<CornerMapAClass>);
EI_REWRITTEN(0x00452330, Map32::Destruct<CornerMapBClass>);
EI_REWRITTEN(0x004525D0, Map32::Destruct<CornerMapCClass>);
EI_REWRITTEN(0x00498CA0, PtrList::Destruct<ClientListClass>);
EI_REWRITTEN(0x0050DBC0, PtrList::Destruct<BoneListClass>);
EI_REWRITTEN(0x0050DE30, PtrList::Destruct<AnimationStateListClass>);
EI_REWRITTEN(0x00519870, PtrList::Destruct<Object3DListClass>);
EI_REWRITTEN(0x00570510, PtrList::Destruct<ClientObjectListClass>);
EI_REWRITTEN(0x00570ED0, PtrList::Destruct<ClientList2Class>);
EI_REWRITTEN(0x00571600, PtrList::Destruct<ClientList3Class>);
EI_REWRITTEN(0x0059B260, PtrList::Destruct<ExperienceListClass>);
EI_REWRITTEN(0x005D99E0, PtrList::Destruct<AttentionListClass>);
EI_REWRITTEN(0x00665FF0, PtrList::Destruct<PlayerListClass>);
EI_REWRITTEN(0x00694160, PtrList::Destruct<TextureListClass>);
EI_REWRITTEN(0x006BC570, PtrList::Destruct<SoundListClass>);
EI_REWRITTEN(0x0046E910, ArrayHeader<TrapCircleArrayClass>::Destruct);
EI_REWRITTEN(0x0046E950, ArrayHeader<TrapPointArrayClass>::Destruct);
EI_REWRITTEN(0x0046EF40, ArrayHeader<TrapCircleArrayClass>::Destruct);
EI_REWRITTEN(0x0046EF80, ArrayHeader<TrapPointArrayClass>::Destruct);
EI_REWRITTEN(0x004C1820, ArrayHeader<ScriptAreaClass>::Destruct);
EI_REWRITTEN(0x004BC980, ArrayHeader<ScriptFloatsClass>::Destruct);
EI_REWRITTEN(0x004BE410, ArrayHeader<ScriptIfThenArrayClass>::Destruct);
EI_REWRITTEN(0x004CAF90, ArrayHeader<ScriptAreaClass>::Destruct);
EI_REWRITTEN(0x004C4840, ArrayHeader<ArgObjectsBaseClass>::Destruct);
EI_REWRITTEN(0x004C4B30, ArrayHeader<ArgGroupsBaseClass>::Destruct);
EI_REWRITTEN(0x004C4E20, ArrayHeader<BindingsBaseClass>::Destruct);
EI_REWRITTEN(0x004CB710, ArrayHeader<PartySlotArrayClass>::Destruct);
EI_REWRITTEN(0x004BF760, ArrayHeader<FloatArrayClass>::Destruct);
EI_REWRITTEN(0x004BF780, ArrayHeader<ArgObjectsBaseClass>::Destruct);
EI_REWRITTEN(0x004BF7A0, ArrayHeader<ArgGroupsBaseClass>::Destruct);
EI_REWRITTEN(0x004BF7C0, ArrayHeader<BindingsBaseClass>::Destruct);
EI_REWRITTEN(0x004CDF00, ArrayHeader<AiLookArrayClass>::Destruct);
EI_REWRITTEN(0x0050FFB0, ArrayHeader<EffectArrayClass>::Destruct);
EI_REWRITTEN(0x00511E00, ArrayHeader<EffectSoundArrayClass>::Destruct);
EI_REWRITTEN(0x00519B30, ArrayHeader<FrameArrayClass>::Destruct);
EI_REWRITTEN(0x0052E240, ArrayHeader<MotionPointArrayClass>::Destruct);
EI_REWRITTEN(0x0052E260, ArrayHeader<MotionWaypointBaseClass>::Destruct);
EI_REWRITTEN(0x00533540, ArrayHeader<MotionPointArrayClass>::Destruct);
EI_REWRITTEN(0x00533580, ArrayHeader<MotionWaypointBaseClass>::Destruct);
EI_REWRITTEN(0x005331C0, ArrayHeader<CarriedArrayClass>::Destruct);
EI_REWRITTEN(0x00533370, ArrayHeader<IdArrayClass>::Destruct);
EI_REWRITTEN(0x005396C0, ArrayHeader<SkinLayerArrayClass>::Destruct);
EI_REWRITTEN(0x00556C20, ArrayHeader<AnimationStepsClass>::Destruct);
EI_REWRITTEN(0x005604B0, ArrayHeader<CountdownArrayClass>::Destruct);
EI_REWRITTEN(0x0059B0B0, ArrayHeader<PartyUnitArrayClass>::Destruct);
EI_REWRITTEN(0x0059E0A0, ArrayHeader<SectorOccluderArrayClass>::Destruct);
EI_REWRITTEN(0x005C1A90, ArrayHeader<SpiralCellArrayClass>::Destruct);
EI_REWRITTEN(0x005C5890, ArrayHeader<SmoothEdgeArrayClass>::Destruct);
EI_REWRITTEN(0x005C58D0, ArrayHeader<SmoothCopyArrayClass>::Destruct);
EI_REWRITTEN(0x005C5910, ArrayHeader<SmoothStepArrayClass>::Destruct);
EI_REWRITTEN(0x005EBBE0, ArrayHeader<UnitPtrArrayClass>::Destruct);
EI_REWRITTEN(0x0060A870, ArrayHeader<ObjectiveExitArrayClass>::Destruct);
EI_REWRITTEN(0x00613A30, ArrayHeader<ItemStripArrayClass>::Destruct);
EI_REWRITTEN(0x0064B1C0, ArrayHeader<QuickEntryArrayClass>::Destruct);
EI_REWRITTEN(0x0065DC70, ArrayHeader<TokenPtrArrayClass>::Destruct);
EI_REWRITTEN(0x0065DE20, ArrayHeader<LinePtrArrayClass>::Destruct);
EI_REWRITTEN(0x006CD6D0, ArrayHeader<TerrainArrayAClass>::Destruct);
EI_REWRITTEN(0x006CD880, ArrayHeader<TerrainArrayBClass>::Destruct);
EI_REWRITTEN(0x006CDA20, ArrayHeader<TerrainArrayCClass>::Destruct);

// The exception classes' own destructors (a jump to std::exception's; their throw information's).
EI_REWRITTEN(0x0040C4A0, ExceptionDestruct);
EI_REWRITTEN(0x0040C4D0, ExceptionDestruct);
EI_REPLACE(0x0040C500, ExceptionDestruct);
EI_REPLACE(0x0041F760, ExceptionDestruct);

// ---- Scalar deleting destructors (vtable slot 1; the exception classes' slot 0) --------------------

// Over the destructors of arrays whose elements own something.
EI_REWRITTEN(0x004528D0, ScalarDeleting<&FakeUnitBoneArrays::Destruct<FakeUnitBoneArraysClass>>::Delete);
EI_REWRITTEN(0x006C4EA0, ScalarDeleting<&ByteArrays::Destruct<TerrainByteArraysClass>>::Delete);
EI_REWRITTEN(0x00476D70, ScalarDeleting<&SceneObjects::Destruct<SceneObjectArrayClass>>::Delete);
EI_REWRITTEN(0x00452950, ScalarDeleting<&AlignedAt4Records::Destruct<AlignedAt4ArrayClass>>::Delete);
EI_REWRITTEN(0x0050A410, ScalarDeleting<&AlignedAt8Records::Destruct<AlignedAt8ArrayClass>>::Delete);
EI_REWRITTEN(0x0045D890, ScalarDeleting<&OwnedPatrols::Destruct<PatrolArrayClass>>::Delete);
EI_REWRITTEN(0x0045E070, ScalarDeleting<&OwnedPatrols::Destruct<PatrolArrayClass>>::Delete);
EI_REWRITTEN(0x004C1A60, ScalarDeleting<&ScriptAreas::Destruct<ScriptAreaArrayClass>>::Delete);
EI_REWRITTEN(0x004CB630, ScalarDeleting<&ScriptAreas::Destruct<ScriptAreaArrayClass>>::Delete);

// Over the destructors of the arrays, lists and maps whose elements need nothing.

EI_REWRITTEN(0x00405540, ScalarDeleting<&PtrList::Destruct<TempListClass>>::Delete);
EI_REWRITTEN(0x0040C480, ScalarDeleting<&ExceptionDestruct>::Delete);
EI_REWRITTEN(0x0040C4B0, ScalarDeleting<&ExceptionDestruct>::Delete);
EI_REWRITTEN(0x0040C4E0, ScalarDeleting<&ExceptionDestruct>::Delete);
EI_REWRITTEN(0x0041F740, ScalarDeleting<&ExceptionDestruct>::Delete);
EI_REWRITTEN(0x00429180, ScalarDeleting<&PtrList::Destruct<ItemListClass>>::Delete);
EI_REWRITTEN(0x00452850, ScalarDeleting<&ArrayHeader<Vector3ArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00452910, ScalarDeleting<&ArrayHeader<NodeItemsAClass>::Destruct>::Delete);
EI_REWRITTEN(0x00452930, ScalarDeleting<&ArrayHeader<NodeItemsBClass>::Destruct>::Delete);
EI_REWRITTEN(0x00452970, ScalarDeleting<&ArrayHeader<BoneLinkArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x004529B0, ScalarDeleting<&ArrayHeader<NodeItemsCClass>::Destruct>::Delete);
EI_REWRITTEN(0x00452A10, ScalarDeleting<&ArrayHeader<TextureLinkArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00452A50, ScalarDeleting<&Map32::Destruct<FigureRegistryMapClass>>::Delete);
EI_REWRITTEN(0x00452A70, ScalarDeleting<&ArrayHeader<MeshCornerArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00452A90, ScalarDeleting<&Map32::Destruct<CornerMapAClass>>::Delete);
EI_REWRITTEN(0x00452AB0, ScalarDeleting<&Map32::Destruct<CornerMapBClass>>::Delete);
EI_REWRITTEN(0x00452AD0, ScalarDeleting<&Map32::Destruct<CornerMapCClass>>::Delete);
EI_REWRITTEN(0x0045E000, ScalarDeleting<&ArrayHeader<ArrayBaseClass>::Destruct>::Delete);
EI_REWRITTEN(0x0045E230, ScalarDeleting<&ArrayHeader<ArrayBaseClass>::Destruct>::Delete);
EI_REWRITTEN(0x004600B0, ScalarDeleting<&ArrayHeader<IntArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00468F50, ScalarDeleting<&ArrayHeader<SpriteArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0046E8F0, ScalarDeleting<&ArrayHeader<TrapCircleArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0046E930, ScalarDeleting<&ArrayHeader<TrapPointArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0046EF20, ScalarDeleting<&ArrayHeader<TrapCircleArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0046EF60, ScalarDeleting<&ArrayHeader<TrapPointArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00476C90, ScalarDeleting<&ArrayHeader<FilePointArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00476CB0, ScalarDeleting<&ArrayHeader<FileUvArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00476CD0, ScalarDeleting<&ArrayHeader<FileFaceArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00476CF0, ScalarDeleting<&ArrayHeader<RotKeyArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00476D10, ScalarDeleting<&ArrayHeader<PosKeyArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00476D30, ScalarDeleting<&ArrayHeader<MorphHeadArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00476D50, ScalarDeleting<&Map32::Destruct<DwordMapClass>>::Delete);
EI_REWRITTEN(0x00476D90, ScalarDeleting<&ArrayHeader<IndexArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00476DB0, ScalarDeleting<&ArrayHeader<NormalArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00499020, ScalarDeleting<&PtrList::Destruct<ClientListClass>>::Delete);
EI_REWRITTEN(0x004C1800, ScalarDeleting<&ArrayHeader<ScriptAreaClass>::Destruct>::Delete);
EI_REWRITTEN(0x004C1880, ScalarDeleting<&ArrayHeader<ScriptFloatsClass>::Destruct>::Delete);
EI_REWRITTEN(0x004C1A00, ScalarDeleting<&ArrayHeader<ScriptIfThenArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CAF70, ScalarDeleting<&ArrayHeader<ScriptAreaClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CAFF0, ScalarDeleting<&ArrayHeader<ScriptFloatsClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CB030, ScalarDeleting<&ArrayHeader<FloatArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CB070, ScalarDeleting<&ArrayHeader<ArgObjectsBaseClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CB090, ScalarDeleting<&ArrayHeader<ArgGroupsBaseClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CB0B0, ScalarDeleting<&ArrayHeader<BindingsBaseClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CB110, ScalarDeleting<&ArrayHeader<ScriptIfThenArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CB6F0, ScalarDeleting<&ArrayHeader<PartySlotArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CB730, ScalarDeleting<&PtrList::Destruct<PtrListClass>>::Delete);
EI_REWRITTEN(0x004CB750, ScalarDeleting<&ArrayHeader<AiLookArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CCED0, ScalarDeleting<&ArrayHeader<FloatArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CCEF0, ScalarDeleting<&ArrayHeader<ArgObjectsBaseClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CCF10, ScalarDeleting<&ArrayHeader<ArgGroupsBaseClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CCF30, ScalarDeleting<&ArrayHeader<BindingsBaseClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CDF20, ScalarDeleting<&ArrayHeader<AiLookArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0050DFB0, ScalarDeleting<&PtrList::Destruct<BoneListClass>>::Delete);
EI_REWRITTEN(0x0050DFD0, ScalarDeleting<&PtrList::Destruct<AnimationStateListClass>>::Delete);
EI_REWRITTEN(0x00510180, ScalarDeleting<&ArrayHeader<EffectArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00512010, ScalarDeleting<&ArrayHeader<EffectSoundArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00519D30, ScalarDeleting<&PtrList::Destruct<Object3DListClass>>::Delete);
EI_REWRITTEN(0x00519D50, ScalarDeleting<&ArrayHeader<FrameArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00532E30, ScalarDeleting<&ArrayHeader<MotionPointArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00532E50, ScalarDeleting<&ArrayHeader<MotionWaypointBaseClass>::Destruct>::Delete);
EI_REWRITTEN(0x00533520, ScalarDeleting<&ArrayHeader<MotionPointArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00533560, ScalarDeleting<&ArrayHeader<MotionWaypointBaseClass>::Destruct>::Delete);
EI_REWRITTEN(0x005335A0, ScalarDeleting<&ArrayHeader<CarriedArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x005335C0, ScalarDeleting<&ArrayHeader<IdArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00539870, ScalarDeleting<&PtrList::Destruct<CellListClass>>::Delete);
EI_REWRITTEN(0x005398F0, ScalarDeleting<&ArrayHeader<SkinLayerArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00556C00, ScalarDeleting<&ArrayHeader<AnimationStepsClass>::Destruct>::Delete);
EI_REWRITTEN(0x00558740, ScalarDeleting<&ArrayHeader<PointArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00558760, ScalarDeleting<&ArrayHeader<AttackOptionArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00558800, ScalarDeleting<&ArrayHeader<AnimationStepsClass>::Destruct>::Delete);
EI_REWRITTEN(0x00558880, ScalarDeleting<&PtrList::Destruct<LootListClass>>::Delete);
EI_REWRITTEN(0x00560490, ScalarDeleting<&ArrayHeader<CountdownArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00571E90, ScalarDeleting<&PtrList::Destruct<ClientObjectListClass>>::Delete);
EI_REWRITTEN(0x00571EB0, ScalarDeleting<&PtrList::Destruct<ClientList2Class>>::Delete);
EI_REWRITTEN(0x00571FA0, ScalarDeleting<&PtrList::Destruct<ClientList3Class>>::Delete);
EI_REWRITTEN(0x0059B410, ScalarDeleting<&ArrayHeader<CellRowArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0059B450, ScalarDeleting<&PtrList::Destruct<NodeListClass>>::Delete);
EI_REWRITTEN(0x0059B470, ScalarDeleting<&ArrayHeader<AreaListClass>::Destruct>::Delete);
EI_REWRITTEN(0x0059B540, ScalarDeleting<&ArrayHeader<PartyUnitArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0059B560, ScalarDeleting<&PtrList::Destruct<ExperienceListClass>>::Delete);
EI_REWRITTEN(0x0059E240, ScalarDeleting<&ArrayHeader<SectorOccluderArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x005C1C50, ScalarDeleting<&ArrayHeader<SpiralCellArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x005C5870, ScalarDeleting<&ArrayHeader<SmoothEdgeArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x005C58B0, ScalarDeleting<&ArrayHeader<SmoothCopyArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x005C58F0, ScalarDeleting<&ArrayHeader<SmoothStepArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x005CDD40, ScalarDeleting<&PtrList::Destruct<StaticListClass>>::Delete);
EI_REWRITTEN(0x005D9D20, ScalarDeleting<&PtrList::Destruct<AttentionListClass>>::Delete);
EI_REWRITTEN(0x005EBD90, ScalarDeleting<&ArrayHeader<UnitPtrArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0060ABC0, ScalarDeleting<&ArrayHeader<ObjectiveExitArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00613A10, ScalarDeleting<&ArrayHeader<ItemStripArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0064B630, ScalarDeleting<&ArrayHeader<QuickEntryArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0065DFD0, ScalarDeleting<&ArrayHeader<TokenPtrArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x0065DFF0, ScalarDeleting<&ArrayHeader<LinePtrArrayClass>::Destruct>::Delete);
EI_REWRITTEN(0x00666170, ScalarDeleting<&PtrList::Destruct<PlayerListClass>>::Delete);
EI_REWRITTEN(0x006942E0, ScalarDeleting<&PtrList::Destruct<TextureListClass>>::Delete);
EI_REWRITTEN(0x006BD170, ScalarDeleting<&PtrList::Destruct<SoundListClass>>::Delete);
EI_REWRITTEN(0x006CDBD0, ScalarDeleting<&ArrayHeader<TerrainArrayAClass>::Destruct>::Delete);
EI_REWRITTEN(0x006CDBF0, ScalarDeleting<&ArrayHeader<TerrainArrayBClass>::Destruct>::Delete);
EI_REWRITTEN(0x006CDC10, ScalarDeleting<&ArrayHeader<TerrainArrayCClass>::Destruct>::Delete);
EI_REWRITTEN(0x004CAFD0, ScalarDeleting<&Strings::Destruct<StringArrayClass>>::Delete);

// ---- Serialize (vtable slot 2) -------------------------------------------------------------------

// Arrays of plain elements (zero filled when SetSize builds them; the comment names the vtable).
EI_REWRITTEN(0x0044F650, Plain12::Serialize);   // Vector3ArrayClass
EI_REWRITTEN(0x00450A00, Plain20::Serialize);   // NodeItemsAClass
EI_REWRITTEN(0x00450BB0, Plain24::Serialize);   // NodeItemsBClass
EI_REWRITTEN(0x004511F0, Plain4::Serialize);   // NodeItemsCClass
EI_REWRITTEN(0x00451850, Plain8::Serialize);   // TextureLinkArrayClass
EI_REWRITTEN(0x00451EE0, Plain16::Serialize);   // MeshCornerArrayClass
EI_REWRITTEN(0x0045D950, Plain20::Serialize);   // ArrayBaseClass
EI_REWRITTEN(0x0045FA40, Plain4::Serialize);   // IntArrayClass
EI_REWRITTEN(0x00468DA0, Plain8::Serialize);   // SpriteArrayClass
EI_REWRITTEN(0x0046EC10, Plain12::Serialize);   // TrapCircleArrayClass
EI_REWRITTEN(0x0046EDA0, Plain8::Serialize);   // TrapPointArrayClass
EI_REWRITTEN(0x00474F50, Plain12::Serialize);   // FilePointArrayClass
EI_REWRITTEN(0x00475130, Plain12::Serialize);   // FileUvArrayClass
EI_REWRITTEN(0x00475310, Plain28::Serialize);   // FileFaceArrayClass
EI_REWRITTEN(0x004756A0, Plain20::Serialize);   // RotKeyArrayClass
EI_REWRITTEN(0x00475A00, Plain24::Serialize);   // PosKeyArrayClass
EI_REWRITTEN(0x00475D60, Plain8::Serialize);   // MorphHeadArrayClass
EI_REWRITTEN(0x004767A0, Plain4::Serialize);   // IndexArrayClass
EI_REWRITTEN(0x00476AD0, Plain20::Serialize);   // NormalArrayClass
EI_REWRITTEN(0x00498EA0, Plain8::Serialize);   // gamearrays::ParticleDepthArrayClass
EI_REWRITTEN(0x004C1E70, Plain20::Serialize);   // ScriptAreaClass
EI_REWRITTEN(0x004C29E0, Plain4::Serialize);   // ScriptFloatsClass
EI_REWRITTEN(0x004C4560, Plain4::Serialize);   // FloatArrayClass
EI_REWRITTEN(0x004C4860, Plain4::Serialize);   // ArgObjectsBaseClass
EI_REWRITTEN(0x004C4B50, Plain4::Serialize);   // ArgGroupsBaseClass
EI_REWRITTEN(0x004C4E40, Plain12::Serialize);   // BindingsBaseClass
EI_REWRITTEN(0x004C5B10, Plain8::Serialize);   // ScriptIfThenArrayClass
EI_REWRITTEN(0x004C9B10, Plain16::Serialize);   // AiLookArrayClass
EI_REWRITTEN(0x0050FFD0, Plain20::Serialize);   // EffectArrayClass
EI_REWRITTEN(0x00532E70, Plain28::Serialize);   // MotionPointArrayClass
EI_REWRITTEN(0x00533030, Plain12::Serialize);   // MotionWaypointBaseClass
EI_REWRITTEN(0x005331E0, Plain4::Serialize);   // CarriedArrayClass
EI_REWRITTEN(0x00533390, Plain4::Serialize);   // IdArrayClass
EI_REWRITTEN(0x00557270, Plain8::Serialize);   // PointArrayClass
EI_REWRITTEN(0x00557430, Plain40::Serialize);   // AttackOptionArrayClass
EI_REWRITTEN(0x00560200, Plain16::Serialize);   // CountdownArrayClass
EI_REWRITTEN(0x005991A0, Plain4::Serialize);   // CellRowArrayClass
EI_REWRITTEN(0x005997C0, Plain8::Serialize);   // AreaListClass
EI_REWRITTEN(0x0059B0D0, Plain4::Serialize);   // PartyUnitArrayClass
EI_REWRITTEN(0x005C1900, Plain4::Serialize);   // gamearrays::SpiralOffsetArrayClass
EI_REWRITTEN(0x005C4D80, Plain8::Serialize);   // gamearrays::SmoothNodeArrayClass
EI_REWRITTEN(0x005C5070, Plain20::Serialize);   // SmoothEdgeArrayClass
EI_REWRITTEN(0x005C5360, Plain8::Serialize);   // SmoothCopyArrayClass
EI_REWRITTEN(0x005C5670, Plain36::Serialize);   // SmoothStepArrayClass
EI_REWRITTEN(0x005EBC00, Plain4::Serialize);   // UnitPtrArrayClass
EI_REWRITTEN(0x0060A890, Plain28::Serialize);   // ObjectiveExitArrayClass
EI_REWRITTEN(0x00613690, Plain4::Serialize);   // ItemStripArrayClass
EI_REWRITTEN(0x0064B1E0, Plain12::Serialize);   // QuickEntryArrayClass
EI_REWRITTEN(0x0065DC90, Plain4::Serialize);   // TokenPtrArrayClass
EI_REWRITTEN(0x0065DE40, Plain4::Serialize);   // LinePtrArrayClass
EI_REWRITTEN(0x006CD6F0, Plain4::Serialize);   // TerrainArrayAClass
EI_REWRITTEN(0x006CDA40, Plain4::Serialize);   // TerrainArrayCClass

// Lists of plain elements.
EI_REWRITTEN(0x00405190, PtrList::Serialize);   // TempListClass
EI_REWRITTEN(0x00427890, PtrList::Serialize);   // ItemListClass
EI_REWRITTEN(0x00498D00, PtrList::Serialize);   // ClientListClass
EI_REWRITTEN(0x004C9750, PtrList::Serialize);   // PtrListClass
EI_REWRITTEN(0x0050DC20, PtrList::Serialize);   // BoneListClass
EI_REWRITTEN(0x0050DE90, PtrList::Serialize);   // AnimationStateListClass
EI_REWRITTEN(0x005198D0, PtrList::Serialize);   // Object3DListClass
EI_REWRITTEN(0x00539440, List8::Serialize);   // CellListClass
EI_REWRITTEN(0x005584D0, PtrList::Serialize);   // LootListClass
EI_REWRITTEN(0x00570570, PtrList::Serialize);   // ClientObjectListClass
EI_REWRITTEN(0x00570F30, PtrList::Serialize);   // ClientList2Class
EI_REWRITTEN(0x00571660, PtrList::Serialize);   // ClientList3Class
EI_REWRITTEN(0x00599640, List1C::Serialize);   // NodeListClass
EI_REWRITTEN(0x0059B2C0, List8::Serialize);   // ExperienceListClass
EI_REWRITTEN(0x00666050, PtrList::Serialize);   // PlayerListClass
EI_REWRITTEN(0x006941C0, PtrList::Serialize);   // TextureListClass
EI_REWRITTEN(0x006BC5D0, PtrList::Serialize);   // SoundListClass

// CMap<DWORD, DWORD> and the corner maps (64-bit keys).
EI_REWRITTEN(0x00451CC0, Map32::Serialize);   // FigureRegistryMapClass
EI_REWRITTEN(0x00452110, Map64x64::Serialize);   // CornerMapAClass
EI_REWRITTEN(0x004523B0, Map64x32::Serialize);   // CornerMapBClass
EI_REWRITTEN(0x00452650, Map32::Serialize);   // CornerMapCClass
EI_REWRITTEN(0x004761C0, Map32::Serialize);   // DwordMapClass

// Arrays whose elements SetSize builds (zeroed, then their fields) and nothing destroys.
EI_REWRITTEN(0x00451060, KeyFrames::Serialize);
EI_REWRITTEN(0x004C9510, Placements::Serialize);   // PartySlotArrayClass
EI_REWRITTEN(0x00511E20, SoundRecords::Serialize);   // EffectSoundArrayClass
EI_REWRITTEN(0x005396E0, PathRecords::Serialize);   // SkinLayerArrayClass
EI_REWRITTEN(0x00557E90, AnimationSteps::Serialize);   // AnimationStepsClass
EI_REWRITTEN(0x0059E0C0, Plain8::Serialize);   // SectorOccluderArrayClass
EI_REWRITTEN(0x005C1AB0, Plain8::Serialize);   // SpiralCellArrayClass
EI_REWRITTEN(0x006CD8A0, Plain16::Serialize);   // TerrainArrayBClass
EI_REWRITTEN(0x00519B50, Plain56::Serialize);   // FrameArrayClass

// Arrays whose elements SetSize builds and destroys (a shrinking load destroys the ones past the count).
EI_REWRITTEN(0x0045DC60, OwnedPatrols::Serialize);   // PatrolArrayClass
EI_REWRITTEN(0x004C88E0, ScriptAreas::Serialize);   // ScriptAreaArrayClass
EI_REWRITTEN(0x006C4A40, ByteArrays::Serialize);   // TerrainByteArraysClass
EI_REWRITTEN(0x005D9610, AiLookRecords::Serialize);
EI_REWRITTEN(0x00450D40, AlignedAt4Records::Serialize);   // AlignedAt4ArrayClass
EI_REWRITTEN(0x0050A210, AlignedAt8Records::Serialize);   // AlignedAt8ArrayClass
EI_REWRITTEN(0x00450570, OwnedBoneLinkArrays::Serialize);   // FakeUnitBoneArraysClass
EI_REWRITTEN(0x00476430, OwnedDwordMaps::Serialize);   // SceneObjectArrayClass
EI_REWRITTEN(0x006C4C90, AlignedArrays::Serialize);

// Records with CStrings.

// More lists of plain elements (the AI's, the client maps' platforms) and AIAttention's records.
EI_REWRITTEN(0x0056FEA0, List4C::Serialize);          // PlatformListClass
EI_REWRITTEN(0x005B1870, List12::Serialize);          // BlockCostListClass
EI_REWRITTEN(0x005BDF40, List12::Serialize);          // WaypointListClass
EI_REWRITTEN(0x005B1A00, List8::Serialize);           // AiMapListClass
EI_REWRITTEN(0x005CDC00, List30::Serialize);          // StaticListClass
EI_REWRITTEN(0x005D9A40, AttentionList::Serialize);   // AttentionListClass

// CArray<CString> and its SerializeElements.
EI_REWRITTEN(0x004C2700, Strings::Serialize);
EI_REWRITTEN(0x007085AF, SerializeStrings);

} // namespace mfc::impl
