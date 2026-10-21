// The game's STLport vector<T> instantiations over game records (record_vectors.h), registered at
// their own addresses; the forwarders in the area files keep calling these addresses. The element
// size of each was read from its size arithmetic. Checked in game by verify_record_vectors.cpp until S21d.
#include "record_vectors.h"

#include "ei/hook.h"

using namespace recordvec;

EI_REWRITTEN(0x00657C60, (stlp::ConstructCopyAt<SaveEntry, SaveEntryElement>));
EI_REWRITTEN(0x0062A0B0, TradeRecordVector::InsertOverflow);
EI_REWRITTEN(0x00652930, OptionBindingVector::InsertOverflow);
EI_REWRITTEN(0x006578F0, SaveEntryVector::EraseRange);
EI_REWRITTEN(0x006579C0, SaveEntryVector::InsertOverflow);
EI_REWRITTEN(0x0061EC40, MpAddressVector::InsertOverflow);
EI_REWRITTEN(0x00629E60, TradeRowVector::InsertOverflow);
EI_REWRITTEN(0x00428A20, EngineObjectRefVector::InsertOverflow);
EI_REWRITTEN(0x0064AB20, HudPortraitSlotVector::PushDefault);
EI_REWRITTEN(0x0064AD40, HudPortraitSlotVector::EraseRange);
EI_REWRITTEN(0x0041F790, ReplicatedVector::PushDefault);
EI_REWRITTEN(0x00426B10, ReplicatedVector::InsertOverflow);
EI_REWRITTEN(0x00465180, LeverVector::ResizeDefault);
EI_REWRITTEN(0x004ADAA0, PartyEntryVector::ResizeDefault);
EI_REWRITTEN(0x00443E30, (stlp::ConstructDefaultAt<net::Query, QueryElement>));
EI_REWRITTEN(0x00443FF0, (stlp::CopyAssignTagged<net::Query, QueryElement>));
EI_REWRITTEN(0x004439C0, QueryVector::InsertOverflow);
EI_REWRITTEN(0x00443BF0, QueryVector::Assign);
EI_REWRITTEN(0x004442B0, QueryVector::EraseRange);
EI_REWRITTEN(0x00444090, QueryVector::ResizeDefault);
EI_REWRITTEN(0x0041F7F0, QueryVector::Destruct);
EI_REWRITTEN(0x006DB9C0, UnitAcksVector::ResizeDefault);
EI_REWRITTEN(0x006DBBD0, ModelAcksVector::ResizeDefault);
EI_REWRITTEN(0x006DBE20, ExtraAcksVector::ResizeDefault);
EI_REWRITTEN(0x006D26E0, PrintVector::ResizeDefault);
EI_REWRITTEN(0x006D2830, Print2Vector::ResizeDefault);
EI_REWRITTEN(0x006E4560, SpeechVector::ResizeDefault);
EI_REWRITTEN(0x006E47F0, EnvironmentSpeechVector::ResizeDefault);
EI_REWRITTEN(0x0055A400, FiguredUnitVector::ResizeDefault);
EI_REWRITTEN(0x0055A580, DressedUnitVector::ResizeDefault);
EI_REWRITTEN(0x005209D0, SkillVector::ResizeDefault);
EI_REWRITTEN(0x00520B20, SkillLevelVector::ResizeDefault);
EI_REWRITTEN(0x00520C20, SkillLevelVector::Clear);
EI_REWRITTEN(0x0053E2A0, UnitPrototypeVector::ResizeDefault);
EI_REWRITTEN(0x0053E520, UnitNpcVector::ResizeDefault);
EI_REWRITTEN(0x0053E730, UnitModelVector::ResizeDefault);
EI_REWRITTEN(0x0053E9B0, UnitLevelVector::ResizeDefault);
EI_REWRITTEN(0x006B2100, QuestVector::ResizeDefault);
EI_REWRITTEN(0x006B2240, BriefingVector::ResizeDefault);
EI_REWRITTEN(0x006B0710, QuestVector::PushBack);
EI_REWRITTEN(0x006B0780, BriefingVector::PushBack);
EI_REWRITTEN(0x00685730, SpellVector::ResizeDefault);
EI_REWRITTEN(0x006858D0, SpellModifierVector::ResizeDefault);
EI_REWRITTEN(0x00684D60, SpellListVector::ResizeDefault);
EI_REWRITTEN(0x006A7F00, MaterialVector::ResizeDefault);
EI_REWRITTEN(0x006A8110, WeaponVector::ResizeDefault);
EI_REWRITTEN(0x006A8290, ArmorVector::ResizeDefault);
EI_REWRITTEN(0x006A83F0, QuickItemVector::ResizeDefault);
EI_REWRITTEN(0x006A85A0, QuestItemVector::ResizeDefault);
EI_REWRITTEN(0x006A8790, LootVector::ResizeDefault);
// The game flow's zone table: _M_insert_overflow (RET 0xC).
EI_REPLACE(0x00583920, ZoneTableVector::Destruct);   // S3c: the zone table 007AFDE0 at exit (0057F4A0)
EI_REWRITTEN(0x00587480, ZoneTableVector::InsertOverflow);
