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

} // namespace mfc::impl
