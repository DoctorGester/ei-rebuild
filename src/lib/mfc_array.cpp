// The game's CArray<T> instantiations (mfc_array.h), registered at their own addresses so the
// standalone image has them; the forwarders in src/game keep calling these addresses. Checked by
// tests/verify/verify_mfc_array.cpp. Instantiations with the same element policy share one body; the
// groups were found by comparing the instructions of every instantiation our code forwards to
// (addresses masked), the element sizes and helpers read from each.
#include "mfc_array.h"

#include "ei/hook.h"

namespace mfc::impl {

// ---- Constructors and destructors of arrays whose elements need nothing ---------------------------

// The constructor: the vtable, the rest zero (21 instantiations).
EI_REPLACE(0x00468D60, ArrayHeader<SpriteArrayClass>::Construct);
EI_REPLACE(0x00475500, ArrayHeader<RotKeyArrayClass>::Construct);
EI_REPLACE(0x00475860, ArrayHeader<PosKeyArrayClass>::Construct);
EI_REPLACE(0x00475BC0, ArrayHeader<MorphHeadArrayClass>::Construct);
EI_REPLACE(0x00476930, ArrayHeader<NormalArrayClass>::Construct);
EI_REPLACE(0x004BB000, ArrayHeader<PartySlotArrayClass>::Construct);
EI_REPLACE(0x004C2030, ArrayHeader<ScriptGroupClass>::Construct);
EI_REPLACE(0x004CD080, ArrayHeader<FloatArrayClass>::Construct);
EI_REPLACE(0x004D5990, ArrayHeader<ArgFloatArrayClass>::Construct);
EI_REPLACE(0x00556810, ArrayHeader<MotionWaypointArrayClass>::Construct);
EI_REPLACE(0x00556850, ArrayHeader<AnimationStepsCopyClass>::Construct);
EI_REPLACE(0x00556D70, ArrayHeader<MotionPointArrayClass>::Construct);
EI_REPLACE(0x00557230, ArrayHeader<PointArrayClass>::Construct);
EI_REPLACE(0x00557B60, ArrayHeader<AnimationStepsClass>::Construct);
EI_REPLACE(0x0056FFE0, ArrayHeader<ExitArrayClass>::Construct);
EI_REPLACE(0x005711C0, ArrayHeader<ClientMapArrayClass>::Construct);
EI_REPLACE(0x005985C0, ArrayHeader<ObjectPtrArrayClass>::Construct);
EI_REPLACE(0x005BD1C0, ArrayHeader<SectorArrayClass>::Construct);
EI_REPLACE(0x005BD1E0, ArrayHeader<CellRowArrayClass>::Construct);
EI_REPLACE(0x005C1760, ArrayHeader<IntArrayClass>::Construct);
EI_REPLACE(0x005FAF80, ArrayHeader<UnitPtrArrayClass>::Construct);

// The destructor: the vtable, the storage freed (20 instantiations).
EI_REPLACE(0x004600D0, ArrayHeader<IntArrayClass>::Destruct);
EI_REPLACE(0x00474F30, ArrayHeader<FilePointArrayClass>::Destruct);
EI_REPLACE(0x00475110, ArrayHeader<FileUvArrayClass>::Destruct);
EI_REPLACE(0x004752F0, ArrayHeader<FileFaceArrayClass>::Destruct);
EI_REPLACE(0x00475680, ArrayHeader<RotKeyArrayClass>::Destruct);
EI_REPLACE(0x004759E0, ArrayHeader<PosKeyArrayClass>::Destruct);
EI_REPLACE(0x00475D40, ArrayHeader<MorphHeadArrayClass>::Destruct);
EI_REPLACE(0x00476780, ArrayHeader<IndexArrayClass>::Destruct);
EI_REPLACE(0x00476AB0, ArrayHeader<NormalArrayClass>::Destruct);
EI_REPLACE(0x004BB020, ArrayHeader<PartySlotArrayClass>::Destruct);
EI_REPLACE(0x004BCF10, ArrayHeader<ScriptAreaClass>::Destruct);
EI_REPLACE(0x004C29C0, ArrayHeader<ScriptFloatsClass>::Destruct);
EI_REPLACE(0x004C5AF0, ArrayHeader<ScriptIfThenArrayClass>::Destruct);
EI_REPLACE(0x004CB050, ArrayHeader<FloatArrayClass>::Destruct);
EI_REPLACE(0x004CB770, ArrayHeader<AiLookArrayClass>::Destruct);
EI_REPLACE(0x00557250, ArrayHeader<PointArrayClass>::Destruct);
EI_REPLACE(0x00557410, ArrayHeader<AttackOptionArrayClass>::Destruct);
EI_REPLACE(0x00557E70, ArrayHeader<AnimationStepsClass>::Destruct);
EI_REPLACE(0x00599180, ArrayHeader<CellRowArrayClass>::Destruct);
EI_REPLACE(0x005997A0, ArrayHeader<AreaListClass>::Destruct);

// ---- Plain data: zero filled, nothing destroyed --------------------------------------------------

// SetSize.
EI_REPLACE(0x00453790, Plain4::SetSize);
EI_REPLACE(0x004C2880, Plain4::SetSize);
EI_REPLACE(0x004C4410, Plain4::SetSize);
EI_REPLACE(0x004C46F0, Plain4::SetSize);
EI_REPLACE(0x004C49F0, Plain4::SetSize);
EI_REPLACE(0x005719B0, Plain4::SetSize);
EI_REPLACE(0x0059C660, Plain4::SetSize);
EI_REPLACE(0x005EBDB0, Plain4::SetSize);
EI_REPLACE(0x0062A390, Plain4::SetSize);
EI_REPLACE(0x0065E010, Plain4::SetSize);
EI_REPLACE(0x00453A30, Plain8::SetSize);
EI_REPLACE(0x00475BE0, Plain8::SetSize);
EI_REPLACE(0x0064B060, Plain12::SetSize);
EI_REPLACE(0x00453CA0, Plain16::SetSize);
EI_REPLACE(0x00475520, Plain20::SetSize);
EI_REPLACE(0x00476950, Plain20::SetSize);
EI_REPLACE(0x004CC640, Plain20::SetSize);
EI_REPLACE(0x00475880, Plain24::SetSize);

// SetAtGrow (SetSize inlined): dwords by value, the rest by reference.
EI_REPLACE(0x00460500, Plain4::SetAtGrowByValue);
EI_REPLACE(0x0059D2E0, Plain4::SetAtGrowByValue);
EI_REPLACE(0x0059D450, Plain4::SetAtGrowByValue);
EI_REPLACE(0x0060AD50, Plain4::SetAtGrowByValue);
EI_REPLACE(0x004773F0, Plain4::SetAtGrow);
EI_REPLACE(0x00453E00, Plain16::SetAtGrow);

// Add (SetSize inlined).
EI_REPLACE(0x00476640, Plain4::Add);

// ConstructElements: the zero fill (the 20- and 24-byte ones construct elements whose constructor
// clears fields the fill already cleared).
EI_REPLACE(0x004CC870, Plain4::ConstructElements);
EI_REPLACE(0x004CCA10, Plain4::ConstructElements);
EI_REPLACE(0x0060AEC0, Plain4::ConstructElements);
EI_REPLACE(0x00468F90, Plain8::ConstructElements);
EI_REPLACE(0x00452DC0, Plain12::ConstructElements);
EI_REPLACE(0x005B1C70, Plain12::ConstructElements);
EI_REPLACE(0x004CDE10, Plain16::ConstructElements);
EI_REPLACE(0x00454F30, Plain20::ConstructElements);
EI_REPLACE(0x0045E130, Plain20::ConstructElements);
EI_REPLACE(0x00452E20, Plain24::ConstructElements);

// ConstructElements of elements with an inlined constructor.
EI_REPLACE(0x00453730, KeyFrames::ConstructElements);
EI_REPLACE(0x004553F0, NamedDwords::ConstructElements);
EI_REPLACE(0x004606A0, Exits3C::ConstructElements);
EI_REPLACE(0x00460720, Allods::ConstructElements);
EI_REPLACE(0x004607E0, MapRecords::ConstructElements);
EI_REPLACE(0x004534F0, BoneLinkArrays::ConstructElements);
EI_REPLACE(0x0045E180, Patrols::ConstructElements);

// ---- Instantiations built by an out-of-line ConstructElements ------------------------------------
//
// The helper is the element policy's Construct (inlined in ours); the helpers are registered too.

// SetSize.
EI_REPLACE(0x0046E970, Plain20::SetSize);
EI_REPLACE(0x0046EAD0, Plain12::SetSize);
EI_REPLACE(0x004C4CE0, Plain12::SetSize);
EI_REPLACE(0x005C86B0, Plain12::SetSize);
EI_REPLACE(0x004C59C0, Plain8::SetSize);
EI_REPLACE(0x0052C8E0, Plain8::SetSize);
EI_REPLACE(0x005B16D0, Plain8::SetSize);
EI_REPLACE(0x005BEC40, Plain8::SetSize);
EI_REPLACE(0x005C8560, Plain28::SetSize);
EI_REPLACE(0x0064EFD0, Plain28::SetSize);   // the zero fill inlined
EI_REPLACE(0x00450F20, KeyFrames::SetSize);
EI_REPLACE(0x0059C3D0, Placements::SetSize);
EI_REPLACE(0x00539580, PathRecords::SetSize);
EI_REPLACE(0x00512330, SoundRecords::SetSize);
EI_REPLACE(0x00557B80, AnimationSteps::SetSize);

// SetAtGrow (SetSize inlined): by reference; the step speeds' floats by value.
EI_REPLACE(0x005C87F0, Plain12::SetAtGrow);
EI_REPLACE(0x0052CC50, Plain12::SetAtGrow);
EI_REPLACE(0x0064F160, Plain28::SetAtGrow);
EI_REPLACE(0x0059C510, Placements::SetAtGrow);
EI_REPLACE(0x00558950, AnimationSteps::SetAtGrow);
EI_REPLACE(0x005C8950, Floats::SetAtGrowByValue);

// Add (SetSize inlined).
EI_REPLACE(0x005BDD70, Plain8::Add);
EI_REPLACE(0x005C8400, Plain12::Add);
EI_REPLACE(0x00557CC0, AnimationSteps::Add);

// Copy (SetSize inlined, then each element assigned).
EI_REPLACE(0x004C1A80, Floats::Copy);
EI_REPLACE(0x004CC4A0, Plain8::Copy);
EI_REPLACE(0x004CE2F0, Plain20::Copy);
EI_REPLACE(0x00556950, AnimationSteps::Copy);

// GetAt (by value), ElementAt, RemoveAt.
EI_REPLACE(0x00558920, Plain12::GetAt);
EI_REPLACE(0x0046EFA0, Plain20::GetAt);
EI_REPLACE(0x00450750, Plain9C::ElementAt);
EI_REPLACE(0x00557E20, AnimationSteps::RemoveAt);

// The animation lists' copy constructors and assignment.
EI_REPLACE(0x0054B260, AnimationStepListAbi::CopyConstruct);
EI_REPLACE(0x00556870, AnimationStepListAbi::CopyConstructSteps);
EI_REPLACE(0x005483C0, AnimationStepListAbi::Assign);

// The ConstructElements helpers: the zero fill (some write zero fields again)...
EI_REPLACE(0x0046F110, Plain8::ConstructElements);
EI_REPLACE(0x004CD4A0, Plain8::ConstructElements);
EI_REPLACE(0x00539980, Plain8::ConstructElements);
EI_REPLACE(0x00558DB0, Plain8::ConstructElements);
EI_REPLACE(0x0059E290, Plain8::ConstructElements);
EI_REPLACE(0x0046F0E0, Plain12::ConstructElements);
EI_REPLACE(0x004CCA50, Plain12::ConstructElements);
EI_REPLACE(0x005338D0, Plain12::ConstructElements);
EI_REPLACE(0x004CE5C0, Plain20::ConstructElements);
EI_REPLACE(0x005338A0, Plain28::ConstructElements);
// ... and the zero fill, then the element's constructor.
EI_REPLACE(0x004CEE20, Placements::ConstructElements);
EI_REPLACE(0x00539910, PathRecords::ConstructElements);
EI_REPLACE(0x00512520, SoundRecords::ConstructElements);
EI_REPLACE(0x00558AB0, AnimationSteps::ConstructElements);

} // namespace mfc::impl
